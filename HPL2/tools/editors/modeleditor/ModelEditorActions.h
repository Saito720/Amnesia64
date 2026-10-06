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

#ifndef MODEL_EDITOR_ACTIONS_H
#define MODEL_EDITOR_ACTIONS_H

#include "../common/EditorAction.h"
#include "../common/EditorVar.h"
#include "ModelEditorWorld.h"

//--------------------------------------------------------------

class cModelEditorWorld;
class cEditorUserClassSubType;

//--------------------------------------------------------------

class cModelEditorActionMeshLoad : public iEditorActionWorldModifier
{
public:
	cModelEditorActionMeshLoad(cModelEditorWorld* apEditorWorld, const tString& asFilename);

	void DoModify();
	void UndoModify();
protected:
	void CreateSkeleton(tIntList& alstBoneIDs, tIntListIt& alstBoneIt, int alParentID, cBone* apBone);
	tString msOldMeshFilename;
	tString msNewMeshFilename;

	tIntList mlstSubMeshIDs;
	tIntList mlstOldSubMeshIDs;

	tIntList mlstBoneIDs;
	tIntList mlstOldBoneIDs;

	tIntList mlstOldSelectedIDs;
};

//--------------------------------------------------------------

// Stores both visible settings and values retained when changing subtype.
class cModelEditorActionSetUserSettings : public iEditorActionWorldModifier
{
public:
	cModelEditorActionSetUserSettings(cModelEditorWorld* apWorld, cEditorUserClassSubType* apType);
	cModelEditorActionSetUserSettings(cModelEditorWorld* apWorld, const tWString& asName, const tWString& asValue);
	cModelEditorActionSetUserSettings(cModelEditorWorld* apWorld, const tAnimWrapperVec& avAnimations);
	cModelEditorActionSetUserSettings(cModelEditorWorld* apWorld, const tVarValueMap& amapPresetValues, const tAnimWrapperVec& avAnimations);

	bool Create();
	void DoModify();
	void UndoModify();

protected:
	void StoreOldSettings(cModelEditorWorld* apWorld);

	cEditorUserClassSubType* mpOldType;
	cEditorUserClassSubType* mpNewType;
	tVarValueMap mmapOldValues;
	tVarValueMap mmapNewValues;
	tVarValueMap mmapOldTempValues;
	tVarValueMap mmapNewTempValues;
	bool mbReplaceAnimations;
	tAnimWrapperVec mvOldAnimations;
	tAnimWrapperVec mvNewAnimations;
};

//--------------------------------------------------------------

#endif // MODEL_EDITOR_ACTIONS_H
