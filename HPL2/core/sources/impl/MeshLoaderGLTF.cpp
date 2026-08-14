/*
 * Copyright (C) 2009-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "impl/MeshLoaderGLTF.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "system/LowLevelSystem.h"
#include "system/String.h"
#include "system/Platform.h"

#include "resources/MaterialManager.h"
#include "resources/MeshManager.h"
#include "resources/Resources.h"

#include "graphics/LowLevelGraphics.h"
#include "graphics/VertexBuffer.h"
#include "graphics/Mesh.h"
#include "graphics/SubMesh.h"
#include "graphics/Material.h"
#include "graphics/Animation.h"
#include "graphics/AnimationTrack.h"

#include "scene/Node3D.h"

#include "impl/MeshLoaderMSH.h"

#include "math/Math.h"

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// LOCAL HELPERS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	struct cGLTFAnimTrackData
	{
		cGLTFAnimTrackData() : mpNode(NULL), mpTranslation(NULL), mpRotation(NULL) {}

		const cgltf_node* mpNode;
		const cgltf_animation_channel* mpTranslation;
		const cgltf_animation_channel* mpRotation;
		tFloatVec mvTimes;
	};

	typedef std::map<const cgltf_node*, cGLTFAnimTrackData> tGLTFAnimTrackMap;
	typedef tGLTFAnimTrackMap::iterator tGLTFAnimTrackMapIt;

	//-----------------------------------------------------------------------

	static const char* GetGLTFResultName(cgltf_result aResult)
	{
		switch(aResult)
		{
		case cgltf_result_success:			return "success";
		case cgltf_result_data_too_short:	return "data too short";
		case cgltf_result_unknown_format:	return "unknown format";
		case cgltf_result_invalid_json:		return "invalid json";
		case cgltf_result_invalid_gltf:		return "invalid gltf";
		case cgltf_result_invalid_options:	return "invalid options";
		case cgltf_result_file_not_found:	return "file not found";
		case cgltf_result_io_error:			return "io error";
		case cgltf_result_out_of_memory:		return "out of memory";
		case cgltf_result_legacy_gltf:		return "legacy gltf";
		default:							return "unknown error";
		}
	}

	//-----------------------------------------------------------------------

	static tString GetSafeName(const char* apName, const tString& asFallback)
	{
		if(apName && apName[0] != 0) return tString(apName);
		return asFallback;
	}

	//-----------------------------------------------------------------------

	static tString MakeUniqueName(const tString& asBaseName, tStringSet& aUsedNames)
	{
		tString sBaseName = asBaseName == "" ? "gltf_node" : asBaseName;
		tString sName = sBaseName;
		int lNum = 1;

		while(aUsedNames.find(sName) != aUsedNames.end())
		{
			sName = sBaseName + "_" + cString::ToString(lNum);
			++lNum;
		}

		aUsedNames.insert(sName);
		return sName;
	}

	//-----------------------------------------------------------------------

	static tString GetMappedNodeName(const cgltf_node* apNode, const cMeshLoaderGLTF::tNodeNameMap& aNodeNames)
	{
		cMeshLoaderGLTF::tNodeNameMap::const_iterator it = aNodeNames.find(apNode);
		if(it != aNodeNames.end()) return it->second;

		if(apNode && apNode->name) return tString(apNode->name);
		return "";
	}

	//-----------------------------------------------------------------------

	static tString GetSubMeshName(const cgltf_node* apNode, int alPrimitiveIndex, const cMeshLoaderGLTF::tNodeNameMap& aNodeNames)
	{
		tString sNodeName = GetMappedNodeName(apNode, aNodeNames);
		if(sNodeName == "") sNodeName = "gltf_mesh";

		if(apNode && apNode->mesh && apNode->mesh->primitives_count > 1)
		{
			return sNodeName + "_" + cString::ToString(alPrimitiveIndex);
		}

		return sNodeName;
	}

	//-----------------------------------------------------------------------

	static cMatrixf GetNodeLocalMatrix(const cgltf_node* apNode)
	{
		float vMatrix[16];
		cgltf_node_transform_local(apNode, vMatrix);

		cMatrixf mtxTransform;
		mtxTransform.FromTranspose(vMatrix);
		return mtxTransform;
	}

	//-----------------------------------------------------------------------

	static cVector3f GetNodeBaseTranslation(const cgltf_node* apNode)
	{
		if(apNode && apNode->has_translation)
		{
			return cVector3f(apNode->translation[0], apNode->translation[1], apNode->translation[2]);
		}

		return GetNodeLocalMatrix(apNode).GetTranslation();
	}

	//-----------------------------------------------------------------------

	static cQuaternion GetNodeBaseRotation(const cgltf_node* apNode)
	{
		if(apNode && apNode->has_rotation)
		{
			cQuaternion qRot(apNode->rotation[3], apNode->rotation[0], apNode->rotation[1], apNode->rotation[2]);
			qRot.Normalize();
			return qRot;
		}

		cMatrixf mtxRot = GetNodeLocalMatrix(apNode).GetRotation();
		cVector3f vRight = mtxRot.GetRight();
		cVector3f vUp = mtxRot.GetUp();
		cVector3f vForward = mtxRot.GetForward();
		if(vRight.SqrLength() > 0) mtxRot.SetRight(cMath::Vector3Normalize(vRight));
		if(vUp.SqrLength() > 0) mtxRot.SetUp(cMath::Vector3Normalize(vUp));
		if(vForward.SqrLength() > 0) mtxRot.SetForward(cMath::Vector3Normalize(vForward));

		cQuaternion qRot;
		qRot.FromRotationMatrix(mtxRot);
		qRot.Normalize();
		return qRot;
	}

	//-----------------------------------------------------------------------

	static float MatrixDeterminant3x3(const cMatrixf& a_mtxMatrix)
	{
		return a_mtxMatrix.m[0][0] * (a_mtxMatrix.m[1][1] * a_mtxMatrix.m[2][2] - a_mtxMatrix.m[1][2] * a_mtxMatrix.m[2][1]) -
			a_mtxMatrix.m[0][1] * (a_mtxMatrix.m[1][0] * a_mtxMatrix.m[2][2] - a_mtxMatrix.m[1][2] * a_mtxMatrix.m[2][0]) +
			a_mtxMatrix.m[0][2] * (a_mtxMatrix.m[1][0] * a_mtxMatrix.m[2][1] - a_mtxMatrix.m[1][1] * a_mtxMatrix.m[2][0]);
	}

	//-----------------------------------------------------------------------

	static bool HasUnsupportedNodeScale(const cgltf_node* apNode)
	{
		if(apNode == NULL) return false;

		if(apNode->has_scale)
		{
			return std::fabs(apNode->scale[0] - 1.0f) > kEpsilonf ||
				std::fabs(apNode->scale[1] - 1.0f) > kEpsilonf ||
				std::fabs(apNode->scale[2] - 1.0f) > kEpsilonf;
		}

		cMatrixf mtxLocal = GetNodeLocalMatrix(apNode);
		return std::fabs(mtxLocal.GetRight().Length() - 1.0f) > kEpsilonf ||
			std::fabs(mtxLocal.GetUp().Length() - 1.0f) > kEpsilonf ||
			std::fabs(mtxLocal.GetForward().Length() - 1.0f) > kEpsilonf ||
			MatrixDeterminant3x3(mtxLocal.GetRotation()) < -kEpsilonf;
	}

	//-----------------------------------------------------------------------

	static bool IsDataURI(const char* apURI)
	{
		return apURI && std::strncmp(apURI, "data:", 5) == 0;
	}

	//-----------------------------------------------------------------------

	static const cgltf_image* GetTextureImage(const cgltf_texture_view& aTextureView)
	{
		if(aTextureView.texture == NULL) return NULL;

		if(aTextureView.texture->image) return aTextureView.texture->image;
		if(aTextureView.texture->has_basisu && aTextureView.texture->basisu_image) return aTextureView.texture->basisu_image;
		if(aTextureView.texture->has_webp && aTextureView.texture->webp_image) return aTextureView.texture->webp_image;

		return NULL;
	}

	//-----------------------------------------------------------------------

	static tString GetMaterialTextureFile(const cgltf_material* apMaterial)
	{
		if(apMaterial == NULL) return "";

		const cgltf_image* pImage = NULL;
		const cgltf_texture* pTexture = NULL;

		if(apMaterial->has_pbr_metallic_roughness && apMaterial->pbr_metallic_roughness.base_color_texture.texture)
		{
			pTexture = apMaterial->pbr_metallic_roughness.base_color_texture.texture;
			pImage = GetTextureImage(apMaterial->pbr_metallic_roughness.base_color_texture);
		}
		else if(apMaterial->has_pbr_specular_glossiness && apMaterial->pbr_specular_glossiness.diffuse_texture.texture)
		{
			pTexture = apMaterial->pbr_specular_glossiness.diffuse_texture.texture;
			pImage = GetTextureImage(apMaterial->pbr_specular_glossiness.diffuse_texture);
		}
		else if(apMaterial->emissive_texture.texture)
		{
			pTexture = apMaterial->emissive_texture.texture;
			pImage = GetTextureImage(apMaterial->emissive_texture);
		}

		if(pImage && pImage->uri && IsDataURI(pImage->uri)==false)
		{
			return cString::GetFileName(tString(pImage->uri));
		}

		if(pTexture && pTexture->name) return tString(pTexture->name);
		if(apMaterial->name) return tString(apMaterial->name);

		return "";
	}

	//-----------------------------------------------------------------------

	static bool IsColliderMeshName(const tString& asName)
	{
		if(asName.length() == 0 || asName[0] != '_') return false;

		tStringVec vStrings;
		tString sSeparator = "_";
		cString::GetStringVec(asName, vStrings, &sSeparator);
		if(vStrings.empty()) return false;

		tString sSpecialName = cString::ToLowerCase(vStrings[0]);
		tString sTypeName = vStrings.size() <= 1 ? "" : cString::ToLowerCase(vStrings[1]);

		return (sSpecialName == "collider" || sSpecialName == "charcollider") &&
				(sTypeName == "" || sTypeName == "mesh");
	}

	//-----------------------------------------------------------------------

	static const cgltf_accessor* FindAttributeAccessor(const cgltf_primitive* apPrimitive, cgltf_attribute_type aType, int alIndex)
	{
		return cgltf_find_accessor(apPrimitive, aType, alIndex);
	}

	//-----------------------------------------------------------------------

	static bool ReadAccessorVec2(const cgltf_accessor* apAccessor, size_t alIndex, cVector3f& avOutput)
	{
		if(apAccessor == NULL) return false;

		float vValue[3] = {0, 0, 0};
		if(cgltf_accessor_read_float(apAccessor, alIndex, vValue, 3)==0) return false;

		avOutput.x = vValue[0];
		avOutput.y = vValue[1];
		avOutput.z = 0;
		return true;
	}

	//-----------------------------------------------------------------------

	static bool ReadAccessorVec3(const cgltf_accessor* apAccessor, size_t alIndex, cVector3f& avOutput)
	{
		if(apAccessor == NULL) return false;

		float vValue[4] = {0, 0, 0, 1};
		if(cgltf_accessor_read_float(apAccessor, alIndex, vValue, 4)==0) return false;

		avOutput.x = vValue[0];
		avOutput.y = vValue[1];
		avOutput.z = vValue[2];
		return true;
	}

	//-----------------------------------------------------------------------

	static bool ReadAccessorColor(const cgltf_accessor* apAccessor, size_t alIndex, cColor& aOutput)
	{
		if(apAccessor == NULL) return false;

		float vValue[4] = {1, 1, 1, 1};
		if(cgltf_accessor_read_float(apAccessor, alIndex, vValue, 4)==0) return false;

		aOutput.r = vValue[0];
		aOutput.g = vValue[1];
		aOutput.b = vValue[2];
		aOutput.a = vValue[3];
		return true;
	}

	//-----------------------------------------------------------------------

	static bool ReadAccessorTangent(const cgltf_accessor* apAccessor, size_t alIndex, cVector3f& avTangent, float& afW)
	{
		if(apAccessor == NULL) return false;

		float vValue[4] = {1, 0, 0, 1};
		if(cgltf_accessor_read_float(apAccessor, alIndex, vValue, 4)==0) return false;

		avTangent.x = vValue[0];
		avTangent.y = vValue[1];
		avTangent.z = vValue[2];
		afW = vValue[3];
		return true;
	}

	//-----------------------------------------------------------------------

	static unsigned int ReadPrimitiveIndex(const cgltf_primitive* apPrimitive, size_t alIndex)
	{
		if(apPrimitive->indices)
		{
			return (unsigned int)cgltf_accessor_read_index(apPrimitive->indices, alIndex);
		}

		return (unsigned int)alIndex;
	}

	//-----------------------------------------------------------------------

	static bool AddTriangle(tUIntVec& avIndices, unsigned int alA, unsigned int alB, unsigned int alC, size_t alVertexCount)
	{
		if(alA >= alVertexCount || alB >= alVertexCount || alC >= alVertexCount) return false;

		// glTF data is CCW. HPL's DAE path flips each triangle before upload.
		avIndices.push_back(alC);
		avIndices.push_back(alB);
		avIndices.push_back(alA);
		return true;
	}

	//-----------------------------------------------------------------------

	static bool BuildPrimitiveIndices(const cgltf_primitive* apPrimitive, size_t alVertexCount, tUIntVec& avIndices)
	{
		size_t lSourceIndexCount = apPrimitive->indices ? apPrimitive->indices->count : alVertexCount;

		if(apPrimitive->type == cgltf_primitive_type_triangles)
		{
			for(size_t i=0; i+2<lSourceIndexCount; i+=3)
			{
				if(AddTriangle(avIndices,
					ReadPrimitiveIndex(apPrimitive, i+0),
					ReadPrimitiveIndex(apPrimitive, i+1),
					ReadPrimitiveIndex(apPrimitive, i+2),
					alVertexCount)==false)
				{
					Warning("glTF primitive has an index outside the vertex range. Skipping triangle.\n");
				}
			}
			return avIndices.empty()==false;
		}

		if(apPrimitive->type == cgltf_primitive_type_triangle_strip)
		{
			for(size_t i=0; i+2<lSourceIndexCount; ++i)
			{
				unsigned int lA = ReadPrimitiveIndex(apPrimitive, i+0);
				unsigned int lB = ReadPrimitiveIndex(apPrimitive, i+1);
				unsigned int lC = ReadPrimitiveIndex(apPrimitive, i+2);

				if((i % 2) != 0)
				{
					unsigned int lTemp = lA;
					lA = lB;
					lB = lTemp;
				}

				if(AddTriangle(avIndices, lA, lB, lC, alVertexCount)==false)
				{
					Warning("glTF triangle strip has an index outside the vertex range. Skipping triangle.\n");
				}
			}
			return avIndices.empty()==false;
		}

		if(apPrimitive->type == cgltf_primitive_type_triangle_fan)
		{
			for(size_t i=1; i+1<lSourceIndexCount; ++i)
			{
				if(AddTriangle(avIndices,
					ReadPrimitiveIndex(apPrimitive, 0),
					ReadPrimitiveIndex(apPrimitive, i),
					ReadPrimitiveIndex(apPrimitive, i+1),
					alVertexCount)==false)
				{
					Warning("glTF triangle fan has an index outside the vertex range. Skipping triangle.\n");
				}
			}
			return avIndices.empty()==false;
		}

		return false;
	}

	//-----------------------------------------------------------------------

	static void GenerateNormals(tVertexVec& avVertices, const tUIntVec& avIndices)
	{
		for(size_t i=0; i<avVertices.size(); ++i)
		{
			avVertices[i].norm = cVector3f(0, 0, 0);
		}

		for(size_t i=0; i+2<avIndices.size(); i+=3)
		{
			unsigned int lIdx0 = avIndices[i+0];
			unsigned int lIdx1 = avIndices[i+1];
			unsigned int lIdx2 = avIndices[i+2];

			cVector3f vEdge1 = avVertices[lIdx1].pos - avVertices[lIdx0].pos;
			cVector3f vEdge2 = avVertices[lIdx2].pos - avVertices[lIdx0].pos;
			cVector3f vNormal = cMath::Vector3Cross(vEdge2, vEdge1);

			if(vNormal.SqrLength() > 0)
			{
				vNormal.Normalize();
				avVertices[lIdx0].norm += vNormal;
				avVertices[lIdx1].norm += vNormal;
				avVertices[lIdx2].norm += vNormal;
			}
		}

		for(size_t i=0; i<avVertices.size(); ++i)
		{
			if(avVertices[i].norm.SqrLength() > 0)
			{
				avVertices[i].norm.Normalize();
			}
			else
			{
				avVertices[i].norm = cVector3f(0, 1, 0);
			}
		}
	}

	//-----------------------------------------------------------------------

	static bool ReadSamplerTime(const cgltf_accessor* apInput, size_t alIndex, float& afTime)
	{
		if(apInput == NULL) return false;

		float vValue[1] = {0};
		if(cgltf_accessor_read_float(apInput, alIndex, vValue, 1)==0) return false;

		afTime = vValue[0];
		return true;
	}

	//-----------------------------------------------------------------------

	static void AddSamplerTimes(const cgltf_animation_channel* apChannel, tFloatVec& avTimes)
	{
		if(apChannel == NULL || apChannel->sampler == NULL || apChannel->sampler->input == NULL) return;

		const cgltf_accessor* pInput = apChannel->sampler->input;
		for(size_t i=0; i<pInput->count; ++i)
		{
			float fTime = 0;
			if(ReadSamplerTime(pInput, i, fTime)) avTimes.push_back(fTime);
		}
	}

	//-----------------------------------------------------------------------

	static void SortAndUniqueTimes(tFloatVec& avTimes)
	{
		std::sort(avTimes.begin(), avTimes.end());

		tFloatVec vUniqueTimes;
		for(size_t i=0; i<avTimes.size(); ++i)
		{
			if(vUniqueTimes.empty() || std::fabs(vUniqueTimes.back() - avTimes[i]) > 0.0001f)
			{
				vUniqueTimes.push_back(avTimes[i]);
			}
		}

		avTimes.swap(vUniqueTimes);
	}

	//-----------------------------------------------------------------------

	static size_t GetOutputValueIndex(const cgltf_animation_sampler* apSampler, size_t alInputIndex)
	{
		if(apSampler->interpolation == cgltf_interpolation_type_cubic_spline)
		{
			return alInputIndex * 3 + 1;
		}

		return alInputIndex;
	}

	//-----------------------------------------------------------------------

	static bool GetSamplerBounds(const cgltf_animation_sampler* apSampler, float afTime, size_t& alBefore, size_t& alAfter, float& afT, float& afDuration)
	{
		if(apSampler == NULL || apSampler->input == NULL || apSampler->output == NULL || apSampler->input->count == 0) return false;
		afDuration = 0;

		const cgltf_accessor* pInput = apSampler->input;
		float fFirstTime = 0;
		if(ReadSamplerTime(pInput, 0, fFirstTime)==false) return false;

		if(afTime <= fFirstTime)
		{
			alBefore = 0;
			alAfter = 0;
			afT = 0;
			return true;
		}

		for(size_t i=1; i<pInput->count; ++i)
		{
			float fNextTime = 0;
			if(ReadSamplerTime(pInput, i, fNextTime)==false) return false;

			if(afTime <= fNextTime)
			{
				float fPrevTime = 0;
				ReadSamplerTime(pInput, i-1, fPrevTime);

				alBefore = i-1;
				alAfter = i;
				afDuration = fNextTime - fPrevTime;
				afT = afDuration > 0 ? (afTime - fPrevTime) / afDuration : 0;

				if(apSampler->interpolation == cgltf_interpolation_type_step) afT = 0;
				return true;
			}
		}

		alBefore = pInput->count - 1;
		alAfter = alBefore;
		afT = 0;
		return true;
	}

	//-----------------------------------------------------------------------

	static float CubicHermite(float afBefore, float afBeforeTangent, float afAfter, float afAfterTangent, float afT, float afDuration)
	{
		float fT2 = afT * afT;
		float fT3 = fT2 * afT;
		float fH00 = 2.0f * fT3 - 3.0f * fT2 + 1.0f;
		float fH10 = fT3 - 2.0f * fT2 + afT;
		float fH01 = -2.0f * fT3 + 3.0f * fT2;
		float fH11 = fT3 - fT2;

		return fH00 * afBefore + fH10 * afDuration * afBeforeTangent +
			fH01 * afAfter + fH11 * afDuration * afAfterTangent;
	}

	//-----------------------------------------------------------------------

	static bool ReadChannelVec3(const cgltf_animation_channel* apChannel, float afTime, const cVector3f& avDefault, cVector3f& avOutput)
	{
		avOutput = avDefault;

		if(apChannel == NULL || apChannel->sampler == NULL || apChannel->sampler->output == NULL) return false;

		const cgltf_animation_sampler* pSampler = apChannel->sampler;
		size_t lBefore = 0;
		size_t lAfter = 0;
		float fT = 0;
		float fDuration = 0;
		if(GetSamplerBounds(pSampler, afTime, lBefore, lAfter, fT, fDuration)==false) return false;

		cVector3f vBefore;
		if(ReadAccessorVec3(pSampler->output, GetOutputValueIndex(pSampler, lBefore), vBefore)==false) return false;

		if(lBefore == lAfter || fT <= 0)
		{
			avOutput = vBefore;
			return true;
		}

		cVector3f vAfter;
		if(ReadAccessorVec3(pSampler->output, GetOutputValueIndex(pSampler, lAfter), vAfter)==false) return false;

		if(pSampler->interpolation == cgltf_interpolation_type_cubic_spline)
		{
			cVector3f vBeforeTangent;
			cVector3f vAfterTangent;
			if(ReadAccessorVec3(pSampler->output, lBefore * 3 + 2, vBeforeTangent)==false) return false;
			if(ReadAccessorVec3(pSampler->output, lAfter * 3, vAfterTangent)==false) return false;

			avOutput.x = CubicHermite(vBefore.x, vBeforeTangent.x, vAfter.x, vAfterTangent.x, fT, fDuration);
			avOutput.y = CubicHermite(vBefore.y, vBeforeTangent.y, vAfter.y, vAfterTangent.y, fT, fDuration);
			avOutput.z = CubicHermite(vBefore.z, vBeforeTangent.z, vAfter.z, vAfterTangent.z, fT, fDuration);
			return true;
		}

		avOutput = vBefore * (1.0f - fT) + vAfter * fT;
		return true;
	}

	//-----------------------------------------------------------------------

	static bool ReadAccessorQuatComponents(const cgltf_accessor* apAccessor, size_t alIndex, float* apOutput)
	{
		if(apAccessor == NULL || apOutput == NULL) return false;
		return cgltf_accessor_read_float(apAccessor, alIndex, apOutput, 4) != 0;
	}

	//-----------------------------------------------------------------------

	static bool ReadAccessorQuat(const cgltf_accessor* apAccessor, size_t alIndex, cQuaternion& aqOutput)
	{
		if(apAccessor == NULL) return false;

		float vValue[4] = {0, 0, 0, 1};
		if(ReadAccessorQuatComponents(apAccessor, alIndex, vValue)==false) return false;

		aqOutput = cQuaternion(vValue[3], vValue[0], vValue[1], vValue[2]);
		aqOutput.Normalize();
		return true;
	}

	//-----------------------------------------------------------------------

	static bool ReadChannelQuat(const cgltf_animation_channel* apChannel, float afTime, const cQuaternion& aqDefault, cQuaternion& aqOutput)
	{
		aqOutput = aqDefault;

		if(apChannel == NULL || apChannel->sampler == NULL || apChannel->sampler->output == NULL) return false;

		const cgltf_animation_sampler* pSampler = apChannel->sampler;
		size_t lBefore = 0;
		size_t lAfter = 0;
		float fT = 0;
		float fDuration = 0;
		if(GetSamplerBounds(pSampler, afTime, lBefore, lAfter, fT, fDuration)==false) return false;

		cQuaternion qBefore;
		if(ReadAccessorQuat(pSampler->output, GetOutputValueIndex(pSampler, lBefore), qBefore)==false) return false;

		if(lBefore == lAfter || fT <= 0)
		{
			aqOutput = qBefore;
			return true;
		}

		if(pSampler->interpolation == cgltf_interpolation_type_cubic_spline)
		{
			float vBefore[4] = {0, 0, 0, 1};
			float vBeforeTangent[4] = {0, 0, 0, 0};
			float vAfter[4] = {0, 0, 0, 1};
			float vAfterTangent[4] = {0, 0, 0, 0};
			if(ReadAccessorQuatComponents(pSampler->output, lBefore * 3 + 1, vBefore)==false) return false;
			if(ReadAccessorQuatComponents(pSampler->output, lBefore * 3 + 2, vBeforeTangent)==false) return false;
			if(ReadAccessorQuatComponents(pSampler->output, lAfter * 3 + 1, vAfter)==false) return false;
			if(ReadAccessorQuatComponents(pSampler->output, lAfter * 3, vAfterTangent)==false) return false;

			float vResult[4];
			for(int i=0; i<4; ++i)
			{
				vResult[i] = CubicHermite(vBefore[i], vBeforeTangent[i], vAfter[i], vAfterTangent[i], fT, fDuration);
			}

			aqOutput = cQuaternion(vResult[3], vResult[0], vResult[1], vResult[2]);
			aqOutput.Normalize();
			return true;
		}

		cQuaternion qAfter;
		if(ReadAccessorQuat(pSampler->output, GetOutputValueIndex(pSampler, lAfter), qAfter)==false) return false;

		aqOutput = cMath::QuaternionSlerp(fT, qBefore, qAfter, true);
		aqOutput.Normalize();
		return true;
	}

	//-----------------------------------------------------------------------

	static tString GetAnimationName(const cgltf_animation* apAnimation, int alIndex)
	{
		if(apAnimation && apAnimation->name && apAnimation->name[0] != 0) return tString(apAnimation->name);
		if(alIndex == 0) return "Default";

		return "Animation_" + cString::ToString(alIndex);
	}

	//-----------------------------------------------------------------------

	static bool HasRootSceneNodes(cgltf_data* apData)
	{
		return apData && apData->scene && apData->scene->nodes_count > 0;
	}

	//-----------------------------------------------------------------------

	static void AddNodeAndChildrenToSet(const cgltf_node* apNode, cMeshLoaderGLTF::tNodeSet& aSceneNodes)
	{
		if(apNode == NULL) return;

		aSceneNodes.insert(apNode);
		for(size_t i=0; i<apNode->children_count; ++i)
		{
			AddNodeAndChildrenToSet(apNode->children[i], aSceneNodes);
		}
	}

	//-----------------------------------------------------------------------

	static void BuildSceneNodeSet(cgltf_data* apData, cMeshLoaderGLTF::tNodeSet& aSceneNodes)
	{
		cgltf_scene* pScene = HasRootSceneNodes(apData) ? apData->scene : (apData->scenes_count > 0 ? &apData->scenes[0] : NULL);

		if(pScene)
		{
			for(size_t i=0; i<pScene->nodes_count; ++i)
			{
				AddNodeAndChildrenToSet(pScene->nodes[i], aSceneNodes);
			}
		}
		else
		{
			for(size_t i=0; i<apData->nodes_count; ++i)
			{
				if(apData->nodes[i].parent == NULL)
				{
					AddNodeAndChildrenToSet(&apData->nodes[i], aSceneNodes);
				}
			}
		}
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cMeshLoaderGLTF::cMeshLoaderGLTF(iLowLevelGraphics *apLowLevelGraphics, cMeshLoaderMSH *apMeshLoaderMSH, bool abLoadAndSaveMSHFormat)
		: iMeshLoader(apLowLevelGraphics)
	{
		mpMeshLoaderMSH = apMeshLoaderMSH;
		mbLoadAndSaveMSHFormat = abLoadAndSaveMSHFormat;

		AddSupportedExtension("gltf");
		AddSupportedExtension("glb");
	}

	//-----------------------------------------------------------------------

	cMeshLoaderGLTF::~cMeshLoaderGLTF()
	{
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cMesh* cMeshLoaderGLTF::LoadMesh(const tWString& asFile, tMeshLoadFlag aFlags)
	{
		/////////////////////////////////////////////////
		// TRY USING MSH LOADER
		if(mbLoadAndSaveMSHFormat)
		{
			tWString sMSHFile = cString::SetFileExtW(asFile, _W("msh"));
			cDate currentDate = cPlatform::FileModifiedDate(asFile);
			cDate mshDate = cPlatform::FileModifiedDate(sMSHFile);

			if(cResources::GetForceCacheLoadingAndSkipSaving() || mshDate > currentDate || cPlatform::FileExists(asFile)==false)
			{
				cMesh *pMesh = mpMeshLoaderMSH->LoadMesh(sMSHFile, aFlags);
				if(pMesh)
				{
					pMesh->SetFullPath(asFile);
					return pMesh;
				}
			}
		}

		cgltf_data* pData = NULL;
		if(LoadGLTFData(asFile, &pData)==false)
		{
			return NULL;
		}

		tNodeNameMap mapNodeNames;
		BuildNodeNameMap(pData, mapNodeNames);

		tNodeSet setSceneNodes;
		BuildSceneNodeSet(pData, setSceneNodes);

		cMesh *pMesh = hplNew( cMesh, (cString::To8Char(asFile), asFile, mpMaterialManager, mpAnimationManager) );

		cgltf_scene* pScene = HasRootSceneNodes(pData) ? pData->scene : (pData->scenes_count > 0 ? &pData->scenes[0] : NULL);

		if(pScene)
		{
			for(size_t i=0; i<pScene->nodes_count; ++i)
			{
				CreateNodeHierarchy(pMesh, pMesh->GetRootNode(), pScene->nodes[i], mapNodeNames);
			}

			if((aFlags & eMeshLoadFlag_NoGeometry)==0)
			{
				for(size_t i=0; i<pScene->nodes_count; ++i)
				{
					ImportNodeMeshes(pMesh, pScene->nodes[i], mapNodeNames, asFile, aFlags);
				}
			}
		}
		else
		{
			for(size_t i=0; i<pData->nodes_count; ++i)
			{
				if(pData->nodes[i].parent == NULL)
				{
					CreateNodeHierarchy(pMesh, pMesh->GetRootNode(), &pData->nodes[i], mapNodeNames);
				}
			}

			if((aFlags & eMeshLoadFlag_NoGeometry)==0)
			{
				for(size_t i=0; i<pData->nodes_count; ++i)
				{
					if(pData->nodes[i].parent == NULL)
					{
						ImportNodeMeshes(pMesh, &pData->nodes[i], mapNodeNames, asFile, aFlags);
					}
				}
			}
		}

		pMesh->CompileBonesAndSubMeshes();

		tStringSet setAnimationNames;
		for(size_t i=0; i<pData->animations_count; ++i)
		{
			tString sAnimName = MakeUniqueName(GetAnimationName(&pData->animations[i], (int)i), setAnimationNames);
			cAnimation* pAnimation = CreateAnimationFromGLTF(pData, &pData->animations[i], asFile, sAnimName, mapNodeNames, setSceneNodes);
			if(pAnimation) pMesh->AddAnimation(pAnimation);
		}

		/////////////////////////////////////////////////
		// SAVE MSH FORMAT
		if(cResources::GetForceCacheLoadingAndSkipSaving()==false && mbLoadAndSaveMSHFormat)
		{
			tWString sMSHFile = cString::SetFileExtW(asFile, _W("msh"));
			mpMeshLoaderMSH->SaveMesh(pMesh, sMSHFile);
		}

		cgltf_free(pData);
		return pMesh;
	}

	//-----------------------------------------------------------------------

	cAnimation* cMeshLoaderGLTF::LoadAnimation(const tWString& asFile)
	{
		/////////////////////////////////////////////////
		// TRY USING MSH LOADER
		if(mbLoadAndSaveMSHFormat)
		{
			tWString sMSHFile = cString::SetFileExtW(asFile, _W("anm"));
			cDate currentDate = cPlatform::FileModifiedDate(asFile);
			cDate mshDate = cPlatform::FileModifiedDate(sMSHFile);

			if(cResources::GetForceCacheLoadingAndSkipSaving() || mshDate > currentDate || cPlatform::FileExists(asFile)==false)
			{
				cAnimation *pAnim = mpMeshLoaderMSH->LoadAnimation(sMSHFile);
				if(pAnim)
				{
					pAnim->SetFullPath(asFile);
					return pAnim;
				}
			}
		}

		cgltf_data* pData = NULL;
		if(LoadGLTFData(asFile, &pData)==false)
		{
			return NULL;
		}

		if(pData->animations_count == 0)
		{
			cgltf_free(pData);
			return NULL;
		}

		tNodeNameMap mapNodeNames;
		BuildNodeNameMap(pData, mapNodeNames);

		tNodeSet setSceneNodes;
		BuildSceneNodeSet(pData, setSceneNodes);

		tString sAnimName = cString::GetFileName(cString::To8Char(asFile));
		cAnimation* pAnimation = CreateAnimationFromGLTF(pData, &pData->animations[0], asFile, cString::To8Char(asFile), mapNodeNames, setSceneNodes);

		if(pAnimation) pAnimation->SetAnimationName(sAnimName);

		/////////////////////////////////////////////////
		// SAVE MSH FORMAT
		if(cResources::GetForceCacheLoadingAndSkipSaving()==false && mbLoadAndSaveMSHFormat && pAnimation)
		{
			tWString sMSHFile = cString::SetFileExtW(asFile, _W("anm"));
			mpMeshLoaderMSH->SaveAnimation(pAnimation, sMSHFile);
		}

		cgltf_free(pData);
		return pAnimation;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	bool cMeshLoaderGLTF::LoadGLTFData(const tWString& asFile, cgltf_data** apData)
	{
		cgltf_options options;
		std::memset(&options, 0, sizeof(options));

		tString sFile = cString::To8Char(asFile);

		cgltf_result result = cgltf_parse_file(&options, sFile.c_str(), apData);
		if(result != cgltf_result_success)
		{
			Error("Could not parse glTF file '%s': %s\n", sFile.c_str(), GetGLTFResultName(result));
			return false;
		}

		result = cgltf_load_buffers(&options, *apData, sFile.c_str());
		if(result != cgltf_result_success)
		{
			Error("Could not load glTF buffers for '%s': %s\n", sFile.c_str(), GetGLTFResultName(result));
			cgltf_free(*apData);
			*apData = NULL;
			return false;
		}

		result = cgltf_validate(*apData);
		if(result != cgltf_result_success)
		{
			Error("glTF file '%s' did not validate: %s\n", sFile.c_str(), GetGLTFResultName(result));
			cgltf_free(*apData);
			*apData = NULL;
			return false;
		}

		return true;
	}

	//-----------------------------------------------------------------------

	void cMeshLoaderGLTF::BuildNodeNameMap(cgltf_data* apData, tNodeNameMap& aNodeNames)
	{
		tStringSet setUsedNames;

		for(size_t i=0; i<apData->nodes_count; ++i)
		{
			cgltf_node* pNode = &apData->nodes[i];
			tString sBaseName = GetSafeName(pNode->name, "");

			if(sBaseName == "" && pNode->mesh)
			{
				sBaseName = GetSafeName(pNode->mesh->name, "");
			}

			if(sBaseName == "")
			{
				sBaseName = "gltf_node_" + cString::ToString((unsigned long)i);
			}

			aNodeNames.insert(tNodeNameMap::value_type(pNode, MakeUniqueName(sBaseName, setUsedNames)));
		}
	}

	//-----------------------------------------------------------------------

	void cMeshLoaderGLTF::CreateNodeHierarchy(cMesh* apMesh, cNode3D* apParentNode, const cgltf_node* apNode, const tNodeNameMap& aNodeNames)
	{
		tString sNodeName = GetMappedNodeName(apNode, aNodeNames);
		cNode3D* pNode = apParentNode->CreateChild(sNodeName);
		apMesh->AddNode(pNode);

		pNode->SetMatrix(GetNodeLocalMatrix(apNode));

		if(apNode->mesh && apNode->mesh->primitives_count <= 1)
			pNode->SetCustomFlags(1);
		else
			pNode->SetCustomFlags(0);

		if(apNode->skin)
		{
			Warning("glTF skin on node '%s' is not imported yet; loading mesh data in bind pose.\n", sNodeName.c_str());
		}

		if(apNode->mesh && apNode->mesh->primitives_count > 1)
		{
			for(size_t i=0; i<apNode->mesh->primitives_count; ++i)
			{
				tString sSubMeshName = GetSubMeshName(apNode, (int)i, aNodeNames);
				cNode3D* pPrimitiveNode = pNode->CreateChild(sSubMeshName);
				apMesh->AddNode(pPrimitiveNode);
				pPrimitiveNode->SetMatrix(cMatrixf::Identity);
				pPrimitiveNode->SetCustomFlags(1);
			}
		}

		for(size_t i=0; i<apNode->children_count; ++i)
		{
			CreateNodeHierarchy(apMesh, pNode, apNode->children[i], aNodeNames);
		}
	}

	//-----------------------------------------------------------------------

	void cMeshLoaderGLTF::ImportNodeMeshes(cMesh* apMesh, const cgltf_node* apNode, const tNodeNameMap& aNodeNames, const tWString& asFile, tMeshLoadFlag aFlags)
	{
		if(apNode->mesh)
		{
			for(size_t i=0; i<apNode->mesh->primitives_count; ++i)
			{
				CreateSubMeshFromPrimitive(apMesh, apNode, &apNode->mesh->primitives[i], (int)i, aNodeNames, asFile, aFlags);
			}
		}

		for(size_t i=0; i<apNode->children_count; ++i)
		{
			ImportNodeMeshes(apMesh, apNode->children[i], aNodeNames, asFile, aFlags);
		}
	}

	//-----------------------------------------------------------------------

	cSubMesh* cMeshLoaderGLTF::CreateSubMeshFromPrimitive(cMesh* apMesh, const cgltf_node* apNode, const cgltf_primitive* apPrimitive, int alPrimitiveIndex, const tNodeNameMap& aNodeNames, const tWString& asFile, tMeshLoadFlag aFlags)
	{
		if(apPrimitive->has_draco_mesh_compression)
		{
			Warning("Skipping Draco-compressed glTF primitive on node '%s'.\n", GetMappedNodeName(apNode, aNodeNames).c_str());
			return NULL;
		}

		const cgltf_accessor* pPosition = FindAttributeAccessor(apPrimitive, cgltf_attribute_type_position, 0);
		if(pPosition == NULL || pPosition->count == 0)
		{
			Warning("Skipping glTF primitive on node '%s' because it has no positions.\n", GetMappedNodeName(apNode, aNodeNames).c_str());
			return NULL;
		}
		if(pPosition->count > (size_t)(std::numeric_limits<int>::max)())
		{
			Warning("Skipping glTF primitive on node '%s' because it has too many vertices for HPL.\n", GetMappedNodeName(apNode, aNodeNames).c_str());
			return NULL;
		}

		const cgltf_accessor* pNormal = FindAttributeAccessor(apPrimitive, cgltf_attribute_type_normal, 0);
		const cgltf_accessor* pTexCoord = FindAttributeAccessor(apPrimitive, cgltf_attribute_type_texcoord, 0);
		const cgltf_accessor* pColor = FindAttributeAccessor(apPrimitive, cgltf_attribute_type_color, 0);
		const cgltf_accessor* pTangent = FindAttributeAccessor(apPrimitive, cgltf_attribute_type_tangent, 0);

		tVertexVec vVertices;
		tFloatVec vTangents;
		vVertices.resize(pPosition->count);
		if(pTangent) vTangents.resize(pPosition->count * 4);

		for(size_t i=0; i<pPosition->count; ++i)
		{
			cVertex& vtx = vVertices[i];
			vtx.pos = cVector3f(0, 0, 0);
			vtx.norm = cVector3f(0, 1, 0);
			vtx.tex = cVector3f(0, 0, 0);
			vtx.tan = cVector3f(1, 0, 0);
			vtx.col = cColor(1, 1);

			ReadAccessorVec3(pPosition, i, vtx.pos);

			if(pNormal) ReadAccessorVec3(pNormal, i, vtx.norm);

			if(pTexCoord)
			{
				ReadAccessorVec2(pTexCoord, i, vtx.tex);
			}

			if(pColor) ReadAccessorColor(pColor, i, vtx.col);

			if(pTangent)
			{
				float fW = 1.0f;
				ReadAccessorTangent(pTangent, i, vtx.tan, fW);
				vTangents[i*4 + 0] = vtx.tan.x;
				vTangents[i*4 + 1] = vtx.tan.y;
				vTangents[i*4 + 2] = vtx.tan.z;
				// HPL flips V on import, which also reverses the tangent-space bitangent.
				vTangents[i*4 + 3] = -fW;
			}
		}

		tUIntVec vIndices;
		if(BuildPrimitiveIndices(apPrimitive, pPosition->count, vIndices)==false)
		{
			Warning("Skipping non-triangle glTF primitive on node '%s'.\n", GetMappedNodeName(apNode, aNodeNames).c_str());
			return NULL;
		}
		if(vIndices.size() > (size_t)(std::numeric_limits<int>::max)())
		{
			Warning("Skipping glTF primitive on node '%s' because it has too many indices for HPL.\n", GetMappedNodeName(apNode, aNodeNames).c_str());
			return NULL;
		}

		if(pNormal == NULL)
		{
			GenerateNormals(vVertices, vIndices);
		}
		else
		{
			for(size_t i=0; i<vVertices.size(); ++i)
			{
				if(vVertices[i].norm.SqrLength() > 0) vVertices[i].norm.Normalize();
			}
		}

		iVertexBuffer *pVtxBuff = mpLowLevelGraphics->CreateVertexBuffer(
			eVertexBufferType_Hardware,
			eVertexBufferDrawType_Tri,
			eVertexBufferUsageType_Static,
			(int)vVertices.size(),
			(int)vIndices.size());
		if(pVtxBuff == NULL)
		{
			Error("Could not create a vertex buffer for glTF primitive on node '%s'.\n", GetMappedNodeName(apNode, aNodeNames).c_str());
			return NULL;
		}

		tString sSubMeshName = GetSubMeshName(apNode, alPrimitiveIndex, aNodeNames);
		cSubMesh* pSubMesh = apMesh->CreateSubMesh(sSubMeshName);
		pSubMesh->SetLocalTransform(apNode->mesh && apNode->mesh->primitives_count <= 1 ? GetNodeLocalMatrix(apNode) : cMatrixf::Identity);
		pSubMesh->SetIsCollideShape(IsColliderMeshName(sSubMeshName));

		pVtxBuff->CreateElementArray(eVertexBufferElement_Position, eVertexBufferElementFormat_Float, 4);
		pVtxBuff->CreateElementArray(eVertexBufferElement_Normal, eVertexBufferElementFormat_Float, 3);
		pVtxBuff->CreateElementArray(eVertexBufferElement_Texture0, eVertexBufferElementFormat_Float, 3);
		pVtxBuff->CreateElementArray(eVertexBufferElement_Color0, eVertexBufferElementFormat_Float, 4);

		bool bCanGenerateTangents = pTangent == NULL && pTexCoord != NULL;
		if(pTangent || bCanGenerateTangents==false)
		{
			pVtxBuff->CreateElementArray(eVertexBufferElement_Texture1Tangent, eVertexBufferElementFormat_Float, 4);
		}

		for(size_t i=0; i<vVertices.size(); ++i)
		{
			pVtxBuff->AddVertexVec3f(eVertexBufferElement_Position, vVertices[i].pos);
			pVtxBuff->AddVertexVec3f(eVertexBufferElement_Normal, vVertices[i].norm);
			pVtxBuff->AddVertexVec3f(eVertexBufferElement_Texture0, vVertices[i].tex);
			pVtxBuff->AddVertexColor(eVertexBufferElement_Color0, vVertices[i].col);

			if(pTangent)
			{
				pVtxBuff->AddVertexVec4f(eVertexBufferElement_Texture1Tangent, vVertices[i].tan, vTangents[i*4 + 3]);
			}
			else if(bCanGenerateTangents==false)
			{
				pVtxBuff->AddVertexVec4f(eVertexBufferElement_Texture1Tangent, cVector3f(1, 0, 0), 1.0f);
			}
		}

		for(size_t i=0; i<vIndices.size(); ++i)
		{
			pVtxBuff->AddIndex(vIndices[i]);
		}

		pSubMesh->SetVertexBuffer(pVtxBuff);

		if(apPrimitive->material)
		{
			pSubMesh->SetDoubleSided(apPrimitive->material->double_sided != 0);

			tString sMatName = GetMaterialTextureFile(apPrimitive->material);
			if(sMatName != "")
			{
				tWString sRelativePath = cString::GetRelativePathW(cString::GetFilePathW(asFile), cPlatform::GetWorkingDir());
				sMatName = cString::SetFilePath(sMatName, cString::To8Char(sRelativePath));

				pSubMesh->SetMaterialName(cString::SetFileExt(sMatName, "mat"));

				if(mpMeshManager->GetUseFastloadMaterial())
				{
					sMatName = mpMeshManager->GetFastloadMaterial();
				}

				if((aFlags & eMeshLoadFlag_NoMaterial)==0 && sMatName != "")
				{
					cMaterial *pMaterial = mpMaterialManager->CreateMaterial(sMatName);
					if(pMaterial==NULL)
					{
						Error("Couldn't create material '%s' for glTF object '%s'\n", sMatName.c_str(), sSubMeshName.c_str());
					}
					pSubMesh->SetMaterial(pMaterial);
				}
			}
			else
			{
				pSubMesh->SetMaterialName("");
			}
		}
		else
		{
			pSubMesh->SetMaterialName("");
		}

		pSubMesh->Compile();
		pVtxBuff->Compile(bCanGenerateTangents ? eVertexCompileFlag_CreateTangents : 0);

		return pSubMesh;
	}

	//-----------------------------------------------------------------------

	cAnimation* cMeshLoaderGLTF::CreateAnimationFromGLTF(cgltf_data* apData, cgltf_animation* apGLTFAnimation, const tWString& asFile, const tString& asResourceName, const tNodeNameMap& aNodeNames, const tNodeSet& aSceneNodes)
	{
		if(apGLTFAnimation == NULL || apGLTFAnimation->channels_count == 0) return NULL;

		tGLTFAnimTrackMap mapAnimTracks;

		for(size_t i=0; i<apGLTFAnimation->channels_count; ++i)
		{
			cgltf_animation_channel* pChannel = &apGLTFAnimation->channels[i];
			if(pChannel->target_node == NULL || pChannel->sampler == NULL) continue;
			if(aSceneNodes.find(pChannel->target_node) == aSceneNodes.end()) continue;

			if(pChannel->target_path == cgltf_animation_path_type_scale)
			{
				Warning("glTF scale animation on node '%s' is ignored by HPL animation tracks.\n", GetMappedNodeName(pChannel->target_node, aNodeNames).c_str());
				continue;
			}

			if(pChannel->target_path != cgltf_animation_path_type_translation &&
				pChannel->target_path != cgltf_animation_path_type_rotation)
			{
				continue;
			}

			cGLTFAnimTrackData& trackData = mapAnimTracks[pChannel->target_node];
			trackData.mpNode = pChannel->target_node;

			if(pChannel->target_path == cgltf_animation_path_type_translation)
			{
				trackData.mpTranslation = pChannel;
			}
			else if(pChannel->target_path == cgltf_animation_path_type_rotation)
			{
				trackData.mpRotation = pChannel;
			}

			AddSamplerTimes(pChannel, trackData.mvTimes);
		}

		if(mapAnimTracks.empty()) return NULL;

		tString sFileName = cString::GetFileName(cString::To8Char(asFile));
		cAnimation *pAnimation = hplNew( cAnimation, (asResourceName, asFile, sFileName) );
		pAnimation->SetAnimationName(GetSafeName(apGLTFAnimation->name, asResourceName));
		pAnimation->ReserveTrackNum((int)apData->nodes_count);

		float fAnimationEnd = 0;

		for(size_t node=0; node<apData->nodes_count; ++node)
		{
			cgltf_node* pNode = &apData->nodes[node];
			if(aSceneNodes.find(pNode) == aSceneNodes.end()) continue;

			tString sTrackName = GetMappedNodeName(pNode, aNodeNames);
			if(HasUnsupportedNodeScale(pNode))
			{
				Warning("glTF node '%s' has scale or reflection that HPL animation tracks cannot represent.\n", sTrackName.c_str());
			}

			cAnimationTrack *pTrack = pAnimation->CreateTrack(sTrackName, eAnimTransformFlag_Rotate | eAnimTransformFlag_Translate);
			pTrack->SetNodeIndex(-1);

			cVector3f vBaseTrans = GetNodeBaseTranslation(pNode);
			cQuaternion qBaseRot = GetNodeBaseRotation(pNode);

			tGLTFAnimTrackMapIt it = mapAnimTracks.find(pNode);
			if(it == mapAnimTracks.end())
			{
				cKeyFrame *pFrame = pTrack->CreateKeyFrame(0);
				pFrame->trans = vBaseTrans;
				pFrame->rotation = qBaseRot;
				continue;
			}

			cGLTFAnimTrackData& animTrack = it->second;
			SortAndUniqueTimes(animTrack.mvTimes);
			if(animTrack.mvTimes.empty()) animTrack.mvTimes.push_back(0);

			for(size_t time=0; time<animTrack.mvTimes.size(); ++time)
			{
				float fTime = animTrack.mvTimes[time];
				cKeyFrame *pFrame = pTrack->CreateKeyFrame(fTime);

				cVector3f vTrans = vBaseTrans;
				cQuaternion qRot = qBaseRot;

				ReadChannelVec3(animTrack.mpTranslation, fTime, vBaseTrans, vTrans);
				ReadChannelQuat(animTrack.mpRotation, fTime, qBaseRot, qRot);

				pFrame->trans = vTrans;
				pFrame->rotation = qRot;

				if(fAnimationEnd < fTime) fAnimationEnd = fTime;
			}
		}

		pAnimation->SetLength(fAnimationEnd);

		return pAnimation;
	}

	//-----------------------------------------------------------------------
}
