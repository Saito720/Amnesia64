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

	bool GetIntersected() { return mbIntersected; }
	const cVector3f& GetPos() { return mvPos; }

	iPhysicsBody* GetBody() { return mpBody; }
	iLuxEnemy* GetEnemy() { return mpEnemy; }

	bool Grabbed() { return mbGrabbed; }
	bool IsEnemy() { return mbIsEnemy; }

private:
	cVector3f mvPos;
	bool mbIntersected;
	bool mbGrabbed;
	bool mbIsEnemy;
	float mfClosestT;
	iPhysicsBody* mpBody;
	iLuxEnemy* mpEnemy;
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

	void Update(float afTimeStep);
	void PostUpdate(float afTimeStep);

	bool OnDoAction(eLuxPlayerAction aAction, bool abPressed);

	void OnScroll(float afAmount);

	bool OnAddYaw(float afAmount);
	bool OnAddPitch(float afAmount);

	void OnSaveBody(iPhysicsBody* apBody, float& afMass, bool& abCollideCharacter) {};

	float DrawDebug(cGuiSet* apSet, iFontData* apFont, float afStartY) { return afStartY; };

	void RenderSolid(cRendererCallbackFunctions* apFunctions);
	void DrawDebugRay(cRendererCallbackFunctions* apFunctions, cVector3f avStart, cVector3f avEnd);

	void SetPhysGunEntity(cMeshEntity* apMeshEntity) { mpMeshEntity = apMeshEntity; }
	void SetPhysGunDebug(bool abDebug) { mbDebug = abDebug; }

	std::vector<cVector3f> QuadraticBezier(cVector3f avStart, cVector3f avEnd, float afStepSize, float afCurvatureFactor);

	cVector3f Interpolate(const cVector3f& avFrom, const cVector3f& avTo, float afPercent)
	{
		return avFrom + (avTo - avFrom) * afPercent;
	}

	/////////////////////////////////
	// Save data stuff
	bool IsSaved() { return false; }
	iLuxPlayerState_SaveData* CreateSaveData() { return NULL; }

private:
	cVector3f mvGrabPos;
	cVector3f mvGrabLocalPos;
	cVector3f mvLocalBodyOffset;
	bool mbInteracting;
	bool mbDebug;
	cCamera* mpCam;
	cMeshEntity* mpMeshEntity;
	iPhysicsBody* mpCurrentBody;
	iPhysicsWorld* mpPhysicsWorld;
	cLuxPhysGunRayCallback* mpRayCallback;
	cVector2f mvCrossSize;
	cVector3f mvCrossPos;

	float mfMaxForce;
	float mfMaxTorque;
	float mfMaxAngularSpeed;

	cMatrixf m_mtxBodyRotation;
	float mfEnemyYaw;

	cPidController<cVector3f> mForcePid;
	cPidController<cVector3f> mSpeedTorquePid;

	float mfGrabDepth;
};

#endif // LUX_PLAYER_STATE_PHYSGUN_H
