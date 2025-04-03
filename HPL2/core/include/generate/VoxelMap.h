/*
 * Copyright © 2009-2020 Frictional Games
 * 
 * This file is part of Amnesia: The Dark Descent.
 * 
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HPL_VOXEL_MAP_H
#define HPL_VOXEL_MAP_H

#include "generate/GenerateTypes.h"
#include "graphics/Renderer.h"
#include "scene/Viewport.h"
#include "math/MathTypes.h"

namespace hpl {

	//-------------------------------

	class cRendererCallbackFunctions;

	class cVoxelMapDebugRenderCallback : public iRendererCallback
	{
	public:
		cVoxelMapDebugRenderCallback();

		void OnPostSolidDraw(cRendererCallbackFunctions* apFunctions);

		void OnPostTranslucentDraw(cRendererCallbackFunctions* apFunctions);

		cViewport* mpViewport;
		iVertexBuffer* mpVtxBuffer;
	};

	//-------------------------------

	class cVoxelMap
	{
	public:
		cVoxelMap(const cVector3l& avSize);
		~cVoxelMap();

		/////////////////////////////////
		// Action
		void SetVoxel(const cVector3l &avPos, char alVal);

		/////////////////////////////////
		// Properties
		const cVector3l& GetSize(){ return mvSize; }
		void SetSize(const cVector3l& avSize);

		void SetVertexBuffer(iVertexBuffer* apVertexBuffer);
		void SetViewport(cViewport* apViewport);

		float GetVoxelSize(){ return mfVoxelSize;}
		void SetVoxelSize(float afX){ mfVoxelSize = afX;}

		const cVector3f& GetPosition(){ return mvPosition;}
		void SetPosition(const cVector3f& avPos){ mvPosition = avPos;}

		void Update(float afTimeStep);

	private:
		unsigned char* mpData;

		cVector3l mvSize;

		float mfVoxelSize;
		cVector3f mvPosition;

		bool mbVertexBufferIsSet;
		bool mbAddRenderCallback;
		cVoxelMapDebugRenderCallback mRenderCallback;
	};
};

#endif // HPL_VOXEL_MAP_H
