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

#include "ModelEditorActions.h"

#include "ModelEditor.h"
#include "ModelEditorWorld.h"

#include "../common/EntityWrapperFactory.h"
#include "../common/EntityWrapperSubMesh.h"
#include "../common/EntityWrapperBone.h"
#include "../common/EditorSelection.h"
#include "../common/EditorUserClassDefinitionManager.h"

//---------------------------------------------------------------------------------

cModelEditorActionMeshLoad::cModelEditorActionMeshLoad(cModelEditorWorld* apEditorWorld, const tString& asFilename) : iEditorActionWorldModifier("Load Mesh", apEditorWorld)
{
	cModelEditorWorld* pWorld = (cModelEditorWorld*)mpEditorWorld;

	msOldMeshFilename = pWorld->GetSubMeshType()->GetMeshFilename();
	mlstOldSubMeshIDs = pWorld->GetSubMeshType()->GetSubMeshIDs();// ((cModelEditorWorld*)mpEditorWorld)->GetSubMeshIDs();
	mlstOldBoneIDs = pWorld->GetBoneType()->GetBoneIDs(); //((cModelEditorWorld*)mpEditorWorld)->GetBoneIDs();

	msNewMeshFilename = asFilename;

	cMeshManager* pManager = mpEditorWorld->GetEditor()->GetEngine()->GetResources()->GetMeshManager();
	cMesh* pMesh = pManager->CreateMesh(asFilename);

	if(pMesh)
	{
		for(int i=0;i<pMesh->GetSubMeshNum();++i)
			mlstSubMeshIDs.push_back(mpEditorWorld->GetFreeID());

		cSkeleton* pSkeleton = pMesh->GetSkeleton();
		if(pSkeleton)
		{
			for(int i=0;i<pSkeleton->GetBoneNum();++i)
				mlstBoneIDs.push_back(mpEditorWorld->GetFreeID());
		}
	}

	pManager->Destroy(pMesh);
}

//---------------------------------------------------------------------------------

void cModelEditorActionMeshLoad::DoModify()
{
	mpEditorWorld->GetEditor()->GetSelection()->ClearEntities();
	tEntityDataVec temp;
	((cModelEditorWorld*)mpEditorWorld)->GetSubMeshType()->SetMesh(msNewMeshFilename, true, 
																	temp, mlstSubMeshIDs, 
																	temp, mlstBoneIDs);
}

//---------------------------------------------------------------------------------

void cModelEditorActionMeshLoad::UndoModify()
{
	cEditorSelection* pSelection = mpEditorWorld->GetEditor()->GetSelection();
	tEntityDataVec temp;
	((cModelEditorWorld*)mpEditorWorld)->GetSubMeshType()->SetMesh(msOldMeshFilename, true,
																	temp, mlstOldSubMeshIDs, 
																	temp, mlstOldBoneIDs);
	tIntListIt it = mlstOldSelectedIDs.begin();
	for(;it!=mlstOldSelectedIDs.end();++it)
	{
		iEntityWrapper* pEnt = mpEditorWorld->GetEntity(*it);

		pSelection->AddEntity(pEnt);
	}
}

//---------------------------------------------------------------------------------

void cModelEditorActionSetUserSettings::StoreOldSettings(cModelEditorWorld* apWorld)
{
	cEditorClassInstance* pClass = apWorld->GetClass();
	mpOldType = pClass ? (cEditorUserClassSubType*)pClass->GetClass() : NULL;
	mpNewType = mpOldType;
	if(pClass) pClass->SaveValuesToMap(mmapOldValues);
	mmapNewValues = mmapOldValues;
	mmapOldTempValues = apWorld->GetTempValues();
	mmapNewTempValues = mmapOldTempValues;
	mbReplaceAnimations = false;
}

//---------------------------------------------------------------------------------

cModelEditorActionSetUserSettings::cModelEditorActionSetUserSettings(cModelEditorWorld* apWorld, cEditorUserClassSubType* apType)
	: iEditorActionWorldModifier("Set Entity Subtype", apWorld)
{
	StoreOldSettings(apWorld);
	mpNewType = apType;
	mmapNewValues.clear();
	if(apType)
	{
		// Preserve the same named values exactly as the existing dropdown did.
		for(tVarValueMap::const_iterator it = mmapOldValues.begin(); it!=mmapOldValues.end(); ++it)
			mmapNewTempValues[it->first] = it->second;
		cEditorClassInstance* pClass = apType->CreateInstance(eEditorVarCategory_Type);
		pClass->LoadValuesFromMap(mmapNewTempValues);
		pClass->SaveValuesToMap(mmapNewValues);
		hplDelete(pClass);
	}
}

//---------------------------------------------------------------------------------

cModelEditorActionSetUserSettings::cModelEditorActionSetUserSettings(cModelEditorWorld* apWorld, const tWString& asName, const tWString& asValue)
	: iEditorActionWorldModifier("Set Entity Variable", apWorld)
{
	StoreOldSettings(apWorld);
	tVarValueMap::iterator it = mmapNewValues.find(asName);
	if(it!=mmapNewValues.end()) it->second = asValue;
}

//---------------------------------------------------------------------------------

cModelEditorActionSetUserSettings::cModelEditorActionSetUserSettings(cModelEditorWorld* apWorld, const tAnimWrapperVec& avAnimations)
	: iEditorActionWorldModifier("Set Entity Animations", apWorld)
{
	StoreOldSettings(apWorld);
	mbReplaceAnimations = true;
	mvOldAnimations = apWorld->GetAnimations();
	mvNewAnimations = avAnimations;
}

//---------------------------------------------------------------------------------

cModelEditorActionSetUserSettings::cModelEditorActionSetUserSettings(cModelEditorWorld* apWorld, const tVarValueMap& amapPresetValues, const tAnimWrapperVec& avAnimations)
	: iEditorActionWorldModifier("Apply Enemy_Llama Rig Preset", apWorld)
{
	StoreOldSettings(apWorld);
	mbReplaceAnimations = true;
	mvOldAnimations = apWorld->GetAnimations();
	mvNewAnimations = avAnimations;
	for(tVarValueMap::const_iterator it = amapPresetValues.begin(); it!=amapPresetValues.end(); ++it)
	{
		tVarValueMap::iterator target = mmapNewValues.find(it->first);
		if(target!=mmapNewValues.end()) target->second = it->second;
	}
}

//---------------------------------------------------------------------------------

static bool RigAnimationsEqual(tAnimWrapperVec& avFirst, tAnimWrapperVec& avSecond)
{
	if(avFirst.size()!=avSecond.size()) return false;
	for(std::size_t i=0; i<avFirst.size(); ++i)
	{
		cAnimationWrapper& first = avFirst[i];
		cAnimationWrapper& second = avSecond[i];
		if(first.GetName()!=second.GetName() || first.GetFile()!=second.GetFile() ||
		   first.GetSpeed()!=second.GetSpeed() || first.GetSpecialEventTime()!=second.GetSpecialEventTime()) return false;
		tAnimEventWrapperVec& firstEvents = first.GetEvents();
		tAnimEventWrapperVec& secondEvents = second.GetEvents();
		if(firstEvents.size()!=secondEvents.size()) return false;
		for(std::size_t j=0; j<firstEvents.size(); ++j)
			if(firstEvents[j].GetTime()!=secondEvents[j].GetTime() || firstEvents[j].GetType()!=secondEvents[j].GetType() ||
			   firstEvents[j].GetValue()!=secondEvents[j].GetValue()) return false;
	}
	return true;
}

//---------------------------------------------------------------------------------

bool cModelEditorActionSetUserSettings::Create()
{
	return mpNewType && (mpOldType!=mpNewType || mmapOldValues!=mmapNewValues ||
						 (mbReplaceAnimations && RigAnimationsEqual(mvOldAnimations, mvNewAnimations)==false));
}

//---------------------------------------------------------------------------------

void cModelEditorActionSetUserSettings::DoModify()
{
	((cModelEditorWorld*)mpEditorWorld)->SetUserSettings(mpNewType, mmapNewValues, mmapNewTempValues, mbReplaceAnimations ? &mvNewAnimations : NULL);
}

//---------------------------------------------------------------------------------

void cModelEditorActionSetUserSettings::UndoModify()
{
	((cModelEditorWorld*)mpEditorWorld)->SetUserSettings(mpOldType, mmapOldValues, mmapOldTempValues, mbReplaceAnimations ? &mvOldAnimations : NULL);
}

//---------------------------------------------------------------------------------

