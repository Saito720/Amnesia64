#include "LuxPlayerState_PhysGun.h"

#include "LuxPlayer.h"
#include "LuxMapHandler.h"
#include "LuxMap.h"
#include "LuxInputHandler.h"
#include "LuxDebugHandler.h"

cLuxPlayerState_PhysGun::cLuxPlayerState_PhysGun(cLuxPlayer* apPlayer) : iLuxPlayerState_Interact(apPlayer, eLuxPlayerState_PhysGun)
{
	// Grab settings from game config
	mfMaxForce = gpBase->mpGameCfg->GetFloat("Player_Interaction", "PhysGunMaxForce", 0);
	mfMaxTorque = gpBase->mpGameCfg->GetFloat("Player_Interaction", "PhysGunMaxTorque", 0);
	mfMaxAngularSpeed = gpBase->mpGameCfg->GetFloat("Player_Interaction", "PhysGunMaxAngularSpeed", 0);

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
}

void cLuxPlayerState_PhysGun::OnLeaveState(eLuxPlayerState aNewState) {}

void cLuxPlayerState_PhysGun::OnInteraction()
{
	cCamera* pCam = gpBase->mpPlayer->GetCamera();
	cVector3f vStart = pCam->GetPosition();
	cVector3f vEnd = cMath::MatrixMul(mpCurrentBody->GetLocalMatrix(), mvLocalPos);
	mfDepth = cMath::Vector3Dist(vStart, vEnd);

	mForcePid.Reset();
	mSpeedTorquePid.Reset();

	cVector3f vCamRotation(pCam->GetPitch(), pCam->GetYaw(), pCam->GetRoll());
	cMatrixf mtxCamRot = cMath::MatrixRotate(vCamRotation, eEulerRotationOrder_XYZ);
	cMatrixf mtxInvCamRot = cMath::MatrixInverse(mtxCamRot);

	m_mtxBodyRotation = cMath::MatrixMul(mtxInvCamRot, mpCurrentBody->GetLocalMatrix().GetRotation());

	mvLocalBodyOffset = mpCurrentBody->GetLocalPosition() - vEnd;
	mvLocalBodyOffset = cMath::MatrixMul(mtxInvCamRot, mvLocalBodyOffset);
}

void cLuxPlayerState_PhysGun::Update(float afTimeStep) {}

void cLuxPlayerState_PhysGun::PostUpdate(float afTimeStep)
{
	// Does the player have a valid entity?
	if (mpRayCallback->GetValid())
	{
		// Camera rotation
		cCamera* pCam = gpBase->mpPlayer->GetCamera();
		cVector3f vCamRotation(pCam->GetPitch(), pCam->GetYaw(), pCam->GetRoll());
		cMatrixf mtxCamTransform = cMath::MatrixRotate(vCamRotation, eEulerRotationOrder_XYZ);
		mtxCamTransform.SetTranslation(pCam->GetPosition());

		// Define the target grab position in world space
		cVector3f vTargetGrab = pCam->GetPosition() + pCam->GetForward() * mfDepth;

		// Compute the new center of the body
		cVector3f vNewCenter = vTargetGrab + cMath::MatrixMul(m_mtxBodyRotation, mvLocalBodyOffset);

		// The final body matrix
		cMatrixf mtxGoal = cMath::MatrixMul(cMath::MatrixTranslate(vNewCenter), m_mtxBodyRotation);
		mtxGoal = cMath::MatrixMul(mtxCamTransform, mtxGoal);

		///////////////////////
		// Force
		cVector3f vCurrentGrab = cMath::MatrixMul(mpCurrentBody->GetLocalMatrix(), mvLocalPos);
		cVector3f vError = vTargetGrab - vCurrentGrab;
		cVector3f vForce = mForcePid.Output(vError, afTimeStep) * mpCurrentBody->GetMass();
		vForce = cMath::Vector3MaxLength(vForce, mfMaxForce);
		mpCurrentBody->AddForce(vForce);

		/////////////////////////
		// Get the wanted speed
		cVector3f vWantedRotSpeed = 0;

		cMatrixf mtxGoalInv = cMath::MatrixInverse(mtxGoal);

		cVector3f vWantedUp = mtxGoalInv.GetUp();
		cVector3f vWantedRight = mtxGoalInv.GetRight();

		cMatrixf mtxBodyInv = cMath::MatrixInverse(mpCurrentBody->GetLocalMatrix());
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
		{
			vWantedRotSpeed = (vWantedRotSpeed / fSpeed) * 6.0f;
		}

		/////////////////////////
		// Set speed by torque
		cVector3f vRotError = vWantedRotSpeed - mpCurrentBody->GetAngularVelocity();

		cVector3f vTorque = mSpeedTorquePid.Output(vRotError, afTimeStep);
		vTorque = cMath::MatrixMul(mpCurrentBody->GetInertiaMatrix(), vTorque);

		// Make sure force is not too large
		vTorque = cMath::Vector3MaxLength(vTorque, mfMaxTorque);

		mpCurrentBody->AddTorque(vTorque);
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
			mpRayCallback->Reset();
		}
	}

	return true;
}

void cLuxPlayerState_PhysGun::OnScroll(float afAmount)
{
	mfDepth += afAmount;
}

bool cLuxPlayerState_PhysGun::OnAddYaw(float afAmount)
{
	cInput* pInput = gpBase->mpEngine->GetInput();
	if (pInput->IsTriggerd(eLuxAction_Rotate))
	{
		m_mtxBodyRotation = cMath::MatrixMul(cMath::MatrixRotateY(afAmount * -3.2f), m_mtxBodyRotation);
		return false;
	}

	return true;
}

bool cLuxPlayerState_PhysGun::OnAddPitch(float afAmount)
{
	cInput* pInput = gpBase->mpEngine->GetInput();
	if (pInput->IsTriggerd(eLuxAction_Rotate))
	{
		m_mtxBodyRotation = cMath::MatrixMul(cMath::MatrixRotateX(afAmount * -3.2f), m_mtxBodyRotation);
		return false;
	}

	return true;
}

void cLuxPlayerState_PhysGun::RenderSolid(cRendererCallbackFunctions* apFunctions)
{
	// Draw the PhysGun crosshair
	if (mpPhysGunCrosshairGfx)
		gpBase->mpGameHudSet->DrawGfx(mpPhysGunCrosshairGfx, mvCrossPos, mvCrossSize, cColor(1, 1));

	// Are we are interacting?
	if (mbInteracting)
	{
		// Only cast rays if we don't have an valid entity
		if (!mpRayCallback->GetValid())
		{
			// Raycast start and end points
			cCamera* pCam = gpBase->mpPlayer->GetCamera();
			cVector3f vStart = pCam->GetPosition();
			cVector3f vEnd = vStart + pCam->GetForward() * 100;

			// Reset ray callback and cast a new ray
			mpRayCallback->Reset();
			iPhysicsWorld* pPhysicsWorld = gpBase->mpMapHandler->GetCurrentMap()->GetPhysicsWorld();
			pPhysicsWorld->CastRay(mpRayCallback, vStart, vEnd, false, false, true, true);

			// If we got a valid entity with this cast, set up the interaction
			if (mpRayCallback->GetValid())
			{
				mpCurrentBody = mpRayCallback->GetBody();
				mvLocalPos = mpRayCallback->GetLocalPos();
				OnInteraction();
			}
		}

		// Only draw if the ray intersected
		if (mpRayCallback->GetIntersected())
		{
			cVector3f vRayStart = mpMeshEntity->GetBoneStateFromName("ray_start")->GetWorldPosition();
			cVector3f vRayEnd;
			if (mpRayCallback->GetValid())
				vRayEnd = cMath::MatrixMul(mpCurrentBody->GetLocalMatrix(), mvLocalPos);
			else
				vRayEnd = mpRayCallback->GetPos();

			/* float fRayWidth = 0.025f;
			cVector3f vDir = cMath::Vector3Normalize(vRayEnd - vRayStart);
			cVector3f vCamUp = gpBase->mpPlayer->GetCamera()->GetUp();
			cVector3f vRight = cMath::Vector3Normalize(cMath::Vector3Cross(vDir, vCamUp));
			cVector3f vOffset = vRight * (fRayWidth / 2.0f);

			tVertexVec vBeamQuads[4]; vBeamQuads[0].resize(4);
			vBeamQuads[0][0].pos = vRayStart + vOffset;
			vBeamQuads[0][1].pos = vRayStart - vOffset;
			vBeamQuads[0][2].pos = vRayEnd - vOffset;
			vBeamQuads[0][3].pos = vRayEnd + vOffset;

			apFunctions->GetLowLevelGfx()->DrawQuad(vBeamQuads[0], cColor(0, 0, 1)); */

			// Debug drawing
			if (mbDebug)
			{
				if (mpRayCallback->GetValid())
				{
					float fDistance = Distance(vRayStart, vRayEnd);
					float fStepSize = 0.25;
					if (fDistance > 0)
					{
						if (fDistance / (fStepSize * 2) >= 1.0f)
						{
							cVector3f vCamPos = gpBase->mpPlayer->GetCamera()->GetPosition();
							cVector3f vCamForward = cMath::Vector3Normalize(gpBase->mpPlayer->GetCamera()->GetForward());

							cVector3f vMid = (vRayStart + vRayEnd) * 0.5f;
							cVector3f vToRayEnd = vRayEnd - vCamPos;
							float forwardComponent = cMath::Vector3Dot(vToRayEnd, vCamForward);
							cVector3f vLateral = vToRayEnd - vCamForward * forwardComponent;
							float curvatureFactor = 0.5f;
							cVector3f controlPoint = vMid - vLateral * curvatureFactor;

							float fStep = fStepSize / fDistance;
							for (float t = 0; t <= 1.0f; t += fStep)
							{
								cVector3f vA = Interpolate(vRayStart, controlPoint, t);
								cVector3f vB = Interpolate(controlPoint, vRayEnd, t);
								cVector3f vC = Interpolate(vA, vB, t);

								if(t == 0)
									apFunctions->GetLowLevelGfx()->DrawSphere(vC, 0.05f, cColor(1, 0, 0));
								else if (t + fStep >= 1.0f)
								{
									fStep = 1.0f;
									vC = vRayEnd;
									apFunctions->GetLowLevelGfx()->DrawSphere(vC, 0.05f, cColor(1, 0, 0));
								}
								else
									apFunctions->GetLowLevelGfx()->DrawSphere(vC, 0.025f, cColor(0, 1, 0));

								if(vPrevC != -1)
									apFunctions->GetLowLevelGfx()->DrawLine(vPrevC, vC, cColor(1, 1, 1));
								vPrevC = vC;
							}
							vPrevC = -1;
						}
						else
						{
							apFunctions->GetLowLevelGfx()->DrawSphere(vRayStart, 0.05f, cColor(1, 0, 0));
							apFunctions->GetLowLevelGfx()->DrawSphere(vRayEnd, 0.05f, cColor(1, 0, 0));
							apFunctions->GetLowLevelGfx()->DrawLine(vRayStart, vRayEnd, cColor(1, 1, 1));
						}
					}
				}
				else
				{
					apFunctions->GetLowLevelGfx()->DrawSphere(vRayStart, 0.05f, cColor(1, 0, 0));
					apFunctions->GetLowLevelGfx()->DrawSphere(vRayEnd, 0.05f, cColor(1, 0, 0));
					apFunctions->GetLowLevelGfx()->DrawLine(vRayStart, vRayEnd, cColor(1, 1, 1));
				}
			}
		}
	}
}

bool cLuxPhysGunRayCallback::BeforeIntersect(iPhysicsBody* pBody)
{
	// If the entity isn't collidable or it's the character, ignore it
	if (pBody->GetCollide() == false || pBody->IsCharacter()) return false;
	return true;
}

bool cLuxPhysGunRayCallback::OnIntersect(iPhysicsBody* pBody, cPhysicsRayParams* apParams)
{
	// Get the first ray intersection
	if (apParams->mfT < mfClosestT)
	{
		mfClosestT = apParams->mfT;
		mvPos = apParams->mvPoint;
		mpBody = pBody;

		// Does the entity have a mass greater than 0? If so, it's interactable.
		if (mpBody->GetMass() > 0)
		{
			cMatrixf mtxInvWorld = cMath::MatrixInverse(mpBody->GetLocalMatrix());
			mvLocalPos = cMath::MatrixMul(mtxInvWorld, mvPos);

			mbValid = true;
		}
		else
			mbValid = false;
	}
	// If we intersected anything, flag it
	mbIntersected = true;

	return true;
}

void cLuxPhysGunRayCallback::Reset()
{
	mbValid = false;
	mbIntersected = false;
	mfClosestT = 99999.0f;
}
