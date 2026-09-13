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

#include "gui/GuiPopUp.h"

#include "system/LowLevelSystem.h"

#include "math/Math.h"

#include "gui/Gui.h"
#include "gui/GuiSkin.h"
#include "gui/GuiSet.h"

#include "gui/WidgetWindow.h"

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	iGuiPopUp::iGuiPopUp(cGuiSet *apSet, bool abAddCloseButton, const cVector2f& avPopUpSize)
	{
		mpSet = apSet;
		mpSkin = mpSet->GetSkin();
		mbHasResizePlacement = false;

		mpSet->mlPopupCount++;

		cVector3f vPos = cVector3f(mpSet->GetVirtualSize()-avPopUpSize)*0.5f + cVector3f(0,0, mpSet->GetPopUpZ());
		mpSet->IncPopUpZ();

		mpWindow = mpSet->CreateWidgetWindow(abAddCloseButton?eWidgetWindowButtonFlag_ButtonClose:eWidgetWindowButtonFlag_None,
											 vPos, avPopUpSize, _W(""), NULL);
		mpWindow->SetCloseButtonDisablesWindow(false);
		mpWindow->AddCallback(eGuiMessage_WindowClose, this, kGuiCallback(Window_OnClose));
		mpWindow->AddCallback(eGuiMessage_OnVirtualSizeChange, this, kGuiCallback(Window_OnVirtualSizeChange));

		////////////////////////
		// Set up Attention and focus

		mbAttChanged = false;
		mpNewAttention = mpSet->GetAttentionWidget();
		mbFocusChanged = false;
		mpNewFocused = mpSet->GetFocusedWidget();
		mbDefaultUIFocusChanged = false;
		mpNewDefaultUIFocus = mpSet->GetDefaultFocusNavWidget();

		mpSet->PushAttentionWidget();
		mpSet->PushFocusedWidget();
		mpSet->PushDefaultFocusNavWidget();
				
		mpSet->SetAttentionWidget(mpWindow);

		SetKillOnEscapeKey(true);

		mpDestroyCallbackObject = NULL;
		mpDestroyCallback = NULL;
	}

	//-----------------------------------------------------------------------

	iGuiPopUp::~iGuiPopUp()
	{
		mpSet->mlPopupCount--;

		mpSet->PopAttentionWidget();
		mpSet->PopFocusedWidget();
		mpSet->PopDefaultFocusNavWidget();
		
		if(mbAttChanged)
			mpSet->SetAttentionWidget(mpNewAttention);
		if(mbFocusChanged)
			mpSet->SetFocusedWidget(mpNewFocused);
		if(mbDefaultUIFocusChanged)
			mpSet->SetDefaultFocusNavWidget(mpNewDefaultUIFocus);
		
		if(mpDestroyCallbackObject && mpDestroyCallback) {
			mpDestroyCallback(mpDestroyCallbackObject, NULL, cGuiMessageData());
		}

		if(mpWindow) mpSet->DestroyWidget(mpWindow, true);
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	void iGuiPopUp::AddOnDestroyCallback(void *apCallbackObject, tGuiCallbackFunc apCallback)
	{
		mpDestroyCallbackObject = apCallbackObject;
		mpDestroyCallback = apCallback;
	}

	void iGuiPopUp::SetKillOnEscapeKey(bool abX)
	{
		mpWindow->SetEscapeKeyClosesWindow(abX);
	}

	bool iGuiPopUp::GetKillOnEscapeKey()
	{
		return mpWindow->GetEscapeKeyClosesWindow();
	}

	//-----------------------------------------------------------------------


	//////////////////////////////////////////////////////////////////////////
	// PROTECTED METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	bool iGuiPopUp::Window_OnClose(iWidget* apWidget, const cGuiMessageData& aData)
	{
		OnCloseSpecific();
		SelfDestruct();

		return true;
	}
	kGuiCallbackDeclaredFuncEnd(iGuiPopUp, Window_OnClose);

	//-----------------------------------------------------------------------

	bool iGuiPopUp::Window_OnVirtualSizeChange(iWidget* apWidget, const cGuiMessageData& aData)
	{
		const cVector2f& vOldSize = aData.mvPos;
		const cVector2f& vOldOffset = aData.mvRel;
		const cVector2f& vNewSize = mpSet->GetVirtualSize();
		const cVector2f& vNewOffset = mpSet->GetVirtualSizeOffset();
		const cVector2f& vSize = mpWindow->GetSize();
		cVector3f vPos = mpWindow->GetGlobalPosition();
		const cVector2f vPosition(vPos.x, vPos.y);
		const bool bMoved = !mbHasResizePlacement || vPosition != mvLastResizePosition || vSize != mvLastResizeSize;

		// Keep centered dialogs centered, and retain the relative placement of
		// moved windows and pickers without recreating their widgets or focus.
		for(int i=0; i<2; ++i)
		{
			if(bMoved)
				mvResizeCenter.v[i] = vOldSize.v[i] > 0 ?
					(vPos.v[i] + vOldOffset.v[i] + vSize.v[i]*0.5f) / vOldSize.v[i] : 0.5f;
			vPos.v[i] = mvResizeCenter.v[i]*vNewSize.v[i] - vNewOffset.v[i] - vSize.v[i]*0.5f;
		}
		mpWindow->SetGlobalPosition(vPos);
		mpSet->PositionWidgetInsideBounds(mpWindow);
		// Retain the intended placement through temporary clamping. A later
		// expansion restores it; an actual drag establishes a new placement.
		vPos = mpWindow->GetGlobalPosition();
		mvLastResizePosition = cVector2f(vPos.x, vPos.y);
		mvLastResizeSize = vSize;
		mbHasResizePlacement = true;
		return true;
	}
	kGuiCallbackDeclaredFuncEnd(iGuiPopUp, Window_OnVirtualSizeChange);

	//-----------------------------------------------------------------------
	
	void iGuiPopUp::SelfDestruct()
	{
		mpSet->DestroyPopUp(this);
	}

	//-----------------------------------------------------------------------

	bool iGuiPopUp::RunCallback(void* apObj, tGuiCallbackFunc apCallback, 
							iWidget* apWidget, const cGuiMessageData& aData, bool abRunFocusChangeChecks)
	{
		if(apObj==NULL || apCallback==NULL) return false;

		iWidget *pPreCallAtt = NULL, *pPreCallFocus = NULL, *pPreCallDefaultUIFocus = NULL, *pPostCallAtt, *pPostCallFocus, *pPostCallDefaultUIFocus;
		
		if(abRunFocusChangeChecks)
		{
			pPreCallAtt = mpSet->GetAttentionWidget();
			pPreCallFocus = mpSet->GetFocusedWidget();
			pPreCallDefaultUIFocus = mpSet->GetDefaultFocusNavWidget();
		}

		bool bRet = apCallback(apObj, apWidget, aData);

		if(abRunFocusChangeChecks)
		{
			pPostCallAtt = mpSet->GetAttentionWidget();
			pPostCallFocus = mpSet->GetFocusedWidget();
			pPostCallDefaultUIFocus = mpSet->GetDefaultFocusNavWidget();

			mbAttChanged = pPreCallAtt!=pPostCallAtt;
			if(mbAttChanged)
				mpNewAttention = pPostCallAtt;
			
			mbFocusChanged = pPreCallFocus!=pPostCallFocus;
			if(mbFocusChanged)
				mpNewFocused = pPostCallFocus;

			mbDefaultUIFocusChanged = pPreCallDefaultUIFocus!=pPostCallDefaultUIFocus;
			if(mbDefaultUIFocusChanged)
				mpNewDefaultUIFocus = pPostCallDefaultUIFocus;
		}

		return bRet;
	}

	//-----------------------------------------------------------------------

	void iGuiPopUp::SetUpDefaultFocus(iWidget* apWidget)
	{
		mpSet->SetDefaultFocusNavWidget(apWidget);
		mpSet->SetFocusedWidget(apWidget);
	}
}
