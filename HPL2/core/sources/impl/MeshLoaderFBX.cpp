/*
 * Copyright © 2011-2020 Frictional Games
 * 
 * This file is part of Amnesia: A Machine For Pigs.
 * 
 * Amnesia: A Machine For Pigs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 

 * Amnesia: A Machine For Pigs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: A Machine For Pigs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "impl/MeshLoaderFBX.h"
#include "impl/MeshLoaderMSH.h"
#include "ufbx.h"

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
#include "graphics/Skeleton.h"
#include "graphics/Bone.h"
#include "graphics/Animation.h"
#include "graphics/AnimationTrack.h"
#include "math/Math.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <map>
#include <memory>
#include <set>

namespace hpl {
namespace {

typedef std::unique_ptr<ufbx_scene, void(*)(ufbx_scene*)> tFBXScenePtr;

tString FBXString(const ufbx_string& aString)
{
	return aString.length ? tString(aString.data, aString.length) : tString();
}

bool FBXError(const tWString& asFile, const char* asMessage, const char* asNode = "")
{
	Error("FBX '%s': %s%s%s\n", cString::To8Char(asFile).c_str(), asMessage,
		asNode[0] ? ": " : "", asNode);
	return false;
}

cVector3f FBXVector(const ufbx_vec3& avValue)
{
	return cVector3f((float)avValue.x, (float)avValue.y, (float)avValue.z);
}

cQuaternion FBXQuaternion(const ufbx_quat& aqValue)
{
	cQuaternion q((float)aqValue.w, (float)aqValue.x, (float)aqValue.y, (float)aqValue.z);
	q.Normalize();
	return q;
}

cMatrixf FBXMatrix(const ufbx_matrix& aMatrix)
{
	cMatrixf m = cMatrixf::Identity;
	for(int row = 0; row < 3; ++row)
		for(int col = 0; col < 4; ++col)
			m.m[row][col] = (float)aMatrix.v[col * 3 + row];
	return m;
}

bool FBXFinite(const cVector3f& avValue)
{
	return std::isfinite(avValue.x) && std::isfinite(avValue.y) && std::isfinite(avValue.z);
}

bool FBXInvertible(const ufbx_matrix& aMatrix)
{
	for(size_t i = 0; i < 12; ++i)
		if(!std::isfinite(aMatrix.v[i])) return false;
	return std::fabs(ufbx_matrix_determinant(&aMatrix)) > 1.0e-15;
}

bool FBXMatrixEqual(const ufbx_matrix& a, const ufbx_matrix& b)
{
	for(size_t i = 0; i < 12; ++i)
		if(!std::isfinite(a.v[i]) || !std::isfinite(b.v[i]) ||
			std::fabs(a.v[i] - b.v[i]) > 1.0e-4 * std::max(1.0, std::max(std::fabs(a.v[i]), std::fabs(b.v[i]))))
			return false;
	return true;
}

tFBXScenePtr FBXLoadScene(const tWString& asFile)
{
	// Use the engine's wide-character file API, including on Windows.
	FILE* pFile = cPlatform::OpenFile(asFile, _W("rb"));
	if(!pFile)
	{
		FBXError(asFile, "could not open source file");
		return tFBXScenePtr(NULL, ufbx_free_scene);
	}
	ufbx_load_opts opts = {};
	opts.file_format = UFBX_FILE_FORMAT_FBX;
	opts.generate_missing_normals = true;
	// The original AMFP importer did not convert FBX axes or units. Applying
	// target_axes/target_unit_meters here would invalidate shipped entity data.
	// Keep geometric transforms separate: the AMFP mesh and animation paths
	// intentionally apply different conventions, documented at each use below.
	ufbx_error error = {};
	ufbx_scene* pScene = ufbx_load_stdio(pFile, &opts, &error);
	fclose(pFile);
	if(!pScene) FBXError(asFile, "could not parse source", error.description.data);
	return tFBXScenePtr(pScene, ufbx_free_scene);
}

struct cFBXSkeletonData
{
	cFBXSkeletonData() : mpSkeleton(NULL) {}
	~cFBXSkeletonData() { if(mpSkeleton) hplDelete(mpSkeleton); }
	cSkeleton* Release() { cSkeleton* pResult = mpSkeleton; mpSkeleton = NULL; return pResult; }

	cSkeleton* mpSkeleton;
	std::vector<bool> mvSelected;
	std::vector<bool> mvHasBind;
	std::vector<bool> mvDeformingSubtree;
	std::vector<int> mvBoneIndices;
	std::vector<ufbx_matrix> mvBindWorld;
	std::vector<ufbx_matrix> mvBindLocal;
	std::vector<const ufbx_node*> mvNodes;
	std::set<tString> mNames;
};

bool FBXCreateBones(const ufbx_node* apNode, cBone* apParent, int alParentNode,
	cFBXSkeletonData& aData, const tWString& asFile)
{
	const uint32_t id = apNode->typed_id;
	if(aData.mvSelected[id])
	{
		const tString sName = FBXString(apNode->name);
		if(sName.empty() || !aData.mNames.insert(sName).second)
			return FBXError(asFile, "empty or duplicate bone name", sName.c_str());
		// Skinning buffers store bone indices as unsigned bytes on both builds.
		if(aData.mpSkeleton->GetBoneNum() >= 256)
			return FBXError(asFile, "skeleton exceeds HPL's 256-bone limit");
		// AMFP's SDK loader starts unclustered bones at identity in their
		// parent's bind space. These placeholders are significant: shipped ANM
		// tracks include the full motion for such roots, rather than subtracting
		// the FBX default pose. Preserve this when mixing raw FBX and caches.
		if(!aData.mvHasBind[id])
			aData.mvBindWorld[id] = alParentNode >= 0 ? aData.mvBindWorld[alParentNode] : ufbx_identity_matrix;
		if(!FBXInvertible(aData.mvBindWorld[id]))
			return FBXError(asFile, "singular or non-finite bone bind transform", sName.c_str());
		ufbx_matrix local = aData.mvBindWorld[id];
		if(alParentNode >= 0)
		{
			ufbx_matrix inverse = ufbx_matrix_invert(&aData.mvBindWorld[alParentNode]);
			local = ufbx_matrix_mul(&inverse, &local);
		}
		cBone* pBone = apParent->CreateChildBone(sName, sName);
		pBone->SetTransform(FBXMatrix(local));
		const ufbx_transform transform = ufbx_matrix_to_transform(&local);
		pBone->SetTransformUnscaled(cMath::MatrixQuaternion(FBXQuaternion(transform.rotation)));
		aData.mvBoneIndices[id] = aData.mpSkeleton->GetBoneNum() - 1;
		aData.mvBindLocal[id] = local;
		aData.mvNodes.push_back(apNode);
		apParent = pBone;
		alParentNode = (int)id;
	}
	for(size_t i = 0; i < apNode->children.count; ++i)
		if(!FBXCreateBones(apNode->children[i], apParent, alParentNode, aData, asFile)) return false;
	return true;
}

void FBXCollectBindPoses(const ufbx_node* apNode, cFBXSkeletonData& aData,
	const tWString& asFile, bool& abHasBones, bool& abWarned)
{
	// Match the SDK loader's depth-first LoadSceneRec/LoadMeshData order.
	// Some shipped child animations contain multiple controller meshes with
	// differing Head bind poses. Their ANM caches use the last encountered
	// cluster, so choosing the first cluster or averaging would change motion.
	if(apNode->mesh)
		for(size_t i = 0; i < apNode->mesh->skin_deformers.count; ++i)
			for(size_t j = 0; j < apNode->mesh->skin_deformers[i]->clusters.count; ++j)
			{
				const ufbx_skin_cluster* pCluster = apNode->mesh->skin_deformers[i]->clusters[j];
				if(!pCluster->bone_node) continue;
				const uint32_t id = pCluster->bone_node->typed_id;
				if(aData.mvHasBind[id] && !FBXMatrixEqual(aData.mvBindWorld[id], pCluster->bind_to_world) && !abWarned)
				{
					Warning("FBX '%s': differing controller bind poses; using the last scene-order cluster as in the original importer\n", cString::To8Char(asFile).c_str());
					abWarned = true;
				}
				aData.mvSelected[id] = abHasBones = true;
				aData.mvBindWorld[id] = pCluster->bind_to_world;
				aData.mvHasBind[id] = true;
			}
	for(size_t i = 0; i < apNode->children.count; ++i)
		FBXCollectBindPoses(apNode->children[i], aData, asFile, abHasBones, abWarned);
}

bool FBXLoadSkeleton(const ufbx_scene* apScene, cFBXSkeletonData& aData, const tWString& asFile)
{
	if(apScene->nodes.count > INT_MAX) return FBXError(asFile, "too many scene nodes");
	const size_t count = apScene->nodes.count;
	aData.mvSelected.resize(count, false);
	aData.mvHasBind.resize(count, false);
	aData.mvDeformingSubtree.resize(count, false);
	aData.mvBoneIndices.resize(count, -1);
	aData.mvBindWorld.resize(count);
	aData.mvBindLocal.resize(count);
	bool bHasBones = false;
	for(size_t i = 0; i < count; ++i)
	{
		const ufbx_node* pNode = apScene->nodes[i];
		aData.mvBindWorld[i] = pNode->node_to_world;
		if(pNode->bone && !pNode->is_root) aData.mvSelected[i] = bHasBones = true;
	}
	bool warnedBind = false;
	FBXCollectBindPoses(apScene->root_node, aData, asFile, bHasBones, warnedBind);
	if(!bHasBones) return true;
	for(size_t i = 0; i < count; ++i)
	{
		const ufbx_node* pNode = apScene->nodes[i];
		const bool hasGeometry = pNode->mesh && pNode->mesh->num_triangles &&
			FBXString(pNode->name).compare(0, 4, "CON_") != 0;
		if(aData.mvHasBind[i] || hasGeometry)
			for(const ufbx_node* pParent = pNode; pParent; pParent = pParent->parent)
				aData.mvDeformingSubtree[pParent->typed_id] = true;
	}
	aData.mpSkeleton = hplNew(cSkeleton, ());
	return FBXCreateBones(apScene->root_node, aData.mpSkeleton->GetRootBone(), -1, aData, asFile);
}

struct cFBXVertex
{
	cVector3f pos, normal, uv, tangent, bitangent;
	cColor color;
	uint32_t controlPoint;
	float tangentSign;
};

tString FBXMaterialName(const ufbx_material* apMaterial)
{
	if(!apMaterial) return "";
	tString sName = FBXString(apMaterial->name);
	const ufbx_texture* pTexture = apMaterial->fbx.diffuse_color.texture;
	// FBX surface material names identify HPL .mat resources. Texture paths
	// may be stale authoring references (gent still points at wretch.dds), and
	// texture object labels such as "Map #15" are not material resource names.
	if(sName.empty() && pTexture)
	{
		tString sTexture = FBXString(pTexture->relative_filename);
		if(sTexture.empty()) sTexture = FBXString(pTexture->filename);
		if(sTexture.empty()) sTexture = FBXString(pTexture->name);
		if(!sTexture.empty()) sName = cString::GetFileName(sTexture);
	}
	return sName.empty() ? sName : cString::SetFileExt(sName, "mat");
}

void FBXBuildTangents(std::vector<cFBXVertex>& avVertices, const tUIntVec& avIndices, bool abImported)
{
	if(!abImported)
	{
		for(size_t i = 0; i < avIndices.size(); i += 3)
		{
			cFBXVertex& a = avVertices[avIndices[i]];
			cFBXVertex& b = avVertices[avIndices[i + 1]];
			cFBXVertex& c = avVertices[avIndices[i + 2]];
			const cVector3f edge1 = b.pos - a.pos, edge2 = c.pos - a.pos;
			const cVector3f uv1 = b.uv - a.uv, uv2 = c.uv - a.uv;
			const float det = uv1.x * uv2.y - uv1.y * uv2.x;
			if(std::fabs(det) < 1.0e-12f) continue;
			const cVector3f tangent = (edge1 * uv2.y - edge2 * uv1.y) / det;
			const cVector3f bitangent = (edge2 * uv1.x - edge1 * uv2.x) / det;
			a.tangent += tangent; b.tangent += tangent; c.tangent += tangent;
			a.bitangent += bitangent; b.bitangent += bitangent; c.bitangent += bitangent;
		}
	}
	for(size_t i = 0; i < avVertices.size(); ++i)
	{
		cFBXVertex& vertex = avVertices[i];
		vertex.tangent -= vertex.normal * cMath::Vector3Dot(vertex.normal, vertex.tangent);
		if(vertex.tangent.Normalize() <= 1.0e-8f)
		{
			const cVector3f axis = std::fabs(vertex.normal.x) < 0.9f ? cVector3f(1,0,0) : cVector3f(0,1,0);
			vertex.tangent = cMath::Vector3Cross(axis, vertex.normal);
			vertex.tangent.Normalize();
		}
		vertex.tangentSign = cMath::Vector3Dot(cMath::Vector3Cross(vertex.normal, vertex.tangent), vertex.bitangent) < 0 ? -1.0f : 1.0f;
	}
}

bool FBXLoadMeshNode(const ufbx_node* apNode, cMesh* apResult, const cFBXSkeletonData& aSkeleton,
	iLowLevelGraphics* apGraphics, cMaterialManager* apMaterials, cMeshManager* apMeshes,
	const tWString& asFile, tMeshLoadFlag aFlags)
{
	const ufbx_mesh* pMesh = apNode->mesh;
	if(!pMesh || pMesh->num_triangles == 0 || FBXString(apNode->name).compare(0, 4, "CON_") == 0) return true;
	if(pMesh->num_indices > INT_MAX || pMesh->num_triangles > INT_MAX / 3)
		return FBXError(asFile, "mesh exceeds HPL index limits", apNode->name.data);
	if(pMesh->skin_deformers.count > 1)
		return FBXError(asFile, "multiple skin deformers are not representable by HPL", apNode->name.data);
	if(pMesh->blend_deformers.count || pMesh->cache_deformers.count)
		return FBXError(asFile, "blend shapes and vertex cache deformation are not supported by HPL", apNode->name.data);
	const ufbx_skin_deformer* pSkin = pMesh->skin_deformers.count ? pMesh->skin_deformers[0] : NULL;
	int rigidBone = -1;
	const ufbx_node* pRigidNode = NULL;
	if(!pSkin && !aSkeleton.mvNodes.empty())
	{
		// HPL skins every submesh of a skeletal mesh. A rigid attachment has an
		// unambiguous owner only when it is parented beneath an imported bone.
		for(const ufbx_node* pParent = apNode; pParent; pParent = pParent->parent)
			if(aSkeleton.mvBoneIndices[pParent->typed_id] >= 0)
			{
				rigidBone = aSkeleton.mvBoneIndices[pParent->typed_id];
				pRigidNode = pParent;
				break;
			}
		if(rigidBone < 0)
			return FBXError(asFile, "unskinned mesh in a skeletal scene has no bone parent", apNode->name.data);
	}
	if(pSkin && pSkin->skinning_method != UFBX_SKINNING_METHOD_LINEAR && pSkin->skinning_method != UFBX_SKINNING_METHOD_RIGID)
		return FBXError(asFile, "dual-quaternion skinning is not supported by HPL", apNode->name.data);
	// Match the SDK loader's EvaluateGlobalTransform() for mesh positions.
	// Shipped child meshes have non-zero geometric translations and different
	// cluster bind poses, but their MSH caches deliberately contain node-space
	// geometry transformed by node_to_world alone. Applying geometry_to_world
	// or recovering a mesh bind transform would move body parts out of place.
	ufbx_matrix transform = apNode->node_to_world;
	if(pRigidNode)
	{
		if(!FBXInvertible(pRigidNode->node_to_world))
			return FBXError(asFile, "singular rigid attachment parent transform", apNode->name.data);
		const ufbx_matrix inverse = ufbx_matrix_invert(&pRigidNode->node_to_world);
		const ufbx_matrix local = ufbx_matrix_mul(&inverse, &transform);
		transform = ufbx_matrix_mul(&aSkeleton.mvBindWorld[pRigidNode->typed_id], &local);
	}
	if(!FBXInvertible(transform)) return FBXError(asFile, "singular or non-finite mesh transform", apNode->name.data);
	const ufbx_matrix normalTransform = ufbx_matrix_for_normals(&transform);
	const bool bReverse = ufbx_matrix_determinant(&transform) >= 0;
	const bool bTangents = pMesh->vertex_tangent.exists && pMesh->vertex_bitangent.exists;
	std::vector<uint32_t> triangles(pMesh->max_face_triangles * 3);

	for(size_t partIndex = 0; partIndex < pMesh->material_parts.count; ++partIndex)
	{
		const ufbx_mesh_part& part = pMesh->material_parts[partIndex];
		if(part.num_triangles == 0) continue;
		const ufbx_material* pMaterial = partIndex < apNode->materials.count ? apNode->materials[partIndex] : NULL;
		cColor color(1,1);
		if(pMaterial && pMaterial->fbx.diffuse_color.has_value)
		{
			const ufbx_vec3& c = pMaterial->fbx.diffuse_color.value_vec3;
			color = cColor((float)c.x, (float)c.y, (float)c.z, (float)pMaterial->fbx.diffuse_factor.value_real);
		}
		std::vector<cFBXVertex> vertices;
		tUIntVec indices;
		typedef std::array<uint32_t, 7> tVertexKey;
		std::map<tVertexKey, uint32_t> vertexMap;
		indices.reserve(part.num_triangles * 3);
		for(size_t faceIndex = 0; faceIndex < part.face_indices.count; ++faceIndex)
		{
			const ufbx_face face = pMesh->faces[part.face_indices[faceIndex]];
			if(face.num_indices < 3) continue;
			const uint32_t numTriangles = ufbx_triangulate_face(triangles.data(), triangles.size(), pMesh, face);
			for(size_t i = 0; i < size_t(numTriangles) * 3; ++i)
			{
				const uint32_t corner = triangles[i];
				tVertexKey key = {{
					pMesh->vertex_indices[corner], pMesh->vertex_normal.indices[corner],
					pMesh->vertex_uv.exists ? pMesh->vertex_uv.indices[corner] : UFBX_NO_INDEX,
					pMesh->vertex_color.exists ? pMesh->vertex_color.indices[corner] : UFBX_NO_INDEX,
					bTangents ? pMesh->vertex_tangent.indices[corner] : UFBX_NO_INDEX,
					bTangents ? pMesh->vertex_bitangent.indices[corner] : UFBX_NO_INDEX, 0
				}};
				std::map<tVertexKey, uint32_t>::iterator found = vertexMap.find(key);
				uint32_t index;
				if(found != vertexMap.end()) index = found->second;
				else
				{
					cFBXVertex vertex;
					vertex.pos = FBXVector(ufbx_transform_position(&transform, pMesh->vertex_position[corner]));
					vertex.normal = FBXVector(ufbx_transform_direction(&normalTransform, pMesh->vertex_normal[corner]));
					vertex.normal.Normalize();
					vertex.uv = cVector3f(0,0,0);
					if(pMesh->vertex_uv.exists)
					{
						const ufbx_vec2 uv = pMesh->vertex_uv[corner];
						vertex.uv = cVector3f((float)uv.x, 1.0f - (float)uv.y, 0);
					}
					vertex.color = color;
					if(pMesh->vertex_color.exists)
					{
						const ufbx_vec4 c = pMesh->vertex_color[corner];
						vertex.color = cColor((float)c.x, (float)c.y, (float)c.z, (float)c.w);
					}
					vertex.tangent = cVector3f(0,0,0);
					vertex.bitangent = cVector3f(0,0,0);
					if(bTangents)
					{
						vertex.tangent = FBXVector(ufbx_transform_direction(&transform, pMesh->vertex_tangent[corner]));
						// Flipping V also flips the bitangent direction.
						vertex.bitangent = FBXVector(ufbx_transform_direction(&transform, pMesh->vertex_bitangent[corner])) * -1.0f;
					}
					vertex.controlPoint = key[0];
					vertex.tangentSign = 1.0f;
					if(!FBXFinite(vertex.pos) || !FBXFinite(vertex.normal) || !FBXFinite(vertex.uv) ||
						!FBXFinite(vertex.tangent) || !FBXFinite(vertex.bitangent))
						return FBXError(asFile, "non-finite vertex data", apNode->name.data);
					index = (uint32_t)vertices.size();
					vertices.push_back(vertex);
					vertexMap.insert(std::make_pair(key, index));
				}
				indices.push_back(index);
			}
		}
		if(indices.empty()) continue;
		// HPL's clockwise front faces differ from FBX. A reflected world transform
		// already reverses orientation and must not be reversed a second time.
		if(bReverse)
			for(size_t i = 0; i < indices.size(); i += 3) std::swap(indices[i], indices[i + 2]);
		FBXBuildTangents(vertices, indices, bTangents);
		tString sName = FBXString(apNode->name);
		if(pMesh->material_parts.count > 1) sName += "_" + cString::ToString((int)partIndex);
		cSubMesh* pSubMesh = apResult->CreateSubMesh(sName);
		iVertexBuffer* pBuffer = apGraphics->CreateVertexBuffer(eVertexBufferType_Hardware,
			eVertexBufferDrawType_Tri, eVertexBufferUsageType_Static, (int)vertices.size(), (int)indices.size());
		if(!pBuffer) return FBXError(asFile, "could not create vertex buffer", apNode->name.data);
		pSubMesh->SetVertexBuffer(pBuffer);
		pBuffer->CreateElementArray(eVertexBufferElement_Position, eVertexBufferElementFormat_Float, 4);
		pBuffer->CreateElementArray(eVertexBufferElement_Normal, eVertexBufferElementFormat_Float, 3);
		pBuffer->CreateElementArray(eVertexBufferElement_Texture0, eVertexBufferElementFormat_Float, 3);
		pBuffer->CreateElementArray(eVertexBufferElement_Color0, eVertexBufferElementFormat_Float, 4);
		pBuffer->CreateElementArray(eVertexBufferElement_Texture1Tangent, eVertexBufferElementFormat_Float, 4);
		for(size_t i = 0; i < vertices.size(); ++i)
		{
			const cFBXVertex& vertex = vertices[i];
			pBuffer->AddVertexVec4f(eVertexBufferElement_Position, vertex.pos, 1.0f);
			pBuffer->AddVertexVec3f(eVertexBufferElement_Normal, vertex.normal);
			pBuffer->AddVertexVec3f(eVertexBufferElement_Texture0, vertex.uv);
			pBuffer->AddVertexColor(eVertexBufferElement_Color0, vertex.color);
			pBuffer->AddVertexVec4f(eVertexBufferElement_Texture1Tangent, vertex.tangent, vertex.tangentSign);
			if(pSkin)
			{
				if(vertex.controlPoint >= pSkin->vertices.count)
					return FBXError(asFile, "vertex has no skin influences", apNode->name.data);
				const ufbx_skin_vertex& skinVertex = pSkin->vertices[vertex.controlPoint];
				const size_t limit = pSkin->skinning_method == UFBX_SKINNING_METHOD_RIGID ? 1 : 4;
				size_t numWeights = 0;
				unsigned int boneIndices[4];
				double weights[4], totalWeight = 0;
				for(size_t j = 0; j < skinVertex.num_weights && numWeights < limit; ++j)
				{
					const ufbx_skin_weight& weight = pSkin->weights[skinVertex.weight_begin + j];
					if(weight.weight <= 0) continue;
					const ufbx_node* pBone = pSkin->clusters[weight.cluster_index]->bone_node;
					const int boneIndex = pBone ? aSkeleton.mvBoneIndices[pBone->typed_id] : -1;
					if(boneIndex < 0 || !std::isfinite(weight.weight))
						return FBXError(asFile, "invalid skin influence", apNode->name.data);
					boneIndices[numWeights] = (unsigned int)boneIndex;
					weights[numWeights] = weight.weight;
					totalWeight += weight.weight;
					++numWeights;
				}
				if(numWeights == 0 || !std::isfinite(totalWeight) || totalWeight <= 0)
					return FBXError(asFile, "vertex has no positive skin influences", apNode->name.data);
				for(size_t j = 0; j < numWeights; ++j)
					pSubMesh->AddVertexBonePair(cVertexBonePair((unsigned int)i, boneIndices[j], (float)(weights[j] / totalWeight)));
			}
			else if(rigidBone >= 0)
				pSubMesh->AddVertexBonePair(cVertexBonePair((unsigned int)i, (unsigned int)rigidBone, 1.0f));
		}
		for(size_t i = 0; i < indices.size(); ++i) pBuffer->AddIndex(indices[i]);
		if(!pBuffer->Compile(0)) return FBXError(asFile, "could not compile vertex buffer", apNode->name.data);
		const tString sMaterial = FBXMaterialName(pMaterial);
		pSubMesh->SetMaterialName(sMaterial);
		if(!(aFlags & eMeshLoadFlag_NoMaterial) && !sMaterial.empty())
		{
			const tString sLoadMaterial = apMeshes->GetUseFastloadMaterial() ? apMeshes->GetFastloadMaterial() : sMaterial;
			pSubMesh->SetMaterial(apMaterials->CreateMaterial(sLoadMaterial));
		}
		pSubMesh->Compile();
	}
	return true;
}

// The runtime post-multiplies the full bind matrix by a rotation and adds
// translation; it does not animate scale. Test the actual linear delta rather
// than discarding shear/non-uniform scale through a lossy TRS decomposition.
bool FBXAnimationDelta(const ufbx_matrix& aBind, const ufbx_matrix& aAnimated, cKeyFrame& aKey)
{
	if(!FBXInvertible(aAnimated)) return false;
	ufbx_matrix inverse = ufbx_matrix_invert(&aBind);
	ufbx_matrix delta = ufbx_matrix_mul(&inverse, &aAnimated);
	delta.m03 = delta.m13 = delta.m23 = 0;
	ufbx_transform transform = ufbx_matrix_to_transform(&delta);
	transform.translation = ufbx_zero_vec3;
	transform.scale.x = transform.scale.y = transform.scale.z = 1;
	const ufbx_matrix rotation = ufbx_transform_to_matrix(&transform);
	if(!FBXMatrixEqual(delta, rotation)) return false;
	aKey.trans = cVector3f((float)(aAnimated.m03 - aBind.m03),
		(float)(aAnimated.m13 - aBind.m13), (float)(aAnimated.m23 - aBind.m23));
	aKey.rotation = FBXQuaternion(transform.rotation);
	return FBXFinite(aKey.trans);
}

cAnimation* FBXLoadAnimation(const ufbx_scene* apScene, const cFBXSkeletonData& aSkeleton, const tWString& asFile)
{
	const ufbx_anim_stack* pStack = NULL;
	for(size_t i = 0; i < apScene->anim_stacks.count; ++i)
		if(apScene->anim_stacks[i]->time_end > apScene->anim_stacks[i]->time_begin)
		{
			pStack = apScene->anim_stacks[i];
			break;
		}
	if(!pStack || pStack->layers.count == 0 || aSkeleton.mvNodes.empty())
	{
		FBXError(asFile, "no skeletal animation take found");
		return NULL;
	}
	ufbx_error error = {};
	const double begin = pStack->time_begin, end = pStack->time_end;
	if(!std::isfinite(begin) || !std::isfinite(end) || std::fabs(begin) > 1.0e7 || std::fabs(end) > 1.0e7)
	{
		FBXError(asFile, "invalid animation time span");
		return NULL;
	}
	std::set<float> times;
	std::vector<std::set<float> > trackTimes(aSkeleton.mvNodes.size());
	// Preserve authored per-bone keys, including empty tracks. The SDK loader
	// used the first layer's Lcl T/R/S curves and truncated FBX time to integer
	// milliseconds before storing float seconds. Resampling all bones at a
	// global frame rate changes sparse cloth curves and dormant marker tracks.
	const char* properties[] = { UFBX_Lcl_Translation, UFBX_Lcl_Rotation, UFBX_Lcl_Scaling };
	for(size_t i = 0; i < aSkeleton.mvNodes.size(); ++i)
	{
		for(size_t property = 0; property < 3; ++property)
		{
			const ufbx_anim_prop* pProp = ufbx_find_anim_prop(pStack->layers[0], &aSkeleton.mvNodes[i]->element, properties[property]);
			if(!pProp) continue;
			for(size_t axis = 0; axis < 3; ++axis)
			{
				const ufbx_anim_curve* pCurve = pProp->anim_value->curves[axis];
				if(!pCurve) continue;
				for(size_t k = 0; k < pCurve->keyframes.count; ++k)
				{
					const double sourceTime = pCurve->keyframes[k].time;
					if(!std::isfinite(sourceTime) || std::fabs(sourceTime) > 1.0e7)
					{
						FBXError(asFile, "invalid animation key time");
						return NULL;
					}
					const float time = (float)(int64_t)(sourceTime * 1000.0) / 1000.0f;
					trackTimes[i].insert(time);
					times.insert(time);
				}
			}
		}
	}
	if(times.size() > 1000000)
	{
		FBXError(asFile, "animation has too many samples");
		return NULL;
	}
	cAnimation* pAnimation = hplNew(cAnimation, (FBXString(pStack->name), asFile, cString::GetFileName(cString::To8Char(asFile))));
	pAnimation->SetLength((float)(int64_t)(end * 1000.0) / 1000.0f);
	std::vector<cAnimationTrack*> tracks;
	for(size_t i = 0; i < aSkeleton.mvNodes.size(); ++i)
		tracks.push_back(pAnimation->CreateTrack(FBXString(aSkeleton.mvNodes[i]->name), eAnimTransformFlag_Translate | eAnimTransformFlag_Rotate));
	bool warnedMarkerScale = false;
	for(std::set<float>::const_iterator it = times.begin(); it != times.end(); ++it)
	{
		const float time = *it;
		// Match the SDK's SetMilliSeconds((int64)(floatSeconds * 1000)).
		const double evaluateTime = (double)(int64_t)(time * 1000.0f) / 1000.0;
		ufbx_evaluate_opts evalOpts = {};
		tFBXScenePtr evaluated(ufbx_evaluate_scene(apScene, pStack->anim, evaluateTime, &evalOpts, &error), ufbx_free_scene);
		if(!evaluated)
		{
			FBXError(asFile, "could not evaluate animation", error.description.data);
			hplDelete(pAnimation);
			return NULL;
		}
		for(size_t i = 0; i < tracks.size(); ++i)
		{
			if(trackTimes[i].find(time) == trackTimes[i].end()) continue;
			const uint32_t id = aSkeleton.mvNodes[i]->typed_id;
			// The legacy animation path evaluates local, not world, and includes
			// bone geometric transforms before extracting its delta.
			const ufbx_node* pNode = evaluated->nodes[id];
			const ufbx_matrix local = ufbx_matrix_mul(&pNode->node_to_parent, &pNode->geometry_to_node);
			cKeyFrame key;
			if(!FBXAnimationDelta(aSkeleton.mvBindLocal[id], local, key))
			{
				if(aSkeleton.mvDeformingSubtree[id] || !FBXInvertible(local))
				{
					FBXError(asFile, "animated scale/shear cannot be represented by HPL rotation/translation tracks", aSkeleton.mvNodes[i]->name.data);
					hplDelete(pAnimation);
					return NULL;
				}
				// Unclustered end markers (eg. Biped toe nubs) have identity
				// bind placeholders in shipped caches. The SDK importer discarded
				// their scale. Retain its rotation/translation tracks, but never
				// apply this exception to a bone that can deform geometry.
				ufbx_matrix marker = local;
				// SDK GetElements assigns a reflection to all three scale axes,
				// whereas ufbx assigns it to one. Match the SDK rotation choice
				// for mirrored nondeforming Biped finger/toe end markers.
				if(ufbx_matrix_determinant(&marker) < 0)
					for(size_t k = 0; k < 9; ++k) marker.v[k] = -marker.v[k];
				const ufbx_transform transform = ufbx_matrix_to_transform(&marker);
				key.trans = FBXVector(transform.translation) - FBXVector(aSkeleton.mvBindLocal[id].cols[3]);
				key.rotation = FBXQuaternion(transform.rotation);
				if(!warnedMarkerScale)
				{
					Warning("FBX '%s': ignoring scale/shear on nondeforming bone markers, as in the original importer\n", cString::To8Char(asFile).c_str());
					warnedMarkerScale = true;
				}
			}
			cKeyFrame* pKey = tracks[i]->CreateKeyFrame(time);
			pKey->trans = key.trans;
			pKey->rotation = key.rotation;
		}
	}
	return pAnimation;
}

bool FBXUseCache(const tWString& asSource, const tWString& asCache)
{
	return cResources::GetForceCacheLoadingAndSkipSaving() || !cPlatform::FileExists(asSource) ||
		cPlatform::FileModifiedDate(asCache) > cPlatform::FileModifiedDate(asSource);
}

} // namespace

cMeshLoaderFBX::cMeshLoaderFBX(iLowLevelGraphics* apLowLevelGraphics, cMeshLoaderMSH* apMeshLoaderMSH,
	bool abLoadAndSaveMSHFormat) : iMeshLoader(apLowLevelGraphics),
	mpMeshLoaderMSH(apMeshLoaderMSH), mbLoadAndSaveMSHFormat(abLoadAndSaveMSHFormat)
{
	AddSupportedExtension("fbx");
}

cMeshLoaderFBX::~cMeshLoaderFBX() {}

cMesh* cMeshLoaderFBX::LoadMesh(const tWString& asFile, tMeshLoadFlag aFlags)
{
	const tWString sCache = cString::SetFileExtW(asFile, _W("msh"));
	if(mbLoadAndSaveMSHFormat && mpMeshLoaderMSH && FBXUseCache(asFile, sCache))
	{
		cMesh* pCached = mpMeshLoaderMSH->LoadMesh(sCache, aFlags);
		if(pCached) { pCached->SetFullPath(asFile); return pCached; }
	}
	tFBXScenePtr scene = FBXLoadScene(asFile);
	if(!scene) return NULL;
	cFBXSkeletonData skeleton;
	if(!FBXLoadSkeleton(scene.get(), skeleton, asFile)) return NULL;
	cMesh* pMesh = hplNew(cMesh, (cString::To8Char(asFile), asFile, mpMaterialManager, mpAnimationManager));
	pMesh->SetSkeleton(skeleton.Release());
	if(!(aFlags & eMeshLoadFlag_NoGeometry))
	{
		for(size_t i = 0; i < scene->nodes.count; ++i)
			if(!FBXLoadMeshNode(scene->nodes[i], pMesh, skeleton, mpLowLevelGraphics, mpMaterialManager, mpMeshManager, asFile, aFlags))
			{
				hplDelete(pMesh);
				return NULL;
			}
		if(pMesh->GetSubMeshNum() == 0)
		{
			FBXError(asFile, "no renderable triangle meshes found");
			hplDelete(pMesh);
			return NULL;
		}
		pMesh->CompileBonesAndSubMeshes();
	}
	if(mbLoadAndSaveMSHFormat && mpMeshLoaderMSH && !(aFlags & eMeshLoadFlag_NoGeometry) &&
		!cResources::GetForceCacheLoadingAndSkipSaving())
		mpMeshLoaderMSH->SaveMesh(pMesh, sCache);
	return pMesh;
}

cAnimation* cMeshLoaderFBX::LoadAnimation(const tWString& asFile)
{
	const tWString sCache = cString::SetFileExtW(asFile, _W("anm"));
	if(mbLoadAndSaveMSHFormat && mpMeshLoaderMSH && FBXUseCache(asFile, sCache))
	{
		cAnimation* pCached = mpMeshLoaderMSH->LoadAnimation(sCache);
		if(pCached) { pCached->SetFullPath(asFile); return pCached; }
	}
	tFBXScenePtr scene = FBXLoadScene(asFile);
	if(!scene) return NULL;
	cFBXSkeletonData skeleton;
	if(!FBXLoadSkeleton(scene.get(), skeleton, asFile)) return NULL;
	cAnimation* pAnimation = FBXLoadAnimation(scene.get(), skeleton, asFile);
	if(pAnimation && mbLoadAndSaveMSHFormat && mpMeshLoaderMSH && !cResources::GetForceCacheLoadingAndSkipSaving())
		mpMeshLoaderMSH->SaveAnimation(pAnimation, sCache);
	return pAnimation;
}

} // namespace hpl
