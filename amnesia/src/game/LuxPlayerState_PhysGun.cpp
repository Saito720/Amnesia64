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

	// Create pointers
	mpCam = gpBase->mpPlayer->GetCamera();
	mpPhysicsWorld = gpBase->mpMapHandler->GetCurrentMap()->GetPhysicsWorld();
}

void cLuxPlayerState_PhysGun::OnLeaveState(eLuxPlayerState aNewState) {}

void cLuxPlayerState_PhysGun::OnGrab()
{
	// Set the current body and grab position
	mpCurrentBody = mpRayCallback->GetBody();
	mvGrabPos = mpRayCallback->GetPos();

	// Local grab position
	cMatrixf mtxInvWorld = cMath::MatrixInverse(mpCurrentBody->GetLocalMatrix());
	mvGrabLocalPos = cMath::MatrixMul(mtxInvWorld, mvGrabPos);

	// Grab depth
	cVector3f vStart = mpCam->GetPosition();
	cVector3f vEnd = cMath::MatrixMul(mpCurrentBody->GetLocalMatrix(), mvGrabLocalPos);
	mfGrabDepth = cMath::Vector3Dist(vStart, vEnd);

	// If the grabbed body belongs to an enemy
	if (mpRayCallback->IsEnemy())
	{
		float vCurrentYaw = cMath::MatrixToEulerAngles(mpCurrentBody->GetLocalMatrix(), eEulerRotationOrder_XYZ).y;
		mfEnemyYaw = vCurrentYaw;
		mfGrabDepth += 0.6;
		return;
	}

	// Make sure PIDs are reset
	mForcePid.Reset();
	mSpeedTorquePid.Reset();

	// Camera rotation
	cVector3f vCamRotation(mpCam->GetPitch(), mpCam->GetYaw(), mpCam->GetRoll());
	cMatrixf mtxCamRot = cMath::MatrixRotate(vCamRotation, eEulerRotationOrder_XYZ);
	cMatrixf mtxInvCamRot = cMath::MatrixInverse(mtxCamRot);

	// Body rotation
	m_mtxBodyRotation = cMath::MatrixMul(mtxInvCamRot, mpCurrentBody->GetLocalMatrix().GetRotation());

	// Local offset between the current body and grab position
	mvLocalBodyOffset = mpCurrentBody->GetLocalPosition() - mvGrabLocalPos;
	mvLocalBodyOffset = cMath::MatrixMul(mtxInvCamRot, mvLocalBodyOffset);
}

void cLuxPlayerState_PhysGun::Update(float afTimeStep) {}

void cLuxPlayerState_PhysGun::PostUpdate(float afTimeStep)
{
	if (mpRayCallback->Grabbed())
	{
		// Camera rotation
		cVector3f vCamRotation(mpCam->GetPitch(), mpCam->GetYaw(), mpCam->GetRoll());
		cMatrixf mtxCamTransform = cMath::MatrixRotate(vCamRotation, eEulerRotationOrder_XYZ);
		mtxCamTransform.SetTranslation(mpCam->GetPosition());

		// Define the target grab position in world space
		cVector3f vTargetGrab = mpCam->GetPosition() + mpCam->GetForward() * mfGrabDepth;

		// If the grabbed body belongs to an enemy
		if (mpRayCallback->IsEnemy())
		{
			mpCurrentBody->GetCharacterBody()->SetPosition(vTargetGrab, false);
			mpCurrentBody->GetCharacterBody()->AddYaw(mfEnemyYaw);
			mpRayCallback->GetEnemy()->SendMessage(eLuxEnemyMessage_PlayerDead, 20, true);
			return;
		}

		// Compute the new center of the body
		cVector3f vNewCenter = vTargetGrab + cMath::MatrixMul(m_mtxBodyRotation, mvLocalBodyOffset);

		// The final body matrix
		cMatrixf mtxGoal = cMath::MatrixMul(cMath::MatrixTranslate(vNewCenter), m_mtxBodyRotation);
		mtxGoal = cMath::MatrixMul(mtxCamTransform, mtxGoal);

		///////////////////////
		// Force
		cVector3f vCurrentGrab = cMath::MatrixMul(mpCurrentBody->GetLocalMatrix(), mvGrabLocalPos);
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
			vWantedRotSpeed = (vWantedRotSpeed / fSpeed) * 6.0f;

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
			if (mpRayCallback->IsEnemy())
				mpRayCallback->GetEnemy()->GetMover()->SetGrabbed(false);
			// Make sure the ray callback is reset when not interacting
			mpRayCallback->Reset();
		}
	}

	return true;
}

void cLuxPlayerState_PhysGun::OnScroll(float afAmount)
{
	mfGrabDepth += afAmount;
}

bool cLuxPlayerState_PhysGun::OnAddYaw(float afAmount)
{
	cInput* pInput = gpBase->mpEngine->GetInput();
	if (pInput->IsTriggerd(eLuxAction_Rotate))
	{
		m_mtxBodyRotation = cMath::MatrixMul(cMath::MatrixRotateY(afAmount * -3.2f), m_mtxBodyRotation);

		if (mpRayCallback->IsEnemy())
			mfEnemyYaw = afAmount * -3.2;

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

	// Check if we're interacting
	if (mbInteracting)
	{
		// If a body isn't grabbed, we need to cast a ray
		if (!mpRayCallback->Grabbed())
		{
			// Ray cast start and end points
			cVector3f vStart = mpCam->GetPosition();
			cVector3f vEnd = vStart + mpCam->GetForward() * 100;

			// Reset the ray callback and cast a new ray
			mpRayCallback->Reset();
			mpPhysicsWorld->CastRay(mpRayCallback, vStart, vEnd, false, false, true, true);

			// If we grabbed a body with this ray cast, set up the interaction
			if (mpRayCallback->Grabbed())
				OnGrab();
		}

		// Only draw if the ray intersected a body
		if (mpRayCallback->GetIntersected())
		{
			cVector3f vRayStart = mpMeshEntity->GetBoneStateFromName("ray_start")->GetWorldPosition();
			cVector3f vRayEnd;
			if (mpRayCallback->Grabbed())
				vRayEnd = cMath::MatrixMul(mpCurrentBody->GetLocalMatrix(), mvGrabLocalPos);
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
				// If a body is grabbed, calculate the Physgun beam curvature
				if (mpRayCallback->Grabbed())
				{
					// If the grabbed body belongs to an enemy
					if (mpRayCallback->IsEnemy())
					{
						DrawDebugRay(apFunctions, vRayStart, mpCurrentBody->GetCharacterBody()->GetPosition());
						return;
					}

					// Get the distance between the start and end of the beam and define a step size
					float fDistance = cMath::Vector3Dist(vRayStart, vRayEnd);
					float fStepSize = 0.25;
					// Make sure we don't divide by 0 in the following conditional
					if (fDistance > 0)
					{
						// Check if the distance is great enough to calculate at least one point
						if (fDistance / (fStepSize * 2) >= 1.0f)
						{
							std::vector<cVector3f> vBezierPoints = QuadraticBezier(vRayStart, vRayEnd, fStepSize, 0.5f);
							int PointCount = vBezierPoints.size();

							cVector3f vPrevPoint = -1;
							for (int i = 0; i < PointCount; ++i)
							{
								if (i == 0)
									apFunctions->GetLowLevelGfx()->DrawSphere(vBezierPoints[i], 0.05f, cColor(1, 0, 0));
								else if (i + 1 == PointCount)
								{
									apFunctions->GetLowLevelGfx()->DrawSphere(vRayEnd, 0.05f, cColor(1, 0, 0));
									apFunctions->GetLowLevelGfx()->DrawLine(vPrevPoint, vRayEnd, cColor(1, 1, 1));
									break;
								}
								else
									apFunctions->GetLowLevelGfx()->DrawSphere(vBezierPoints[i], 0.025f, cColor(0, 1, 0));
								if (vPrevPoint != -1)
									apFunctions->GetLowLevelGfx()->DrawLine(vPrevPoint, vBezierPoints[i], cColor(1, 1, 1));
								vPrevPoint = vBezierPoints[i];
							}
						}
						// If we grabbed a body, but the distance is short, draw directly from start to end
						else
							DrawDebugRay(apFunctions, vRayStart, vRayEnd);
					}
				}
				// If we haven't grabbed a body, draw directly from start to end
				else
					DrawDebugRay(apFunctions, vRayStart, vRayEnd);
			}
		}
	}
}

std::vector<cVector3f> cLuxPlayerState_PhysGun::QuadraticBezier(cVector3f avStart, cVector3f avEnd, float afStepSize, float afCurvatureFactor)
{
	cVector3f vCamPos = mpCam->GetPosition();
	cVector3f vCamForward = cMath::Vector3Normalize(mpCam->GetForward());

	cVector3f vMid = (avStart + avEnd) * 0.5f;
	cVector3f vToRayEnd = avEnd - vCamPos;
	float fForwardComponent = cMath::Vector3Dot(vToRayEnd, vCamForward);
	cVector3f vLateral = vToRayEnd - vCamForward * fForwardComponent;
	cVector3f vControlPoint = vMid - vLateral * afCurvatureFactor;

	std::vector<cVector3f> vBezierPoints;
	float fStep = afStepSize / cMath::Vector3Dist(avStart, avEnd);;
	for (float t = 0; t <= 1.0f; t += fStep)
	{
		cVector3f vA = Interpolate(avStart, vControlPoint, t);
		cVector3f vB = Interpolate(vControlPoint, avEnd, t);
		cVector3f vC = Interpolate(vA, vB, t);
		vBezierPoints.push_back(vC);
	}

	return vBezierPoints;
}

void cLuxPlayerState_PhysGun::DrawDebugRay(cRendererCallbackFunctions* apFunctions, cVector3f avStart, cVector3f avEnd)
{
	apFunctions->GetLowLevelGfx()->DrawSphere(avStart, 0.05f, cColor(1, 0, 0));
	apFunctions->GetLowLevelGfx()->DrawSphere(avEnd, 0.05f, cColor(1, 0, 0));
	apFunctions->GetLowLevelGfx()->DrawLine(avStart, avEnd, cColor(1, 1, 1));
}

bool cLuxPhysGunRayCallback::BeforeIntersect(iPhysicsBody* pBody)
{
	if (pBody->GetName() == "Enemy_Grunt" || "Enemy_WaterLurker" || "Enemy_ManPig") return true;
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

		// If the entity has a mass greater than 0, it is grabbable
		if (mpBody->GetMass() > 0 || pBody->GetName() == "Enemy_Grunt")
		{
			mbGrabbed = true;

			if (pBody->GetName() == "Enemy_Grunt")
			{
				cLuxEnemyIterator it = gpBase->mpMapHandler->GetCurrentMap()->GetEnemyIterator();
				while (it.HasNext())
				{
					iLuxEnemy* pEnemy = it.Next();
					if (pEnemy->GetCharacterBody()->GetCurrentBody() == pBody)
					{
						mpEnemy = pEnemy;
						gpBase->mpDebugHandler->AddMessage(cString::To16Char("TRUE"), false);
						mpEnemy->GetMover()->SetGrabbed(true);
						mbIsEnemy = true;
						break;
					}
				}
			}
		}
		else
		{
			mbGrabbed = false;
			mbIsEnemy = false;
		}
	}
	// If we intersected anything, flag it
	mbIntersected = true;

	return true;
}

void cLuxPhysGunRayCallback::Reset()
{
	mpEnemy = NULL;
	mbGrabbed = false;
	mbIsEnemy = false;
	mbIntersected = false;
	mfClosestT = 99999.0f;
}
