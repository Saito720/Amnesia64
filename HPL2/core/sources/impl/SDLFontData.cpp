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

#include "impl/SDLFontData.h"

#include "resources/Resources.h"
#include "graphics/LowLevelGraphics.h"
#include "system/LowLevelSystem.h"
#include "resources/BitmapLoaderHandler.h"
#include "graphics/Bitmap.h"
#include "resources/ImageManager.h"
#include "graphics/FrameTexture.h"
#include "graphics/FrameSubImage.h"
#include "graphics/Texture.h"

#include "impl/tinyXML/tinyxml.h"

#include "system/String.h"
#include "system/Platform.h"

namespace hpl {


	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cSDLFontData::cSDLFontData(const tString &asName,iLowLevelGraphics* apLowLevelGraphics)
		: iFontData(asName,apLowLevelGraphics)
	{
	
	
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	bool cSDLFontData::CreateFromBitmapFile(const tWString &asFileName)
	{
		SetFullPath(asFileName);

		tWString sPath = cString::GetFilePathW(asFileName);

		////////////////////////////////////////////
		// Load xml file
		FILE *pFile = cPlatform::OpenFile(asFileName, _W("rb"));
		if(pFile==NULL) return false;
		
		TiXmlDocument *pXmlDoc = hplNew( TiXmlDocument,() );
		if(pXmlDoc->LoadFile(pFile)==false)
		{
			Error("Couldn't load angle code font file '%s'\n",asFileName.c_str());
			fclose(pFile);
			hplDelete(pXmlDoc);
			return false;
		}

		TiXmlElement *pRootElem = pXmlDoc->RootElement();
		if(pRootElem == NULL || pRootElem->FirstChildElement("common") == NULL ||
			pRootElem->FirstChildElement("chars") == NULL ||
			pRootElem->FirstChildElement("pages") == NULL)
		{
			fclose(pFile);
			hplDelete(pXmlDoc);
			return false;
		}

		////////////////////////////////////////////
		// Load Common info
		TiXmlElement *pCommonElem = pRootElem->FirstChildElement("common");

		int lLineHeight = cString::ToInt(pCommonElem->Attribute("lineHeight"),0);
		int lBase = cString::ToInt(pCommonElem->Attribute("base"),0);
		if(lLineHeight <= 0 || lBase <= 0)
		{
			fclose(pFile);
			hplDelete(pXmlDoc);
			return false;
		}

		mfHeight = (float)lLineHeight;

		mvSizeRatio.x = (float)lBase / (float)lLineHeight;
		mvSizeRatio.y = 1;

		int lLargestGlyphId=-1;

		////////////////////////////////////////////
		// Check for largest glyph number and resize array.
		TiXmlElement *pCharsRootElem = pRootElem->FirstChildElement("chars");
		TiXmlElement *pCharElem = pCharsRootElem->FirstChildElement("char");
		for(; pCharElem != NULL; pCharElem = pCharElem->NextSiblingElement("char"))
		{
			int lId = cString::ToInt(pCharElem->Attribute("id"),0);
			if(lId >lLargestGlyphId) lLargestGlyphId = lId;
		}

		if(lLargestGlyphId < 0 || lLargestGlyphId > 65535)
		{
			fclose(pFile);
			hplDelete(pXmlDoc);
			return false;
		}
		mlFirstChar =0;
		mlLastChar = lLargestGlyphId;
		mvGlyphs.resize(lLargestGlyphId+1, NULL);

		////////////////////////////////////////////
		// Load bitmaps
		std::vector<cFrameTexture*> vFrameTextures;
		
		TiXmlElement *pPagesRootElem = pRootElem->FirstChildElement("pages");
		if(pPagesRootElem->FirstChildElement("page") == NULL)
		{
			fclose(pFile);
			hplDelete(pXmlDoc);
			return false;
		}
		for(TiXmlElement *pPage = pPagesRootElem->FirstChildElement("page"); pPage;
			pPage = pPage->NextSiblingElement("page"))
		{
			const char* pPageFile = pPage->Attribute("file");
			if(pPageFile == NULL || *pPageFile == 0)
			{
				fclose(pFile);
				hplDelete(pXmlDoc);
				return false;
			}
		}

		TiXmlElement *pPageElem = pPagesRootElem->FirstChildElement("page");
		for(; pPageElem != NULL; pPageElem = pPageElem->NextSiblingElement("page"))
		{
			tWString sFileName = cString::To16Char(pPageElem->Attribute("file"));
			tWString sFilePath = cString::SetFilePathW(sFileName,sPath);

			//////////////////////////////
			//Load image to bitmap
			cBitmap *pBitmap = mpResources->GetBitmapLoaderHandler()->LoadBitmap(sFilePath, 0);
			if(pBitmap==NULL)
			{
				Error("Couldn't load bitmap %s for FNT file '%s'\n",cString::To8Char(sFilePath).c_str(),cString::To8Char(asFileName).c_str());
				fclose(pFile);
				hplDelete(pXmlDoc);
				return false;
			}
			
			//Make a fix here so we get a white luminence and alpha filled with data.
			if(pBitmap->GetPixelFormat() == ePixelFormat_Luminance)
			{
				cBitmap *pTempBitmap = hplNew( cBitmap, ());
				pTempBitmap->CreateData(pBitmap->GetSize(),ePixelFormat_LuminanceAlpha,0,0);

				pBitmap->SetPixelFormat(ePixelFormat_Alpha);
				pTempBitmap->Blit(pBitmap,0,pBitmap->GetSize(),0);

				hplDelete( pBitmap );

                pBitmap = pTempBitmap;
			}
			///////////////////////
			//Create a texture from bitmap (do not want to load it from texture manager since that would delete the texture on its own).
			iTexture *pTexture = mpLowLevelGraphics->CreateTexture("",eTextureType_2D,eTextureUsage_Normal);

			pTexture->CreateFromBitmap(pBitmap);

			hplDelete( pBitmap ); //Bitmap no longer needed

			///////////////////////
			//Create Custom Frame for images
			cFrameTexture *pFrameTexture = mpResources->GetImageManager()->CreateCustomFrame(pTexture);

			vFrameTextures.push_back(pFrameTexture);
		}

		////////////////////////////////////////////
		// Load glyphs
		pCharsRootElem = pRootElem->FirstChildElement("chars");
		pCharElem = pCharsRootElem->FirstChildElement("char");
		for(; pCharElem != NULL; pCharElem = pCharElem->NextSiblingElement("char"))
		{
			//Get the info on the character
			int lId = cString::ToInt(pCharElem->Attribute("id"),0);
			int lX = cString::ToInt(pCharElem->Attribute("x"),0);
			int lY = cString::ToInt(pCharElem->Attribute("y"),0);

			int lW = cString::ToInt(pCharElem->Attribute("width"),0);
			int lH = cString::ToInt(pCharElem->Attribute("height"),0);

			int lXOffset = cString::ToInt(pCharElem->Attribute("xoffset"),0);
			int lYOffset = cString::ToInt(pCharElem->Attribute("yoffset"),0);

			int lAdvance = cString::ToInt(pCharElem->Attribute("xadvance"),0);

			int lPage = cString::ToInt(pCharElem->Attribute("page"),0);

			if(lId<0 || lId>=(int)mvGlyphs.size() || lPage < 0 || lPage >= (int)vFrameTextures.size())
			{
				Warning("Font '%s' contain glyph with invalid id: %d. Skipping loading of it!\n", cString::To8Char(asFileName).c_str(), lId);
				continue;
			}

			//Get the bitmap where the character graphics is
			cFrameTexture* pFrameTexture = vFrameTextures[lPage];
			const cVector3l& vPageSize = pFrameTexture->GetTexture()->GetSize();
			if(lX < 0 || lY < 0 || lW < 0 || lH < 0 || lX > vPageSize.x ||
				lY > vPageSize.y || lW > vPageSize.x - lX || lH > vPageSize.y - lY)
			{
				Warning("Font '%s' contains glyph %d outside bitmap page %d. Skipping it!\n",
					cString::To8Char(asFileName).c_str(), lId, lPage);
				continue;
			}
			
			cFrameSubImage *pImage = pFrameTexture->CreateCustomImage(cVector2l(lX, lY),cVector2l(lW,lH));
            
			//Create glyph and place it correctly.
            cGlyph *pGlyph = CreateGlyph(pImage,cVector2l(lXOffset,lYOffset),cVector2l(lW,lH),
										cVector2l(lBase,lLineHeight),lAdvance);
			
			if(mvGlyphs[lId]) hplDelete(mvGlyphs[lId]);
			mvGlyphs[lId] = pGlyph;

		}

		// BMFont kerning is measured in source pixels, like xadvance.
		TiXmlElement *pKernings = pRootElem->FirstChildElement("kernings");
		if(pKernings)
		{
			for(TiXmlElement *pKerning = pKernings->FirstChildElement("kerning"); pKerning;
				pKerning = pKerning->NextSiblingElement("kerning"))
			{
				const int lFirst = cString::ToInt(pKerning->Attribute("first"),-1);
				const int lSecond = cString::ToInt(pKerning->Attribute("second"),-1);
				const int lAmount = cString::ToInt(pKerning->Attribute("amount"),0);
				if(lFirst >= 0 && lSecond >= 0)
					mKerning[std::make_pair((unsigned int)lFirst,(unsigned int)lSecond)] =
						(float)lAmount / (float)lLineHeight;
			}
		}

		//Destroy XML
		fclose(pFile);
		hplDelete(pXmlDoc);
		return true;
	}
	
	//-----------------------------------------------------------------------
	
	bool cSDLFontData::CreateFromFontFile(const tWString &, int,
		unsigned short, unsigned short)
	{
		return false;
	}

	float cSDLFontData::GetKerning(unsigned int alLeft, unsigned int alRight) const
	{
		std::map<std::pair<unsigned int,unsigned int>, float>::const_iterator it =
			mKerning.find(std::make_pair(alLeft,alRight));
		return it == mKerning.end() ? 0 : it->second;
	}

}
