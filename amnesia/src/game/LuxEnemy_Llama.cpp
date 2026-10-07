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
#include "LuxLlamaController.h"
#include "graphics/SceneObservation.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

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
	pEnemy->mvObservationSize.x = cMath::Clamp(GetVarInt("LlamaObservationWidth", 1280), 64, 2048);
	pEnemy->mvObservationSize.y = cMath::Clamp(GetVarInt("LlamaObservationHeight", 864), 64, 2048);
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
	pEnemy->mbModelControlEnabled = GetVarBool("LlamaControlEnabled", true);
	float fInterval = GetVarFloat("LlamaDecisionInterval", 1.0f);
	float fDuration = GetVarFloat("LlamaActionMaxSeconds", 1.5f);
	float fCooldown = GetVarFloat("LlamaAttackCooldown", 1.0f);
	pEnemy->mfDecisionInterval = std::isfinite(fInterval) ? cMath::Clamp(fInterval,0.1f,10.0f) : 1.0f;
	pEnemy->mfActionMaxSeconds = std::isfinite(fDuration) ? cMath::Clamp(fDuration,0.1f,2.0f) : 1.5f;
	pEnemy->mfAttackCooldown = std::isfinite(fCooldown) ? cMath::Clamp(fCooldown,0.2f,10.0f) : 1.0f;
	pEnemy->msAttackAnimation = GetVarString("LlamaAttackAnimation", "");
	pEnemy->msDoorAttackAnimation = GetVarString("LlamaDoorAttackAnimation", "");
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
	mpObservationCamera(NULL), mpObservation(NULL), mvObservationSize(1280,864), mfObservationFOV(cMath::ToRad(120.0f)),
	mvCameraOffset(0,-0.1f,0), mfObservationInterval(0.25f), mfHearingRange(12), mfSoundThreshold(0.2f),
	mbObservationEnabled(false), mfElapsedTime(0), mfObservationTime(0), mfLastObservationAttempt(-10), mlObservationFrameId(0), mlModelActionEndFrame(0),
	mvObservationPosition(0), mfObservationYaw(0), mObservationProjection(cMatrixf::Identity), msObservationStatus("Observation disabled; inference disabled"),
	mfDebugForward(0), mfDebugTurn(0), mfDebugInputTimeout(0),
	mbModelControlEnabled(true), mbDebugOverride(false), mfDecisionInterval(1), mfActionMaxSeconds(1.5f), mfAttackCooldown(1),
	mModelBehavior(eLuxLlamaBehavior_Patrol), mModelAction(eLuxLlamaAction_Wait), mfModelActionRemaining(0),
	mfModelForward(0), mfModelTurnGoal(0), mvModelActionStart(0), mbModelActionTracked(false),
	mfModelActionDuration(0), mfModelActionElapsed(0), mfModelActionStartYaw(0), mfModelActionRequestedTurn(0),
	mfModelDistanceMoved(0), mfModelActionSpeedLimit(0), mvModelActionLastPosition(0), mTrackedModelAction(eLuxLlamaAction_Wait),
	msModelContextSummary("No engine action history"), msModelStatus("Waiting for VLM"),
	msLastActionFeedback("No previous action"), mbModelAttackActive(false), mbModelAttackHit(false),
	mModelAttackTarget(eLuxLlamaTarget_None), mlModelAttackEntityID(-1), mlModelAttackBodyID(-1), mvModelAttackPoint(0),
	mfModelAttackElapsed(0), mfModelAttackImpactDelay(0.35f), mfModelAttackDuration(0.8f), mfModelAttackCooldown(0)
{
	mCurrentState = mNextState = mPreviousState = eLuxEnemyState_Idle;
}

cLuxEnemy_Llama::~cLuxEnemy_Llama()
{
	if(gpBase->mpMapHandler && gpBase->mpMapHandler->GetLlamaController())
		gpBase->mpMapHandler->GetLlamaController()->OnOwnerUnavailable(this);
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
		msObservationStatus = "Debug observation disabled";
	}
	else
	{
		mfLastObservationAttempt = mfElapsedTime-mfObservationInterval;
		msObservationStatus = "Waiting for observation";
	}
}

void cLuxEnemy_Llama::CaptureObservation()
{
	cLuxLlamaController* pController = gpBase->mpMapHandler->GetLlamaController();
	if(!mbObservationEnabled && !(pController && (pController->IsPerceptionOnly() ||
		(mbModelControlEnabled && pController->IsEnabled())))) return;
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
	msObservationStatus = "Observation captured";
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
	if(afForward!=0 || afTurn!=0) SetDebugOverride(true);
	if(mfDebugTurn!=0 && afTurn==0 && mpMover) mpMover->TurnToAngle(mpCharBody->GetYaw());
	mfDebugForward = cMath::Clamp(afForward,-1.0f,1.0f);
	mfDebugTurn = cMath::Clamp(afTurn,-1.0f,1.0f);
	// A missed debug update must never leave an enemy walking forever.
	mfDebugInputTimeout = 0.2f;
}

void cLuxEnemy_Llama::SetDebugOverride(bool abOverride)
{
	if(mbDebugOverride==abOverride) return;
	StopModelAction(abOverride ? "manual debug override" : "manual debug override ended");
	mbDebugOverride = abOverride;
	cLuxLlamaController* pController = gpBase->mpMapHandler->GetLlamaController();
	// A perception-only reply describes its frozen frame and cannot move the
	// enemy. Opening/closing F1 need not discard that diagnostic.
	if(pController && !pController->IsPerceptionOnly()) pController->OnManualOverride(this);
	ResetModelAction();
	msModelStatus = abOverride ? "Manual debug override" : "Waiting for fresh VLM observation";
}

bool cLuxEnemy_Llama::SnapshotModelObservation()
{
	const std::vector<unsigned char>& rgb = GetObservationRGB();
	if(!GetObservationTexture() || rgb.size()!=(size_t)mvObservationSize.x*mvObservationSize.y*3 ||
		mfElapsedTime-mfObservationTime>mfObservationInterval+0.1f) return false;
	mModelObservation.mlFrameId = mlObservationFrameId;
	mModelObservation.mfTime = mfObservationTime;
	mModelObservation.mvSize = mvObservationSize;
	mModelObservation.mvPosition = mvObservationPosition;
	mModelObservation.mfYaw = mfObservationYaw;
	mModelObservation.mProjection = mObservationProjection;
	mModelObservation.mvRGB = rgb;
	mModelObservation.mvSounds = mvObservationSounds;
	UpdateModelContextSummary();
	return true;
}

void cLuxEnemy_Llama::MeasureModelActionProgress()
{
	if(!mbModelActionTracked || !mpCharBody) return;
	cVector3f moved = mpCharBody->GetPosition()-mvModelActionLastPosition;
	moved.y = 0;
	const float distance = moved.Length();
	if(std::isfinite(distance)) mfModelDistanceMoved += distance;
	mvModelActionLastPosition = mpCharBody->GetPosition();
}

tString cLuxEnemy_Llama::DescribeModelActionProgress(const char* asState) const
{
	std::ostringstream feedback;
	feedback.imbue(std::locale::classic());
	feedback << std::fixed << std::setprecision(2);
	feedback << GetLuxLlamaActionName(mTrackedModelAction) << " " << asState
		<< ": elapsed=" << mfModelActionElapsed << "/" << mfModelActionDuration << "s";
	if(mTrackedModelAction==eLuxLlamaAction_Move || mTrackedModelAction==eLuxLlamaAction_Turn)
	{
		cVector3f moved = mpCharBody->GetPosition()-mvModelActionStart;
		moved.y = 0;
		const float turned = -cMath::ToDeg(cMath::GetAngleDistanceRad(mfModelActionStartYaw,mpCharBody->GetYaw()));
		const float remaining = std::fabs(cMath::ToDeg(cMath::GetAngleDistanceRad(mpCharBody->GetYaw(),mfModelTurnGoal)));
		feedback << ", horizontal_distance=" << mfModelDistanceMoved << ", displacement=" << moved.Length()
			<< ", forward_input=" << mfModelForward << ", turn_right=" << turned
			<< "/" << mfModelActionRequestedTurn << "deg, remaining_turn=" << remaining << "deg";
		if(mTrackedModelAction==eLuxLlamaAction_Move && mfModelForward!=0 && mfModelActionElapsed>=0.1f)
		{
			// This budget comes from the authored speed limit, not a promise of
			// travel: acceleration, turning and collisions can all reduce it.
			// Character Move scales acceleration input, not that speed limit.
			const float budget = mfModelActionSpeedLimit*mfModelActionElapsed;
			feedback << ", nominal_travel_budget=" << budget;
			if(mfModelDistanceMoved<0.05f) feedback << "; blocked/no movement progress";
			else if((mpMover && mpMover->GetStuckCounter()>0.1f) ||
				(mfModelActionElapsed>=0.3f && budget>0.1f && mfModelDistanceMoved<budget*0.5f))
				feedback << "; limited movement progress (collision, acceleration or turning)";
		}
		if(remaining>5.0f) feedback << "; turn incomplete";
		else feedback << "; turn achieved";
	}
	return feedback.str();
}

void cLuxEnemy_Llama::SnapshotModelActionFeedback()
{
	if(!mbModelActionTracked) return;
	MeasureModelActionProgress();
	// Attack impact supplies the mechanical hit/miss result. Keep that evidence
	// intact while the rest of the animation finishes.
	if(mTrackedModelAction!=eLuxLlamaAction_Attack)
		msLastActionFeedback = DescribeModelActionProgress("in progress");
}

void cLuxEnemy_Llama::RecordModelActionOutcome(const tString& asOutcome)
{
	std::ostringstream entry;
	entry.imbue(std::locale::classic());
	entry << std::fixed << std::setprecision(2) << "t=" << mfElapsedTime << "s: " << asOutcome;
	const tString outcome = entry.str().substr(0,512);
	if(mvModelActionHistory.size()>=8) mvModelActionHistory.erase(mvModelActionHistory.begin());
	mvModelActionHistory.push_back(outcome);
	UpdateModelContextSummary();
}

void cLuxEnemy_Llama::UpdateModelContextSummary()
{
	std::ostringstream summary;
	summary.imbue(std::locale::classic());
	summary << "Engine factual action history (newest last):\n";
	if(mvModelActionHistory.empty()) summary << "none\n";
	for(size_t i=0; i<mvModelActionHistory.size(); ++i) summary << mvModelActionHistory[i] << "\n";
	if(mModelObservation.mlFrameId)
	{
		size_t playerPixels=0, doorPixels=0;
		for(size_t i=0; i+2<mModelObservation.mvRGB.size(); i+=3)
		{
			const unsigned char* pixel=&mModelObservation.mvRGB[i];
			if(pixel[0]==255 && pixel[1]==0 && pixel[2]==255) ++playerPixels;
			if(pixel[0]==0 && pixel[1]==255 && pixel[2]==255) ++doorPixels;
		}
		summary << "Latest submitted observation=" << mModelObservation.mlFrameId
			<< ": visible player-mask pixels=" << playerPixels << ", breakable-door-mask pixels=" << doorPixels
			<< ", perceived sound events=" << mModelObservation.mvSounds.size() << ".\n";
	}
	summary << "Only measured action outcomes and submitted perception are retained; no hidden target positions.\n";
	msModelContextSummary = summary.str();
}

void cLuxEnemy_Llama::StopModelAction(const tString& asReason)
{
	EndModelAction("interrupted",asReason);
	ResetModelAction(false);
}

void cLuxEnemy_Llama::EndModelAction(const char* asState, const tString& asReason)
{
	if(mbModelActionTracked)
	{
		MeasureModelActionProgress();
		if(mTrackedModelAction==eLuxLlamaAction_Attack)
		{
			if(!mbModelAttackActive && mfModelAttackElapsed>=mfModelAttackDuration)
				msLastActionFeedback = "attack completed: " + msLastActionFeedback;
			else msLastActionFeedback = "attack " + tString(asState) + ": " + asReason + "; " + msLastActionFeedback;
		}
		else msLastActionFeedback = DescribeModelActionProgress(asState);
		if(!asReason.empty() && mTrackedModelAction!=eLuxLlamaAction_Attack)
			msLastActionFeedback += "; reason=" + asReason;
		// A frame rendered during this action cannot establish its final heading
		// or position. Make the next render due and plan only from that newer frame.
		mlModelActionEndFrame = mlObservationFrameId;
		mfLastObservationAttempt = cMath::Min(mfLastObservationAttempt,
			mfElapsedTime-mfObservationInterval-0.001f);
		RecordModelActionOutcome(msLastActionFeedback);
		mbModelActionTracked = false;
	}
}

void cLuxEnemy_Llama::RecordRejectedModelDecision(const tString& asReason)
{
	// Rejection is an unapplied decision, never an executed action. Retain the
	// latest measured action feedback for the next planning request.
	RecordModelActionOutcome("Decision not applied: " + asReason);
}

void cLuxEnemy_Llama::ResetModelAction(bool abClearCooldown)
{
	EndModelAction("interrupted","controller reset");
	mfModelActionRemaining = mfModelForward = 0;
	mModelAction = eLuxLlamaAction_Wait;
	const float fCooldown = mfModelAttackCooldown;
	ResetModelAttack();
	// An alive save can restore a mover override from an unfinished attack,
	// even though transient attack bookkeeping was intentionally not saved.
	if(mpMover && mfHealth>0) mpMover->UseMoveStateAnimations();
	if(!abClearCooldown) mfModelAttackCooldown = fCooldown;
	else
	{
		mModelBehavior = eLuxLlamaBehavior_Patrol;
		mlModelActionEndFrame = 0;
		msModelMemory.clear();
		mvModelActionHistory.clear();
		msModelContextSummary = "No engine action history";
		msLastActionFeedback = "No previous action";
		mModelObservation = cLuxLlamaObservation();
	}
	if(mpCharBody && mpMover && mpPathfinder) StopMotion();
}

void cLuxEnemy_Llama::ApplyModelDecision(const cLuxLlamaDecision& aDecision)
{
	if(!IsControllerOwner() || mbDebugOverride || !mbModelControlEnabled || IsModelAttacking()) return;
	if(IsModelActionActive()) StopModelAction("superseded by another decision");
	ResetModelAction(false);
	mModelBehavior = aDecision.mBehavior;
	mModelAction = aDecision.mAction;
	msModelMemory = aDecision.msMemory;
	mfModelActionRemaining = cMath::Clamp(aDecision.mfDuration,0.1f,mfActionMaxSeconds);
	mfModelForward = aDecision.mfForward;
	mfModelTurnGoal = mModelObservation.mfYaw-cMath::ToRad(aDecision.mfTurnDegrees);
	mvModelActionStart = mpCharBody->GetPosition();
	mvModelActionLastPosition = mvModelActionStart;
	mbModelActionTracked = true;
	mTrackedModelAction = aDecision.mAction;
	mfModelActionDuration = mfModelActionRemaining;
	mfModelActionElapsed = mfModelDistanceMoved = 0;
	mfModelActionStartYaw = mpCharBody->GetYaw();
	mfModelActionRequestedTurn = aDecision.mfTurnDegrees;
	SetMoveSpeed(aDecision.mbRun ? eLuxEnemyMoveSpeed_Run : eLuxEnemyMoveSpeed_Walk);
	mfModelActionSpeedLimit = mfModelForward<0 ? mfBackwardSpeed : mfForwardSpeed;
	if(!std::isfinite(mfModelActionSpeedLimit) || mfModelActionSpeedLimit<0) mfModelActionSpeedLimit = 0;
	msLastActionFeedback = "Executing " + tString(GetCurrentActionName());
	if(aDecision.mAction==eLuxLlamaAction_Attack)
	{
		mfModelActionRemaining = 0;
		tString error;
		if(!BeginModelAttack(aDecision.mTarget,aDecision.mlTargetX,aDecision.mlTargetY,error))
		{
			msLastActionFeedback = "Attack rejected: " + error;
			RecordModelActionOutcome(msLastActionFeedback);
			mbModelActionTracked = false;
			mfModelActionRemaining = 0;
			mModelAction = eLuxLlamaAction_Wait;
		}
		else mfModelActionDuration = mfModelAttackDuration;
		return;
	}
	if(aDecision.mAction==eLuxLlamaAction_Move || aDecision.mAction==eLuxLlamaAction_Turn)
		mpMover->TurnToAngle(mfModelTurnGoal);
}

void cLuxEnemy_Llama::UpdateEnemySpecific(float afTimeStep)
{
	if(!std::isfinite(afTimeStep) || afTimeStep<=0) return;
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
		ResetModelAction();
		return;
	}
	if(mbModelActionTracked)
	{
		MeasureModelActionProgress();
		mfModelActionElapsed = cMath::Min(mfModelActionDuration,mfModelActionElapsed+afTimeStep);
	}
	UpdateModelAttack(afTimeStep);
	if(mbModelAttackActive) return;
	if(mbModelActionTracked && mTrackedModelAction==eLuxLlamaAction_Attack)
	{
		EndModelAction("completed","");
	}
	if(!mbDebugOverride)
	{
		if(mfModelActionRemaining>0)
		{
			mfModelActionRemaining -= afTimeStep;
			if(mfModelActionRemaining>0)
			{
				if(mModelAction==eLuxLlamaAction_Move) mpCharBody->Move(eCharDir_Forward,mfModelForward);
				if(mModelAction==eLuxLlamaAction_Move || mModelAction==eLuxLlamaAction_Turn)
					mpMover->TurnToAngle(mfModelTurnGoal);
			}
			else
			{
				EndModelAction("completed","");
				ResetModelAction(false);
			}
		}
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
	if(gpBase->mpMapHandler->GetLlamaController()) gpBase->mpMapHandler->GetLlamaController()->OnOwnerUnavailable(this);
	ResetModelAction();
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
		if(gpBase->mpMapHandler->GetLlamaController()) gpBase->mpMapHandler->GetLlamaController()->OnOwnerUnavailable(this);
		ResetModelAction();
		// Inactive enemies stop receiving logic ticks, so retained evidence cannot age.
		mvSounds.clear();
	}
	if(mfHealth<=0) mpCharBody->SetActive(false);
}
void cLuxEnemy_Llama::OnControllerDisabled()
{
	if(gpBase->mpMapHandler->GetLlamaController()) gpBase->mpMapHandler->GetLlamaController()->OnOwnerUnavailable(this);
	ResetModelAction();
	// Disabled enemies also stop receiving the ticks used to expire live evidence.
	mvSounds.clear();
}
void cLuxEnemy_Llama::OnDisableTriggers() { mvSounds.clear(); }
void cLuxEnemy_Llama::OnResetProperties()
{
	ClearLegacyPerception();
	if(gpBase->mpMapHandler->GetLlamaController()) gpBase->mpMapHandler->GetLlamaController()->OnOwnerUnavailable(this);
	ResetModelAction();
	msModelMemory.clear();
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
	if(gpBase->mpMapHandler->GetLlamaController()) gpBase->mpMapHandler->GetLlamaController()->OnOwnerUnavailable(this);
	ResetModelAction();
	mbDebugOverride = false;
	msModelMemory.clear();
	mCurrentState=mNextState=mPreviousState=mfHealth>0 ? eLuxEnemyState_Idle : eLuxEnemyState_Dead;
	mvSounds.clear();
	if(mfHealth<=0) mpCharBody->SetActive(false);
}
void cLuxEnemy_Llama::SetupSaveData(iLuxEntity_SaveData* apSaveData)
{
	super_class::SetupSaveData(apSaveData);
	// Saved paths and unfinished actions cannot resume against a new observation.
	ResetModelAction();
}
