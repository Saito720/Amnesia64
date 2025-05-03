#include "physics/FlexHelper.h"
#include "system/LowLevelSystem.h"
#include "impl/LowLevelGraphicsSDL.h"
#include "scene/MeshEntity.h"
#include "graphics/VertexBuffer.h"
#include "math/Math.h"

#include "../../../amnesia/src/game/LuxBase.h"
#include "../../../amnesia/src/game/LuxPlayer.h"

// DEBUG
#include "../../../amnesia/src/game/LuxDebugHandler.h"

#include <array>

namespace hpl {

	cFlexHelper::cFlexHelper()
	{

	}

	cFlexHelper::~cFlexHelper()
	{

	}

	void cFlexHelper::PresetParameters(NvFlexParams& params, int alType)
	{
		switch (alType) {
		case ePresetType_GranularPile:
			params.numIterations			= 12;								// Num Iterations
			params.radius					= 0.075f;							// Radius
			params.solidRestDistance		= params.radius;					// Solid Radius
			params.fluidRestDistance		= 0;								// Fluid Radius
			params.dynamicFriction			= 0.5f;								// Dynamic Friction
			params.staticFriction			= 1.0f;								// Static Friction
			params.particleFriction			= 0.05f;							// Particle Friction
			params.restitution				= 0.2f;								// Restitution
			params.sleepThreshold			= params.radius * 0.25f;			// Sleep Threshold
			params.shockPropagation			= 6.0f;								// Shock Propogation
			params.damping					= 0.14f;							// Damping
			params.dissipation				= 0;								// Dissapation
			params.relaxationFactor			= 1.0f;								// SOR
			params.collisionDistance		= params.radius * 0.5f;				// Collision Distance
			params.shapeCollisionMargin		= params.collisionDistance * 0.5f;	// Collision Margin
			params.viscosity				= 0.0f;								// Viscosity
			params.particleCollisionMargin	= params.radius * 0.25f;			// 5% collision margin
			break;
		}
	}

	SimBuffers* cFlexHelper::AllocBuffers(NvFlexLibrary* apFlexLibrary)
	{
		return new SimBuffers(apFlexLibrary);
	}

	void cFlexHelper::MapBuffers(SimBuffers* apSimBuffers)
	{
		apSimBuffers->positions.map();
		apSimBuffers->velocities.map();
		apSimBuffers->phases.map();
		apSimBuffers->activeIndices.map();
	}

	void cFlexHelper::UnmapBuffers(SimBuffers* apSimBuffers)
	{
		apSimBuffers->positions.unmap();
		apSimBuffers->velocities.unmap();
		apSimBuffers->phases.unmap();
		apSimBuffers->activeIndices.unmap();
	}

	void cFlexHelper::DestroyBuffers(SimBuffers* apSimBuffers)
	{
		apSimBuffers->positions.destroy();
		apSimBuffers->velocities.destroy();
		apSimBuffers->phases.destroy();
		apSimBuffers->activeIndices.destroy();

		delete apSimBuffers;
	}

	uint32_t cFlexHelper::GetClosestParticle(SimBuffers* apSimBuffers, float afGrabRadius, float afRayLength)
	{
		cCamera* pCam = gpBase->mpPlayer->GetCamera();
		if (!pCam || apSimBuffers->positions.empty()) return UINT32_MAX;

		cVector3f vRayOrigin = pCam->GetPosition();
		cVector3f vRayDir    = pCam->GetForward();

		const float fGrabRadius = afGrabRadius;
		const float fGrabRadius2 = fGrabRadius * fGrabRadius;

		uint32_t lBestIndex = UINT32_MAX;
		float    fBestDist2 = FLT_MAX;
		for (size_t i = 0; i < apSimBuffers->positions.size(); ++i)
		{
			const cVector3f& vPoint = cVector3f(apSimBuffers->positions[i].x, apSimBuffers->positions[i].y, apSimBuffers->positions[i].z);
			cVector3f vDelta = vPoint - vRayOrigin;

			// Project onto ray
			float fT = cMath::Vector3Dot(vDelta, vRayDir);
			if (fT < 0.0f || fT > afRayLength)
				continue; // Outside of reach segment

			// Find the closest point on ray, then distance^2 to vPoint
			cVector3f vClosestPoint = vRayOrigin + vRayDir * fT;
			float fDist2 = (vPoint - vClosestPoint).SqrLength();

			// Within grab radius and closer than any before?
			if (fDist2 < fGrabRadius2 && fDist2 < fBestDist2)
			{
				fBestDist2 = fDist2;
				lBestIndex = i;
			}
		}

		return lBestIndex;
	}

	int cFlexHelper::CreateParticleCube(SimBuffers* apSimBuffers, cVector3f avPos, int alSize, float afSpacing, int alMaxParticles)
	{
		const int lParticleCount = alSize * alSize * alSize;
		if (alMaxParticles < lParticleCount) return -1;

		MapBuffers(apSimBuffers);

		apSimBuffers->positions.resize(lParticleCount);
		apSimBuffers->velocities.resize(lParticleCount);
		apSimBuffers->phases.resize(lParticleCount);
		apSimBuffers->activeIndices.resize(lParticleCount);

		Vec3 vPos = Vec3(avPos.x, avPos.y, avPos.z) - 0.5f * Vec3((alSize - 1) * afSpacing, (alSize - 1) * afSpacing, (alSize - 1) * afSpacing);
		const float fInvMass = 1.0f;
		const Vec3 vInitialVel = Vec3(0.0f);
		const int lPhase = NvFlexMakePhase(0, eNvFlexPhaseSelfCollide);
		uint32_t lParticleIdx = 0;

		for (int z = 0; z < alSize; ++z) {
			for (int y = 0; y < alSize; ++y) {
				for (int x = 0; x < alSize; ++x)
				{
					float px = vPos.x + x * afSpacing;
					float py = vPos.y + y * afSpacing;
					float pz = vPos.z + z * afSpacing;
					Vec4 vParticlePos = Vec4(px, py, pz, fInvMass);

					apSimBuffers->positions[lParticleIdx] = vParticlePos;
					apSimBuffers->velocities[lParticleIdx] = vInitialVel;
					apSimBuffers->phases[lParticleIdx] = lPhase;
					apSimBuffers->activeIndices[lParticleIdx] = lParticleIdx;

					lParticleIdx++;
				}
			}
		}

		// Sanity check
		if (lParticleIdx != lParticleCount || lParticleIdx != apSimBuffers->positions.size())
			FatalError("Creation of particle cube failed!\n\nFinal Index: %d\nExpected Count: %d\nActual Count: %d", lParticleIdx, lParticleCount, apSimBuffers->positions.size());

		UnmapBuffers(apSimBuffers);
		return lParticleIdx;
	}

	void cFlexHelper::PackStatic(cWorld* apWorld)
	{
		std::vector<cVector3f>    vStaticVerts;
		std::vector<unsigned int> vStaticIndices;

		int lIndexWritePos = 0;

		cMeshEntityIterator staticIt = apWorld->GetStaticMeshEntityIterator();
		while (staticIt.HasNext())
		{
			cMeshEntity* pMeshEntity = staticIt.Next();
			for (int i = 0; i < pMeshEntity->GetSubMeshEntityNum(); ++i)
			{
				iVertexBuffer* pVB = pMeshEntity->GetSubMeshEntity(i)->GetVertexBuffer();
				float* pPos = pVB->GetFloatArray(eVertexBufferElement_Position);
				unsigned int* pIdx = pVB->GetIndices();
				int lVtxCount = pVB->GetVertexNum();
				int lIdxCount = pVB->GetIndexNum();
				int lOffset = (int)vStaticVerts.size();

				vStaticVerts.resize(lOffset + lVtxCount);
				vStaticIndices.resize(lIndexWritePos + lIdxCount);

				for (int j = 0; j < lVtxCount; ++j) {
					vStaticVerts[lOffset + j].x = pPos[j * 4 + 0];
					vStaticVerts[lOffset + j].y = pPos[j * 4 + 1];
					vStaticVerts[lOffset + j].z = pPos[j * 4 + 2];
				}

				for (int k = 0; k < lIdxCount; ++k) {
					vStaticIndices[lIndexWritePos + k] = pIdx[k] + lOffset;
				}

				lIndexWritePos += lIdxCount;
			}
		}
	}

	void cFlexHelper::CreateCollisionPlanes(NvFlexParams& params, cVector3f avMinX, cVector3f avMaxX)
	{
		params.planes[0][0] = 1.0f; params.planes[0][1] = 0.0f; params.planes[0][2] = 0.0f; params.planes[0][3] = -avMinX.x;
		params.planes[1][0] = -1.0f; params.planes[1][1] = 0.0f; params.planes[1][2] = 0.0f; params.planes[1][3] = avMaxX.x;
		params.planes[2][0] = 0.0f; params.planes[2][1] = 1.0f; params.planes[2][2] = 0.0f; params.planes[2][3] = -avMinX.y;
		params.planes[3][0] = 0.0f; params.planes[3][1] = -1.0f; params.planes[3][2] = 0.0f; params.planes[3][3] = avMaxX.y;
		params.planes[4][0] = 0.0f; params.planes[4][1] = 0.0f; params.planes[4][2] = 1.0f; params.planes[4][3] = -avMinX.z;
		params.planes[5][0] = 0.0f; params.planes[5][1] = 0.0f; params.planes[5][2] = -1.0f; params.planes[5][3] = avMaxX.z;
		params.numPlanes = 6;
	}

	void cFlexHelper::DrawCollisionPlanes(NvFlexParams& params, const cColor& aSolidColor, const cColor& aWireColor, bool abDrawFloor)
	{
		const float fMinX = -params.planes[0][3];
		const float fMaxX = params.planes[1][3];
		const float fMinY = -params.planes[2][3];
		const float fMaxY = params.planes[3][3];
		const float fMinZ = -params.planes[4][3];
		const float fMaxZ = params.planes[5][3];

		const cVector3f v000{ fMinX, fMinY, fMinZ };
		const cVector3f v001{ fMinX, fMinY, fMaxZ };
		const cVector3f v010{ fMinX, fMaxY, fMinZ };
		const cVector3f v011{ fMinX, fMaxY, fMaxZ };
		const cVector3f v100{ fMaxX, fMinY, fMinZ };
		const cVector3f v101{ fMaxX, fMinY, fMaxZ };
		const cVector3f v110{ fMaxX, fMaxY, fMinZ };
		const cVector3f v111{ fMaxX, fMaxY, fMaxZ };

		static const std::array<std::array<cVector3f, 3>, 12> tris{{
			{{ v000, v001, v011 }}, {{ v000, v011, v010 }},
			{{ v100, v110, v101 }}, {{ v110, v111, v101 }},
			{{ v000, v010, v100 }}, {{ v010, v110, v100 }},
			{{ v001, v111, v011 }}, {{ v001, v101, v111 }},
			{{ v000, v100, v001 }}, {{ v100, v101, v001 }},
			{{ v010, v011, v111 }}, {{ v010, v111, v110 }}
		}};

		// Solid
		glDisable(GL_CULL_FACE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glColor4f(aSolidColor.r, aSolidColor.g, aSolidColor.b, aSolidColor.a);

		glBegin(GL_TRIANGLES);
		for (size_t i = 0; i < tris.size(); ++i)
		{
			if (!abDrawFloor && (i == 8 || i == 9))
				continue;

			const auto& t = tris[i];
			glVertex3f(t[0].x, t[0].y, t[0].z);
			glVertex3f(t[1].x, t[1].y, t[1].z);
			glVertex3f(t[2].x, t[2].y, t[2].z);
		}
		glEnd();

		// Wire
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.0f);
		glColor4f(aWireColor.r, aWireColor.g, aWireColor.b, aWireColor.a);

		glBegin(GL_TRIANGLES);
		for (size_t i = 0; i < tris.size(); ++i)
		{
			if (!abDrawFloor && (i == 8 || i == 9))
				continue;

			const auto& t = tris[i];
			glVertex3f(t[0].x, t[0].y, t[0].z);
			glVertex3f(t[1].x, t[1].y, t[1].z);
			glVertex3f(t[2].x, t[2].y, t[2].z);
		}
		glEnd();

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glEnable(GL_CULL_FACE);
	}
}