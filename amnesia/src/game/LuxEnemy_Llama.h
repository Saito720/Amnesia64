/* Enemy_Llama observation/controller foundation. GPL-3.0-or-later. */
#ifndef LUX_ENEMY_LLAMA_H
#define LUX_ENEMY_LLAMA_H

#include "LuxEnemy.h"
#include "LuxLlamaDecision.h"

namespace hpl { class cSceneObservation; }

// Heard evidence is deliberately approximate and relative to the listener when heard.
struct cLuxLlamaSound
{
	float mfAge;
	float mfBearing; // Radians; positive is to the right.
	float mfDistance;
	float mfVolume;
	float mfListenerYaw; // Heading when heard; preserves meaning after the enemy turns.
};

// An immutable copy of the frame submitted to inference. New preview captures
// cannot change the camera, mask pixels or sounds used to ground its reply.
struct cLuxLlamaObservation
{
	cLuxLlamaObservation() : mlFrameId(0), mfTime(0), mvSize(0), mvPosition(0), mfYaw(0), mProjection(cMatrixf::Identity) {}
	unsigned int mlFrameId;
	float mfTime;
	cVector2l mvSize;
	cVector3f mvPosition;
	float mfYaw;
	cMatrixf mProjection;
	std::vector<unsigned char> mvRGB;
	std::vector<cLuxLlamaSound> mvSounds;
};

class cLuxEnemy_Llama_SaveData : public iLuxEnemy_SaveData
{
	kSerializableClassInit(cLuxEnemy_Llama_SaveData)
};

class cLuxEnemy_Llama : public iLuxEnemy
{
	friend class cLuxEnemyLoader_Llama;
	friend class cLuxLlamaController;
	typedef iLuxEnemy super_class;
public:
	cLuxEnemy_Llama(const tString& asName, int alID, cLuxMap* apMap);
	~cLuxEnemy_Llama();
	static cLuxEnemy_Llama* GetControllerOwner(cLuxMap* apMap);
	bool IsControllerOwner() const;

	void SetObservationEnabled(bool abEnabled);
	bool IsObservationEnabled() const { return mbObservationEnabled; }
	bool IsAutonomousControlEnabled() const { return mbModelControlEnabled; }
	void SetDebugOverride(bool abOverride);
	bool IsDebugOverride() const { return mbDebugOverride; }
	const tString& GetControllerStatus() const { return msModelStatus; }
	const char* GetBehaviorName() const { return GetLuxLlamaBehaviorName(mModelBehavior); }
	const char* GetCurrentActionName() const { return GetLuxLlamaActionName(mModelAction); }
	const tString& GetLastModelReply() const { return msLastModelReply; }
	const tString& GetLastActionFeedback() const { return msLastActionFeedback; }
	bool IsModelActionActive() const { return mbModelActionTracked || mbModelAttackActive; }
	void SnapshotModelActionFeedback();
	const tString& GetModelContextSummary() const { return msModelContextSummary; }
	void StopModelAction(const tString& asReason);
	void RecordRejectedModelDecision(const tString& asReason);
	bool SnapshotModelObservation();
	void ApplyModelDecision(const cLuxLlamaDecision& aDecision);
	void ResetModelAction(bool abClearCooldown = true);
	bool BeginModelAttack(eLuxLlamaTarget aTarget, int alX, int alY, tString& asError);
	void UpdateModelAttack(float afTimeStep);
	void ResetModelAttack();
	bool IsModelAttacking() const { return mbModelAttackActive; }
	// Called before Scene::Render, never from within a main renderer callback.
	void CaptureObservation();
	iTexture* GetObservationTexture();
	iTexture* GetObservationDepthTexture();
	const std::vector<unsigned char>& GetObservationRGB();
	unsigned int GetObservationFrameId() const { return mlObservationFrameId; }
	float GetObservationTime() const { return mfObservationTime; }
	cVector2l GetObservationSize() const { return mvObservationSize; }
	float GetObservationFOV() const { return mfObservationFOV; } // Horizontal radians.
	cVector3f GetObservationPosition() const { return mvObservationPosition; }
	float GetObservationYaw() const { return mfObservationYaw; }
	const tString& GetObservationStatus() const { return msObservationStatus; }
	const tString& GetRigProfile() const { return msRigProfile; }
	const std::vector<cLuxLlamaSound>& GetPerceivedSounds() const { return mvObservationSounds; }
	const cMatrixf& GetObservationProjection() const { return mObservationProjection; }
	void SetDebugInput(float afForward, float afTurn);

	iLuxEntity_SaveData* CreateSaveData();
	void LoadFromSaveData(iLuxEntity_SaveData* apSaveData);
	void SetupSaveData(iLuxEntity_SaveData* apSaveData);

protected:
	bool UsesLegacyAI() const { return false; }
	bool StateEventImplement(int, eLuxEnemyStateEvent, cLuxStateMessage*) { return false; }
	bool PlayerIsDetected() { return false; }
	float GetDamageMul(float, int) { return 1.0f; }
	void OnSetupAfterLoad(cWorld* apWorld);
	void OnAfterWorldLoad() {}
	void UpdateEnemySpecific(float afTimeStep);
	void OnRenderSolidImplemented(cRendererCallbackFunctions*) {}
	void OnControllerMessage(eLuxEnemyMessage aType, const cVector3f& avValue, float afValue);
	void OnControllerDeath();
	void OnControllerDisabled();
	void OnSetActiveEnemySpecific(bool abActive);
	void OnDisableTriggers();
	void OnResetProperties();

private:
	void StopMotion();
	void RecordModelActionOutcome(const tString& asOutcome);
	void EndModelAction(const char* asState, const tString& asReason);
	void UpdateModelContextSummary();
	tString DescribeModelActionProgress(const char* asState) const;
	void MeasureModelActionProgress();
	void ClearLegacyPerception();
	void SelectMovementAnimations();
	bool ValidateModelAttackTarget(eLuxLlamaTarget aTarget, int alEntityID, int alBodyID, const cVector3f& avPoint, tString& asError);
	tString SelectAnimation(const tString& asConfigured, const char* asStandard, const char* asBiped, const tString& asFallback);

	cCamera* mpObservationCamera;
	cSceneObservation* mpObservation;
	cVector2l mvObservationSize;
	float mfObservationFOV;
	cVector3f mvCameraOffset;
	float mfObservationInterval;
	float mfHearingRange;
	float mfSoundThreshold;
	tString msAnimationNames[4]; // Idle, Walk, Run, Backward.
	tString msDeathAnimation;
	bool mbObservationEnabled;
	float mfElapsedTime;
	float mfObservationTime;
	float mfLastObservationAttempt;
	unsigned int mlObservationFrameId;
	unsigned int mlModelActionEndFrame;
	cVector3f mvObservationPosition;
	float mfObservationYaw;
	cMatrixf mObservationProjection;
	tString msObservationStatus;
	tString msRigProfile;
	float mfDebugForward;
	float mfDebugTurn;
	float mfDebugInputTimeout;
	std::vector<cLuxLlamaSound> mvSounds;
	std::vector<cLuxLlamaSound> mvObservationSounds;
	bool mbModelControlEnabled;
	bool mbDebugOverride;
	float mfDecisionInterval;
	float mfActionMaxSeconds;
	float mfAttackCooldown;
	tString msAttackAnimation;
	tString msDoorAttackAnimation;
	eLuxLlamaBehavior mModelBehavior;
	eLuxLlamaAction mModelAction;
	float mfModelActionRemaining;
	float mfModelForward;
	float mfModelTurnGoal;
	cVector3f mvModelActionStart;
	bool mbModelActionTracked;
	float mfModelActionDuration;
	float mfModelActionElapsed;
	float mfModelActionStartYaw;
	float mfModelActionRequestedTurn;
	float mfModelDistanceMoved;
	float mfModelActionSpeedLimit;
	cVector3f mvModelActionLastPosition;
	eLuxLlamaAction mTrackedModelAction;
	std::vector<tString> mvModelActionHistory;
	tString msModelContextSummary;
	tString msModelStatus;
	tString msLastModelReply;
	tString msLastActionFeedback;
	tString msModelMemory;
	cLuxLlamaObservation mModelObservation;
	bool mbModelAttackActive;
	bool mbModelAttackHit;
	eLuxLlamaTarget mModelAttackTarget;
	int mlModelAttackEntityID;
	int mlModelAttackBodyID; // Index within the target prop, resolved again at impact.
	cVector3f mvModelAttackPoint; // Body-local anchor that follows the target's motion.
	float mfModelAttackElapsed;
	float mfModelAttackImpactDelay;
	float mfModelAttackDuration;
	float mfModelAttackCooldown;
	tString msModelAttackAnimation;
};

class cLuxEnemyLoader_Llama : public iLuxEnemyLoader
{
public:
	cLuxEnemyLoader_Llama(const tString& asName) : iLuxEnemyLoader(asName) {}
	iLuxEnemy* CreateEnemy(const tString& asName, int alID, cLuxMap* apMap);
	void AfterLoad(cXmlElement* apRootElem, const cMatrixf& a_mtxTransform, cWorld* apWorld, cResourceVarsObject* apInstanceVars);
	void LoadVariables(iLuxEnemy* apEnemy, cXmlElement* apRootElem);
	void LoadInstanceVariables(iLuxEnemy* apEnemy, cResourceVarsObject* apInstanceVars);
private:
	tString msResolvedProfile;
};

#endif
