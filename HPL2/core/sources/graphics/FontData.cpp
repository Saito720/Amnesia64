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

#include "graphics/FontData.h"
#include <stdarg.h>
#include <stdlib.h>

#include "system/LowLevelSystem.h"

#include "resources/Resources.h"
#include "graphics/FrameSubImage.h"
#include "resources/ImageManager.h"

#include "gui/GuiGfxElement.h"
#include "gui/Gui.h"

namespace hpl {
	unsigned int DecodeFontCodepoint(const wchar_t*& apText)
	{
		unsigned int lCodepoint = (unsigned int)*apText++;
		if(sizeof(wchar_t) == 2)
		{
			if(lCodepoint >= 0xD800 && lCodepoint <= 0xDBFF)
			{
				unsigned int lLow = (unsigned int)*apText;
				if(lLow >= 0xDC00 && lLow <= 0xDFFF)
				{
					++apText;
					return 0x10000 + ((lCodepoint - 0xD800) << 10) + (lLow - 0xDC00);
				}
				return 0xFFFD;
			}
		}
		return (lCodepoint >= 0xD800 && lCodepoint <= 0xDFFF) ||
			lCodepoint > 0x10FFFF ? 0xFFFD : lCodepoint;
	}


	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	
	//-----------------------------------------------------------------------

	cGlyph::cGlyph(	cGuiGfxElement *apGuiGfx,const cVector2f &avOffset, const cVector2f &avSize, float afAdvance)
	{
		mpGuiGfx = apGuiGfx;
		mvOffset = avOffset;
		mvSize = avSize;
		mfAdvance = afAdvance;
	}
		


	cGlyph::~cGlyph()
	{ 
		if(mpGuiGfx) hplDelete(mpGuiGfx);
	
	}

	//-----------------------------------------------------------------------
	
	iFontData::iFontData(const tString &asName,iLowLevelGraphics* apLowLevelGraphics) : iResourceBase(asName,_W(""),0)
	{
		mpLowLevelGraphics = apLowLevelGraphics;
		mpResources = NULL;
	}
	
	//-----------------------------------------------------------------------

	iFontData::~iFontData()
	{
		for(int i=0;i<(int)mvGlyphs.size();i++)
		{
			if(mvGlyphs[i]) hplDelete(mvGlyphs[i]);
		}
	}

	cGlyph* iFontData::GetGlyphForCodepoint(unsigned int alCodepoint)
	{
		if(alCodepoint < mlFirstChar || alCodepoint > mlLastChar) return NULL;
		return GetGlyph((int)(alCodepoint - mlFirstChar));
	}

	float iFontData::GetKerning(unsigned int, unsigned int) const
	{
		return 0;
	}

	
	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	/*void iFontData::Draw(const cVector3f& avPos,const cVector2f& avSize, const cColor& aCol,
						eFontAlign aAlign,	const wchar_t* fmt,...)
	{
		wchar_t sText[256];
		va_list ap;	
		if (fmt == NULL) return;	
		va_start(ap, fmt);
		vswprintf(sText, 255, fmt, ap);
		va_end(ap);


		int lCount=0;
		float lXAdd =0;
		cVector3f vPos = avPos;

		if(aAlign == eFontAlign_Center){
			vPos.x -= GetLength(avSize, sText)/2;
		}
		else if(aAlign == eFontAlign_Right)
		{
			vPos.x -= GetLength(avSize, sText);
		}

		while(sText[lCount] != 0)
		{
			wchar_t lGlyphNum = ((wchar_t)sText[lCount]);
			if(lGlyphNum<mlFirstChar || lGlyphNum>mlLastChar){
				lCount++;
				continue;
			}
			lGlyphNum -= mlFirstChar;


			cGlyph *pGlyph = mvGlyphs[lGlyphNum];
			if(pGlyph)
			{
				cVector2f vOffset(pGlyph->mvOffset * avSize);
				cVector2f vSize(pGlyph->mvSize * avSize);

				mpGraphicsDrawer->DrawGfxObject(pGlyph->mpGfxObject,vPos + vOffset,vSize,aCol);

				vPos.x += pGlyph->mfAdvance*avSize.x; 
			}
			lCount++;
		}
	}*/
	
	//-----------------------------------------------------------------------

	/*int iFontData::DrawWordWrap(cVector3f avPos,float afLength,float afFontHeight,cVector2f avSize,const cColor& aCol,
								eFontAlign aAlign,	const tWString &asString)
	{
		int rows = 0;

		unsigned int pos;
		unsigned int first_letter=0;
		unsigned int last_space=0;

		tUIntList RowLengthList;
		
		float fTextLength;

		for(pos = 0; pos < asString.size();pos++)
		{
			if(asString[pos] == _W(' ') || asString[pos] == _W('\n'))
			{
				tWString temp = asString.substr(first_letter, pos-first_letter);
				fTextLength =  GetLength(avSize,temp.c_str());

				bool nothing = true;
				if(fTextLength > afLength)
				{
					rows++;
					RowLengthList.push_back(last_space);
					first_letter=last_space+1;
					last_space = pos;
					nothing = false;
				}
				if(asString[pos] == _W('\n'))
				{
					last_space = pos;
					first_letter=last_space+1;
					RowLengthList.push_back(last_space-1);
					rows++;
					nothing = false;
				}
				if(nothing)
				{
					last_space = pos;
				}
			}
		}
		tWString temp =  asString.substr(first_letter, pos-first_letter);
		fTextLength = GetLength(avSize,temp.c_str());
		if(fTextLength > afLength)
		{
			rows++;
			RowLengthList.push_back(last_space);
		}

		if(rows==0)
		{
			Draw(avPos,avSize,aCol,aAlign,_W("%ls"),asString.c_str());
		}
		else
		{
			first_letter=0;
			unsigned int i=0;
			
			for(tUIntListIt it = RowLengthList.begin();it != RowLengthList.end();++it)
			{
				Draw(cVector3f(avPos.x,avPos.y + i*afFontHeight,avPos.z),avSize,aCol,aAlign,
								_W("%ls"),asString.substr(first_letter,*it-first_letter).c_str());
				i++;
				first_letter = *it+1;
			}
			Draw(cVector3f(avPos.x,avPos.y + i*afFontHeight,avPos.z),avSize,aCol,aAlign,
				_W("%ls"),asString.substr(first_letter).c_str());

		}

		return rows;
	}*/

	//-----------------------------------------------------------------------

	static bool IsFontWrapSpace(unsigned int alCodepoint)
	{
		return alCodepoint == ' ' || alCodepoint == '\t' || alCodepoint == 0x3000;
	}

	static bool IsFontIdeograph(unsigned int alCodepoint)
	{
		return (alCodepoint >= 0x3400 && alCodepoint <= 0x9FFF) ||
			(alCodepoint >= 0xF900 && alCodepoint <= 0xFAFF) ||
			(alCodepoint >= 0x20000 && alCodepoint <= 0x323AF) ||
			(alCodepoint >= 0x3040 && alCodepoint <= 0x30FF) ||
			(alCodepoint >= 0xAC00 && alCodepoint <= 0xD7AF);
	}

	void iFontData::GetWordWrapRows(float afLength,float afFontHeight,cVector2f avSize,
							const tWString& asString,tWStringVec *apRowVec)
	{
		(void)afFontHeight;
		if(apRowVec == NULL) return;
		const size_t lInitialRows = apRowVec->size();
		if(asString.empty())
		{
			apRowVec->push_back(_W(""));
			return;
		}

		const wchar_t* pBase = asString.c_str();
		const size_t lLength = asString.size();
		size_t lStart = 0;
		bool bTrailingNewline = false;
		while(lStart < lLength)
		{
			size_t lPos = lStart;
			size_t lBreak = tWString::npos;
			size_t lResume = tWString::npos;
			bool bHasContent = false;
			bool bBreakHasContent = false;
			float fWidth = 0;
			unsigned int lPrevious = 0;
			bool bFinishedRow = false;
			while(lPos < lLength)
			{
				const wchar_t* pText = pBase + lPos;
				const unsigned int lCodepoint = DecodeFontCodepoint(pText);
				const size_t lNext = (size_t)(pText - pBase);
				if(lCodepoint == '\r' || lCodepoint == '\n')
				{
					apRowVec->push_back(asString.substr(lStart, lPos-lStart));
					lStart = lNext;
					if(lCodepoint == '\r' && lStart < lLength && asString[lStart] == _W('\n')) ++lStart;
					bTrailingNewline = lStart == lLength;
					bFinishedRow = true;
					break;
				}

				cGlyph* pGlyph = GetGlyphForCodepoint(lCodepoint);
				float fAdvance = pGlyph ? pGlyph->mfAdvance * avSize.x : 0;
				if(pGlyph && lPrevious) fAdvance += GetKerning(lPrevious,lCodepoint) * avSize.x;
				if(afLength > 0 && IsFontWrapSpace(lCodepoint) && fWidth + fAdvance > afLength)
				{
					if(bHasContent) apRowVec->push_back(asString.substr(lStart, lPos-lStart));
					lStart = lNext;
					while(lStart < lLength && IsFontWrapSpace((unsigned int)asString[lStart])) ++lStart;
					bFinishedRow = true;
					break;
				}
				if(afLength > 0 && fWidth + fAdvance > afLength)
				{
					if(lPos == lStart)
					{
						apRowVec->push_back(asString.substr(lStart, lNext-lStart));
						lStart = lNext;
					}
					else if(lBreak != tWString::npos)
					{
						if(bBreakHasContent) apRowVec->push_back(asString.substr(lStart, lBreak-lStart));
						lStart = lResume;
						while(lStart < lLength && IsFontWrapSpace((unsigned int)asString[lStart])) ++lStart;
					}
					else
					{
						apRowVec->push_back(asString.substr(lStart, lPos-lStart));
						lStart = lPos;
					}
					bFinishedRow = true;
					break;
				}
				fWidth += fAdvance;
				lPrevious = pGlyph ? lCodepoint : 0;
				if(!IsFontWrapSpace(lCodepoint)) bHasContent = true;
				if(IsFontWrapSpace(lCodepoint))
				{
					lBreak = lPos;
					lResume = lNext;
					bBreakHasContent = bHasContent;
				}
				else if(IsFontIdeograph(lCodepoint))
				{
					lBreak = lNext;
					lResume = lNext;
				}
				lPos = lNext;
			}
			if(!bFinishedRow)
			{
				apRowVec->push_back(asString.substr(lStart));
				break;
			}
		}
		if(bTrailingNewline || apRowVec->size() == lInitialRows) apRowVec->push_back(_W(""));
	}
	
	//-----------------------------------------------------------------------
	
	float iFontData::GetLength(const cVector2f& avSize,const wchar_t* sText)
	{
		if(sText == NULL) return 0;
		float fLength = 0;
		unsigned int lPrevious = 0;
		while(*sText)
		{
			const unsigned int lCodepoint = DecodeFontCodepoint(sText);
			cGlyph *pGlyph = GetGlyphForCodepoint(lCodepoint);
			if(pGlyph)
			{
				if(lPrevious) fLength += GetKerning(lPrevious, lCodepoint) * avSize.x;
				fLength += pGlyph->mfAdvance * avSize.x;
				lPrevious = lCodepoint;
			}
			else lPrevious = 0;
		}
		return fLength;
	}
	
	//-----------------------------------------------------------------------

	
	float iFontData::GetLengthFmt(const cVector2f& avSize,const wchar_t* fmt,...)
	{
		wchar_t sText[256];
		va_list ap;	
		if (fmt == NULL) return 0;	
		va_start(ap, fmt);
		vswprintf(sText, 255, fmt, ap);
		va_end(ap);

		return GetLength(avSize, sText);
	}
	//-----------------------------------------------------------------------
	
	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////
	
	//-----------------------------------------------------------------------
	
	cGlyph* iFontData::CreateGlyph(	cFrameSubImage* apImage, const cVector2l &avOffset,const cVector2l &avSize,
									const cVector2l& avFontSize, int alAdvance)
	{
		return CreateGlyph(apImage, cVector2f((float)avOffset.x,(float)avOffset.y),
			cVector2f((float)avSize.x,(float)avSize.y),
			cVector2f((float)avFontSize.x,(float)avFontSize.y),(float)alAdvance);
	}

	cGlyph* iFontData::CreateGlyph(	cFrameSubImage* apImage, const cVector2f &avOffset,const cVector2f &avSize,
									const cVector2f& avFontSize, float afAdvance)
	{
		//////////////////////////
		//Gui gfx
		cGuiGfxElement* pGuiGfx = NULL;
		if(apImage)
		{
			pGuiGfx = mpGui->CreateGfxFilledRect(cColor(1,1),eGuiMaterial_FontNormal,false);
			pGuiGfx->AddImage(apImage);
		}
		
		//////////////////////////
		//Sizes
		cVector2f vSize;
		vSize.x = ((float)avSize.x)/((float)avFontSize.x) * mvSizeRatio.x;
		vSize.y = ((float)avSize.y)/((float)avFontSize.y) * mvSizeRatio.y;

		cVector2f vOffset;
		vOffset.x = ((float)avOffset.x)/((float)avFontSize.x) * mvSizeRatio.x;
		vOffset.y = ((float)avOffset.y)/((float)avFontSize.y) * mvSizeRatio.y;
		
		float fAdvance = afAdvance / avFontSize.x * mvSizeRatio.x;
		
		cGlyph* pGlyph = hplNew( cGlyph,(pGuiGfx,vOffset,vSize,fAdvance));

		return pGlyph;
	}
	
	//-----------------------------------------------------------------------
	
	void iFontData::AddGlyph(cGlyph *apGlyph)
	{
		mvGlyphs.push_back(apGlyph);
	}

	

	//-----------------------------------------------------------------------

}
