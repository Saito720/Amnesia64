/* Enemy_Llama observation/controller foundation. GPL-3.0-or-later. */
#ifndef LUX_ENEMY_LLAMA_H
#define LUX_ENEMY_LLAMA_H

#include "LuxEnemy.h"

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

class cLuxEnemy_Llama_SaveData : public iLuxEnemy_SaveData
{
	kSerializableClassInit(cLuxEnemy_Llama_SaveData)
};

class cLuxEnemy_Llama : public iLuxEnemy
{
	friend class cLuxEnemyLoader_Llama;
	typedef iLuxEnemy super_class;
public:
	cLuxEnemy_Llama(const tString& asName, int alID, cLuxMap* apMap);
	~cLuxEnemy_Llama();
	static cLuxEnemy_Llama* GetControllerOwner(cLuxMap* apMap);
	bool IsControllerOwner() const;

	void SetObservationEnabled(bool abEnabled);
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
	void ClearLegacyPerception();
	void SelectMovementAnimations();
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
