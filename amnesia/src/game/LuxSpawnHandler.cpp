#include "LuxSpawnHandler.h"

#include "LuxEntity.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxProp_Object.h"
#include "resources/EntFileManager.h"

#include <cmath>

namespace
{
	// Garry's Mod sandbox/commands.lua uses an eye trace of 2048 Source units.
	// HPL's world uses metres; convert the conventional inch-sized Source unit.
	const float kMaxSpawnDistance = 2048.0f * 0.0254f;
	const float kSpawnClearance = 0.02f;

	class cSpawnRay : public iPhysicsRayCallback
	{
	public:
		cSpawnRay(iPhysicsBody *apPlayerBody, const cVector3f& avEnd)
			: mpPlayerBody(apPlayerBody), mbHit(false), mfDistance(kMaxSpawnDistance + 1), mvPoint(avEnd), mvNormal(0) {}

		bool BeforeIntersect(iPhysicsBody *apBody)
		{
			return apBody != mpPlayerBody && apBody->IsActive() && apBody->GetCollide();
		}

		bool OnIntersect(iPhysicsBody *apBody, cPhysicsRayParams *apParams)
		{
			if(BeforeIntersect(apBody) && apParams->mfDist < mfDistance)
			{
				mbHit = true;
				mfDistance = apParams->mfDist;
				mvPoint = apParams->mvPoint;
				mvNormal = apParams->mvNormal;
			}
			return true; // The physics backend does not promise nearest-first hits.
		}

		iPhysicsBody *mpPlayerBody;
		bool mbHit;
		float mfDistance;
		cVector3f mvPoint;
		cVector3f mvNormal;
	};

	// Ordinary StaticProp loads have no Lux owner and cannot be individually
	// removed. Give sandbox statics the existing prop ownership/cleanup path,
	// retaining static interaction and physics, and a mutable render container.
	class cSpawnStaticLoader : public cLuxPropLoader_Object
	{
	public:
		cSpawnStaticLoader() : cLuxPropLoader_Object("SpawnStaticProp") {}
		void LoadVariables(iLuxProp *apProp, cXmlElement *apRootElem)
		{
			msEntitySubType = "Static";
			cLuxPropLoader_Object::LoadVariables(apProp, apRootElem);
		}
	};

	void ExpandBounds(cVector3f& avMin, cVector3f& avMax, const cVector3f& avPoint)
	{
		for(int i=0; i<3; ++i)
		{
			avMin.v[i] = cMath::Min(avMin.v[i], avPoint.v[i]);
			avMax.v[i] = cMath::Max(avMax.v[i], avPoint.v[i]);
		}
	}

	bool GetSpawnBounds(cWorld *apWorld, cEntFile *apFile, cVector3f& avMin, cVector3f& avMax)
	{
		cXmlElement *pModel = apFile->GetXmlDoc()->GetFirstElement("ModelData");
		cXmlElement *pMeshElement = pModel ? pModel->GetFirstElement("Mesh") : NULL;
		if(pMeshElement == NULL) return false;

		tString sMeshFile = pMeshElement->GetAttributeString("Filename");
		if(cString::GetFilePath(sMeshFile).empty())
			sMeshFile = cString::SetFilePath(sMeshFile, cString::To8Char(cString::GetFilePathW(apFile->GetFullPath())));
		cMesh *pMesh = apWorld->GetResources()->GetMeshManager()->CreateMesh(sMeshFile);
		if(pMesh == NULL) return false;
		if(pMesh->GetSubMeshNum() == 0)
		{
			apWorld->GetResources()->GetMeshManager()->Destroy(pMesh);
			return false;
		}

		// Measure before creating gameplay bodies: moving an already-created door
		// or enemy would leave world-anchored joints and AI start positions behind.
		cMeshEntity *pPreview = apWorld->CreateMeshEntity("__spawn_bounds", pMesh, false);
		pPreview->SetVisible(false);
		if(pMesh->GetSkeleton() == NULL)
		{
			cXmlNodeListIterator it = pMeshElement->GetChildIterator();
			while(it.HasNext())
			{
				cXmlElement *pSub = it.Next()->ToElement();
				if(pSub == NULL) continue;
				cSubMeshEntity *pEntity = pPreview->GetSubMeshEntityName(pSub->GetAttributeString("Name"));
				if(pEntity == NULL) continue;
				cMatrixf mtx = cMath::MatrixMul(cMath::MatrixRotate(pSub->GetAttributeVector3f("Rotation"), eEulerRotationOrder_XYZ),
					cMath::MatrixScale(pSub->GetAttributeVector3f("Scale", 1)));
				mtx.SetTranslation(pSub->GetAttributeVector3f("WorldPos"));
				pEntity->SetWorldMatrix(mtx);
			}
		}
		pPreview->UpdateLogic(0);
		avMin = pPreview->GetBoundingVolume()->GetMin();
		avMax = pPreview->GetBoundingVolume()->GetMax();
		apWorld->DestroyMeshEntity(pPreview);

		// Authored collision shapes can extend beyond the visible model.
		cXmlElement *pShapes = pModel->GetFirstElement("Shapes");
		if(pShapes)
		{
			cXmlNodeListIterator it = pShapes->GetChildIterator();
			while(it.HasNext())
			{
				cXmlElement *pShape = it.Next()->ToElement();
				if(pShape == NULL) continue;
				cVector3f vHalf = pShape->GetAttributeVector3f("Scale", 1) * 0.5f;
				cMatrixf mtx = cMath::MatrixRotate(pShape->GetAttributeVector3f("Rotation"), eEulerRotationOrder_XYZ);
				mtx.SetTranslation(pShape->GetAttributeVector3f("WorldPos"));
				for(int n=0; n<8; ++n)
					ExpandBounds(avMin, avMax, cMath::MatrixMul(mtx, cVector3f((n&1)?vHalf.x:-vHalf.x, (n&2)?vHalf.y:-vHalf.y, (n&4)?vHalf.z:-vHalf.z)));
			}
		}
		return true;
	}

	bool FindSpawnPosition(iPhysicsWorld *apPhysics, iPhysicsBody *apPlayerBody,
		const cVector3f& avEye, const cVector3f& avForward, const cMatrixf& aRotation,
		const cVector3f& avLocalMin, const cVector3f& avLocalMax, cVector3f& avPosition)
	{
		for(int axis=0; axis<3; ++axis)
		{
			if(std::isfinite(avLocalMin.v[axis]) == false || std::isfinite(avLocalMax.v[axis]) == false ||
				avLocalMin.v[axis] > avLocalMax.v[axis] ||
				std::isfinite(avEye.v[axis]) == false || std::isfinite(avForward.v[axis]) == false)
				return false;
		}
		if((avLocalMax - avLocalMin).Length() > 100000.0f) return false;
		const cVector3f vEnd = avEye + avForward * kMaxSpawnDistance;
		cSpawnRay trace(apPlayerBody, vEnd);
		apPhysics->CastRay(&trace, avEye, vEnd, true, true, true, true);
		avPosition = trace.mvPoint;

		cVector3f vMin(1e20f), vMax(-1e20f);
		for(int n=0; n<8; ++n)
			ExpandBounds(vMin, vMax, cMath::MatrixMul3x3(aRotation, cVector3f((n&1)?avLocalMax.x:avLocalMin.x,
				(n&2)?avLocalMax.y:avLocalMin.y, (n&4)?avLocalMax.z:avLocalMin.z)));

		// Move the supporting side of the bounds flush to the hit plane. This
		// handles floor, wall and ceiling hits, including off-centre model origins.
		if(trace.mbHit)
		{
			const cVector3f vSupport(trace.mvNormal.x >= 0 ? vMin.x : vMax.x,
				trace.mvNormal.y >= 0 ? vMin.y : vMax.y, trace.mvNormal.z >= 0 ? vMin.z : vMax.z);
			avPosition += trace.mvNormal * (kSpawnClearance - cMath::Vector3Dot(trace.mvNormal, vSupport));
		}

		// GMod's correction traces in both directions along each bounds axis.
		const cVector3f vCenter = (vMin + vMax) * 0.5f;
		for(int axis=0; axis<3; ++axis)
		{
			const cVector3f vStart = avPosition + vCenter;
			cVector3f vLow = vStart, vHigh = vStart;
			// Keep the end away from the exact surface established above: Newton
			// has an assertion for a box ray whose first hit is precisely t == 1.
			vLow.v[axis] = avPosition.v[axis] + vMin.v[axis] - kSpawnClearance * 0.5f;
			vHigh.v[axis] = avPosition.v[axis] + vMax.v[axis] + kSpawnClearance * 0.5f;
			cSpawnRay low(apPlayerBody, vLow), high(apPlayerBody, vHigh);
			apPhysics->CastRay(&low, vStart, vLow, true, true, true, true);
			apPhysics->CastRay(&high, vStart, vHigh, true, true, true, true);
			if(low.mbHit && high.mbHit) return false; // Does not fit between these surfaces.
			if(low.mbHit) avPosition += low.mvPoint - vLow;
			if(high.mbHit) avPosition += high.mvPoint - vHigh;
		}

		// Validate the full bounds as well as the centre rays. A conservative hull
		// prevents corners intersecting geometry or spawning around the player.
		cVector3f vSize = vMax - vMin;
		for(int axis=0; axis<3; ++axis) vSize.v[axis] = cMath::Max(vSize.v[axis], kSpawnClearance);
		iCollideShape *pHull = apPhysics->CreateBoxShape(vSize, NULL);
		bool bFits = false;
		const cVector3f vInitialPosition = avPosition;
		for(int attempt=0; attempt<5; ++attempt)
		{
			cMatrixf mtx = cMatrixf::Identity;
			mtx.SetTranslation(avPosition + vCenter);
			cVector3f vPush;
			if(apPhysics->CheckShapeWorldCollision(&vPush, pHull, mtx) == false)
			{
				bFits = true;
				break;
			}
			if(vPush.Length() < 0.0001f) break;
			avPosition += vPush + cMath::Vector3Normalize(vPush) * kSpawnClearance;
			if(cMath::Vector3Dist(avPosition, vInitialPosition) > 2.0f) break;
		}
		apPhysics->DestroyShape(pHull);
		return bFits && cMath::Vector3Dist(avPosition, avEye) <= kMaxSpawnDistance + kSpawnClearance;
	}
}

cLuxSpawnHandler::cLuxSpawnHandler() : iLuxUpdateable("LuxSpawnHandler"), mpMap(NULL), mlNextName(0)
{
	Reset();
}

cLuxSpawnHandler::~cLuxSpawnHandler() {}

void cLuxSpawnHandler::Reset()
{
	mpMap = NULL;
	mvSpawnedEntities.clear();
	msStatus = _W("Click an entity to spawn it. Z undoes the latest spawn.");
}

void cLuxSpawnHandler::OnMapEnter(cLuxMap *apMap)
{
	Reset();
	mpMap = apMap;
}

void cLuxSpawnHandler::OnMapLeave(cLuxMap *apMap) { Reset(); }
void cLuxSpawnHandler::DestroyWorldEntities(cLuxMap *apMap) { if(mpMap == apMap) Reset(); }

bool cLuxSpawnHandler::SpawnEntity(const tString& asFile)
{
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	cViewport *pViewport = gpBase->mpMapHandler->GetViewport();
	cCamera *pCamera = pViewport ? pViewport->GetCamera() : NULL;
	if(pMap == NULL || pCamera == NULL || pMap->GetPhysicsWorld() == NULL)
	{
		msStatus = _W("Load a map before spawning entities.");
		return false;
	}
	if(mpMap != pMap) OnMapEnter(pMap);

	cEntFileManager *pFiles = gpBase->mpEngine->GetResources()->GetEntFileManager();
	cEntFile *pFile = pFiles->CreateEntFile(asFile);
	if(pFile == NULL)
	{
		msStatus = _W("Could not load entity: ") + cString::To16Char(cString::GetFileName(asFile));
		return false;
	}
	cXmlElement *pVars = pFile->GetXmlDoc()->GetFirstElement("UserDefinedVariables");
	const tString sType = pVars ? pVars->GetAttributeString("EntityType") : "";
	iEntityLoader *pLoader = gpBase->mpEngine->GetResources()->GetEntityLoader(sType);
	// Resources may return a generic fallback for unknown types. Such an object
	// need not have a Lux owner, so accepting it could leave an untracked spawn.
	if(pLoader == NULL || sType.empty() || pLoader->GetName() != sType)
	{
		pFiles->Destroy(pFile);
		msStatus = _W("This entity type is not supported by the game.");
		return false;
	}
	const bool bStatic = sType == "StaticProp";
	cVector3f vMin, vMax;
	const bool bHasBounds = GetSpawnBounds(pMap->GetWorld(), pFile, vMin, vMax);
	pFiles->Destroy(pFile);
	if(bHasBounds == false)
	{
		msStatus = _W("Could not load the entity's mesh.");
		return false;
	}

	cMatrixf mtx = cMath::MatrixRotateY(pCamera->GetYaw() + kPif);
	cVector3f vPosition;
	iPhysicsBody *pPlayerBody = gpBase->mpPlayer->GetCharacterBody() ? gpBase->mpPlayer->GetBody(0) : NULL;
	if(FindSpawnPosition(pMap->GetPhysicsWorld(), pPlayerBody, pCamera->GetPosition(), pCamera->GetForward(), mtx, vMin, vMax, vPosition) == false)
	{
		msStatus = _W("Not enough room here. Aim at a more open surface.");
		return false;
	}
	mtx.SetTranslation(vPosition);

	tString sName;
	do { sName = "__player_spawn_" + cString::ToString(++mlNextName); }
	while(pMap->GetEntityByName(sName));
	if(bStatic)
	{
		cSpawnStaticLoader loader;
		pMap->CreateEntity(sName, asFile, mtx, 1, &loader);
	}
	else pMap->CreateEntity(sName, asFile, mtx, 1);

	iLuxEntity *pEntity = pMap->GetEntityByName(sName);
	if(pEntity == NULL)
	{
		msStatus = _W("The engine could not create this entity.");
		return false;
	}
	if(bStatic) static_cast<iLuxProp*>(pEntity)->SetStaticPhysics(true);
	// Sandbox ownership is intentionally local to the loaded map/session.
	pEntity->SetIsSaved(false);
	pEntity->AfterWorldLoad();
	pEntity->OnMapEnter();
	cSpawnedEntity entry;
	entry.mlID = pEntity->GetID();
	entry.msName = sName;
	entry.msFile = asFile;
	mvSpawnedEntities.push_back(entry);
	msStatus = _W("Spawned ") + cString::To16Char(cString::GetFileName(asFile)) + _W(". Z to undo.");
	return true;
}

bool cLuxSpawnHandler::UndoLastSpawn()
{
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap != mpMap) OnMapEnter(pMap);
	while(pMap && mvSpawnedEntities.empty() == false)
	{
		const cSpawnedEntity entry = mvSpawnedEntities.back();
		mvSpawnedEntities.pop_back();
		iLuxEntity *pEntity = pMap->GetEntityByID(entry.mlID);
		// IDs can be reused; both fields must match. Skip picked-up, broken,
		// expired, and already queued-for-destruction entities in the same press.
		if(pEntity == NULL || pEntity->GetName() != entry.msName || pEntity->GetDestroyMe()) continue;
		pMap->DestroyEntity(pEntity);
		msStatus = _W("Removed ") + cString::To16Char(cString::GetFileName(entry.msFile));
		return true;
	}
	msStatus = _W("No spawned entities left to undo.");
	return false;
}
