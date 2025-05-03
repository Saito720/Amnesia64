#ifndef HPL_FLEX_HELPER_H
#define HPL_FLEX_HELPER_H

#include "physics/Flex.h"
#include "math/MathTypes.h"
#include "graphics/Color.h"
#include "scene/SubMeshEntity.h"
#include "scene/World.h"

#include "Flex/core/maths.h"
#include "Flex/core/Voxelize.h"
#include "Flex/core/FlexMesh.h"

enum ePresetType
{
	ePresetType_GranularPile = 0
};

namespace hpl {

	class cFlexHelper
	{
	public:
		cFlexHelper();
		~cFlexHelper();

		static void PresetParameters(NvFlexParams& params, int alType);

		static int CreateParticleCube(SimBuffers* apSimBuffers, cVector3f avPos, int alSize, float afSpacing, int alMaxParticles);
		static void PackStatic(cWorld* apWorld);

		static SimBuffers* AllocBuffers(NvFlexLibrary* apFlexLibrary);
		static void MapBuffers(SimBuffers* apSimBuffers);
		static void UnmapBuffers(SimBuffers* apSimBuffers);
		static void DestroyBuffers(SimBuffers* apSimBuffers);

		static uint32_t GetClosestParticle(SimBuffers* apSimBuffers, float afGrabRadius, float afRayLength = 2.0f);

		static void CreateCollisionPlanes(NvFlexParams& params, cVector3f avMinX, cVector3f avMaxX);
		static void DrawCollisionPlanes(NvFlexParams& params, const cColor& aSolidColor = cColor(1, 0, 0, 0.5f), const cColor& aWireColor = cColor(1), bool abDrawFloor = true);
	};
};

#endif // HPL_FLEX_HELPER_H