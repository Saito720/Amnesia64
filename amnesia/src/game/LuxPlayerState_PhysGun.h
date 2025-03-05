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
	const cVector3f& GetLocalPos() { return mvLocalPos; }

	iPhysicsBody* GetBody() { return mpBody; }

	bool GetValid() { return mbValid; }

private:
	cVector3f mvPos;
	cVector3f mvLocalPos;
	float mfClosestT;
	bool mbIntersected;
	bool mbValid;
	iPhysicsBody* mpBody;
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

	void OnInteraction();

	void Update(float afTimeStep);
	void PostUpdate(float afTimeStep);

	bool OnDoAction(eLuxPlayerAction aAction, bool abPressed);

	void OnScroll(float afAmount);

	bool OnAddYaw(float afAmount);
	bool OnAddPitch(float afAmount);

	void OnSaveBody(iPhysicsBody* apBody, float& afMass, bool& abCollideCharacter) {};

	float DrawDebug(cGuiSet* apSet, iFontData* apFont, float afStartY) { return afStartY; };

	void RenderSolid(cRendererCallbackFunctions* apFunctions);

	void SetPhysGunEntity(cMeshEntity* apMeshEntity) { mpMeshEntity = apMeshEntity; }
	void SetPhysGunDebug(bool abDebug) { mbDebug = abDebug; }

	cVector3f Interpolate(const cVector3f& avFrom, const cVector3f& avTo, float afPercent)
	{
		return avFrom + (avTo - avFrom) * afPercent;
	}

	float Distance(const cVector3f& avA, const cVector3f& avB)
	{
		float fDX = avB.x - avA.x;
		float fDY = avB.y - avA.y;
		float fDZ = avB.z - avA.z;
		return std::sqrt(fDX * fDX + fDY * fDY + fDZ * fDZ);
	}

	/////////////////////////////////
	// Save data stuff
	bool IsSaved() { return false; }
	iLuxPlayerState_SaveData* CreateSaveData() { return NULL; }

private:
	cVector3f mvLocalPos;
	cVector3f mvLocalBodyOffset;
	bool mbInteracting;
	bool mbDebug;
	cMeshEntity* mpMeshEntity;
	iPhysicsBody* mpCurrentBody;
	cLuxPhysGunRayCallback* mpRayCallback;
	cVector3f vEnd;
	cVector3f vPrevC = -1;

	// Crosshair values
	cVector2f mvCrossSize;
	cVector3f mvCrossPos;

	float mfMaxForce;
	float mfMaxTorque;
	float mfMaxAngularSpeed;

	cMatrixf m_mtxBodyRotation;

	cPidController<cVector3f> mForcePid;
	cPidController<cVector3f> mSpeedTorquePid;

	float mfDepth;
};

#endif // LUX_PLAYER_STATE_PHYSGUN_H
