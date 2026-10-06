/* Enemy_Llama observation/controller foundation. GPL-3.0-or-later. */
#include "LuxEnemy_Llama.h"
#include "LuxEnemy_LlamaProfiles.h"
#include "LuxEnemyMover.h"
#include "LuxEnemyPathfinder.h"
#include "LuxDebugHandler.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxProp_SwingDoor.h"
#include "graphics/SceneObservation.h"

#include <cmath>

iLuxEnemy* cLuxEnemyLoader_Llama::CreateEnemy(const tString& asName, int alID, cLuxMap* apMap)
{
	cLuxEnemy_Llama* pEnemy = hplNew(cLuxEnemy_Llama, (asName, alID, apMap));
	pEnemy->msRigProfile = msResolvedProfile;
	return pEnemy;
}

void cLuxEnemyLoader_Llama::AfterLoad(cXmlElement* apRootElem, const cMatrixf& a_mtxTransform,
	cWorld* apWorld, cResourceVarsObject* apInstanceVars)
{
	const tString sSubtype = cString::ToLowerCase(msEntitySubType.empty() ? "Grunt" : msEntitySubType);
	const cLuxLlamaProfile* pProfile = &kLuxLlamaProfiles[0];
	bool bFound = false;
	for(size_t i=0; i<kLuxLlamaProfileCount; ++i)
	{
		if(sSubtype == cString::ToLowerCase(kLuxLlamaProfiles[i].msName))
		{
			pProfile = &kLuxLlamaProfiles[i];
			bFound = true;
			break;
		}
	}
	if(!bFound) Warning("Enemy_Llama '%s': unknown subtype '%s'; using Grunt defaults.\n", msFileName.c_str(), msEntitySubType.c_str());
	msResolvedProfile = pProfile->msName;
	// Subtypes supply defaults only. A custom asset can override any physical setting.
	for(size_t i=0; i<pProfile->mlVariableCount; ++i)
	{
		const cLuxLlamaProfileVariable& var = pProfile->mpVariables[i];
		if(GetUserVariable(var.msName)==NULL) SetUserVariable(var.msName,var.msValue);
	}
	iLuxEnemyLoader::AfterLoad(apRootElem,a_mtxTransform,apWorld,apInstanceVars);
}

void cLuxEnemyLoader_Llama::LoadVariables(iLuxEnemy* apEnemy, cXmlElement*)
{
	cLuxEnemy_Llama* pEnemy = static_cast<cLuxEnemy_Llama*>(apEnemy);
	pEnemy->mvObservationSize.x = cMath::Clamp(GetVarInt("LlamaObservationWidth", 384), 64, 1024);
	pEnemy->mvObservationSize.y = cMath::Clamp(GetVarInt("LlamaObservationHeight", 256), 64, 1024);
	float fFOV = GetVarFloat("FOV", 120.0f);
	if(!std::isfinite(fFOV)) fFOV = 120.0f;
	// Read the authored angle directly, independent of legacy AI FOV multipliers.
	pEnemy->mfObservationFOV = cMath::ToRad(cMath::Clamp(fFOV, 1.0f, 179.0f));
	pEnemy->mfObservationInterval = cMath::Clamp(GetVarFloat("LlamaObservationInterval", 0.25f), 0.05f, 5.0f);
	pEnemy->mvCameraOffset = GetVarVector3f("LlamaCameraOffset", cVector3f(0,-0.1f,0));
	pEnemy->mfHearingRange = cMath::Clamp(GetVarFloat("LlamaHearingRange", 12), 0.0f, 100.0f);
	pEnemy->mfSoundThreshold = cMath::Clamp(GetVarFloat("LlamaSoundThreshold", 0.2f), 0.0f, 1.0f);
	pEnemy->msAnimationNames[0] = GetVarString("LlamaIdleAnimation", "");
	pEnemy->msAnimationNames[1] = GetVarString("LlamaWalkAnimation", "");
	pEnemy->msAnimationNames[2] = GetVarString("LlamaRunAnimation", "");
	pEnemy->msAnimationNames[3] = GetVarString("LlamaBackwardAnimation", "");
	pEnemy->msDeathAnimation = GetVarString("LlamaDeathAnimation", "");
	pEnemy->mbCausesSanityDecrease = pEnemy->mbCausesSanityDecreaseAsDefault = false;
	pEnemy->mbAlignEntityWithGroundRay = true;
}

void cLuxEnemyLoader_Llama::LoadInstanceVariables(iLuxEnemy* apEnemy, cResourceVarsObject*)
{
	// The first controller supports ordinary biped character bodies only.
	cLuxEnemy_Llama* pEnemy = static_cast<cLuxEnemy_Llama*>(apEnemy);
	pEnemy->mCurrentPose = eLuxEnemyPoseType_Biped;
	pEnemy->mbHallucination = false;
	pEnemy->SetMoveSpeed(eLuxEnemyMoveSpeed_Walk);
}

cLuxEnemy_Llama::cLuxEnemy_Llama(const tString& asName, int alID, cLuxMap* apMap)
	: iLuxEnemy(asName, alID, apMap, eLuxEnemyType_Llama),
	mpObservationCamera(NULL), mpObservation(NULL), mvObservationSize(384,256), mfObservationFOV(cMath::ToRad(120.0f)),
	mvCameraOffset(0,-0.1f,0), mfObservationInterval(0.25f), mfHearingRange(12), mfSoundThreshold(0.2f),
	mbObservationEnabled(false), mfElapsedTime(0), mfObservationTime(0), mfLastObservationAttempt(-10), mlObservationFrameId(0),
	mvObservationPosition(0), mfObservationYaw(0), mObservationProjection(cMatrixf::Identity), msObservationStatus("Observation disabled; inference disabled"),
	mfDebugForward(0), mfDebugTurn(0), mfDebugInputTimeout(0)
{
	mCurrentState = mNextState = mPreviousState = eLuxEnemyState_Idle;
}

cLuxEnemy_Llama::~cLuxEnemy_Llama()
{
	if(gpBase->mpDebugHandler) gpBase->mpDebugHandler->OnLlamaEnemyDestroyed(GetID());
	if(mpCharBody && mpCharBody->GetCamera()==mpObservationCamera) mpCharBody->SetCamera(NULL);
	if(mpObservationCamera) gpBase->mpEngine->GetScene()->DestroyCamera(mpObservationCamera);
	if(mpObservation) hplDelete(mpObservation);
}

cLuxEnemy_Llama* cLuxEnemy_Llama::GetControllerOwner(cLuxMap* apMap)
{
	if(!apMap) return NULL;
	cLuxEnemy_Llama* pOwner = NULL;
	cLuxEnemyIterator it = apMap->GetEnemyIterator();
	while(it.HasNext())
	{
		iLuxEnemy* pEnemy = it.Next();
		if(pEnemy->GetEnemyType()!=eLuxEnemyType_Llama || !pEnemy->IsActive() ||
			pEnemy->IsDisabled() || !(pEnemy->GetHealth()>0) || pEnemy->GetDestroyMe()) continue;
		if(!pOwner || pEnemy->GetID()<pOwner->GetID()) pOwner = static_cast<cLuxEnemy_Llama*>(pEnemy);
	}
	return pOwner;
}

bool cLuxEnemy_Llama::IsControllerOwner() const
{
	return gpBase->mpMapHandler->GetCurrentMap()==mpMap && GetControllerOwner(mpMap)==this;
}

void cLuxEnemy_Llama::OnSetupAfterLoad(cWorld*)
{
	SelectMovementAnimations();
	ClearLegacyPerception();
	mpObservationCamera = gpBase->mpEngine->GetScene()->CreateCamera(eCameraMoveMode_Walk);
	const float fAspect = float(mvObservationSize.x) / float(mvObservationSize.y);
	mpObservationCamera->SetAspect(fAspect);
	// HPL's projection FOV is vertical; the entity's observation FOV is horizontal.
	mpObservationCamera->SetFOV(2.0f * std::atan(std::tan(mfObservationFOV*0.5f) / fAspect));
	mpObservationCamera->SetNearClipPlane(0.05f);
	mpObservationCamera->SetFarClipPlane(100.0f);
	mpObservationCamera->SetPitch(0);
	mpObservationCamera->SetRoll(0);
	mpCharBody->SetCamera(mpObservationCamera);
	mpCharBody->SetCameraPosAdd(mvCameraOffset);
	mpCharBody->SetCameraSmoothPosNum(0);
}

tString cLuxEnemy_Llama::SelectAnimation(const tString& asConfigured, const char* asStandard,
	const char* asBiped, const tString& asFallback)
{
	if(!asConfigured.empty())
	{
		if(mpMeshEntity->GetAnimationStateFromName(asConfigured)) return asConfigured;
		Warning("Enemy_Llama '%s': animation '%s' missing; using fallback.\n", msName.c_str(), asConfigured.c_str());
	}
	if(mpMeshEntity->GetAnimationStateFromName(asStandard)) return asStandard;
	if(mpMeshEntity->GetAnimationStateFromName(asBiped)) return asBiped;
	return asFallback;
}

void cLuxEnemy_Llama::SelectMovementAnimations()
{
	const tString sFirst = mpMeshEntity->GetAnimationStateNum()>0 ? mpMeshEntity->GetAnimationState(0)->GetName() : "";
	const tString sIdle = SelectAnimation(msAnimationNames[0], "Idle", "IdleBiped", sFirst);
	msIdleAnimationName[eLuxEnemyMoveType_Normal][eLuxEnemyPoseType_Biped] = sIdle;
	msWalkAnimationName[eLuxEnemyMoveType_Normal][eLuxEnemyPoseType_Biped] = SelectAnimation(msAnimationNames[1], "Walk", "WalkBiped", sIdle);
	msRunAnimationName[eLuxEnemyMoveType_Normal][eLuxEnemyPoseType_Biped] = SelectAnimation(msAnimationNames[2], "Run", "RunBiped", sIdle);
	msBackwardAnimationName[eLuxEnemyMoveType_Normal][eLuxEnemyPoseType_Biped] = SelectAnimation(msAnimationNames[3], "Backward", "BackwardBiped", sIdle);
	msDeathAnimation = SelectAnimation(msDeathAnimation, "Dead", "DeadBiped", "");
	mbUseAnimations = !sIdle.empty();
}

void cLuxEnemy_Llama::SetObservationEnabled(bool abEnabled)
{
	if(mbObservationEnabled==abEnabled) return;
	mbObservationEnabled = abEnabled;
	if(!abEnabled)
	{
		SetDebugInput(0,0);
		msObservationStatus = "Observation disabled; inference disabled";
	}
	else
	{
		mfLastObservationAttempt = mfElapsedTime-mfObservationInterval;
		msObservationStatus = "Waiting for observation; inference disabled";
	}
}

void cLuxEnemy_Llama::CaptureObservation()
{
	if(!mbObservationEnabled) return;
	if(!IsControllerOwner())
	{
		msObservationStatus = "Idle: only the lowest eligible Enemy_Llama ID owns the controller";
		return;
	}
	if(!IsActive() || mbDisabled || mfHealth<=0)
	{
		msObservationStatus = "Enemy inactive, disabled or dead; observation paused";
		return;
	}
	if(mfElapsedTime-mfLastObservationAttempt < mfObservationInterval) return;
	mfLastObservationAttempt = mfElapsedTime;
	if(!mpObservation)
	{
		mpObservation = hplNew(cSceneObservation, (gpBase->mpEngine->GetGraphics()));
	}
	if(!mpObservation->IsInitialized() && !mpObservation->Initialize(mvObservationSize))
	{
		msObservationStatus = mpObservation->GetLastError();
		return;
	}

	// Mirror the character-body camera rule also before the first physics tick/after teleport.
	const float fYaw = mpCharBody->GetYaw();
	const cMatrixf mtxYaw = cMath::MatrixRotateY(fYaw);
	const cVector3f vRight = cMath::MatrixMul3x3(mtxYaw,cVector3f(1,0,0));
	const cVector3f vForward = cMath::MatrixMul3x3(mtxYaw,cVector3f(0,0,-1));
	cVector3f vOffset = cVector3f(0,mpCharBody->GetShape(0)->GetSize().y-mpCharBody->GetSize().y*0.5f,0);
	vOffset += cVector3f(0,mvCameraOffset.y,0) + vRight*mvCameraOffset.x + vForward*mvCameraOffset.z;
	mpObservationCamera->SetPosition(mpCharBody->GetPosition()+vOffset);
	mpObservationCamera->SetYaw(fYaw);
	mpObservationCamera->SetPitch(0);
	mpObservationCamera->SetRoll(0);

	std::vector<cMeshEntity*> vDoors;
	cLuxEntityIterator it = mpMap->GetEntityIterator();
	while(it.HasNext())
	{
		iLuxEntity* pEntity = it.Next();
		if(!pEntity->IsActive() || pEntity->GetEntityType()!=eLuxEntityType_Prop) continue;
		iLuxProp* pProp = static_cast<iLuxProp*>(pEntity);
		if(pProp->GetPropType()!=eLuxPropType_SwingDoor) continue;
		cLuxProp_SwingDoor* pDoor = static_cast<cLuxProp_SwingDoor*>(pProp);
		if(pDoor->CanBeBroken() && pDoor->GetEffectMeshEntity()) vDoors.push_back(pDoor->GetEffectMeshEntity());
	}

	iCharacterBody* pPlayer = gpBase->mpPlayer->GetCharacterBody();
	if(!mpObservation->Capture(mpMap->GetWorld(), mpObservationCamera, mpMeshEntity, pPlayer, vDoors))
	{
		msObservationStatus = mpObservation->GetLastError();
		return;
	}
	++mlObservationFrameId;
	mfObservationTime = mfElapsedTime;
	mvObservationPosition = mpObservationCamera->GetPosition();
	mfObservationYaw = mpObservationCamera->GetYaw();
	mObservationProjection = mpObservationCamera->GetProjectionMatrix();
	mvObservationSounds = mvSounds;
	msObservationStatus = "Observation only; inference disabled";
}

iTexture* cLuxEnemy_Llama::GetObservationTexture() { return mpObservation ? mpObservation->GetColorTexture() : NULL; }
iTexture* cLuxEnemy_Llama::GetObservationDepthTexture() { return mpObservation ? mpObservation->GetDepthTexture() : NULL; }
const std::vector<unsigned char>& cLuxEnemy_Llama::GetObservationRGB()
{
	static const std::vector<unsigned char> vEmpty;
	return mpObservation ? mpObservation->GetRGBPixels() : vEmpty;
}

void cLuxEnemy_Llama::SetDebugInput(float afForward, float afTurn)
{
	if(!IsControllerOwner())
	{
		StopMotion();
		return;
	}
	if(mfDebugTurn!=0 && afTurn==0 && mpMover) mpMover->TurnToAngle(mpCharBody->GetYaw());
	mfDebugForward = cMath::Clamp(afForward,-1.0f,1.0f);
	mfDebugTurn = cMath::Clamp(afTurn,-1.0f,1.0f);
	// A missed debug update must never leave an enemy walking forever.
	mfDebugInputTimeout = 0.2f;
}

void cLuxEnemy_Llama::UpdateEnemySpecific(float afTimeStep)
{
	mfElapsedTime += afTimeStep;
	for(size_t i=0; i<mvSounds.size();)
	{
		mvSounds[i].mfAge += afTimeStep;
		if(mvSounds[i].mfAge>5.0f) mvSounds.erase(mvSounds.begin()+i);
		else ++i;
	}
	if(mfHealth<=0) return;
	if(!IsControllerOwner())
	{
		StopMotion();
		return;
	}
	mfDebugInputTimeout -= afTimeStep;
	if(mfDebugInputTimeout<=0) mfDebugForward=mfDebugTurn=0;
	if(mfDebugInputTimeout<=0) mpMover->TurnToAngle(mpCharBody->GetYaw());
	if(mfDebugForward!=0) mpCharBody->Move(eCharDir_Forward,mfDebugForward);
	// Same turning limits and collision handling as the shared enemy mover.
	if(mfDebugTurn!=0) mpMover->TurnToAngle(mpCharBody->GetYaw()-mfDebugTurn*cMath::ToRad(60.0f));
}

void cLuxEnemy_Llama::OnControllerMessage(eLuxEnemyMessage aType, const cVector3f& avValue, float afValue)
{
	if(aType!=eLuxEnemyMessage_SoundHeard || !IsControllerOwner() || mfHealth<=0 || !std::isfinite(afValue) ||
		!std::isfinite(avValue.x) || !std::isfinite(avValue.y) || !std::isfinite(avValue.z) ||
		afValue<mfSoundThreshold || mfHearingRange<=0) return;
	cVector3f vToSound = avValue-mpCharBody->GetPosition();
	const float fDistance = vToSound.Length();
	if(fDistance>mfHearingRange || fDistance<0.5f) return;
	// Character-body direction vectors are cached until its next physics tick.
	const float fYaw = mpCharBody->GetYaw();
	const cMatrixf mtxYaw = cMath::MatrixRotateY(fYaw);
	const float fForward = cMath::Vector3Dot(vToSound,cMath::MatrixMul3x3(mtxYaw,cVector3f(0,0,-1)));
	const float fRight = cMath::Vector3Dot(vToSound,cMath::MatrixMul3x3(mtxYaw,cVector3f(1,0,0)));
	const float fStep = cMath::ToRad(15.0f);
	cLuxLlamaSound sound;
	sound.mfAge = 0;
	sound.mfBearing = std::floor(std::atan2(fRight,fForward)/fStep+0.5f)*fStep;
	sound.mfDistance = std::floor(fDistance*0.5f+0.5f)*2.0f;
	sound.mfVolume = cMath::Clamp(afValue,0.0f,1.0f);
	sound.mfListenerYaw = fYaw;
	// Coalesce repeated footsteps/impacts instead of flooding the future prompt.
	for(size_t i=0; i<mvSounds.size(); ++i)
	{
		if(mvSounds[i].mfAge<0.25f && mvSounds[i].mfDistance==sound.mfDistance &&
			std::fabs(cMath::GetAngleDistanceRad(mvSounds[i].mfBearing-mvSounds[i].mfListenerYaw,
				sound.mfBearing-sound.mfListenerYaw))<fStep*0.5f)
		{
			mvSounds.erase(mvSounds.begin()+i);
			mvSounds.push_back(sound);
			return;
		}
	}
	if(mvSounds.size()>=8) mvSounds.erase(mvSounds.begin());
	mvSounds.push_back(sound);
}

void cLuxEnemy_Llama::StopMotion()
{
	mfDebugForward=mfDebugTurn=mfDebugInputTimeout=0;
	mpPathfinder->Stop();
	const float fVerticalVelocity = mpCharBody->GetForceVelocity().y;
	mpCharBody->StopMovement();
	// Idling must not reset gravity every tick while an extra enemy is airborne.
	mpCharBody->SetForceVelocity(cVector3f(0,fVerticalVelocity,0));
	// Cancel any outstanding manual turn using the public mover API.
	mpMover->TurnToAngle(mpCharBody->GetYaw());
}

void cLuxEnemy_Llama::ClearLegacyPerception()
{
	mbCanSeePlayer=mbPlayerDetected=mbPlayerInRange=false;
	mlPlayerInLOSCount=0;
	mvLastKnownPlayerPos=0;
	mfLastPlayerPosCount=0;
	mlstMessages.clear();
	mbHallucination=false;
	mbCausesSanityDecrease=false;
	mCurrentPose=eLuxEnemyPoseType_Biped;
}

void cLuxEnemy_Llama::OnControllerDeath()
{
	StopMotion();
	mCurrentState=mNextState=mPreviousState=eLuxEnemyState_Dead;
	if(!msDeathAnimation.empty())
	{
		// An explicit death override may reuse the clip that is already playing.
		cAnimationState* pDeath = mpMeshEntity->GetAnimationStateFromName(msDeathAnimation);
		pDeath->SetActive(false);
		pDeath->SetSpeed(1.0f);
		PlayAnim(msDeathAnimation,false,0.3f,false,1.0f,false,true,false);
	}
	else
	{
		mpMover->SetOverideMoveState(true);
		for(int i=0; i<mpMeshEntity->GetAnimationStateNum(); ++i)
		{
			mpMeshEntity->GetAnimationState(i)->SetSpeed(0);
			mpMeshEntity->GetAnimationState(i)->SetFadeStep(0);
		}
	}
	mpCharBody->SetActive(false);
	mvSounds.clear();
}

void cLuxEnemy_Llama::OnSetActiveEnemySpecific(bool abActive)
{
	if(!abActive || mfHealth<=0)
	{
		StopMotion();
		// Inactive enemies stop receiving logic ticks, so retained evidence cannot age.
		mvSounds.clear();
	}
	if(mfHealth<=0) mpCharBody->SetActive(false);
}
void cLuxEnemy_Llama::OnControllerDisabled()
{
	StopMotion();
	// Disabled enemies also stop receiving the ticks used to expire live evidence.
	mvSounds.clear();
}
void cLuxEnemy_Llama::OnDisableTriggers() { mvSounds.clear(); }
void cLuxEnemy_Llama::OnResetProperties()
{
	ClearLegacyPerception();
	StopMotion();
	mCurrentState=mNextState=mPreviousState=mfHealth>0 ? eLuxEnemyState_Idle : eLuxEnemyState_Dead;
	mvSounds.clear();
}

kBeginSerialize(cLuxEnemy_Llama_SaveData, iLuxEnemy_SaveData)
kEndSerialize()

iLuxEntity_SaveData* cLuxEnemy_Llama::CreateSaveData() { return hplNew(cLuxEnemy_Llama_SaveData, ()); }
void cLuxEnemy_Llama::LoadFromSaveData(iLuxEntity_SaveData* apSaveData)
{
	super_class::LoadFromSaveData(apSaveData);
	ClearLegacyPerception();
	StopMotion();
	mCurrentState=mNextState=mPreviousState=mfHealth>0 ? eLuxEnemyState_Idle : eLuxEnemyState_Dead;
	mvSounds.clear();
	if(mfHealth<=0) mpCharBody->SetActive(false);
}
void cLuxEnemy_Llama::SetupSaveData(iLuxEntity_SaveData* apSaveData)
{
	super_class::SetupSaveData(apSaveData);
	// Base setup resolves saved paths; an observation-only enemy has no autonomous goal.
	StopMotion();
}
