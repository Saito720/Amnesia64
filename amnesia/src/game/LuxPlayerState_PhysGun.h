#ifndef LUX_PLAYER_STATE_PHYSGUN_H
#define LUX_PLAYER_STATE_PHYSGUN_H

#include "LuxPlayerState_Interact.h"

class cLuxPlayer;
class cLuxInteractData_PhysGun;

class cLuxPlayerState_PhysGun;
class cLuxPhysGunRayCallback : public iPhysicsRayCallback
{
public:
	cLuxPhysGunRayCallback(cLuxPlayerState_PhysGun* apPhysGun) : mpPhysGun(apPhysGun) {}

	void Reset();

	bool BeforeIntersect(iPhysicsBody* pBody);
	bool OnIntersect(iPhysicsBody* pBody, cPhysicsRayParams* apParams);

	bool GetIntersected() const { return mbIntersected; }
	const cVector3f& GetPos() { return mvPos; }

	iPhysicsBody* GetBody() { return mpBody; }
	bool IsEnemy() const { return mbIsEnemy; }

private:
	bool mbIntersected;
	float mfClosestT;
	cVector3f mvPos;
	iPhysicsBody* mpBody;
	bool mbIsEnemy;
	
	cLuxPlayerState_PhysGun* mpPhysGun;
};

class cLuxPlayerState_PhysGun_SaveData : public iLuxPlayerState_SaveData {};

class cLuxPlayerState_PhysGun : public iLuxPlayerState_Interact
{
	typedef iLuxPlayerState_Interact super_class;
public:
	cLuxPlayerState_PhysGun(cLuxPlayer* apPlayer);
	virtual ~cLuxPlayerState_PhysGun();

	void OnEnterState(eLuxPlayerState aPrevState);
	void OnLeaveState(eLuxPlayerState aNewState);

	void OnGrab();
	void OnRelease();

	void Update(float afTimeStep);
	void PostUpdate(float afTimeStep);

	bool OnDoAction(eLuxPlayerAction aAction, bool abPressed);

	void OnScroll(float afAmount);

	bool OnAddYaw(float afAmount);
	bool OnAddPitch(float afAmount);

	void OnSaveBody(iPhysicsBody* apBody, float& afMass, bool& abCollideCharacter) {};

	float DrawDebug(cGuiSet* apSet, iFontData* apFont, float afStartY) { return afStartY; };

	void RenderSolid(cRendererCallbackFunctions* apFunctions);
	void DrawPhysBeam(cRendererCallbackFunctions* apFunctions, cVector3f avStart, cVector3f avEnd, float afStepSize = 0, float afCurvatureFactor = 0.5f, float afBeamWidth = 0.025f);
	void DrawDebugRay(cRendererCallbackFunctions* apFunctions, cVector3f avStart, cVector3f avEnd, float afStepSize = 0, float afCurvatureFactor = 0.5f);

	std::vector<cVector3f> QuadraticBezier(cVector3f avStart, cVector3f avEnd, float afStepSize, float afCurvatureFactor);
	cVector3f Interpolate(const cVector3f& avFrom, const cVector3f& avTo, float afPercent) { return avFrom + (avTo - avFrom) * afPercent; }

	void SetPhysGunEntity(cMeshEntity* apMeshEntity) { mpPhysgunEntity = apMeshEntity; }
	void SetPhysGunDebug(bool abDebug) { mbDebug = abDebug; }

	/////////////////////////////////
	// Save data stuff
	bool IsSaved() { return false; }
	iLuxPlayerState_SaveData* CreateSaveData() { return NULL; }

private:
	// Pointers
	cLuxPhysGunRayCallback* mpRayCallback;
	cMeshEntity* mpPhysgunEntity;
	iPhysicsBody* mpBody;
	iLuxEnemy* mpEnemy;
	cMaterial* mpMaterial;
	iVertexBuffer* mpVtxBuffer;

	// Interaction Specific
	bool mbInteracting;
	cVector3f mvGrabPos;
	cVector3f mvGrabLocalPos;
	cVector3f mvLocalBodyOffset;
	cMatrixf m_mtxBodyRotation;
	float mfGrabDepth;
	float mfEnemyYaw;
	float mfMaxForce;
	float mfMaxTorque;
	float mfMaxAngularSpeed;
	float mfUvOffset;

	// PID
	cPidController<cVector3f> mForcePid;
	cPidController<cVector3f> mSpeedTorquePid;

	// Crosshair
	cVector2f mvCrossSize;
	cVector3f mvCrossPos;

	// Debug
	bool mbDebug;
};

#endif // LUX_PLAYER_STATE_PHYSGUN_H
