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

#include "gui/WidgetTextBox.h"

#include "system/LowLevelSystem.h"

#include "math/Math.h"

#include "graphics/FontData.h"

#include "system/String.h"
#include "system/Platform.h"

#include "gui/Gui.h"
#include "gui/GuiSkin.h"
#include "gui/GuiSet.h"
#include "gui/GuiGfxElement.h"

#include "gui/WidgetButton.h"

namespace hpl {
	static bool IsHighSurrogate(wchar_t aChar)
	{
		return sizeof(wchar_t) == 2 && (unsigned int)aChar >= 0xD800 && (unsigned int)aChar <= 0xDBFF;
	}

	static bool IsLowSurrogate(wchar_t aChar)
	{
		return sizeof(wchar_t) == 2 && (unsigned int)aChar >= 0xDC00 && (unsigned int)aChar <= 0xDFFF;
	}

	static int ClampTextPosition(const tWString& asText, int alPosition)
	{
		if(alPosition < 0) return 0;
		if(alPosition > (int)asText.size()) return (int)asText.size();
		if(alPosition > 0 && alPosition < (int)asText.size() &&
			IsHighSurrogate(asText[alPosition-1]) && IsLowSurrogate(asText[alPosition])) --alPosition;
		return alPosition;
	}

	static int PreviousTextCodepoint(const tWString& asText, int alPosition)
	{
		int lPrevious = alPosition > 0 ? alPosition - 1 : 0;
		if(lPrevious > 0 && lPrevious < (int)asText.size() &&
			IsLowSurrogate(asText[lPrevious]) && IsHighSurrogate(asText[lPrevious-1])) --lPrevious;
		return lPrevious;
	}

	static int NextTextCodepoint(const tWString& asText, int alPosition)
	{
		if(alPosition >= (int)asText.size()) return (int)asText.size();
		return alPosition + (IsHighSurrogate(asText[alPosition]) &&
			alPosition + 1 < (int)asText.size() && IsLowSurrogate(asText[alPosition+1]) ? 2 : 1);
	}

	static tWString EncodeTextCodepoint(unsigned int alCodepoint)
	{
		tWString sText;
		if(alCodepoint > 0x10FFFF || (alCodepoint >= 0xD800 && alCodepoint <= 0xDFFF)) return sText;
		if(sizeof(wchar_t) == 2 && alCodepoint > 0xFFFF)
		{
			alCodepoint -= 0x10000;
			sText += (wchar_t)(0xD800 + (alCodepoint >> 10));
			sText += (wchar_t)(0xDC00 + (alCodepoint & 0x3FF));
		}
		else sText += (wchar_t)alCodepoint;
		return sText;
	}

	static int CountTextCodepoints(const tWString& asText)
	{
		int lCount = 0;
		const wchar_t* pText = asText.c_str();
		while(*pText)
		{
			DecodeFontCodepoint(pText);
			++lCount;
		}
		return lCount;
	}

	static int TextPrefixUnits(const tWString& asText, int alCodepoints)
	{
		const wchar_t* pText = asText.c_str();
		while(*pText && alCodepoints-- > 0) DecodeFontCodepoint(pText);
		return (int)(pText - asText.c_str());
	}

	int cWidgetTextBox::mlDefaultDecimals = 3;

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cWidgetTextBox::cWidgetTextBox(cGuiSet *apSet, cGuiSkin *apSkin, eWidgetTextBoxInputType aType) : iWidget(eWidgetType_TextBox,apSet, apSkin)
	{
		mvButtons[0] = NULL;
		mvButtons[1] = NULL;
		mInputType = aType;
		mbShowButtons = false;
		
		mpPrevAttention = NULL;

		mlMarkerCharPos = -1;
		mlSelectedTextEnd = -1;

		mlFirstVisibleChar =0;
		mlVisibleCharSize =0;

		mfTextMaxSize =0;
		mfMaxTextSizeNeg =0;

		mlMaxCharacters = -1;

		mbPressed = false;

		mbCanEdit = true;

		msIllegalChars = _W("");
		
		mbLegalCharCodeLimitEnabled = false;
		mlLegalCharCodeMaxLimit = 0;
		mlLegalCharCodeMinLimit = 0;

		mbChangedSinceLastEnter = false;
		mbForceCallBackOnEnter = false;
		mbCallbackOnLostFocus = true;

		mbHasLowerBound = false;
		mbHasUpperBound = false;
		mfLowerBound = 0;
		mfUpperBound = 0;

		mlDecimals = -1;

		mfNumericAdd = 0.5f;
		mfNumericValue = 0;
		mbNumericValueUpdated = true;

		mbUIKeyboardOpen = false;
		mbGotFocusRecently = false;
		// Loading graphics calls OnChangeSize/OnChangeText, so initialize state first.
		LoadGraphics();
		mpPointerGfx = mpSkin->GetGfx(eGuiSkinGfx_PointerText);
	}

	//-----------------------------------------------------------------------

	cWidgetTextBox::~cWidgetTextBox()
	{
		if(mpSet->IsDestroyingSet()==false)
		{
			for(int i=0;i<2;++i)
				if(mvButtons[i]) mpSet->DestroyWidget(mvButtons[i]);
		}
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetDefaultFontSize(const cVector2f& avSize)
	{
		iWidget::SetDefaultFontSize(avSize);
		OnChangeSize();
	}

	//-----------------------------------------------------------------------
	
	void cWidgetTextBox::SetMaxTextLength(int alLength)
	{
		if(mlMaxCharacters == alLength) return;

		mlMaxCharacters = alLength;

		if(mlMaxCharacters >=0 && CountTextCodepoints(msText) > mlMaxCharacters)
		{
			const int lEnd = TextPrefixUnits(msText,mlMaxCharacters);
			SetText(cString::SubW(msText,0,lEnd));
		}
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetSelectedText(int alStart, int alCount)
	{
		int lTextLen = (int)msText.length();

		if(alStart<0)
			alStart = 0;
		if(alStart>lTextLen)
			alStart = lTextLen;
		if(alCount < 0 || alCount > lTextLen-alStart)
			alCount = lTextLen-alStart;

		mlSelectedTextEnd = ClampTextPosition(msText, alStart);
		SetMarkerPos(alStart+alCount);
	}

	//-----------------------------------------------------------------------

	cVector2f cWidgetTextBox::GetBackgroundSize()
	{
		return cVector2f(mvSize.x - mvGfxCorners[0]->GetActiveSize().x - 
										mvGfxCorners[1]->GetActiveSize().x,
						mvSize.y - mvGfxCorners[0]->GetActiveSize().y -
									mvGfxCorners[2]->GetActiveSize().y);
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetMaxTextSizeNeg(float afX)
	{
		mfMaxTextSizeNeg = afX;

		OnChangeSize();
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetCanEdit(bool abX)
	{
		mbCanEdit = abX;

		if(mbCanEdit)	mpPointerGfx = mpSkin->GetGfx(eGuiSkinGfx_PointerText);
		else			mpPointerGfx = mpSkin->GetGfx(eGuiSkinGfx_PointerNormal);

		for(int i=0;i<2;++i)
		{
			cWidgetButton* pButton = mvButtons[i];
			if(pButton)
				pButton->SetEnabled(mbCanEdit);
		}

	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetNumericValue(float afX)
	{
		if(mInputType!=eWidgetTextBoxInputType_Numeric)
			return;

		if(mbHasLowerBound && afX<mfLowerBound)
			afX = mfLowerBound;
		if(mbHasUpperBound && afX>mfUpperBound)
			afX = mfUpperBound;

		if(mfNumericValue==afX)
			return;

		mfNumericValue = afX;

		tWString sText = cString::ToStringW(mfNumericValue, GetDecimals(), true);
		SetText(sText);

		SetTextUpdated();
		mbNumericValueUpdated = false;
	}

	//-----------------------------------------------------------------------

	float cWidgetTextBox::GetNumericValue()
	{
		if(mbNumericValueUpdated)
		{
			mbNumericValueUpdated = false;
			mfNumericValue = cString::ToFloat(cString::To8Char(msText).c_str(), 0);
			mfNumericValue = cMath::RoundFloatToDecimals(mfNumericValue, GetDecimals());
		}

		return mfNumericValue;
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetLowerBound(bool abX, float afValue)
	{
		mbHasLowerBound = abX;
		mfLowerBound = afValue;
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetUpperBound(bool abX, float afValue)
	{
		mbHasUpperBound = abX;
		mfUpperBound = afValue;
	}

	//-----------------------------------------------------------------------

	int cWidgetTextBox::GetDecimals()
	{
		if(mlDecimals<0)
			return mlDefaultDecimals;

		return mlDecimals;
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetIllegalChars(const tWString &asIllegalChars)
	{
		msIllegalChars = asIllegalChars;
	}

	const tWString& cWidgetTextBox::GetIllegalChars()
	{
		return msIllegalChars;
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetLegalCharCodeLimitEnabled(bool abX)
	{
		mbLegalCharCodeLimitEnabled = abX;
	}
	
	void cWidgetTextBox::SetLegalCharCodeMinLimit(wchar_t alX)
	{
		mlLegalCharCodeMinLimit = alX;
	}
	
	void cWidgetTextBox::SetLegalCharCodeMaxLimit(wchar_t alX)
	{
		mlLegalCharCodeMaxLimit = alX;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PROTECTED METHODS
	//////////////////////////////////////////////////////////////////////////
	
	//-----------------------------------------------------------------------

	bool cWidgetTextBox::Widget_OnValueUp(iWidget* apWidget, const cGuiMessageData& aData)
	{
		ButtonPressed(mvButtons[0], cGuiMessageData());
		return true;
	}
	kGuiCallbackDeclaredFuncEnd(cWidgetTextBox, Widget_OnValueUp);

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::Widget_OnValueDown(iWidget* apWidget, const cGuiMessageData& aData)
	{
		ButtonPressed(mvButtons[1], cGuiMessageData());
		return true;
	}
	kGuiCallbackDeclaredFuncEnd(cWidgetTextBox, Widget_OnValueDown);

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::ButtonPressed(iWidget* apWidget, const cGuiMessageData& aData)
	{
		if(IsEnabled()==false)
			return false;

		float fAdd=0;

		if(apWidget==mvButtons[0])
		{
			fAdd = mfNumericAdd;
		}
		else if(apWidget==mvButtons[1])
		{
			fAdd = -mfNumericAdd;
		}

		if(fAdd==0)
			return true;

		SetNumericValue(GetNumericValue()+fAdd);

		return OnEnter(aData);
	}
	kGuiCallbackDeclaredFuncEnd(cWidgetTextBox, ButtonPressed);

	//-----------------------------------------------------------------------

	int cWidgetTextBox::GetLastCharInSize(int alStartPos, float afMaxSize, float afLengthAdd)
	{
		const wchar_t* pText = msText.c_str() + ClampTextPosition(msText, alStartPos);
		float fLength = 0;
		unsigned int lPrevious = 0;
		while(*pText)
		{
			const int lCharacterStart = (int)(pText - msText.c_str());
			const unsigned int lCodepoint = DecodeFontCodepoint(pText);
			cGlyph* pGlyph = mpDefaultFontType->GetGlyphForCodepoint(lCodepoint);
			if(pGlyph)
			{
				if(lPrevious) fLength += mpDefaultFontType->GetKerning(lPrevious,lCodepoint) * mvDefaultFontSize.x;
				fLength += pGlyph->mfAdvance * mvDefaultFontSize.x;
				if(fLength + afLengthAdd >= afMaxSize) return lCharacterStart;
			}
			lPrevious = pGlyph ? lCodepoint : 0;
		}
		return (int)msText.size();
	}
	
	//-----------------------------------------------------------------------

	int cWidgetTextBox::GetFirstCharInSize(int alStartPos, float afMaxSize, float afLengthAdd)
	{
		float fLength = 0;
		unsigned int lNext = 0;
		// alStartPos is a caret boundary; measure the text before it.
		for(int i = ClampTextPosition(msText, alStartPos); i > 0;)
		{
			i = PreviousTextCodepoint(msText, i);
			const wchar_t* pText = msText.c_str() + i;
			const unsigned int lCodepoint = DecodeFontCodepoint(pText);
			cGlyph* pGlyph = mpDefaultFontType->GetGlyphForCodepoint(lCodepoint);
			if(pGlyph)
			{
				if(lNext) fLength += mpDefaultFontType->GetKerning(lCodepoint,lNext) * mvDefaultFontSize.x;
				fLength += pGlyph->mfAdvance * mvDefaultFontSize.x;
				if(fLength + afLengthAdd >= afMaxSize) return i;
			}
			lNext = pGlyph ? lCodepoint : 0;
		}
		return -1;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::WidgetConsiderSomeCharsIllegal()
	{
		if(msIllegalChars.length()>0) return true;
		if(mbLegalCharCodeLimitEnabled) return true;
		return false;
	}
	
	bool cWidgetTextBox::IsIllegalChar(unsigned int alChar)
	{
		const wchar_t* pText = msIllegalChars.c_str();
		while(*pText)
			if(DecodeFontCodepoint(pText) == alChar) return true;

        if(mbLegalCharCodeLimitEnabled)
		{
			if(alChar > (unsigned int)mlLegalCharCodeMaxLimit) return true;
			if(alChar < (unsigned int)mlLegalCharCodeMinLimit) return true;
		}
		return false;
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetTextUpdated()
	{
		mbChangedSinceLastEnter = true;

		if(mInputType==eWidgetTextBoxInputType_Numeric)
			mbNumericValueUpdated = true;
	}

	//-----------------------------------------------------------------------


	int cWidgetTextBox::WorldToCharPos(const cVector2f &avWorldPos)
	{
		float fTextPos =	WorldToLocalPosition(avWorldPos).x - 
							mvGfxCorners[0]->GetActiveSize().x + 3;

		int lMarkerCharPos;
		if(fTextPos >0)
		{
			lMarkerCharPos = GetLastCharInSize(mlFirstVisibleChar,fTextPos,3.0f);
		}
		else
		{
			lMarkerCharPos =mlFirstVisibleChar;
		}

		return lMarkerCharPos;
	}

	//-----------------------------------------------------------------------

	float cWidgetTextBox::CharToLocalPos(int alChar)
	{
		float fMarkerPos = -2;
		if(alChar>0 && alChar - mlFirstVisibleChar >0)
		{
			fMarkerPos = mpDefaultFontType->GetLength(mvDefaultFontSize,
				cString::SubW(msText,mlFirstVisibleChar,alChar-mlFirstVisibleChar).c_str());
		}
		return fMarkerPos;
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::SetMarkerPos(int alPos)
	{
		mlMarkerCharPos = ClampTextPosition(msText, alPos);

		if(mlMarkerCharPos > mlFirstVisibleChar + mlVisibleCharSize)
		{
			const int lFirstExcluded = GetFirstCharInSize(mlMarkerCharPos,mfTextMaxSize,0);
			mlFirstVisibleChar = lFirstExcluded < 0 ? 0 : NextTextCodepoint(msText,lFirstExcluded);
			if(msText.size()<=1) mlFirstVisibleChar =0;
			OnChangeText();
		}
		else if(mlMarkerCharPos < mlFirstVisibleChar)
		{
			mlFirstVisibleChar = mlMarkerCharPos;
			OnChangeText();
		}

	}
	
	//-----------------------------------------------------------------------

	void cWidgetTextBox::OnChangeSize()
	{
		mvSize.y =	mvGfxBorders[0]->GetActiveSize().y +
					mvGfxBorders[2]->GetActiveSize().y +
					mvDefaultFontSize.y + 2 * 2;

		float fButtonWidth = 0;
		if(mInputType==eWidgetTextBoxInputType_Numeric && mvButtons[0])
			fButtonWidth = mvButtons[0]->GetSize().x;

		mfTextMaxSize = mvSize.x - mvGfxBorders[0]->GetActiveSize().x -
						mvGfxBorders[1]->GetActiveSize().x  - 3 * 2 -
						mfMaxTextSizeNeg - fButtonWidth;

		OnChangeText();
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::OnChangeText()
	{
		if(mlMaxCharacters >= 0 && CountTextCodepoints(msText) > mlMaxCharacters)
			msText.resize(TextPrefixUnits(msText, mlMaxCharacters));
		if(mlMarkerCharPos >= 0) mlMarkerCharPos = ClampTextPosition(msText, mlMarkerCharPos);
		if(mlSelectedTextEnd >= 0) mlSelectedTextEnd = ClampTextPosition(msText, mlSelectedTextEnd);
		if(mlFirstVisibleChar >= (int)msText.size()) mlFirstVisibleChar = 0;
		if(mlFirstVisibleChar > 0 && IsLowSurrogate(msText[mlFirstVisibleChar]) &&
			IsHighSurrogate(msText[mlFirstVisibleChar-1])) --mlFirstVisibleChar;
		if(msText == _W(""))
			mlVisibleCharSize = 0;
		else
			mlVisibleCharSize = GetLastCharInSize(	mlFirstVisibleChar,mfTextMaxSize,0) - 
													mlFirstVisibleChar;

		if(mbTextChanged && mInputType == eWidgetTextBoxInputType_Numeric)
		{
			mbNumericValueUpdated = true;
		}
	}
	
	//-----------------------------------------------------------------------

	void cWidgetTextBox::OnInit()
	{
		if(mInputType!=eWidgetTextBoxInputType_Numeric)
			return;

		if(mbShowButtons==false)
			return;

		AddCallback(eGuiMessage_TextBoxValueUp, this, kGuiCallback(Widget_OnValueUp));
		AddCallback(eGuiMessage_TextBoxValueDown, this, kGuiCallback(Widget_OnValueDown));

        for(int i=0;i<2;++i)
		{
			mvButtons[i] = mpSet->CreateWidgetButton(cVector3f(mvSize.x-10,1 + i*10.0f,0.1f), cVector2f(10), _W(""), this);
			mvButtons[i]->SetDefaultFontSize(12);
			mvButtons[i]->SetRepeatActive(true);
			mvButtons[i]->SetRepeatFreq(3);
			mvButtons[i]->AddCallback(eGuiMessage_ButtonPressed,this,kGuiCallback(ButtonPressed));
		}

		mvButtons[0]->SetText(_W("+"));
		mvButtons[1]->SetText(_W("-"));

		// Init numeric value + text
		mfNumericValue = -1;
		SetNumericValue(0);
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::OnLoadGraphics()
	{
		mpGfxMarker = mpSkin->GetGfx(eGuiSkinGfx_TextBoxMarker);
		mpGfxSelectedTextBack = mpSkin->GetGfx(eGuiSkinGfx_TextBoxSelectedTextBack);

		mpGfxBackground = mpSkin->GetGfx(eGuiSkinGfx_TextBoxBackground);

		mvGfxBorders[0] = mpSkin->GetGfx(eGuiSkinGfx_FrameBorderRight);
		mvGfxBorders[1] = mpSkin->GetGfx(eGuiSkinGfx_FrameBorderLeft);
		mvGfxBorders[2] = mpSkin->GetGfx(eGuiSkinGfx_FrameBorderUp);
		mvGfxBorders[3] = mpSkin->GetGfx(eGuiSkinGfx_FrameBorderDown);

		mvGfxCorners[0] = mpSkin->GetGfx(eGuiSkinGfx_FrameCornerLU);
		mvGfxCorners[1] = mpSkin->GetGfx(eGuiSkinGfx_FrameCornerRU);
		mvGfxCorners[2] = mpSkin->GetGfx(eGuiSkinGfx_FrameCornerRD);
		mvGfxCorners[3] = mpSkin->GetGfx(eGuiSkinGfx_FrameCornerLD);

		OnChangeSize();
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::OnDraw(float afTimeStep, cGuiClipRegion *apClipRegion)
	{
		////////////////////////////////
		// Text
		cColor col = IsEnabled()? mDefaultFontColor : mpSkin->GetFont(eGuiSkinFont_Disabled)->mColor;

		cVector3f vTextAdd = cVector3f(3,2,0.2f) + mvGfxCorners[0]->GetActiveSize();
		DrawDefaultText(cString::SubW(msText,mlFirstVisibleChar,mlVisibleCharSize),
						GetGlobalPosition()+ vTextAdd,
						eFontAlign_Left,col);

		//Marker
		if(mlMarkerCharPos >=0)
		{
			float fMarkerPos = CharToLocalPos(mlMarkerCharPos);
			mpSet->DrawGfx(	mpGfxMarker,GetGlobalPosition() + vTextAdd + cVector3f(fMarkerPos,0,0.1f),
							cVector2f(2, mvDefaultFontSize.y));

			//Selected text
			if(mlSelectedTextEnd >=0)
			{
				int lStart = mlMarkerCharPos < mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
				int lEnd = mlMarkerCharPos > mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
				int lSelectSize = lEnd - lStart;

				int lHighlightStart = mlFirstVisibleChar > lStart ? mlFirstVisibleChar : lStart;
				int lHighlightEnd =  mlFirstVisibleChar+mlVisibleCharSize < lEnd ? mlFirstVisibleChar+mlVisibleCharSize : lEnd;
				int lHighlightSize = lHighlightEnd - lHighlightStart;

				float fSelectEnd = CharToLocalPos(mlSelectedTextEnd);

				float fPos = fSelectEnd < fMarkerPos ? fSelectEnd : fMarkerPos;
				float fEnd = fSelectEnd > fMarkerPos ? fSelectEnd : fMarkerPos;
				
				if(fPos <0)fPos =0;
				if(fEnd > mfTextMaxSize) fEnd = mfTextMaxSize;
				
				float fSize = fEnd - fPos;

				mpSet->DrawGfx( mpGfxSelectedTextBack, GetGlobalPosition() + 
								vTextAdd + cVector3f(fPos,0,0.01f),
								cVector2f(fSize,mvDefaultFontSize.y));

				if(lHighlightSize > 0)
				{
					// Redrawing a selected substring must retain kerning across its
					// left boundary, or the highlighted glyphs shift over the originals.
					float fHighlightPos = fPos;
					if(lHighlightStart > mlFirstVisibleChar)
					{
						const wchar_t* pPrevious = msText.c_str() + PreviousTextCodepoint(msText,lHighlightStart);
						const wchar_t* pCurrent = msText.c_str() + lHighlightStart;
						const unsigned int lPrevious = DecodeFontCodepoint(pPrevious);
						const unsigned int lCurrent = DecodeFontCodepoint(pCurrent);
						if(mpDefaultFontType->GetGlyphForCodepoint(lPrevious) && mpDefaultFontType->GetGlyphForCodepoint(lCurrent))
							fHighlightPos += mpDefaultFontType->GetKerning(lPrevious,lCurrent) * mvDefaultFontSize.x;
					}
					DrawDefaultTextHighlight(cString::SubW(msText,lHighlightStart,lHighlightSize),
						GetGlobalPosition() + vTextAdd + cVector3f(fHighlightPos,0,0.02f), eFontAlign_Left);
				}
			}
		}

		
		////////////////////////////////
		// Background and Borders
		DrawBordersAndCorners(	mpGfxBackground, mvGfxBorders, mvGfxCorners, 
								GetGlobalPosition(), mvSize);
	}

	//-----------------------------------------------------------------------
	
	bool cWidgetTextBox::OnMouseMove(const cGuiMessageData& aData)
	{
		if(mbPressed)
		{
			int lPos = WorldToCharPos(aData.mvPos);
			if(lPos != mlMarkerCharPos)
			{
				if(mlSelectedTextEnd==-1) mlSelectedTextEnd = mlMarkerCharPos;
				SetMarkerPos(lPos);
			}
		}

		return true;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::OnMouseDown(const cGuiMessageData& aData)
	{
		if((aData.mlVal & eGuiMouseButton_Left) == 0) return false;
		if(mbCanEdit==false) return false;

		SetMarkerPos(WorldToCharPos(aData.mvPos));
		mlSelectedTextEnd=-1;

		mbPressed = true;
		mpPrevAttention = mpSet->GetAttentionWidget();
		mpSet->SetAttentionWidget(this);

		return true;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::OnMouseUp(const cGuiMessageData& aData)
	{
		if((aData.mlVal &  eGuiMouseButton_Left) == 0) return true;
		if(mbCanEdit==false) return true;

		if(mbGotFocusRecently)
		{
			int lPos = WorldToCharPos(aData.mvPos);
			if(mlSelectedTextEnd==-1 || mlSelectedTextEnd==lPos)
			{
				SetMarkerPos((int)msText.size());
				mlSelectedTextEnd=0;
			}

			mbGotFocusRecently = false;
		}

		mbPressed = false;

		// SetAttention steals focus, so restore vars after it
		int lMarker = mlMarkerCharPos;
		int lSelectEnd = mlSelectedTextEnd;

		mpSet->SetAttentionWidget(mpPrevAttention, false);

		mbGotFocusRecently = false;

		SetMarkerPos(lMarker);
		mlSelectedTextEnd = lSelectEnd;

		return true;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::OnMouseDoubleClick(const cGuiMessageData& aData)
	{
		if((aData.mlVal & eGuiMouseButton_Left) == 0) return true;
		if(mbCanEdit==false) return true;

        SetMarkerPos(WorldToCharPos(aData.mvPos));

		if(mlMarkerCharPos!=msText.size() && msText[mlMarkerCharPos] == _W(' ')) return true;

		/////////////////////////////
		//Get space to the right.
		for(mlSelectedTextEnd=mlMarkerCharPos; mlSelectedTextEnd>0; --mlSelectedTextEnd)
		{
			if(msText[mlSelectedTextEnd-1] == _W(' ') )
				break;
		}

		/////////////////////////////
		//Get space to the left
		for(size_t i=mlMarkerCharPos; i<msText.size(); ++i)
		{
			if(msText[i] == _W(' ') || i==msText.size()-1)
			{
				if(i==(int)msText.size()-1) SetMarkerPos((int)i+1);
				else						SetMarkerPos((int)i);
				break;
			}
		}
		
		mbPressed = false;
		
		return true;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::OnMouseEnter(const cGuiMessageData& aData)
	{
		return true;
	}
	
	//-----------------------------------------------------------------------
	
	bool cWidgetTextBox::OnMouseLeave(const cGuiMessageData& aData)
	{
		//mbPressed = false;

		return false;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::OnGotFocus(const cGuiMessageData& aData)
	{
		mbGotFocusRecently = true;
		//SetSelectedText();

		return true;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::OnGotTabFocus(const cGuiMessageData& aData)
	{
		//SetSelectedText();
		mlMarkerCharPos = (int)msText.size();
		mlSelectedTextEnd = 0;
		mbGotFocusRecently = false;

		return true;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::OnLostFocus(const cGuiMessageData& aData)
	{
		// TODO : better change this to call some OnLostFocus callback
		if(mbCallbackOnLostFocus)
			OnEnter(aData);

		mbPressed = false;
		mlMarkerCharPos = -1;
		mlSelectedTextEnd = -1;
		return false;
	}

	//-----------------------------------------------------------------------
	
	bool cWidgetTextBox::OnKeyPress(const cGuiMessageData& aData)
	{
		// XXX = maybe does not have sense to capture keypresses if edit is disabled?
		if(mbCanEdit==false) return false;
		if(mlMarkerCharPos <0) return false;
		
		eKey key = aData.mKeyPress.mKey;
		int mod = aData.mKeyPress.mlModifier;

		if(mpGfxMarker)mpGfxMarker->SetAnimationTime(0);
 
		//////////////////////////////
		//Enter
		if(key == eKey_Return || key == eKey_KP_Enter)
		{
			return OnEnter(aData);
		}
		//////////////////////////////
		//Copy / Pase / Cut
		else if(mod & eKeyModifier_Ctrl)
		{
			int lStart = mlMarkerCharPos < mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
			int lEnd = mlMarkerCharPos > mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
			int lSelectSize = lEnd - lStart;
			
			/////////////////////////////
			// Select all
			if(key == eKey_A)
			{
				mlSelectedTextEnd = 0;
				mlMarkerCharPos = (int)msText.size(); // Marker must be after the last character.

				return true;
			}
			/////////////////////////////
			// Copy
			else if(key == eKey_C)
			{
				if(mlSelectedTextEnd >=0)
					cPlatform::CopyTextToClipboard(cString::SubW(msText,lStart, lSelectSize));

				return true;
			}
			/////////////////////////////
			// Cut
			else if(key == eKey_X)
			{
				if(mlSelectedTextEnd >=0)
				{
					cPlatform::CopyTextToClipboard(cString::SubW(msText,lStart, lSelectSize));
					SetText(cString::SubW(msText,0,	lStart) + cString::SubW(msText,lEnd));
					mlSelectedTextEnd = -1;
					SetMarkerPos(lStart);

					SetTextUpdated();
				}

				return true;
			}
			/////////////////////////////
			// Paste
			else if(key == eKey_V)
			{
				tWString sExtra = cPlatform::LoadTextFromClipboard();
				if(sExtra.empty())
					return true;

				//Make sure extra does not contain illegal character
				if(WidgetConsiderSomeCharsIllegal())
				{
					tWString sTempExtra = sExtra;
					sExtra = _W("");
					const wchar_t* pExtra = sTempExtra.c_str();
					while(*pExtra)
					{
						const unsigned int lCodepoint = DecodeFontCodepoint(pExtra);
						if(!IsIllegalChar(lCodepoint))
							sExtra += EncodeTextCodepoint(lCodepoint);
					}
				}
				
				if(mlSelectedTextEnd <0)
				{
					if(	mlMaxCharacters ==-1 ||
						CountTextCodepoints(msText) + CountTextCodepoints(sExtra) <= mlMaxCharacters)
					{
						SetText(cString::SubW(msText,0,	mlMarkerCharPos)+ sExtra +
								cString::SubW(msText,mlMarkerCharPos) );

						SetMarkerPos(mlMarkerCharPos+(int)sExtra.size());
					}
				}
				else
				{
					if(	mlMaxCharacters < 0 ||
						CountTextCodepoints(msText) - CountTextCodepoints(msText.substr(lStart,lSelectSize)) +
						CountTextCodepoints(sExtra) <= mlMaxCharacters)
					{
						SetText(cString::SubW(msText,0,	lStart) + sExtra + 
								cString::SubW(msText,lEnd));

						mlSelectedTextEnd = -1;
						SetMarkerPos(lStart+(int)sExtra.size());
					}
				}

				SetTextUpdated();

				return true;
			}
		}
		//////////////////////////////
		//Arrow keys		
		else if(key == eKey_Left || key == eKey_Right)
		{
			if(mod & eKeyModifier_Shift)
			{
				if(mlSelectedTextEnd==-1) 
					mlSelectedTextEnd = mlMarkerCharPos;

				if(key == eKey_Left)	SetMarkerPos(PreviousTextCodepoint(msText,mlMarkerCharPos));
				else					SetMarkerPos(NextTextCodepoint(msText,mlMarkerCharPos));
			}
			else
			{
				if(key == eKey_Left)	SetMarkerPos(PreviousTextCodepoint(msText,mlMarkerCharPos));
				else					SetMarkerPos(NextTextCodepoint(msText,mlMarkerCharPos));

				mlSelectedTextEnd = -1;
			}

			return true;
		}
		//////////////////////////////
		//Delete and backspace
		else if(key == eKey_Delete || key == eKey_BackSpace)
		{
            if(mlSelectedTextEnd >=0)
			{
				int lStart = mlMarkerCharPos < mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
				int lEnd = mlMarkerCharPos > mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;

				SetText(cString::SubW(msText,0,	lStart) + cString::SubW(msText,lEnd));

				mlSelectedTextEnd = -1;
				SetMarkerPos(lStart);

				SetTextUpdated();
			}
			else
			{
				if(key == eKey_Delete)
				{
					SetText(cString::SubW(msText,0,	mlMarkerCharPos)+ 
							cString::SubW(msText,NextTextCodepoint(msText,mlMarkerCharPos)));

					SetTextUpdated();
				}
				else
				{
					if(mlMarkerCharPos!=0)
					{
						const int lPrevious = PreviousTextCodepoint(msText,mlMarkerCharPos);
						SetText(cString::SubW(msText,0,lPrevious)+
								cString::SubW(msText,mlMarkerCharPos));
						SetMarkerPos(lPrevious);

						SetTextUpdated();
					}
				}
			}

			return true;
		}
		//////////////////////////////
		//Home
		else if(key == eKey_Home)
		{
			if(mod & eKeyModifier_Shift)
			{
				if(mlSelectedTextEnd==-1) mlSelectedTextEnd = mlMarkerCharPos;
			}
			else
			{
				mlSelectedTextEnd = -1;
			}
			SetMarkerPos(0);


			return true;
		}
		//////////////////////////////
		//End
		else if(key == eKey_End)
		{
			if(mod & eKeyModifier_Shift)
			{
				if(mlSelectedTextEnd==-1) mlSelectedTextEnd = mlMarkerCharPos;
			}
			else
			{
				mlSelectedTextEnd = -1;
			}
			SetMarkerPos((int)msText.size());


			return true;
		}
		//////////////////////////////////
		// Character
		else 
		{
			//////////////////////////////
			// Set up data
			const unsigned int lCodepoint = (unsigned int)aData.mKeyPress.mlUnicode;
			const tWString sUnicode = EncodeTextCodepoint(lCodepoint);
			if(sUnicode.empty() || lCodepoint == 0) return true;
			wchar_t unicode = lCodepoint <= 0xFFFF ? (wchar_t)lCodepoint : 0;
			wchar_t lFirstNum = _W('0');
			wchar_t lLastNum = _W('9');

			//////////////////////
			// Check so character is not illegal
			if(IsIllegalChar(lCodepoint)) return true;
			
			///////////////////////////
			// Numerical Input
			if(mInputType == eWidgetTextBoxInputType_Numeric)
			{
				if(	((unicode >= lFirstNum && unicode <= lLastNum) ||
					unicode == _W('.') || unicode == _W('-')) &&
					mpDefaultFontType->GetGlyphForCodepoint((unsigned int)unicode))
				{
					if(mlSelectedTextEnd<0)
					{
						if(unicode==_W('-'))
						{
							if(mlMarkerCharPos==0)
							{
								if(msText.empty() || msText[0]!=_W('-'))
								{
									SetText(unicode + cString::SubW(msText,0));
									SetMarkerPos(mlMarkerCharPos+1);

									SetTextUpdated();
								}
							}
						}
						else if(unicode==_W('.'))
						{
							if(mlDecimals!=0 && cString::CountCharsInStringW(msText,_W("."))==0)
							{
								SetText(cString::SubW(msText,0,mlMarkerCharPos) + unicode +
										cString::SubW(msText,mlMarkerCharPos) );
								SetMarkerPos(mlMarkerCharPos+1);

								SetTextUpdated();
							}
						}
						else
						{
							
							SetText(cString::SubW(msText,0,mlMarkerCharPos) + unicode +
										cString::SubW(msText,mlMarkerCharPos) );
							SetMarkerPos(mlMarkerCharPos+1);

							SetTextUpdated();
						}
					}
					else
					{
						int lStart = mlMarkerCharPos < mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
						int lEnd = mlMarkerCharPos > mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
					
						if(unicode==_W('-'))
						{
							if(lStart==0)
							{
								SetText(unicode + cString::SubW(msText,lEnd));

								SetTextUpdated();
							}
						}
						else if(unicode==_W('.'))
						{
							int lNumDots = cString::CountCharsInStringW(msText,_W("."));
							int lDotPos = cString::GetLastStringPosW(msText,_W("."));

							if(	lNumDots==0 ||
								lNumDots==1 && 
								lDotPos >=lStart && lDotPos <= lEnd)
							{
								SetText(cString::SubW(msText,0,lStart) + unicode +
										cString::SubW(msText,lEnd));

								SetTextUpdated();
							}
						}
						else
						{
							SetText(cString::SubW(msText,0,lStart) + unicode +
										cString::SubW(msText,lEnd));

							SetTextUpdated();
						}
					
						mlSelectedTextEnd = -1;

						SetMarkerPos(lStart+1);
					}
				}
			}
			///////////////////////////
			// Normal text input
			else
			{
				if(mpDefaultFontType->GetGlyphForCodepoint(lCodepoint))
				{
					if(	mlSelectedTextEnd <0)
					{
						if(mlMaxCharacters ==-1 || CountTextCodepoints(msText) < mlMaxCharacters)
						{
							SetText(cString::SubW(msText,0,	mlMarkerCharPos)+ sUnicode +
									cString::SubW(msText,mlMarkerCharPos) );		

							SetMarkerPos(mlMarkerCharPos+(int)sUnicode.size());

							SetTextUpdated();
						}
					}
					else
					{
						int lStart = mlMarkerCharPos < mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
						int lEnd = mlMarkerCharPos > mlSelectedTextEnd ? mlMarkerCharPos : mlSelectedTextEnd;
						if(mlMaxCharacters >= 0 && CountTextCodepoints(msText) -
							CountTextCodepoints(msText.substr(lStart,lEnd-lStart)) + 1 > mlMaxCharacters) return true;
						SetText(cString::SubW(msText,0,	lStart) + sUnicode +
								cString::SubW(msText,lEnd));

						mlSelectedTextEnd = -1;

						SetMarkerPos(lStart+(int)sUnicode.size());

						SetTextUpdated();
					}
				}
			}

			return true;
		}
		
		return false;
	}

	//-----------------------------------------------------------------------
	
	bool cWidgetTextBox::OnUIButtonPress(const cGuiMessageData& aData)
	{
		// XXX = maybe does not have sense to capture keypresses if edit is disabled?
		if(mbCanEdit==false) return false;
		
		if(HasFocus() && aData.mlVal==eUIButton_Primary)
		{
			mlSelectedTextEnd = 0;
			SetMarkerPos((int)msText.length());
			mpSet->CreatePopUpUIKeyboard(this);

			return true;
		}
		
		return false;
	}

	//-----------------------------------------------------------------------

	bool cWidgetTextBox::OnEnter(const cGuiMessageData& aData)
	{
		if(mInputType==eWidgetTextBoxInputType_Numeric)
		{
			SetText(cString::ToStringW(GetNumericValue(), GetDecimals(), true));
			mbNumericValueUpdated = false;
		}

		if(mbChangedSinceLastEnter || mbForceCallBackOnEnter)
		{
			mbChangedSinceLastEnter = false;
			return ProcessMessage(eGuiMessage_TextBoxEnter, aData);
		}

		return true;
	}

	//-----------------------------------------------------------------------

	void cWidgetTextBox::OnUIKeyboardOpen()
	{
		mbUIKeyboardOpen = true;

		mlSelectedTextEnd = 0;
		SetMarkerPos((int)msText.length());
	}
	
	//-----------------------------------------------------------------------

	void cWidgetTextBox::OnUIKeyboardClose()
	{
		mbUIKeyboardOpen = false;
		OnLostFocus(cGuiMessageData());
	}
	
	//-----------------------------------------------------------------------
	
}
