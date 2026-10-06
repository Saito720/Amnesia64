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

#include "ModelEditorWindowUserSettings.h"
#include "ModelEditor.h"
#include "ModelEditorWorld.h"
#include "../common/EditorUserClassDefinitionManager.h"

#include <algorithm>

//-------------------------------------------------------------------
//-------------------------------------------------------------------

cModelEditorWindowUserSettings::cModelEditorWindowUserSettings(cModelEditor* apEditor) : iEditorWindowPopUp(apEditor, "User Settings Window", true, true, false, cVector2f(700,500))
{
	mpEditor = apEditor;
	mpInputPanel = NULL;
	mpButtonApplyRigPreset = NULL;
	mbUpdatingLists = false;
	mlDisplayedSettingsRevision = 0;
}

//-------------------------------------------------------------------

cModelEditorWindowUserSettings::~cModelEditorWindowUserSettings()
{
	if(mpInputPanel) hplDelete(mpInputPanel);
}

//-------------------------------------------------------------------
//-------------------------------------------------------------------

//-------------------------------------------------------------------

void cModelEditorWindowUserSettings::OnSetActive(bool abX)
{
	iEditorWindowPopUp::OnSetActive(abX);
	if(abX) RefreshFromWorld();
}

//-------------------------------------------------------------------

void cModelEditorWindowUserSettings::OnInitLayout()
{
	iEditorWindowPopUp::OnInitLayout();
	mpWindow->SetText(_W("User Defined Variables"));

	cVector3f vPos = cVector3f(25,30,0.1f);

	mpLabelType = mpSet->CreateWidgetLabel(vPos,0, _W("Type"), mpWindow);

	vPos.x += 50;
	// Type ComboBox
	mpComboBoxType = mpSet->CreateWidgetComboBox(vPos, cVector2f(150,25), _W(""), mpWindow);

	mpComboBoxType->AddCallback(eGuiMessage_SelectionChange, this, kGuiCallback(TypeList_OnChange));

	vPos.x += 160;

	mpLabelSubType = mpSet->CreateWidgetLabel(vPos,0,_W("SubType"), mpWindow);


	vPos.x += 70;

	mpComboBoxSubType = mpSet->CreateWidgetComboBox(vPos, cVector2f(150,25), _W(""), mpWindow);

	mpComboBoxSubType->AddCallback(eGuiMessage_SelectionChange, this, kGuiCallback(SubTypeList_OnChange));

	vPos.x += 160;
	mpButtonApplyRigPreset = mpSet->CreateWidgetButton(vPos, cVector2f(170,25), _W("Apply rig preset"), mpWindow);
	mpButtonApplyRigPreset->SetToolTip(_W("Replace physical settings, animation names and the animation clip/event list with this rig's stock preset. Camera and hearing settings are preserved. Undo restores all previous settings and clips."));
	mpButtonApplyRigPreset->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(ApplyRigPreset_OnPressed));
	mpButtonApplyRigPreset->SetVisible(false);
	mpButtonApplyRigPreset->SetEnabled(false);

	vPos.x = 15;
	vPos.y += 35; 

	mpFrameVars = mpSet->CreateWidgetFrame(cVector3f(mpWindow->GetSize().x*0.025f, vPos.y, vPos.z), cVector2f(mpWindow->GetSize().x*0.95f,mpWindow->GetSize().y*0.8f), false, mpWindow, true, true);


	vPos.x = mpFrameVars->GetSize().x*0.5f-72.5f;
	vPos.y += mpFrameVars->GetSize().y + 5;

	PopulateTypeList();
}

//-------------------------------------------------------------------

void cModelEditorWindowUserSettings::OnUpdate(float afTimeStep)
{
	if(IsActive() && mlDisplayedSettingsRevision!=((cModelEditorWorld*)mpEditor->GetEditorWorld())->GetUserSettingsRevision())
		RefreshFromWorld();
}

//-------------------------------------------------------------------

void cModelEditorWindowUserSettings::OnWorldModify()
{
	if(IsActive()) RefreshFromWorld();
}

//-------------------------------------------------------------------

bool cModelEditorWindowUserSettings::TypeList_OnChange(iWidget* apWidget, const cGuiMessageData& aData)
{
	if(mbUpdatingLists) return true;
	PopulateSubTypeList();
	SubTypeList_OnChange(mpComboBoxSubType, aData);

	return true;
}
kGuiCallbackDeclaredFuncEnd(cModelEditorWindowUserSettings,TypeList_OnChange);

//-------------------------------------------------------------------

bool cModelEditorWindowUserSettings::SubTypeList_OnChange(iWidget* apWidget, const cGuiMessageData& aData)
{
	if(mbUpdatingLists) return true;
	int lTypeIdx = mpComboBoxType->GetSelectedItem();
	int lSubTypeIdx = mpComboBoxSubType->GetSelectedItem();

	cEditorUserClassDefinition* pDef = mpEditor->GetClassDefinitionManager()->GetDefinition(eUserClassDefinition_Entity);	
	cEditorUserClassType* pType = pDef->GetType(lTypeIdx);
	cEditorUserClassSubType* pClass = pType ? pType->GetSubType(lSubTypeIdx) : NULL;

	if(pClass==NULL)
		return true;

	cModelEditorWorld* pEntity = (cModelEditorWorld*)mpEditor->GetEditorWorld();
	mpEditor->AddAction(pEntity->CreateActionSetType(pClass));
	RefreshFromWorld();

	return true;
}
kGuiCallbackDeclaredFuncEnd(cModelEditorWindowUserSettings,SubTypeList_OnChange);

//-------------------------------------------------------------------

bool cModelEditorWindowUserSettings::ApplyRigPreset_OnPressed(iWidget* apWidget, const cGuiMessageData& aData)
{
	((cModelEditorWorld*)mpEditor->GetEditorWorld())->ApplyRigPreset();
	RefreshFromWorld();

	return true;
}
kGuiCallbackDeclaredFuncEnd(cModelEditorWindowUserSettings,ApplyRigPreset_OnPressed);

//-------------------------------------------------------------------

bool cModelEditorWindowUserSettings::VarInputCallback(iEditorVarInput* apInput)
{
	cModelEditorWorld* pWorld = (cModelEditorWorld*)mpEditor->GetEditorWorld();
	// A type change can invalidate this panel before the next layout update.
	// Reject its event without deleting the input while its callback is running.
	if(mlDisplayedSettingsRevision!=pWorld->GetUserSettingsRevision()) return true;
	mpEditor->AddAction(pWorld->CreateActionSetVariable(apInput->GetVar()->GetName(), apInput->GetInput()->GetValue()));
	return true;
}

bool cModelEditorWindowUserSettings::VarInputCallbackStaticHelper(void* apWindow, iEditorVarInput* apInput)
{
	return ((cModelEditorWindowUserSettings*)apWindow)->VarInputCallback(apInput);
}

//-------------------------------------------------------------------

void cModelEditorWindowUserSettings::PopulateTypeList()
{
	bool bWasUpdating = mbUpdatingLists;
	mbUpdatingLists = true;
	cEditorUserClassDefinition* pDef = mpEditor->GetClassDefinitionManager()->GetDefinition(eUserClassDefinition_Entity);

	mpComboBoxType->ClearItems();

	for(int i=0;i<pDef->GetTypeNum();++i)
	{
		cEditorUserClassType* pType = pDef->GetType(i);
		mpComboBoxType->AddItem(pType->GetName());
	}
	mbUpdatingLists = bWasUpdating;
}


//-------------------------------------------------------------------

void cModelEditorWindowUserSettings::PopulateSubTypeList()
{
	bool bWasUpdating = mbUpdatingLists;
	mbUpdatingLists = true;
	mpComboBoxSubType->ClearItems();

	cEditorUserClassDefinition* pDef = mpEditor->GetClassDefinitionManager()->GetDefinition(eUserClassDefinition_Entity);
	cEditorUserClassType* pType = pDef->GetType(mpComboBoxType->GetSelectedItem());
	for(int i=0;pType && i<pType->GetSubTypeNum();++i)
	{
		cEditorUserClassSubType* pSubType = pType->GetSubType(i);
		mpComboBoxSubType->AddItem(pSubType->GetName());
	}
	bool bEnabled = (mpComboBoxSubType->GetItemNum()>1);

	mpComboBoxSubType->SetSelectedItem(pType ? pType->GetDefaultSubTypeIndex() : -1, false, false);
	
	mpLabelSubType->SetEnabled(bEnabled);
	mpComboBoxSubType->SetEnabled(bEnabled);
	mbUpdatingLists = bWasUpdating;
}

//-------------------------------------------------------------------

void cModelEditorWindowUserSettings::PopulateVarList()
{
	cModelEditorWorld* pEntity = (cModelEditorWorld*)mpEditor->GetEditorWorld();
	cEditorClassInstance* pClass = pEntity->GetClass();

	if(mpInputPanel)
		hplDelete(mpInputPanel);
	mpInputPanel = NULL;

	if(pClass)
	{
		mpInputPanel = pClass->CreateInputPanel(this, mpFrameVars, false);
		mpInputPanel->SetCallback(this, VarInputCallbackStaticHelper);
		mpInputPanel->Update();
	}
	mlDisplayedSettingsRevision = pEntity->GetUserSettingsRevision();
}

//-------------------------------------------------------------------

//-------------------------------------------------------------------

void cModelEditorWindowUserSettings::RefreshFromWorld()
{
	cModelEditorWorld* pWorld = (cModelEditorWorld*)mpEditor->GetEditorWorld();
	cEditorClassInstance* pClass = pWorld->GetClass();
	cEditorUserClassSubType* pType = pClass ? (cEditorUserClassSubType*)pClass->GetClass() : NULL;

	// Opening the window or undoing a change must never trigger a type change.
	mbUpdatingLists = true;
	mpComboBoxType->SetSelectedItem(pType ? pType->GetParent()->GetIndex() : -1, false, false);
	PopulateSubTypeList();
	mpComboBoxSubType->SetSelectedItem(pType ? pType->GetIndex() : -1, false, false);
	mbUpdatingLists = false;

	bool bHasRigPreset = pWorld->CanApplyRigPreset();
	mpButtonApplyRigPreset->SetVisible(bHasRigPreset);
	mpButtonApplyRigPreset->SetEnabled(bHasRigPreset);
	if(mpInputPanel==NULL || mlDisplayedSettingsRevision!=pWorld->GetUserSettingsRevision())
		PopulateVarList();
	else
		mpInputPanel->Update();
}

//-------------------------------------------------------------------
