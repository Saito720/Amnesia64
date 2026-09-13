/*
 * Copyright © 2009-2020 Frictional Games
 * 
 * This file is part of Amnesia: The Dark Descent.
 * 
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "scene/Entity3D.h"
#include "scene/Node3D.h"
#include "math/Math.h"
#include "math/TransformInterpolation.h"
#include <climits>

#include "system/LowLevelSystem.h"

namespace hpl {

	// Only objects requested by the renderer join this list. Resource nodes and
	// the many immutable static submeshes need no per-tick snapshots.
	static tEntity3DList& GetInterpolatedEntities()
	{
		static tEntity3DList entities;
		return entities;
	}
	static bool gbRenderInterpolation = false;
	static float gfRenderInterpolationAlpha = 1.0f;
	static int glRenderInterpolationFrame = 0;

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	iEntity3D::iEntity3D(tString asName)
	{
		msName = asName;
		mbIsActive = true;
		
		mpParentNode = NULL;

		m_mtxLocalTransform = cMatrixf::Identity;
		m_mtxWorldTransform = cMatrixf::Identity;

		mbTransformUpdated = true;

		mlCount = 0;

		msSourceFile = "";

		mbApplyTransformToBV = true;
		mbUpdateBoundingVolume = true;

		mpParent = NULL;

		mlIteratorCount =-1;

		mbIsSaved = true;
		mlUniqueID = -1;
		mbInterpolationRegistered = false;
		mlRenderMatrixFrame = mlRenderBoundsFrame = -1;
		mlRenderMatrixCount = -2;
		mlRenderMatrixSimulationCount = -1;
		m_mtxRenderWorldTransform = cMatrixf::Identity;
	}

	iEntity3D::~iEntity3D()
	{
		if(mbInterpolationRegistered) GetInterpolatedEntities().erase(mInterpolationIterator);
		if(mpParentNode)
			mpParentNode->RemoveEntity(this);
		else if(mpParent) 
			mpParent->RemoveChild(this);

		for(tEntity3DListIt it = mlstChildren.begin(); it != mlstChildren.end();++it)
		{
			iEntity3D *pChild = *it;
			pChild->mpParent = NULL;
		}

		for(tNode3DListIt it = mlstNodeChildren.begin(); it != mlstNodeChildren.end();++it)
		{
			cNode3D *pNode = *it;
			pNode->mpEntityParent = NULL;
		}
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	cVector3f iEntity3D::GetLocalPosition()
	{
		return m_mtxLocalTransform.GetTranslation();
	}

	//-----------------------------------------------------------------------

	cMatrixf& iEntity3D::GetLocalMatrix()
	{
		return m_mtxLocalTransform;
	}

	//-----------------------------------------------------------------------

	cVector3f iEntity3D::GetWorldPosition()
	{
		UpdateWorldTransform();

		return m_mtxWorldTransform.GetTranslation();
	}
	
	//-----------------------------------------------------------------------

	cMatrixf& iEntity3D::GetWorldMatrix()
	{
		UpdateWorldTransform();
		
		return m_mtxWorldTransform;
	}

	void iEntity3D::RegisterRenderInterpolation()
	{
		if(mbInterpolationRegistered) return;
		mbInterpolationRegistered = true;
		GetInterpolatedEntities().push_front(this);
		mInterpolationIterator = GetInterpolatedEntities().begin();
		CaptureRenderInterpolation();
	}

	void iEntity3D::CaptureRenderInterpolation()
	{
		m_mtxPreviousLocalTransform = m_mtxLocalTransform;
		mpPreviousParentNode = mpParentNode;
		mpPreviousParent = mpParent;
		cBoundingVolume* pBounds = GetBoundingVolume();
		mvPreviousBoundsMin = pBounds->GetMin();
		mvPreviousBoundsMax = pBounds->GetMax();
		mlRenderMatrixFrame = mlRenderBoundsFrame = -1;
	}

	void iEntity3D::CaptureInterpolationState()
	{
		for(tEntity3DListIt it=GetInterpolatedEntities().begin(); it!=GetInterpolatedEntities().end(); ++it)
			(*it)->CaptureRenderInterpolation();
		cNode3D::CaptureInterpolationState();
	}

	void iEntity3D::ResetInterpolationState()
	{
		EndRenderInterpolation();
		CaptureInterpolationState();
	}

	void iEntity3D::BeginRenderInterpolation(float afAlpha)
	{
		gfRenderInterpolationAlpha = cMath::Clamp(afAlpha,0.0f,1.0f);
		glRenderInterpolationFrame = glRenderInterpolationFrame >= INT_MAX-2 ? 0 : glRenderInterpolationFrame+1;
		gbRenderInterpolation = true;
		for(tEntity3DListIt it=GetInterpolatedEntities().begin(); it!=GetInterpolatedEntities().end(); ++it)
			(*it)->OnRenderInterpolation();
	}

	void iEntity3D::EndRenderInterpolation() { gbRenderInterpolation = false; }
	bool iEntity3D::IsRenderInterpolationActive() { return gbRenderInterpolation; }
	float iEntity3D::GetRenderInterpolationAlpha() { return gbRenderInterpolation ? gfRenderInterpolationAlpha : 1.0f; }
	int iEntity3D::GetRenderInterpolationFrame() { return glRenderInterpolationFrame; }

	void iEntity3D::ResetRenderInterpolation()
	{
		if(mbInterpolationRegistered) CaptureRenderInterpolation();
		for(tEntity3DListIt it=mlstChildren.begin(); it!=mlstChildren.end(); ++it) (*it)->ResetRenderInterpolation();
		for(tNode3DListIt it=mlstNodeChildren.begin(); it!=mlstNodeChildren.end(); ++it) (*it)->ResetRenderInterpolation();
	}

	cMatrixf& iEntity3D::GetRenderWorldMatrix()
	{
		if(!gbRenderInterpolation) return GetWorldMatrix();
		RegisterRenderInterpolation();
		if(mlRenderMatrixFrame == glRenderInterpolationFrame) return m_mtxRenderWorldTransform;
		cMatrixf mtxLocal = (mpPreviousParentNode == mpParentNode && mpPreviousParent == mpParent) ?
			InterpolateTransform(m_mtxPreviousLocalTransform,m_mtxLocalTransform,gfRenderInterpolationAlpha) : m_mtxLocalTransform;
		cMatrixf mtxWorld = mtxLocal;
		if(mpParentNode) mtxWorld = cMath::MatrixMul(mpParentNode->GetRenderWorldMatrix(),mtxLocal);
		else if(mpParent) mtxWorld = cMath::MatrixMul(mpParent->GetRenderWorldMatrix(),mtxLocal);
		if(mtxWorld != m_mtxRenderWorldTransform || mlRenderMatrixSimulationCount != mlCount)
			mlRenderMatrixCount = mlRenderMatrixCount <= INT_MIN+1 ? -2 : mlRenderMatrixCount-1;
		m_mtxRenderWorldTransform = mtxWorld;
		mlRenderMatrixSimulationCount = mlCount;
		mlRenderMatrixFrame = glRenderInterpolationFrame;
		return m_mtxRenderWorldTransform;
	}

	cVector3f iEntity3D::GetRenderWorldPosition() { return GetRenderWorldMatrix().GetTranslation(); }

	int iEntity3D::GetRenderTransformUpdateCount()
	{
		if(!gbRenderInterpolation) return GetTransformUpdateCount();
		GetRenderWorldMatrix();
		return mlRenderMatrixCount;
	}

	cBoundingVolume* iEntity3D::GetRenderBoundingVolume()
	{
		cBoundingVolume* pBounds = GetBoundingVolume();
		if(!gbRenderInterpolation) return pBounds;
		RegisterRenderInterpolation();
		if(mlRenderBoundsFrame == glRenderInterpolationFrame) return &mRenderBoundingVolume;
		cVector3f vMin = pBounds->GetMin(), vMax = pBounds->GetMax();
		cMath::ExpandAABB(vMin,vMax,mvPreviousBoundsMin,mvPreviousBoundsMax);
		// Include the rendered pose as well as both fixed endpoints. In particular,
		// an elongated rotating object can leave the union of its endpoint AABBs.
		const cMatrixf& mtxWorld = GetWorldMatrix();
		const cMatrixf& mtxRender = GetRenderWorldMatrix();
		if(!(mtxWorld == mtxRender) && cMath::Abs(cMath::Vector3Dot(cMath::Vector3Cross(mtxWorld.GetRight(),mtxWorld.GetUp()),mtxWorld.GetForward())) > 0.000001f)
		{
			cMatrixf delta = cMath::MatrixMul(mtxRender,cMath::MatrixInverse(mtxWorld));
			for(int i=0; i<8; ++i)
			{
				cVector3f corner(i&1 ? pBounds->GetMax().x : pBounds->GetMin().x,
					i&2 ? pBounds->GetMax().y : pBounds->GetMin().y,
					i&4 ? pBounds->GetMax().z : pBounds->GetMin().z);
				corner = cMath::MatrixMul(delta,corner);
				cMath::ExpandAABB(vMin,vMax,corner,corner);
			}
		}
		mRenderBoundingVolume.SetLocalMinMax(vMin,vMax);
		mlRenderBoundsFrame = glRenderInterpolationFrame;
		return &mRenderBoundingVolume;
	}
	
	//-----------------------------------------------------------------------

	void iEntity3D::SetPosition(const cVector3f& avPos)
	{
		m_mtxLocalTransform.m[0][3] = avPos.x;
		m_mtxLocalTransform.m[1][3] = avPos.y;
		m_mtxLocalTransform.m[2][3] = avPos.z;

		SetTransformUpdated();
	}
	
	//-----------------------------------------------------------------------

	void iEntity3D::SetMatrix(const cMatrixf& a_mtxTransform)
	{
		m_mtxLocalTransform = a_mtxTransform;

		SetTransformUpdated();
	}

	//-----------------------------------------------------------------------

	void iEntity3D::SetWorldPosition(const cVector3f& avWorldPos)
	{
		if(mpParent)
		{
			SetPosition(avWorldPos - mpParent->GetWorldPosition());
		}
		else
		{
			SetPosition(avWorldPos);
		}
	}

	//-----------------------------------------------------------------------

	void iEntity3D::SetWorldMatrix(const cMatrixf& a_mtxWorldTransform)
	{
		if(mpParent)
		{
			SetMatrix(cMath::MatrixMul(cMath::MatrixInverse(mpParent->GetWorldMatrix()),
				a_mtxWorldTransform));
		}
		else
		{
			SetMatrix(a_mtxWorldTransform);
		}
	}
	
	//-----------------------------------------------------------------------
	
	void iEntity3D::SetTransformUpdated(bool abUpdateCallbacks)
	{
		mlRenderMatrixFrame = mlRenderBoundsFrame = -1;
		mbTransformUpdated = true;
		mlCount++;

		mbUpdateBoundingVolume = true;

		OnTransformUpdated();
		
		//Update children
		for(tEntity3DListIt EntIt = mlstChildren.begin(); EntIt != mlstChildren.end();++EntIt)
		{
			iEntity3D *pChild = *EntIt;
			pChild->SetTransformUpdated(true);
		}
		
		//Update node children
		for(tNode3DListIt nodeIt = mlstNodeChildren.begin(); nodeIt != mlstNodeChildren.end();++nodeIt)
		{
			cNode3D *pNode = *nodeIt;
			pNode->SetWorldTransformUpdated();
		}

		//Update callbacks
		if(mlstCallbacks.empty() || abUpdateCallbacks==false) return;

		tEntityCallbackListIt it = mlstCallbacks.begin();
		for(; it!= mlstCallbacks.end(); ++it)
		{
			iEntityCallback* pCallback = *it;
			pCallback->OnTransformUpdate(this);
		}
	}

	//-----------------------------------------------------------------------

	bool iEntity3D::GetTransformUpdated()
	{
		return mbTransformUpdated;
	}
	
	//-----------------------------------------------------------------------
	
	int iEntity3D::GetTransformUpdateCount()
	{
		return mlCount;
	}

	//-----------------------------------------------------------------------

	cBoundingVolume* iEntity3D::GetBoundingVolume()
	{
		if(mbApplyTransformToBV && mbUpdateBoundingVolume)
		{
			mBoundingVolume.SetTransform(GetWorldMatrix());
			mbUpdateBoundingVolume = false;
		}

		return &mBoundingVolume;
	}

	//-----------------------------------------------------------------------

	void iEntity3D::AddCallback(iEntityCallback *apCallback)
	{
		mlstCallbacks.push_back(apCallback);
	}
	
	//-----------------------------------------------------------------------

	void iEntity3D::RemoveCallback(iEntityCallback *apCallback)
	{
		STLFindAndRemove(mlstCallbacks, apCallback);
	}

	//-----------------------------------------------------------------------

	void iEntity3D::AddChild(iEntity3D *apEntity)
	{
		if(apEntity==NULL)return;
		if(apEntity->mpParent != NULL)
		{
			apEntity->mpParent->RemoveChild(apEntity);
		}

	    mlstChildren.push_back(apEntity);
		apEntity->mpParent = this;

		apEntity->SetTransformUpdated(true);
	}
	
	void iEntity3D::RemoveChild(iEntity3D *apEntity)
	{
		for(tEntity3DListIt it = mlstChildren.begin(); it != mlstChildren.end();)
		{
			iEntity3D *pChild = *it;
			if(pChild == apEntity)
			{
				pChild->mpParent = NULL;
				it = mlstChildren.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	bool iEntity3D::IsChild(iEntity3D *apEntity)
	{
		for(tEntity3DListIt it = mlstChildren.begin(); it != mlstChildren.end();++it)
		{
			iEntity3D *pChild = *it;
			if(pChild == apEntity) return true;
		}
		return false;
	}

	iEntity3D * iEntity3D::GetEntityParent()
	{
		return mpParent;
	}

	cEntity3DIterator iEntity3D::GetChildIterator()
	{
		return cEntity3DIterator(&mlstChildren);
	}
	
	//-----------------------------------------------------------------------

	void iEntity3D::AddNodeChild(cNode3D *apNode)
	{
		if(apNode->mpEntityParent != NULL)
		{
			apNode->mpEntityParent->RemoveNodeChild(apNode);
		}

		mlstNodeChildren.push_back(apNode);
		apNode->mpEntityParent = this;

		apNode->SetWorldTransformUpdated();
	}
	
	void iEntity3D::RemoveNodeChild(cNode3D *apNode)
	{
		for(tNode3DListIt it = mlstNodeChildren.begin(); it != mlstNodeChildren.end();)
		{
			cNode3D *pNode = *it;
			if(pNode == apNode)
			{
				pNode->mpEntityParent = NULL;
				it = mlstNodeChildren.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
	
	bool iEntity3D::IsNodeChild(cNode3D *apNode)
	{
		for(tNode3DListIt it = mlstNodeChildren.begin(); it != mlstNodeChildren.end();++it)
		{
			cNode3D *pNode = *it;
			if(pNode == apNode) return true;
		}
		return false;
	}
	
	//-----------------------------------------------------------------------
	
	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	void iEntity3D::UpdateWorldTransform()
	{
		if(mbTransformUpdated)
		{
			//Log("CREATING Entity '%s' world transform!\n",msName.c_str());

			mbTransformUpdated = false;
			
			//first check if there is a node parent
			if(mpParentNode)
			{
				cNode3D* pNode3D = static_cast<cNode3D*>(mpParentNode);

				m_mtxWorldTransform = cMath::MatrixMul(pNode3D->GetWorldMatrix(), m_mtxLocalTransform);
			}
			//If there is no node parent check for entity parent
			else if(mpParent)
			{
				m_mtxWorldTransform = cMath::MatrixMul(mpParent->GetWorldMatrix(), m_mtxLocalTransform);
			}
			else
			{
				m_mtxWorldTransform = m_mtxLocalTransform;
			}
		}
	}
	
	//-----------------------------------------------------------------------
}
