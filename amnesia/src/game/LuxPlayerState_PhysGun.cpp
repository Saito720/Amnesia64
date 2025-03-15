#include "LuxPlayerState_PhysGun.h"

#include "LuxPlayer.h"
#include "LuxMapHandler.h"
#include "LuxMap.h"
#include "LuxInputHandler.h"
#include "LuxDebugHandler.h"
#include "LuxProp.h"
#include "LuxEnemy.h"
#include "LuxEnemyMover.h"

cLuxPlayerState_PhysGun::cLuxPlayerState_PhysGun(cLuxPlayer* apPlayer) : iLuxPlayerState_Interact(apPlayer, eLuxPlayerState_PhysGun)
{
	// Grab settings from game config
	mfMaxForce = gpBase->mpGameCfg->GetFloat("Player_Interaction", "PhysGunMaxForce", 0);
	mfMaxTorque = gpBase->mpGameCfg->GetFloat("Player_Interaction", "PhysGunMaxTorque", 0);
	mfMaxAngularSpeed = gpBase->mpGameCfg->GetFloat("Player_Interaction", "PhysGunMaxAngularSpeed", 0);

	// Define PID parameters
	mForcePid.SetErrorNum(20);
	mSpeedTorquePid.SetErrorNum(20);

	mForcePid.p = 400;
	mForcePid.i = 0;
	mForcePid.d = 40;

	mSpeedTorquePid.p = 40;
	mSpeedTorquePid.i = 0;
	mSpeedTorquePid.d = 0.4f;

	// Setup crosshair values for rendering
	if(mpPhysGunCrosshairGfx)
	{
		cVector2f vSetSize = gpBase->mvHudVirtualCenterSize;
		mvCrossSize = mpPhysGunCrosshairGfx->GetImageSize() / 2.0f;
		cVector2f vPos = (vSetSize - mvCrossSize) / 2.0f;
		mvCrossPos = cVector3f(vPos.x, vPos.y, 1);
	}

	iLowLevelGraphics* pLowLevelGraphics = gpBase->mpEngine->GetGraphics()->GetLowLevel();
	mpVtxBuffer = pLowLevelGraphics->CreateVertexBuffer(eVertexBufferType_Hardware, eVertexBufferDrawType_Tri, eVertexBufferUsageType_Stream, 8 * 100, 12 * 100);
	mpVtxBuffer->CreateElementArray(eVertexBufferElement_Position, eVertexBufferElementFormat_Float, 4);
	mpVtxBuffer->CreateElementArray(eVertexBufferElement_Normal, eVertexBufferElementFormat_Float, 3);
	mpVtxBuffer->CreateElementArray(eVertexBufferElement_Color0, eVertexBufferElementFormat_Float, 4);
	mpVtxBuffer->CreateElementArray(eVertexBufferElement_Texture0, eVertexBufferElementFormat_Float, 3);

	for (int i = 0; i < 100; ++i)
	{
		for (int j = 0; j < 4; j++)
		{
			for (int k = 0; k < 2; k++)
			{
				mpVtxBuffer->AddVertexVec3f(eVertexBufferElement_Position, 0);
				mpVtxBuffer->AddVertexColor(eVertexBufferElement_Color0, cColor(1, 1, 1, 1));
				mpVtxBuffer->AddVertexVec3f(eVertexBufferElement_Texture0, 0);
				mpVtxBuffer->AddVertexVec3f(eVertexBufferElement_Normal, cVector3f(0, 0, 1));
			}
		}

		mpVtxBuffer->AddIndex((i * 8) + 0);
		mpVtxBuffer->AddIndex((i * 8) + 2);
		mpVtxBuffer->AddIndex((i * 8) + 4);
		mpVtxBuffer->AddIndex((i * 8) + 4);
		mpVtxBuffer->AddIndex((i * 8) + 6);
		mpVtxBuffer->AddIndex((i * 8) + 0);
		mpVtxBuffer->AddIndex((i * 8) + 1);
		mpVtxBuffer->AddIndex((i * 8) + 3);
		mpVtxBuffer->AddIndex((i * 8) + 5);
		mpVtxBuffer->AddIndex((i * 8) + 5);
		mpVtxBuffer->AddIndex((i * 8) + 7);
		mpVtxBuffer->AddIndex((i * 8) + 1);
	}

	mpVtxBuffer->Compile(eVertexCompileFlag_CreateTangents);
	mpMaterial = gpBase->mpEngine->GetResources()->GetMaterialManager()->CreateMaterial("physbeam_active");

	// Create RayCallback
	mpRayCallback = hplNew(cLuxPhysGunRayCallback, (this));
}

cLuxPlayerState_PhysGun::~cLuxPlayerState_PhysGun()
{
	// Delete RayCallback
	hplDelete(mpRayCallback);
}

void cLuxPlayerState_PhysGun::OnEnterState(eLuxPlayerState aPrevState)
{
	// Don't enter interacting
	mbInteracting = false;

	// Remove any existing focus
	mpPlayer->SetEntityInFocus(NULL);
	mpPlayer->SetBodyInFocus(NULL);

	mfUvOffset = 0.0f;
}

void cLuxPlayerState_PhysGun::OnLeaveState(eLuxPlayerState aNewState) {}

void cLuxPlayerState_PhysGun::OnGrab()
{
	// Set the current body and grab position
	mpBody = mpRayCallback->GetBody();
	mvGrabPos = mpRayCallback->GetPos();

	// Local grab position
	cMatrixf mtxInvWorld = cMath::MatrixInverse(mpBody->GetLocalMatrix());
	mvGrabLocalPos = cMath::MatrixMul(mtxInvWorld, mvGrabPos);

	// Grab depth
	cCamera* pCam = gpBase->mpPlayer->GetCamera();
	cVector3f vStart = pCam->GetPosition();
	cVector3f vEnd = cMath::MatrixMul(mpBody->GetLocalMatrix(), mvGrabLocalPos);
	mfGrabDepth = cMath::Vector3Dist(vStart, vEnd);

	// If the grabbed body belongs to an enemy
	if (mpRayCallback->IsEnemy())
	{
		cLuxEnemyIterator it = gpBase->mpMapHandler->GetCurrentMap()->GetEnemyIterator();
		while (it.HasNext())
		{
			iLuxEnemy* pEnemy = it.Next();
			if (pEnemy->GetCharacterBody()->GetCurrentBody() == mpBody)
			{
				mpEnemy = pEnemy;
				break;
			}
		}

		mpEnemy->GetMover()->SetGrabbed(true);
		float vCurrentYaw = cMath::MatrixToEulerAngles(mpBody->GetLocalMatrix(), eEulerRotationOrder_XYZ).y;
		mfEnemyYaw = vCurrentYaw;
		mfGrabDepth += 0.6f;
		return;
	}

	// Make sure PIDs are reset
	mForcePid.Reset();
	mSpeedTorquePid.Reset();

	// Camera rotation
	cVector3f vCamRotation(pCam->GetPitch(), pCam->GetYaw(), pCam->GetRoll());
	cMatrixf mtxCamRot = cMath::MatrixRotate(vCamRotation, eEulerRotationOrder_XYZ);
	cMatrixf mtxInvCamRot = cMath::MatrixInverse(mtxCamRot);

	// Body rotation
	m_mtxBodyRotation = cMath::MatrixMul(mtxInvCamRot, mpBody->GetLocalMatrix().GetRotation());

	// Local offset between the current body and grab position
	mvLocalBodyOffset = mpBody->GetLocalPosition() - mvGrabLocalPos;
	mvLocalBodyOffset = cMath::MatrixMul(mtxInvCamRot, mvLocalBodyOffset);
}

void cLuxPlayerState_PhysGun::Update(float afTimeStep)
{
	if (mpBody)
	{
		if (mpEnemy)
		{
			if (!mpEnemy->IsActive())
				OnRelease();
		}
		else
		{
			iLuxEntity* pEntity = (iLuxEntity*)mpBody->GetUserData();
			if (pEntity->GetDestroyMe())
				OnRelease();
		}
	}
}

void cLuxPlayerState_PhysGun::PostUpdate(float afTimeStep)
{
	if (mpBody)
	{
		// Camera rotation
		cCamera* pCam = gpBase->mpPlayer->GetCamera();
		cVector3f vCamRotation(pCam->GetPitch(), pCam->GetYaw(), pCam->GetRoll());
		cMatrixf mtxCamTransform = cMath::MatrixRotate(vCamRotation, eEulerRotationOrder_XYZ);
		mtxCamTransform.SetTranslation(pCam->GetPosition());

		// Define the target grab position in world space
		cVector3f vTargetGrab = pCam->GetPosition() + pCam->GetForward() * mfGrabDepth;

		// If the grabbed body belongs to an enemy
		if (mpEnemy)
		{
			cInput* pInput = gpBase->mpEngine->GetInput();
			if (pInput->IsTriggerd(eLuxAction_Rotate))
				mpBody->GetCharacterBody()->AddYaw(mfEnemyYaw);
			mpBody->GetCharacterBody()->SetPosition(vTargetGrab, false);
			return;
		}

		// Compute the new center of the body
		cVector3f vNewCenter = vTargetGrab + cMath::MatrixMul(m_mtxBodyRotation, mvLocalBodyOffset);

		// The final body matrix
		cMatrixf mtxGoal = cMath::MatrixMul(cMath::MatrixTranslate(vNewCenter), m_mtxBodyRotation);
		mtxGoal = cMath::MatrixMul(mtxCamTransform, mtxGoal);

		///////////////////////
		// Force
		cVector3f vCurrentGrab = cMath::MatrixMul(mpBody->GetLocalMatrix(), mvGrabLocalPos);
		cVector3f vError = vTargetGrab - vCurrentGrab;
		cVector3f vForce = mForcePid.Output(vError, afTimeStep) * mpBody->GetMass();
		vForce = cMath::Vector3MaxLength(vForce, mfMaxForce);
		mpBody->AddForce(vForce);

		/////////////////////////
		// Get the wanted speed
		cVector3f vWantedRotSpeed = 0;

		cMatrixf mtxGoalInv = cMath::MatrixInverse(mtxGoal);
		cVector3f vWantedUp = mtxGoalInv.GetUp();
		cVector3f vWantedRight = mtxGoalInv.GetRight();

		cMatrixf mtxBodyInv = cMath::MatrixInverse(mpBody->GetLocalMatrix());
		cVector3f vUp = mtxBodyInv.GetUp();
		cVector3f vRight = mtxBodyInv.GetRight();

		// Up alignment
		cVector3f vRotateAxis = cMath::Vector3Cross(vUp, vWantedUp);
		float fError = cMath::Vector3Angle(vWantedUp, vUp);
		vWantedRotSpeed += vRotateAxis * fError * 100;

		// Right alignment
		vRotateAxis = cMath::Vector3Cross(vRight, vWantedRight);
		fError = cMath::Vector3Angle(vWantedRight, vRight);
		vWantedRotSpeed += vRotateAxis * fError * 100;

		// Make sure wanted speed is not too large
		float fSpeed = vWantedRotSpeed.Length();
		if (fSpeed > mfMaxAngularSpeed)
			vWantedRotSpeed = (vWantedRotSpeed / fSpeed) * 6.0f;

		/////////////////////////
		// Set speed by torque
		cVector3f vRotError = vWantedRotSpeed - mpBody->GetAngularVelocity();
		cVector3f vTorque = mSpeedTorquePid.Output(vRotError, afTimeStep);
		vTorque = cMath::MatrixMul(mpBody->GetInertiaMatrix(), vTorque);

		// Make sure force is not too large
		vTorque = cMath::Vector3MaxLength(vTorque, mfMaxTorque);
		mpBody->AddTorque(vTorque);
	}
}

bool cLuxPlayerState_PhysGun::OnDoAction(eLuxPlayerAction aAction, bool abPressed)
{
	// Left Click
	if (aAction == eLuxPlayerAction_Interact)
	{
		if (abPressed)
			mbInteracting = true;
		else
		{
			mbInteracting = false;
			OnRelease();
		}
	}

	return true;
}

void cLuxPlayerState_PhysGun::OnRelease()
{
	mpBody = NULL;
	if (mpEnemy)
		mpEnemy->GetMover()->SetGrabbed(false);
	mpEnemy = NULL;
	mpRayCallback->Reset();
}

void cLuxPlayerState_PhysGun::OnScroll(float afAmount)
{
	if (mbInteracting)
		mfGrabDepth += afAmount;
}

bool cLuxPlayerState_PhysGun::OnAddYaw(float afAmount)
{
	cInput* pInput = gpBase->mpEngine->GetInput();
	if (pInput->IsTriggerd(eLuxAction_Rotate) && mbInteracting)
	{
		if (mpEnemy)
		{
			mfEnemyYaw = afAmount * -3.2;
			return false;
		}
		else if (mpBody)
		{
			m_mtxBodyRotation = cMath::MatrixMul(cMath::MatrixRotateY(afAmount * -3.2f), m_mtxBodyRotation);
			return false;
		}
	}

	return true;
}

bool cLuxPlayerState_PhysGun::OnAddPitch(float afAmount)
{
	cInput* pInput = gpBase->mpEngine->GetInput();
	if (pInput->IsTriggerd(eLuxAction_Rotate) && mbInteracting)
	{
		if (mpBody)
		{
			m_mtxBodyRotation = cMath::MatrixMul(cMath::MatrixRotateX(afAmount * -3.2f), m_mtxBodyRotation);
			return false;
		}
	}

	return true;
}

void cLuxPlayerState_PhysGun::RenderSolid(cRendererCallbackFunctions* apFunctions)
{
	// Draw the PhysGun crosshair
	if (mpPhysGunCrosshairGfx)
		gpBase->mpGameHudSet->DrawGfx(mpPhysGunCrosshairGfx, mvCrossPos, mvCrossSize, cColor(1, 1));

	// Check if we're interacting
	if (mbInteracting)
	{
		// If a body isn't grabbed, we need to cast a ray
		if (!mpBody)
		{
			// Ray cast start and end points
			cCamera* pCam = gpBase->mpPlayer->GetCamera();
			cVector3f vStart = pCam->GetPosition();
			cVector3f vEnd = vStart + pCam->GetForward() * 100;

			// Reset the ray callback and cast a new ray
			mpRayCallback->Reset();
			iPhysicsWorld* pPhysicsWorld = gpBase->mpMapHandler->GetCurrentMap()->GetPhysicsWorld();
			pPhysicsWorld->CastRay(mpRayCallback, vStart, vEnd, false, false, true, true);

			// If we grabbed a body with this ray cast, set up the interaction
			if (mpRayCallback->GetBody())
				OnGrab();
		}

		// Only draw if the ray intersected a body
		if (mpRayCallback->GetIntersected())
		{
			cVector3f vRayStart = mpPhysgunEntity->GetBoneStateFromName("ray_start")->GetWorldPosition();
			cVector3f vRayEnd;
			if (mpBody)
				vRayEnd = cMath::MatrixMul(mpBody->GetLocalMatrix(), mvGrabLocalPos);
			else
				vRayEnd = mpRayCallback->GetPos();
			// Debug drawing
			if (mbDebug)
			{
				if (mpBody)
				{
					if (mpEnemy)
					{
						DrawPhysBeam(apFunctions, vRayStart, mpBody->GetCharacterBody()->GetPosition());
						return;
					}
						DrawPhysBeam(apFunctions, vRayStart, vRayEnd, 0.25, 0.5f, 0.1f);
				}
				else
					DrawPhysBeam(apFunctions, vRayStart, vRayEnd);
			}
		}
	}
}

static inline void SetVec3(float* apPos, const cVector3f& aPos)
{
	apPos[0] = aPos.x;
	apPos[1] = aPos.y;
	apPos[2] = aPos.z;
}

static inline void SetVec4(float* apPos, const cVector3f& aPos)
{
	apPos[0] = aPos.x;
	apPos[1] = aPos.y;
	apPos[2] = aPos.z;
	apPos[3] = 1;
}

void cLuxPlayerState_PhysGun::DrawPhysBeam(cRendererCallbackFunctions* apFunctions, cVector3f avStart, cVector3f avEnd, float afStepSize, float afCurvatureFactor, float afBeamWidth)
{
	float* pPosArray = mpVtxBuffer->GetFloatArray(eVertexBufferElement_Position);
	float* pNrmArray = mpVtxBuffer->GetFloatArray(eVertexBufferElement_Normal);
	float* pUvArray = mpVtxBuffer->GetFloatArray(eVertexBufferElement_Texture0);

	if (afStepSize > 0)
	{
		float fDistance = cMath::Vector3Dist(avStart, avEnd);
		if (fDistance != 0)
		{
			if (fDistance / (afStepSize * 2) >= 1.0f)
			{
				std::vector<cVector3f> vBezierPoints = QuadraticBezier(avStart, avEnd, afStepSize, afCurvatureFactor);
				size_t PointCount = vBezierPoints.size();

				cVector3f vPrevPoint = avStart;
				vBezierPoints[PointCount - 1] = avEnd;
				for (int i = 1; i < PointCount; ++i)
				{
					cVector3f vDir = cMath::Vector3Normalize(vBezierPoints[i] - vPrevPoint);
					cVector3f vCamPos = gpBase->mpPlayer->GetCamera()->GetPosition();
					cVector3f vToCam = cMath::Vector3Normalize(vCamPos - vPrevPoint);
					cVector3f vRight = cMath::Vector3Normalize(cMath::Vector3Cross(vDir, vToCam));
					cVector3f vOffset = vRight * (afBeamWidth / 2.0f);

					cVector3f vCoords[4] = { cVector3f(vPrevPoint + vOffset),
											 cVector3f(vPrevPoint - vOffset),
											 cVector3f(vBezierPoints[i] - vOffset),
											 cVector3f(vBezierPoints[i] + vOffset) };

					cVector3f vTexCoords[4] = { cVector3f(1,mfUvOffset,0),
												cVector3f(-1,mfUvOffset,0),
												cVector3f(-1,mfUvOffset - 2,0),
												cVector3f(1,mfUvOffset - 2,0) };

					cVector3f vEdge1 = vCoords[1] - vCoords[0];
					cVector3f vEdge2 = vCoords[2] - vCoords[0];
					cVector3f vNormal = cMath::Vector3Normalize(cMath::Vector3Cross(vEdge1, vEdge2));

					for (int j = 0; j < 4; j++)
					{
						for (int k = 0; k < 2; k++)
						{
							int vertexIndex = j * 2 + k;
							SetVec4(&pPosArray[vertexIndex * 4], vCoords[j]);
							SetVec3(&pNrmArray[vertexIndex * 3], vNormal);
							SetVec3(&pUvArray[vertexIndex * 3], (k == 0 ? (vTexCoords[j] + cVector2f(1, 1)) / 2 : (vTexCoords[3 - j] + cVector2f(1, 1)) / 2));
						}
					}

					pPosArray += 4 * 8;
					pNrmArray += 3 * 8;
					pUvArray += 3 * 8;

					vPrevPoint = vBezierPoints[i];
				}
				mpVtxBuffer->SetElementNum((PointCount - 1) * 12);
				mpVtxBuffer->UpdateData(eVertexElementFlag_Position | eVertexElementFlag_Normal | eVertexElementFlag_Texture0, false);
				apFunctions->SetProgram(NULL);
				apFunctions->SetDepthTest(mpMaterial->GetDepthTest());
				apFunctions->SetBlendMode(mpMaterial->GetBlendMode());
				apFunctions->SetAlphaMode(mpMaterial->GetAlphaMode());
				apFunctions->SetTexture(0, mpMaterial->GetTexture(eMaterialTexture_Diffuse));
				apFunctions->SetVertexBuffer(mpVtxBuffer);
				apFunctions->DrawCurrent();

				if (mfUvOffset >= 2.0f)
					mfUvOffset = 0.05f;
				else
					mfUvOffset += 0.05f;
				return;
			}
		}
	}
	cVector3f vDir = cMath::Vector3Normalize(avEnd - avStart);
	cVector3f vCamPos = gpBase->mpPlayer->GetCamera()->GetPosition();
	cVector3f vToCam = cMath::Vector3Normalize(vCamPos - avStart);
	cVector3f vRight = cMath::Vector3Normalize(cMath::Vector3Cross(vDir, vToCam));
	cVector3f vOffset = vRight * (afBeamWidth / 2.0f);

	tVertexVec vBeamQuad[4]; vBeamQuad[0].resize(4);
	vBeamQuad[0][0].pos = avStart + vOffset;
	vBeamQuad[0][1].pos = avStart - vOffset;
	vBeamQuad[0][2].pos = avEnd - vOffset;
	vBeamQuad[0][3].pos = avEnd + vOffset;

	apFunctions->GetLowLevelGfx()->DrawQuad(vBeamQuad[0], cColor(0, 0, 1));
}

void cLuxPlayerState_PhysGun::DrawDebugRay(cRendererCallbackFunctions* apFunctions, cVector3f avStart, cVector3f avEnd, float afStepSize, float afCurvatureFactor)
{
	if (afStepSize > 0)
	{
		float fDistance = cMath::Vector3Dist(avStart, avEnd);
		if (fDistance != 0)
		{
			if (fDistance / (afStepSize * 2) >= 1.0f)
			{
				std::vector<cVector3f> vBezierPoints = QuadraticBezier(avStart, avEnd, afStepSize, afCurvatureFactor);
				size_t PointCount = vBezierPoints.size();

				apFunctions->GetLowLevelGfx()->DrawSphere(avStart, 0.05f, cColor(1, 0, 0));
				apFunctions->GetLowLevelGfx()->DrawSphere(avEnd, 0.05f, cColor(1, 0, 0));

				cVector3f vPrevPoint = avStart;
				for (int i = 1; i < PointCount - 1; ++i)
				{
					apFunctions->GetLowLevelGfx()->DrawSphere(vBezierPoints[i], 0.025f, cColor(0, 1, 0));
					apFunctions->GetLowLevelGfx()->DrawLine(vPrevPoint, vBezierPoints[i], cColor(1, 1, 1));
					vPrevPoint = vBezierPoints[i];
				}
				apFunctions->GetLowLevelGfx()->DrawLine(vPrevPoint, avEnd, cColor(1, 1, 1));
				return;
			}
		}
	}
	apFunctions->GetLowLevelGfx()->DrawSphere(avStart, 0.05f, cColor(1, 0, 0));
	apFunctions->GetLowLevelGfx()->DrawSphere(avEnd, 0.05f, cColor(1, 0, 0));
	apFunctions->GetLowLevelGfx()->DrawLine(avStart, avEnd, cColor(1, 1, 1));
}

std::vector<cVector3f> cLuxPlayerState_PhysGun::QuadraticBezier(cVector3f avStart, cVector3f avEnd, float afStepSize, float afCurvatureFactor)
{
	if (afStepSize <= 0)
		FatalError("Tried to create curve with invalid step size!\n");

	cVector3f vCamPos = gpBase->mpPlayer->GetCamera()->GetPosition();
	cVector3f vCamForward = cMath::Vector3Normalize(gpBase->mpPlayer->GetCamera()->GetForward());

	cVector3f vMid = (avStart + avEnd) * 0.5f;
	cVector3f vToRayEnd = avEnd - vCamPos;
	float fForwardComponent = cMath::Vector3Dot(vToRayEnd, vCamForward);
	cVector3f vLateral = vToRayEnd - vCamForward * fForwardComponent;
	cVector3f vControlPoint = vMid - vLateral * afCurvatureFactor;

	std::vector<cVector3f> vBezierPoints;
	float fStep = afStepSize / cMath::Vector3Dist(avStart, avEnd);
	for (float t = 0; t <= 1.0f; t += fStep)
	{
		cVector3f vA = Interpolate(avStart, vControlPoint, t);
		cVector3f vB = Interpolate(vControlPoint, avEnd, t);
		cVector3f vC = Interpolate(vA, vB, t);
		vBezierPoints.push_back(vC);
	}

	return vBezierPoints;
}

bool cLuxPhysGunRayCallback::BeforeIntersect(iPhysicsBody* pBody)
{
	if (pBody->GetName() == "Enemy_Grunt" ||
		pBody->GetName() == "Enemy_WaterLurker" ||
		pBody->GetName() == "Enemy_ManPig")
		return true;

	if (pBody->GetCollide() == false ||
		pBody->IsCharacter())
		return false;

	return true;
}

bool cLuxPhysGunRayCallback::OnIntersect(iPhysicsBody* pBody, cPhysicsRayParams* apParams)
{
	// Get the closest intersection
	if (apParams->mfT < mfClosestT)
	{
		mfClosestT = apParams->mfT;
		mvPos = apParams->mvPoint;

		if (pBody->GetName() == "Enemy_Grunt" ||
			pBody->GetName() == "Enemy_WaterLurker" ||
			pBody->GetName() == "Enemy_ManPig")
			mbIsEnemy = true;
		else
			mbIsEnemy = false;

		if (pBody->GetMass() > 0 || mbIsEnemy)
			mpBody = pBody;
		else
			mpBody = NULL;
	}
	mbIntersected = true;

	return true;
}

void cLuxPhysGunRayCallback::Reset()
{
	mpBody = NULL;
	mbIsEnemy = false;
	mbIntersected = false;
	mfClosestT = 99999.0f;
}
