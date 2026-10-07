/* Grounded mechanical actions for Enemy_Llama. GPL-3.0-or-later. */
#include "LuxEnemy_Llama.h"
#include "LuxEnemyMover.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxLlamaController.h"
#include "LuxPlayer.h"
#include "LuxProp_SwingDoor.h"

#include <cmath>

namespace {

// Newton does not report intersections in distance order. Keep the closest
// solid body, excluding only the observing character and an optional endpoint.
class cLlamaAttackRay : public iPhysicsRayCallback
{
public:
	cLlamaAttackRay(iPhysicsBody* apSelf, iPhysicsBody* apEndpoint = NULL)
		: mpSelf(apSelf), mpEndpoint(apEndpoint), mpBody(NULL), mfT(2), mvPoint(0) {}
	bool BeforeIntersect(iPhysicsBody* apBody)
	{
		return apBody != mpSelf && apBody != mpEndpoint && apBody->IsActive() && apBody->GetCollide();
	}
	bool OnIntersect(iPhysicsBody* apBody, cPhysicsRayParams* apParams)
	{
		if(apParams->mfT < mfT) { mfT = apParams->mfT; mpBody = apBody; mvPoint = apParams->mvPoint; }
		return true;
	}
	iPhysicsBody* mpSelf;
	iPhysicsBody* mpEndpoint;
	iPhysicsBody* mpBody;
	float mfT;
	cVector3f mvPoint;
};

cVector3f EyePosition(iCharacterBody* apBody, const cVector3f& avOffset)
{
	const cMatrixf yaw = cMath::MatrixRotateY(apBody->GetYaw());
	return apBody->GetPosition() + cVector3f(0,apBody->GetShape(0)->GetSize().y-apBody->GetSize().y*0.5f+avOffset.y,0)
		+ cMath::MatrixMul3x3(yaw,cVector3f(1,0,0))*avOffset.x
		+ cMath::MatrixMul3x3(yaw,cVector3f(0,0,-1))*avOffset.z;
}

bool Finite(const cVector3f& avPoint)
{
	return std::isfinite(avPoint.x) && std::isfinite(avPoint.y) && std::isfinite(avPoint.z);
}

bool HasMask(const cLuxLlamaObservation& aImage, int alX, int alY, eLuxLlamaTarget aTarget)
{
	const unsigned char r = aTarget == eLuxLlamaTarget_Player ? 255 : 0;
	const unsigned char g = aTarget == eLuxLlamaTarget_Player ? 0 : 255;
	// A tiny neighborhood tolerates integer coordinates rounding to a mask edge.
	for(int y=cMath::Max(0,alY-1); y<=cMath::Min(aImage.mvSize.y-1,alY+1); ++y)
	for(int x=cMath::Max(0,alX-1); x<=cMath::Min(aImage.mvSize.x-1,alX+1); ++x)
	{
		const size_t i = (size_t(y)*aImage.mvSize.x+x)*3;
		if(aImage.mvRGB[i]==r && aImage.mvRGB[i+1]==g && aImage.mvRGB[i+2]==255) return true;
	}
	return false;
}

iPhysicsBody* ResolveTarget(cLuxMap* apMap, eLuxLlamaTarget aTarget, int alEntityID, int alBodyIndex)
{
	if(aTarget==eLuxLlamaTarget_Player)
		return gpBase->mpPlayer->GetHealth()>0 && gpBase->mpPlayer->GetCharacterBody()
			? gpBase->mpPlayer->GetCharacterBody()->GetCurrentBody() : NULL;
	iLuxEntity* pEntity = apMap->GetEntityByID(alEntityID,eLuxEntityType_Prop);
	if(!pEntity || !pEntity->IsActive() || pEntity->GetDestroyMe()) return NULL;
	iLuxProp* pProp = static_cast<iLuxProp*>(pEntity);
	if(alBodyIndex<0 || alBodyIndex>=pProp->GetBodyNum()) return NULL;
	iPhysicsBody* pBody = pProp->GetBody(alBodyIndex);
	if(aTarget==eLuxLlamaTarget_Door)
	{
		if(pProp->GetPropType()!=eLuxPropType_SwingDoor || !static_cast<cLuxProp_SwingDoor*>(pProp)->CanBeBroken()) return NULL;
	}
	else if(aTarget==eLuxLlamaTarget_Obstacle)
	{
		// Doors require their explicit breakable contract; an obstacle request
		// cannot bypass it. Only registered dynamic props can be pushed/damaged.
		if(pProp->GetPropType()==eLuxPropType_SwingDoor || !pBody || !(pBody->GetMass()>0)) return NULL;
	}
	else return NULL;
	return pBody;
}

cMatrixf AttackMatrix(iCharacterBody* apBody, const cVector3f& avOffset)
{
	cMatrixf matrix = cMath::MatrixRotateY(apBody->GetYaw());
	matrix.SetTranslation(apBody->GetPosition()+cMath::MatrixMul3x3(matrix,cVector3f(avOffset.x,avOffset.y,-avOffset.z)));
	return matrix;
}

}

bool cLuxEnemy_Llama::ValidateModelAttackTarget(eLuxLlamaTarget aTarget, int alEntityID, int alBodyIndex,
	const cVector3f& avLocalPoint, tString& asError)
{
	iPhysicsBody* pBody = ResolveTarget(mpMap,aTarget,alEntityID,alBodyIndex);
	if(!pBody || !pBody->IsActive() || !pBody->GetCollide()) { asError="Target is no longer attackable"; return false; }
	if(mNormalAttackSize.mlShapeIdx<0 || size_t(mNormalAttackSize.mlShapeIdx)>=mvAttackShapes.size() ||
		!mvAttackShapes[mNormalAttackSize.mlShapeIdx]) { asError="Enemy has no configured melee shape"; return false; }
	const cVector3f point = cMath::MatrixMul(pBody->GetLocalMatrix(),avLocalPoint);
	if(!Finite(point)) { asError="Invalid attack geometry"; return false; }
	const cVector3f relative = cMath::MatrixMul3x3(cMath::MatrixRotateY(-mpCharBody->GetYaw()),point-mpCharBody->GetPosition());
	const float distance = std::sqrt(relative.x*relative.x+relative.z*relative.z);
	const float halfArc = cMath::Min(mfObservationFOV*0.5f,cMath::ToRad(60.0f));
	if(!std::isfinite(mfNormalAttackDistance) || !(mfNormalAttackDistance>0) || distance>mfNormalAttackDistance ||
		std::fabs(std::atan2(relative.x,-relative.z))>halfArc)
	{ asError="Target is outside melee reach or facing arc"; return false; }
	cCollideData collision;
	collision.SetMaxSize(4);
	if(!mpMap->GetPhysicsWorld()->CheckShapeCollision(mvAttackShapes[mNormalAttackSize.mlShapeIdx],
		AttackMatrix(mpCharBody,mNormalAttackSize.mvOffset),pBody->GetShape(),pBody->GetLocalMatrix(),collision,4,false))
	{ asError="Target does not overlap the configured melee volume"; return false; }
	// The current eye must still see the grounded surface. Match the shared
	// attack's center-to-body-center obstruction check as well, before swinging.
	cLlamaAttackRay eyeRay(mpCharBody->GetCurrentBody(),pBody);
	mpMap->GetPhysicsWorld()->CastRay(&eyeRay,EyePosition(mpCharBody,mvCameraOffset),point,false,false,true,true);
	cLlamaAttackRay centerRay(mpCharBody->GetCurrentBody(),pBody);
	mpMap->GetPhysicsWorld()->CastRay(&centerRay,mpCharBody->GetPosition(),pBody->GetWorldPosition(),false,false,true,true);
	if(eyeRay.mpBody || centerRay.mpBody) { asError="Target is obstructed"; return false; }
	return true;
}

bool cLuxEnemy_Llama::BeginModelAttack(eLuxLlamaTarget aTarget, int alX, int alY, tString& asError)
{
	asError.clear();
	if(!IsControllerOwner()) { asError="Enemy does not own the controller"; return false; }
	if(mbModelAttackActive || mfModelAttackCooldown>0) { asError="Attack is active or cooling down"; return false; }
	if(aTarget!=eLuxLlamaTarget_Player && aTarget!=eLuxLlamaTarget_Door && aTarget!=eLuxLlamaTarget_Obstacle)
	{ asError="Attack requires a visible target category"; return false; }
	const cLuxLlamaObservation& image = mModelObservation;
	const float maxAge=gpBase->mpMapHandler->GetLlamaController()->GetMaxResultAge();
	if(alX<0 || alX>1000 || alY<0 || alY>1000 || image.mlFrameId==0 || image.mvSize.x<=0 || image.mvSize.y<=0 ||
		image.mvRGB.size()!=size_t(image.mvSize.x)*image.mvSize.y*3 || !Finite(image.mvPosition) ||
		!std::isfinite(image.mfTime) || !std::isfinite(image.mfYaw) || mfElapsedTime-image.mfTime>maxAge || image.mfTime>mfElapsedTime ||
		cMath::Vector3DistSqr(EyePosition(mpCharBody,mvCameraOffset),image.mvPosition)>4 ||
		std::fabs(std::atan2(std::sin(mpCharBody->GetYaw()-image.mfYaw),std::cos(mpCharBody->GetYaw()-image.mfYaw)))>cMath::ToRad(45.0f))
	{ asError="Attack observation is missing, invalid or stale"; return false; }
	const int x = cMath::Min(image.mvSize.x-1,alX*image.mvSize.x/1000);
	const int y = cMath::Min(image.mvSize.y-1,alY*image.mvSize.y/1000);
	if(aTarget!=eLuxLlamaTarget_Obstacle && !HasMask(image,x,y,aTarget))
	{ asError="Requested point has no visible target mask"; return false; }
	if(aTarget==eLuxLlamaTarget_Obstacle)
	{
		const size_t i=(size_t(y)*image.mvSize.x+x)*3;
		if(image.mvRGB[i]==0 && image.mvRGB[i+1]==0 && image.mvRGB[i+2]==0)
		{ asError="Requested point is observation background"; return false; }
	}
	// Pixel centers use the same top-down convention as the submitted RGB.
	const cVector3f clip(2.0f*(x+0.5f)/image.mvSize.x-1.0f,1.0f-2.0f*(y+0.5f)/image.mvSize.y,-1);
	cVector3f direction = cMath::MatrixMulDivideW(cMath::MatrixInverse(image.mProjection),clip);
	direction = cMath::MatrixMul3x3(cMath::MatrixRotateY(image.mfYaw),direction);
	if(!Finite(direction) || direction.SqrLength()<0.000001f) { asError="Invalid observation projection"; return false; }
	direction.Normalize();
	cLlamaAttackRay ray(mpCharBody->GetCurrentBody());
	mpMap->GetPhysicsWorld()->CastRay(&ray,image.mvPosition,image.mvPosition+direction*100.0f,false,false,true,true);
	if(!ray.mpBody) { asError="Requested point has no physical target"; return false; }
	int entityID=-1, bodyIndex=-1;
	if(aTarget==eLuxLlamaTarget_Player)
	{
		if(ray.mpBody!=gpBase->mpPlayer->GetCharacterBody()->GetCurrentBody())
		{ asError="Requested player is physically obstructed"; return false; }
	}
	else
	{
		cLuxEntityIterator it=mpMap->GetEntityIterator();
		while(it.HasNext() && entityID<0)
		{
			iLuxEntity* pEntity=it.Next();
			if(pEntity->GetEntityType()!=eLuxEntityType_Prop) continue;
			iLuxProp* pProp=static_cast<iLuxProp*>(pEntity);
			for(int i=0; i<pProp->GetBodyNum(); ++i)
				if(pProp->GetBody(i)==ray.mpBody) { entityID=pProp->GetID(); bodyIndex=i; break; }
		}
		if(entityID<0 || ResolveTarget(mpMap,aTarget,entityID,bodyIndex)!=ray.mpBody)
		{ asError="Requested point is not an eligible door or dynamic obstacle"; return false; }
	}
	const cVector3f localPoint=cMath::MatrixMul(cMath::MatrixInverse(ray.mpBody->GetLocalMatrix()),ray.mvPoint);
	if(!ValidateModelAttackTarget(aTarget,entityID,bodyIndex,localPoint,asError)) return false;
	StopMotion();
	mModelAttackTarget=aTarget;
	mlModelAttackEntityID=entityID;
	mlModelAttackBodyID=bodyIndex;
	mvModelAttackPoint=localPoint;
	mfModelAttackElapsed=0;
	mfModelAttackImpactDelay=0.35f;
	mfModelAttackDuration=0.8f;
	mfModelAttackCooldown=mfAttackCooldown;
	mbModelAttackHit=false;
	mbModelAttackActive=true;
	const tString& configured=aTarget==eLuxLlamaTarget_Door ? msDoorAttackAnimation : msAttackAnimation;
	msModelAttackAnimation=configured;
	cAnimationState* pClip=msModelAttackAnimation.empty() ? NULL : mpMeshEntity->GetAnimationStateFromName(msModelAttackAnimation);
	if(!pClip)
	{
		msModelAttackAnimation=aTarget==eLuxLlamaTarget_Door ? "BreakDoor" : "SwingClaws01";
		pClip=mpMeshEntity->GetAnimationStateFromName(msModelAttackAnimation);
		if(!pClip && aTarget!=eLuxLlamaTarget_Door)
		{
			msModelAttackAnimation="SwingClaws02";
			pClip=mpMeshEntity->GetAnimationStateFromName(msModelAttackAnimation);
		}
	}
	if(pClip && mbUseAnimations && std::isfinite(pClip->GetBaseSpeed()) && pClip->GetBaseSpeed()>0 &&
		std::isfinite(pClip->GetLength()) && pClip->GetLength()>0)
	{
		mfModelAttackDuration=cMath::Clamp(pClip->GetLength()/pClip->GetBaseSpeed(),0.1f,4.0f);
		const float special=pClip->GetSpecialEventTime();
		if(std::isfinite(special) && special>0 && special<pClip->GetLength())
			mfModelAttackImpactDelay=cMath::Clamp(special/pClip->GetBaseSpeed(),0.05f,mfModelAttackDuration);
		else mfModelAttackImpactDelay=cMath::Min(0.35f,mfModelAttackDuration*0.5f);
		// PlayAnim's same-clip shortcut must not preserve an old loop/time.
		pClip->SetActive(false);
		pClip->SetSpeed(1);
		PlayAnim(msModelAttackAnimation,false,0.15f,false,1,false,true,false);
	}
	else msModelAttackAnimation.clear();
	mpMover->SetOverideMoveState(true);
	return true;
}

void cLuxEnemy_Llama::UpdateModelAttack(float afTimeStep)
{
	if(!std::isfinite(afTimeStep) || afTimeStep<=0) return;
	if(!IsControllerOwner()) { ResetModelAttack(); return; }
	mfModelAttackCooldown=cMath::Max(0.0f,mfModelAttackCooldown-afTimeStep);
	if(!mbModelAttackActive) return;
	mfModelAttackElapsed+=afTimeStep;
	if(!mbModelAttackHit && mfModelAttackElapsed>=mfModelAttackImpactDelay)
	{
		mbModelAttackHit=true; // A failed revalidation also consumes the swing.
		tString error;
		if(ValidateModelAttackTarget(mModelAttackTarget,mlModelAttackEntityID,mlModelAttackBodyID,mvModelAttackPoint,error))
		{
			cEnemyAttackDamageData damage=mModelAttackTarget==eLuxLlamaTarget_Door ? mBreakDoorAttackDamage : mNormalAttackDamage;
			damage.mbCheckPlayer=mModelAttackTarget==eLuxLlamaTarget_Player;
			damage.mbCheckProps=mModelAttackTarget!=eLuxLlamaTarget_Player;
			cEnemyAttackSizeData size=mNormalAttackSize;
			// Shared Attack translates in the character's cached physics basis.
			// Express the fresh yaw offset in that basis, including pre-tick turns.
			const cVector3f offset=AttackMatrix(mpCharBody,size.mvOffset).GetTranslation()-mpCharBody->GetPosition();
			size.mvOffset=cVector3f(cMath::Vector3Dot(offset,mpCharBody->GetRight()),
				cMath::Vector3Dot(offset,mpCharBody->GetUp()),cMath::Vector3Dot(offset,mpCharBody->GetForward()));
			const int previousHits=mlAttackHitCounter;
			Attack(size,damage);
			msLastActionFeedback=mlAttackHitCounter>previousHits ? "Attack hit the current melee volume" : "Attack missed the current melee volume";
		}
		else msLastActionFeedback="Attack missed: "+error;
	}
	if(mfModelAttackElapsed>=mfModelAttackDuration)
	{
		mbModelAttackActive=false;
		mModelAction=eLuxLlamaAction_Wait;
		mpMover->UseMoveStateAnimations();
		msModelAttackAnimation.clear();
	}
}

void cLuxEnemy_Llama::ResetModelAttack()
{
	if(mbModelAttackActive && mpMover && mfHealth>0) mpMover->UseMoveStateAnimations();
	mbModelAttackActive=false;
	mbModelAttackHit=false;
	mModelAttackTarget=eLuxLlamaTarget_None;
	mlModelAttackEntityID=mlModelAttackBodyID=-1;
	mvModelAttackPoint=0;
	mfModelAttackElapsed=mfModelAttackCooldown=0;
	msModelAttackAnimation.clear();
}
