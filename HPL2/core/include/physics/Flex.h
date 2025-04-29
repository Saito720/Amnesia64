#ifndef HPL_FLEX_H
#define HPL_FLEX_H

#include "engine/Updateable.h"
#include "graphics/RenderFunctions.h"

#include "Flex/NvFlex.h"
#include "Flex/NvFlexExt.h"
#include "Flex/NvFlexDevice.h"
#include "Flex/core/maths.h"
#include "impl/LowLevelGraphicsSDL.h"

namespace hpl {

	struct SimBuffers
	{
		// Particles
		NvFlexVector<Vec4> positions;
		NvFlexVector<Vec3> velocities;
		NvFlexVector<int> phases;
		NvFlexVector<int> activeIndices;

		SimBuffers(NvFlexLibrary* l) : positions(l), velocities(l), phases(l), activeIndices(l) {}
	};

	class cFlex : public iUpdateable
	{
	friend class cRendererCallbackFunctions;
	public:
		cFlex();
		~cFlex();

		void Init(int alMaxParticles, int alMaxDiffuseParticles, int alMaxNeighborsPerParticle = 96);
		void Update(float afTimeStep);

		void Render(cRendererCallbackFunctions* apFunctions);

		void SetGpuProgram(iGpuProgram* apProgram) { mpGpuProgram = apProgram; };
		void SetDrawPlanes(bool abDraw) { mbDrawPlanes = abDraw; };

	private:
		NvFlexLibrary* mpFlexLibrary; // Flex library
		NvFlexSolver* mpFlexSolver;   // Flex solver
		SimBuffers* mpSimBuffers;     // Flex simulation buffers
		NvFlexParams SimParams;       // Flex simulation parameters

		std::vector<cVector3f> mvRenderPositions;
		iGpuProgram* mpGpuProgram;
		bool mbDrawPlanes;
		int mlLastParticleCount;
		GLuint mGLParticlesVbo = 0;
		NvFlexBuffer* mFlexGLBuffer = nullptr;

		bool mbMeshSet;
	};
};

#endif // HPL_FLEX_H