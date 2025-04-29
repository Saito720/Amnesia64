#ifndef HPL_FLEX_HELPER_H
#define HPL_FLEX_HELPER_H

#include "physics/Flex.h"
#include "math/MathTypes.h"
#include "graphics/Color.h"
#include "scene/SubMeshEntity.h"

#include "Flex/core/maths.h"
#include "Flex/core/Voxelize.h"
#include "Flex/core/FlexMesh.h"

enum ePresetType
{
	ePresetType_GranularPile = 0
};

struct Cluster
{
	Vec3 mean;
	float radius;

	// indices of particles belonging to this cluster
	std::vector<int> indices;
};

struct Seed
{
	int index;
	float priority;

	bool operator < (const Seed& rhs) const
	{
		return priority < rhs.priority;
	}
};

namespace hpl {

	class cFlexHelper
	{
	public:
		cFlexHelper();
		~cFlexHelper();

		static void PresetParameters(NvFlexParams& params, int alType);

		static int CreateParticleCube(SimBuffers* apSimBuffers, cVector3f avPos, int alSize, float afSpacing, int alMaxParticles);

		static void AddRigidParticleBody(cSubMeshEntity* apSubMeshEntity, NvFlexLibrary* apFlexLibrary, NvFlexSolver* apFlexSolver, SimBuffers* apSimBuffers, float afParticleRadius, float afShellOnly, float afRigidStiffness);

		static SimBuffers* AllocBuffers(NvFlexLibrary* apFlexLibrary);
		static void MapBuffers(SimBuffers* apSimBuffers);
		static void UnmapBuffers(SimBuffers* apSimBuffers);
		static void DestroyBuffers(SimBuffers* apSimBuffers);

		static void CreateCollisionPlanes(NvFlexParams& params, cVector3f avMinX, cVector3f avMaxX);
		static void DrawCollisionPlanes(NvFlexParams& params, const cColor& aSolidColor = cColor(1, 0, 0, 0.5f), const cColor& aWireColor = cColor(1), bool abDrawFloor = true);

		static void SampleMesh(Mesh* mesh, Vec3 lower, Vec3 scale, float rotation, float radius, float volumeSampling, float surfaceSampling, std::vector<Vec3>& outPositions);
		static int CreateClusters(Vec3* particles, const float* priority, int numParticles, std::vector<int>& outClusterOffsets, std::vector<int>& outClusterIndices, std::vector<Vec3>& outClusterPositions, float radius, float smoothing = 0.0f);
		static Vec3 CalculateMean(const Vec3* particles, const int* indices, int numIndices);
	};
};

#endif // HPL_FLEX_HELPER_H