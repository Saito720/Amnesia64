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

#ifndef HPL_MESH_LOADER_FBX_H
#define HPL_MESH_LOADER_FBX_H

#include "resources/MeshLoader.h"

namespace hpl {

class cMeshLoaderMSH;

// Uses source-built ufbx with the original AMFP loader/cache interface.
class cMeshLoaderFBX : public iMeshLoader
{
public:
	cMeshLoaderFBX(iLowLevelGraphics* apLowLevelGraphics, cMeshLoaderMSH* apMeshLoaderMSH, bool abLoadAndSaveMSHFormat);
	~cMeshLoaderFBX();

	cMesh* LoadMesh(const tWString& asFile, tMeshLoadFlag aFlags);
	bool SaveMesh(cMesh*, const tWString&) { return false; }
	cAnimation* LoadAnimation(const tWString& asFile);
	bool SaveAnimation(cAnimation*, const tWString&) { return false; }

private:
	cMeshLoaderMSH* mpMeshLoaderMSH;
	bool mbLoadAndSaveMSHFormat;
};

}
#endif
