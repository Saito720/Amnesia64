#include "physics/Flex.h"
#include "physics/FlexHelper.h"
#include "system/LowLevelSystem.h"
#include "../../../amnesia/src/game/LuxEntity.h"
#include "../../../amnesia/src/game/LuxMap.h"
#include "../../../amnesia/src/game/LuxMapHandler.h"

// DEBUG
#include "../../../amnesia/src/game/LuxDebugHandler.h"

static void ErrorCallback(NvFlexErrorSeverity type, const char* msg, const char* file, int line) {
	if (type == eNvFlexLogError) {
		hpl::FatalError("Flex Error (%s:%d): %s\n", file, line, msg);
	}
	else if (type == eNvFlexLogWarning) {
		hpl::Warning("Flex Warning (%s:%d): %s\n", file, line, msg);
	}
	else {
		hpl::Log("Flex Info/Debug (%s:%d): %s\n", file, line, msg);
	}
}

namespace hpl {

	cFlex::cFlex() : iUpdateable("HPL_Flex")
	{
		mpFlexLibrary = NULL;
		mpFlexSolver = NULL;
		mpSimBuffers = NULL;

		mlLastParticleCount = 0;
		mbDrawPlanes = false;
		mbMeshSet = false;
	}

	cFlex::~cFlex()
	{
		Log("Exiting Flex Module\n");
		Log("--------------------------------------------------------\n");

		cFlexHelper::DestroyBuffers(mpSimBuffers);
		NvFlexDestroySolver(mpFlexSolver);
		NvFlexShutdown(mpFlexLibrary);

		Log("--------------------------------------------------------\n\n");
	}

	void cFlex::Init(int alMaxParticles, int alMaxDiffuseParticles, int alMaxNeighborsPerParticle)
	{
		//////////////////////////
		// Create Optimized CUDA Context
		int lDevice = NvFlexDeviceGetSuggestedOrdinal();

		if (NvFlexDeviceCreateCudaContext(lDevice))
			Log("Successfully created CUDA context\n");
		else
			FatalError("Could not create CUDA context!\n");

		NvFlexInitDesc desc;
		desc.deviceIndex = lDevice;
		desc.enableExtensions = true;
		desc.renderDevice = 0;
		desc.renderContext = 0;
		desc.computeType = eNvFlexCUDA;

		//////////////////////////
		// Library Init
		if ((mpFlexLibrary = NvFlexInit(NV_FLEX_VERSION, ErrorCallback, &desc))) {
			Log("Successfully initialized Flex library\n");

			//////////////////////////
			// Solver Creation
			if ((mpFlexSolver = NvFlexCreateSolver(mpFlexLibrary, alMaxParticles, alMaxDiffuseParticles, alMaxNeighborsPerParticle)))
				Log("Successfully created Flex solver\n");
			else
				FatalError("Could not create Flex solver!\n\nmaxParticles: %d\nmaxDiffuseParticles: %d\nmaxNeighborsPerParticle: %d\n", alMaxParticles, alMaxDiffuseParticles, alMaxNeighborsPerParticle);

			glGenBuffers(1, &mGLParticlesVbo);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, mGLParticlesVbo);
			glBufferData(GL_SHADER_STORAGE_BUFFER, alMaxParticles * sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);

			mFlexGLBuffer = NvFlexRegisterOGLBuffer(mpFlexLibrary, mGLParticlesVbo, alMaxParticles, sizeof(float) * 4);
			if (!mFlexGLBuffer)
				FatalError("NvFlexRegisterOGLBuffer failed!");

			//////////////////////////
			// Buffer Allocation
			if ((mpSimBuffers = cFlexHelper::AllocBuffers(mpFlexLibrary)))
				Log("Successfully allocated Flex buffers\n");
			else
				FatalError("Could not allocate Flex buffers!\n");

			int lParticleCount = cFlexHelper::CreateParticleCube(mpSimBuffers, cVector3f(0, 4, 0), 10, 0.075f, alMaxParticles);
			if (lParticleCount == -1)
				FatalError("Could not create particle cube!");
			__assume(mpSimBuffers != nullptr); // MSVC hint for the analyzer

			// Simulation parameters
			SimParams.gravity[0] = 0.0f;	// Gravity X
			SimParams.gravity[1] = -9.8f;	// Gravity Y
			SimParams.gravity[2] = 0.0f;	// Gravity Z
			SimParams.maxSpeed = FLT_MAX;
			SimParams.maxAcceleration = 100.0f; // approximately 10x gravity
			cFlexHelper::PresetParameters(SimParams, ePresetType_GranularPile);

			// Collision planes
			cFlexHelper::CreateCollisionPlanes(SimParams, cVector3f(-4, 0, -4), cVector3f(4, 8, 4));

			// Send data to Flex
			NvFlexSetParams(mpFlexSolver, &SimParams);
			NvFlexSetParticles(mpFlexSolver, mpSimBuffers->positions.buffer, lParticleCount);
			NvFlexSetVelocities(mpFlexSolver, mpSimBuffers->velocities.buffer, lParticleCount);
			NvFlexSetPhases(mpFlexSolver, mpSimBuffers->phases.buffer, lParticleCount);
			NvFlexSetActive(mpFlexSolver, mpSimBuffers->activeIndices.buffer, lParticleCount);

			GLuint ssbo;
			glGenBuffers(1, &ssbo);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
			glBufferData(GL_SHADER_STORAGE_BUFFER, alMaxParticles * sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);
		}
		else
			FatalError("Could not initialize Flex library!\n");
	}

	void cFlex::Update(float afTimeStep)
	{
		if (!mpFlexSolver) return;

		if (!mbMeshSet)
		{
			if (!gpBase->mpMapHandler->GetCurrentMap()) return;

			cLuxMap* pMap = gpBase->mpMapHandler->GetCurrentMap();
			cSubMeshEntity* pSubMeshEntity = pMap->GetEntityByName("basket_1")->GetMeshEntity()->GetSubMeshEntity(0);

			cFlexHelper::AddRigidParticleBody(pSubMeshEntity, mpFlexLibrary, mpFlexSolver, mpSimBuffers, SimParams.radius * 0.75f, 1.0f, 1.0f);

			mbMeshSet = true;
		}

		NvFlexSetParams(mpFlexSolver, &SimParams);
		NvFlexUpdateSolver(mpFlexSolver, afTimeStep, 2, false);

		uint32_t active = NvFlexGetActiveCount(mpFlexSolver);
		NvFlexGetParticles(mpFlexSolver, mFlexGLBuffer, active);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		mlLastParticleCount = active;
	}

	void cFlex::Render(cRendererCallbackFunctions* apFunctions)
	{
		if (mbDrawPlanes)
			cFlexHelper::DrawCollisionPlanes(SimParams);

		apFunctions->SetProgram(mpGpuProgram);
		apFunctions->SetDepthTest(true);
		apFunctions->SetDepthWrite(true);
		apFunctions->SetDepthTestFunc(eDepthTestFunc_LessOrEqual);
		apFunctions->SetBlendMode(eMaterialBlendMode_Alpha);
		apFunctions->SetAlphaMode(eMaterialAlphaMode_Solid);
		apFunctions->SetChannelMode(eMaterialChannelMode_RGBA);

		cRendererDeferred* pRendererDeferred = static_cast<cRendererDeferred*>(gpBase->mpEngine->GetGraphics()->GetRenderer(eRenderer_Main));

		// Get the camera's inverse matrix
		cMatrixf mtxView = pRendererDeferred->GetCurrentFrustum()->GetViewMatrix();
		cMatrixf mtxProj = pRendererDeferred->GetCurrentFrustum()->GetProjectionMatrix();
		cMatrixf mtxVP = cMath::MatrixMul(mtxProj, mtxView);
		cMatrixf mtxInvVP = cMath::MatrixInverse(mtxVP);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mGLParticlesVbo);

		if (mpGpuProgram)
		{
			mpGpuProgram->SetVec3f(23, pRendererDeferred->GetCurrentFrustum()->GetOrigin());	// Camera position
			mpGpuProgram->SetMatrixf(24, mtxInvVP);												// Inverse view-projection matrix
			mpGpuProgram->SetMatrixf(25, mtxView);												// View matrix
			mpGpuProgram->SetMatrixf(26, mtxProj);												// Projection matrix
			mpGpuProgram->SetInt(27, mlLastParticleCount);
			mpGpuProgram->SetFloat(28, SimParams.radius * 0.5f);
		}

		apFunctions->SetFlatProjection();
		apFunctions->SetTextureRange(NULL, 1);
		apFunctions->DrawQuad(0, 1);
	}
}