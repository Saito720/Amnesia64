#include "physics/Flex.h"
#include "physics/FlexHelper.h"
#include "system/LowLevelSystem.h"

// DEBUG
#include "../../../amnesia/src/game/LuxDebugHandler.h"
#include "../../../amnesia/src/game/LuxBase.h"
#include "../../../amnesia/src/game/LuxPlayer.h"

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

		mpGrabCrosshairGfx = NULL;
		mlFocusedParticle = UINT32_MAX;
		mlGrabbedParticle = UINT32_MAX;
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

		NvFlexSetParams(mpFlexSolver, &SimParams);
		NvFlexUpdateSolver(mpFlexSolver, afTimeStep, 2, false);

		uint32_t active = NvFlexGetActiveCount(mpFlexSolver);
		NvFlexGetParticles(mpFlexSolver, mFlexGLBuffer, active);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		mlLastParticleCount = active;

		if (mlGrabbedParticle == UINT32_MAX) // We don't have a particle
		{
			NvFlexGetParticles(mpFlexSolver, mpSimBuffers->positions.buffer, mpSimBuffers->positions.size());
			cFlexHelper::MapBuffers(mpSimBuffers);

			mlFocusedParticle = cFlexHelper::GetClosestParticle(mpSimBuffers, SimParams.radius / 2, 2.0f);

			if (gpBase->mpEngine->GetInput()->IsTriggerd(eLuxAction_LeftClick) && mlFocusedParticle != UINT32_MAX)
			{
				mlGrabbedParticle = mlFocusedParticle;

				cCamera* pCam = gpBase->mpPlayer->GetCamera();
				cVector3f vRayOrigin = pCam->GetPosition();
				Vec4 vPos = mpSimBuffers->positions[mlGrabbedParticle];
				mfDist = cMath::Vector3Dist(vRayOrigin, cVector3f(vPos.x, vPos.y, vPos.z));
			}

			cFlexHelper::UnmapBuffers(mpSimBuffers);
		}
		else if (gpBase->mpEngine->GetInput()->IsTriggerd(eLuxAction_LeftClick)) // We grabbed a particle
		{
			cFlexHelper::MapBuffers(mpSimBuffers);

			cCamera* pCam = gpBase->mpPlayer->GetCamera();
			cVector3f vRayOrigin = pCam->GetPosition();
			cVector3f vRayDir = pCam->GetForward();
			cVector3f vNewPos = vRayOrigin + vRayDir * mfDist;
			mvLastPos = vNewPos;

			Vec4 glPos = mpSimBuffers->positions[mlGrabbedParticle];
			Vec4 glNewPos = Vec4(vNewPos.x, vNewPos.y, vNewPos.z, glPos.w);
			Vec3 vNewVel = Vec3(0.0f);

			cFlexHelper::UnmapBuffers(mpSimBuffers);

			glBindBuffer(GL_SHADER_STORAGE_BUFFER, mGLParticlesVbo);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(Vec4) * mlGrabbedParticle, sizeof(Vec4), &glNewPos);
			NvFlexSetVelocities(mpFlexSolver, mpSimBuffers->velocities.buffer, mpSimBuffers->velocities.size());
		}
		else if (gpBase->mpEngine->GetInput()->IsTriggerd(eLuxAction_LeftClick) == false) // We dropped the particle
		{
			cFlexHelper::MapBuffers(mpSimBuffers);

			cVector3f vCurrentPos = cVector3f(	mpSimBuffers->positions[mlGrabbedParticle].x,
												mpSimBuffers->positions[mlGrabbedParticle].y,
												mpSimBuffers->positions[mlGrabbedParticle].z);

			cVector3f vDelta = (mvLastPos - vCurrentPos) / afTimeStep;
			Vec3 glNewVel = Vec3(vDelta.x, vDelta.y, vDelta.z);

			mlFocusedParticle = UINT32_MAX;
			mlGrabbedParticle = UINT32_MAX;

			cFlexHelper::UnmapBuffers(mpSimBuffers);

			NvFlexSetVelocities(mpFlexSolver, mpSimBuffers->velocities.buffer, mpSimBuffers->velocities.size());
		}
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

		if (!mpGrabCrosshairGfx)
			mpGrabCrosshairGfx = gpBase->mpEngine->GetGui()->CreateGfxImage("hud_crosshair_over_grab.tga", eGuiMaterial_Alpha);

		if (mlFocusedParticle != UINT32_MAX && mlGrabbedParticle == UINT32_MAX)
		{
			cVector2f vSetSize = gpBase->mvHudVirtualCenterSize;
			cVector2f vGfxSize = mpGrabCrosshairGfx->GetImageSize();
			cVector2f vPos = (vSetSize - vGfxSize) / 2.0f;
			cVector3f vFinalPos = cVector3f(vPos.x, vPos.y, 1);
			gpBase->mpGameHudSet->DrawGfx(mpGrabCrosshairGfx, vFinalPos, vGfxSize, cColor(1, 1));
		}
	}
}