#include "impl/TrueTypeFontData.h"

#include "graphics/Bitmap.h"
#include "graphics/FrameSubImage.h"
#include "resources/ImageManager.h"
#include "resources/Resources.h"
#include "system/LowLevelSystem.h"
#include "system/Platform.h"
#include "system/String.h"

#include <map>
#include <vector>
#include <stdio.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../dependencies/sources/imgui/imstb_truetype.h"

namespace hpl {
	struct cTrueTypeFontDataImpl
	{
		std::vector<unsigned char> mvFileData;
		std::map<unsigned int, cGlyph*> mGlyphs;
		std::map<std::pair<int,int>, int> mLegacyKerning;
		stbtt_fontinfo mFont;
		float mfScale;
		float mfLineHeight;
		float mfAscent;
	};

	cTrueTypeFontData::cTrueTypeFontData(const tString &asName, iLowLevelGraphics* apLowLevelGraphics)
		: iFontData(asName, apLowLevelGraphics), mpImpl(new cTrueTypeFontDataImpl)
	{
		mpImpl->mfScale = 0;
		mpImpl->mfLineHeight = 0;
		mpImpl->mfAscent = 0;
	}

	cTrueTypeFontData::~cTrueTypeFontData()
	{
		for(std::map<unsigned int,cGlyph*>::iterator it = mpImpl->mGlyphs.begin();
			it != mpImpl->mGlyphs.end(); ++it)
			hplDelete(it->second);
		delete mpImpl;
	}

	bool cTrueTypeFontData::CreateFromBitmapFile(const tWString &)
	{
		return false;
	}

	bool cTrueTypeFontData::CreateFromFontFile(const tWString &asFileName, int alSize,
		unsigned short alFirstChar, unsigned short alLastChar)
	{
		if(alSize <= 0 || alSize > 512 || alFirstChar > alLastChar) return false;
		FILE* pFile = cPlatform::OpenFile(asFileName, _W("rb"));
		if(pFile == NULL) return false;
		if(fseek(pFile, 0, SEEK_END) != 0)
		{
			fclose(pFile);
			return false;
		}
		const long lFileSize = ftell(pFile);
		if(lFileSize < 12 || lFileSize > 128 * 1024 * 1024 || fseek(pFile, 0, SEEK_SET) != 0)
		{
			fclose(pFile);
			return false;
		}
		mpImpl->mvFileData.resize((size_t)lFileSize);
		const bool bRead = fread(&mpImpl->mvFileData[0], 1, (size_t)lFileSize, pFile) == (size_t)lFileSize;
		fclose(pFile);
		if(!bRead) return false;

		// Check the SFNT table directory before handing offsets to stb_truetype.
		const unsigned char* pData = &mpImpl->mvFileData[0];
		const bool bTrueType = (pData[0] == 0 && pData[1] == 1 && pData[2] == 0 && pData[3] == 0)
			|| (pData[0] == 't' && pData[1] == 'r' && pData[2] == 'u' && pData[3] == 'e');
		const unsigned int lTables = ((unsigned int)pData[4] << 8) | pData[5];
		if(!bTrueType || lTables == 0 || 12U + lTables * 16U > (unsigned int)lFileSize) return false;
		unsigned int lKernOffset = 0, lKernLength = 0;
		for(unsigned int i = 0; i < lTables; ++i)
		{
			const unsigned char* pEntry = pData + 12 + i * 16;
			const unsigned int lOffset = ((unsigned int)pEntry[8] << 24) | ((unsigned int)pEntry[9] << 16)
				| ((unsigned int)pEntry[10] << 8) | pEntry[11];
			const unsigned int lLength = ((unsigned int)pEntry[12] << 24) | ((unsigned int)pEntry[13] << 16)
				| ((unsigned int)pEntry[14] << 8) | pEntry[15];
			if(lOffset > (unsigned int)lFileSize || lLength > (unsigned int)lFileSize - lOffset) return false;
			if(pEntry[0] == 'k' && pEntry[1] == 'e' && pEntry[2] == 'r' && pEntry[3] == 'n')
			{
				lKernOffset = lOffset;
				lKernLength = lLength;
			}
		}
		if(lKernLength)
		{
			if(lKernLength < 12) return false;
			const unsigned char* pKern = pData + lKernOffset;
			const unsigned int lSubtables = ((unsigned int)pKern[2] << 8) | pKern[3];
			const unsigned int lCoverage = ((unsigned int)pKern[8] << 8) | pKern[9];
			if(lSubtables && lCoverage == 1)
			{
				const unsigned int lPairs = ((unsigned int)pKern[10] << 8) | pKern[11];
				if(18U + lPairs * 6U > lKernLength) return false;
			}
		}
		if(!stbtt_InitFont(&mpImpl->mFont, pData, 0)) return false;
		const int lKerningPairs = stbtt_GetKerningTableLength(&mpImpl->mFont);
		if(lKerningPairs > 0)
		{
			std::vector<stbtt_kerningentry> vPairs((size_t)lKerningPairs);
			const int lLoaded = stbtt_GetKerningTable(&mpImpl->mFont, &vPairs[0], lKerningPairs);
			for(int i = 0; i < lLoaded; ++i)
				mpImpl->mLegacyKerning[std::make_pair(vPairs[i].glyph1,vPairs[i].glyph2)] = vPairs[i].advance;
		}

		int lAscent, lDescent, lLineGap;
		stbtt_GetFontVMetrics(&mpImpl->mFont, &lAscent, &lDescent, &lLineGap);
		if(lAscent <= lDescent || lAscent - lDescent + lLineGap <= 0) return false;
		mpImpl->mfScale = stbtt_ScaleForPixelHeight(&mpImpl->mFont, (float)alSize);
		mpImpl->mfAscent = lAscent * mpImpl->mfScale;
		mpImpl->mfLineHeight = (lAscent - lDescent + lLineGap) * mpImpl->mfScale;
		if(mpImpl->mfLineHeight <= 0) return false;
		mfHeight = mpImpl->mfLineHeight;
		mvSizeRatio = cVector2f(1, 1);
		mlFirstChar = alFirstChar;
		mlLastChar = alLastChar;
		SetFullPath(asFileName);
		return true;
	}

	cGlyph* cTrueTypeFontData::GetGlyphForCodepoint(unsigned int alCodepoint)
	{
		if(alCodepoint > 0x10FFFF || (alCodepoint >= 0xD800 && alCodepoint <= 0xDFFF))
			alCodepoint = 0xFFFD;
		std::map<unsigned int,cGlyph*>::iterator it = mpImpl->mGlyphs.find(alCodepoint);
		if(it != mpImpl->mGlyphs.end()) return it->second;
		if(mpImpl->mfScale <= 0) return NULL;
		if(stbtt_FindGlyphIndex(&mpImpl->mFont, (int)alCodepoint) == 0)
		{
			if(alCodepoint != 0xFFFD && stbtt_FindGlyphIndex(&mpImpl->mFont, 0xFFFD))
				return GetGlyphForCodepoint(0xFFFD);
			if(alCodepoint != '?' && stbtt_FindGlyphIndex(&mpImpl->mFont, '?'))
				return GetGlyphForCodepoint('?');
			return NULL;
		}

		int lAdvance, lBearing;
		stbtt_GetCodepointHMetrics(&mpImpl->mFont, (int)alCodepoint, &lAdvance, &lBearing);
		int x0, y0, x1, y1;
		stbtt_GetCodepointBitmapBox(&mpImpl->mFont, (int)alCodepoint,
			mpImpl->mfScale, mpImpl->mfScale, &x0, &y0, &x1, &y1);
		const int lWidth = x1 - x0, lHeight = y1 - y0;
		if(lWidth < 0 || lHeight < 0 || lWidth > 510 || lHeight > 510) return NULL;
		cFrameSubImage* pImage = NULL;
		if(lWidth && lHeight)
		{
			std::vector<unsigned char> vAlpha((size_t)lWidth * lHeight);
			stbtt_MakeCodepointBitmap(&mpImpl->mFont, &vAlpha[0], lWidth, lHeight,
				lWidth, mpImpl->mfScale, mpImpl->mfScale, (int)alCodepoint);
			cBitmap* pBitmap = hplNew(cBitmap, ());
			pBitmap->CreateData(cVector3l(lWidth, lHeight, 1), ePixelFormat_RGBA, 0, 0);
			unsigned char* pPixels = pBitmap->GetData(0, 0)->mpData;
			for(size_t i = 0; i < vAlpha.size(); ++i)
			{
				pPixels[i * 4 + 0] = 255;
				pPixels[i * 4 + 1] = 255;
				pPixels[i * 4 + 2] = 255;
				pPixels[i * 4 + 3] = vAlpha[i];
			}
			pImage = mpResources->GetImageManager()->CreateFromBitmap("", pBitmap);
			hplDelete(pBitmap);
			if(pImage == NULL) return NULL;
		}
		cGlyph* pGlyph = CreateGlyph(pImage,
			cVector2f((float)x0, mpImpl->mfAscent + (float)y0),
			cVector2f((float)lWidth, (float)lHeight),
			cVector2f(mpImpl->mfLineHeight, mpImpl->mfLineHeight),
			lAdvance * mpImpl->mfScale);
		mpImpl->mGlyphs.insert(std::make_pair(alCodepoint, pGlyph));
		return pGlyph;
	}

	float cTrueTypeFontData::GetKerning(unsigned int alLeft, unsigned int alRight) const
	{
		if(mpImpl->mfScale <= 0 || alLeft > 0x10FFFF || alRight > 0x10FFFF) return 0;
		int lAdvance = stbtt_GetCodepointKernAdvance(&mpImpl->mFont, (int)alLeft, (int)alRight);
		if(lAdvance == 0 && !mpImpl->mLegacyKerning.empty())
		{
			const int lLeftGlyph = stbtt_FindGlyphIndex(&mpImpl->mFont, (int)alLeft);
			const int lRightGlyph = stbtt_FindGlyphIndex(&mpImpl->mFont, (int)alRight);
			std::map<std::pair<int,int>,int>::const_iterator it =
				mpImpl->mLegacyKerning.find(std::make_pair(lLeftGlyph,lRightGlyph));
			if(it != mpImpl->mLegacyKerning.end()) lAdvance = it->second;
		}
		return lAdvance * mpImpl->mfScale / mpImpl->mfLineHeight;
	}
}
