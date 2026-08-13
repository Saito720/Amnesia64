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

#ifndef HPL_MESH_LOADER_GLTF_H
#define HPL_MESH_LOADER_GLTF_H

#include <map>
#include <set>

#include "resources/MeshLoader.h"

typedef struct cgltf_data cgltf_data;
typedef struct cgltf_node cgltf_node;
typedef struct cgltf_primitive cgltf_primitive;
typedef struct cgltf_animation cgltf_animation;

namespace hpl {

	class cMeshLoaderMSH;
	class cMesh;
	class cNode3D;
	class cSubMesh;
	class cAnimation;

	class cMeshLoaderGLTF : public iMeshLoader
	{
	public:
		typedef std::map<const cgltf_node*, tString> tNodeNameMap;
		typedef std::set<const cgltf_node*> tNodeSet;

		cMeshLoaderGLTF(iLowLevelGraphics *apLowLevelGraphics, cMeshLoaderMSH *apMeshLoaderMSH, bool abLoadAndSaveMSHFormat);
		~cMeshLoaderGLTF();

		void SetLoadAndSaveMSHFormat(bool abX){ mbLoadAndSaveMSHFormat = abX; }

		cMesh* LoadMesh(const tWString& asFile, tMeshLoadFlag aFlags);
		bool SaveMesh(cMesh* apMesh,const tWString& asFile){ return false; }

		cAnimation* LoadAnimation(const tWString& asFile);
		bool SaveAnimation(cAnimation* apAnimation, const tWString& asFile){ return false; }

	private:
		bool LoadGLTFData(const tWString& asFile, cgltf_data** apData);
		void BuildNodeNameMap(cgltf_data* apData, tNodeNameMap& aNodeNames);
		void CreateNodeHierarchy(cMesh* apMesh, cNode3D* apParentNode, const cgltf_node* apNode, const tNodeNameMap& aNodeNames);
		void ImportNodeMeshes(cMesh* apMesh, const cgltf_node* apNode, const tNodeNameMap& aNodeNames, const tWString& asFile, tMeshLoadFlag aFlags);
		cSubMesh* CreateSubMeshFromPrimitive(cMesh* apMesh, const cgltf_node* apNode, const cgltf_primitive* apPrimitive, int alPrimitiveIndex, const tNodeNameMap& aNodeNames, const tWString& asFile, tMeshLoadFlag aFlags);
		cAnimation* CreateAnimationFromGLTF(cgltf_data* apData, cgltf_animation* apGLTFAnimation, const tWString& asFile, const tString& asResourceName, const tNodeNameMap& aNodeNames, const tNodeSet& aSceneNodes);

		cMeshLoaderMSH *mpMeshLoaderMSH;
		bool mbLoadAndSaveMSHFormat;
	};
}

#endif // HPL_MESH_LOADER_GLTF_H
