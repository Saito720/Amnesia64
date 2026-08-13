/*
 * Copyright (C) 2009-2020 Frictional Games
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

#include "impl/BitmapLoaderKTX2.h"

#include "graphics/Bitmap.h"
#include "graphics/LowLevelGraphics.h"
#include "system/LowLevelSystem.h"
#include "system/Platform.h"
#include "system/String.h"

#include <cstdio>
#include <limits>

#ifdef HPL2_ENABLE_KTX2
#include <ktx.h>

#ifdef _MSC_VER
#pragma comment(lib, "ktx.lib")
#endif
#endif

namespace hpl {

#ifdef HPL2_ENABLE_KTX2
	namespace {
		const unsigned int kVkFormatR8Unorm = 9;
		const unsigned int kVkFormatR8Srgb = 15;
		const unsigned int kVkFormatR8G8Unorm = 16;
		const unsigned int kVkFormatR8G8Srgb = 22;
		const unsigned int kVkFormatR8G8B8Unorm = 23;
		const unsigned int kVkFormatR8G8B8Srgb = 29;
		const unsigned int kVkFormatB8G8R8Unorm = 30;
		const unsigned int kVkFormatB8G8R8Srgb = 36;
		const unsigned int kVkFormatR8G8B8A8Unorm = 37;
		const unsigned int kVkFormatR8G8B8A8Srgb = 43;
		const unsigned int kVkFormatB8G8R8A8Unorm = 44;
		const unsigned int kVkFormatB8G8R8A8Srgb = 50;
		const unsigned int kVkFormatBC1RGBUnormBlock = 131;
		const unsigned int kVkFormatBC1RGBSrgbBlock = 132;
		const unsigned int kVkFormatBC1RGBAUnormBlock = 133;
		const unsigned int kVkFormatBC1RGBASrgbBlock = 134;
		const unsigned int kVkFormatBC2UnormBlock = 135;
		const unsigned int kVkFormatBC2SrgbBlock = 136;
		const unsigned int kVkFormatBC3UnormBlock = 137;
		const unsigned int kVkFormatBC3SrgbBlock = 138;

		class cKtxTextureGuard
		{
		public:
			cKtxTextureGuard() : mpTexture(NULL) {}
			~cKtxTextureGuard()
			{
				if(mpTexture) ktxTexture_Destroy(mpTexture);
			}

			ktxTexture** GetAddress() { return &mpTexture; }
			ktxTexture* Get() { return mpTexture; }

		private:
			ktxTexture* mpTexture;
		};

		class cFileGuard
		{
		public:
			cFileGuard(FILE* apFile) : mpFile(apFile) {}
			~cFileGuard()
			{
				if(mpFile) fclose(mpFile);
			}

			FILE* Get() { return mpFile; }

		private:
			FILE* mpFile;
		};

		bool CheckKtxResult(KTX_error_code aResult, const char* apAction, const tWString& asFile)
		{
			if(aResult == KTX_SUCCESS) return true;

			Error("%s for KTX2 '%s' failed: %s\n",
				apAction,
				cString::To8Char(asFile).c_str(),
				ktxErrorString(aResult));
			return false;
		}

		int GetLevelWidth(ktxTexture* apTexture, int alMip)
		{
			int lWidth = (int)apTexture->baseWidth >> alMip;
			return lWidth > 0 ? lWidth : 1;
		}

		int GetLevelHeight(ktxTexture* apTexture, int alMip)
		{
			int lHeight = (int)apTexture->baseHeight >> alMip;
			return lHeight > 0 ? lHeight : 1;
		}

		int GetLevelDepth(ktxTexture* apTexture, int alMip)
		{
			int lDepth = (int)apTexture->baseDepth >> alMip;
			return lDepth > 0 ? lDepth : 1;
		}

		ktx_size_t GetExpectedUncompressedImageSize(ktxTexture* apTexture, int alMip, ePixelFormat aPixelFormat)
		{
			return (ktx_size_t)GetLevelWidth(apTexture, alMip) *
				(ktx_size_t)GetLevelHeight(apTexture, alMip) *
				(ktx_size_t)GetLevelDepth(apTexture, alMip) *
				(ktx_size_t)GetBytesPerPixel(aPixelFormat);
		}

		bool IsSupportedTextureShape(ktxTexture* apTexture, const tWString& asFile)
		{
			if(apTexture == NULL)
			{
				Error("KTX2 loader did not create a texture for '%s'.\n", cString::To8Char(asFile).c_str());
				return false;
			}

			if(apTexture->classId != ktxTexture2_c)
			{
				Error("KTX file '%s' is not a KTX2 texture.\n", cString::To8Char(asFile).c_str());
				return false;
			}

			if(apTexture->isArray)
			{
				if(apTexture->numDimensions != 2)
				{
					Error("KTX2 array texture '%s' must be a 2D array.\n", cString::To8Char(asFile).c_str());
					return false;
				}

				if(apTexture->numFaces != 1)
				{
					Error("KTX2 cubemap arrays are not supported: '%s'\n", cString::To8Char(asFile).c_str());
					return false;
				}

				if(apTexture->numLayers == 0)
				{
					Error("KTX2 array texture '%s' does not contain any layers.\n", cString::To8Char(asFile).c_str());
					return false;
				}
			}
			else if(apTexture->numFaces != 1 && apTexture->numFaces != 6)
			{
				Error("Unsupported KTX2 face count %d in '%s'\n", apTexture->numFaces, cString::To8Char(asFile).c_str());
				return false;
			}

			if(apTexture->numFaces == 6 && (apTexture->numDimensions != 2 || apTexture->baseWidth != apTexture->baseHeight))
			{
				Error("KTX2 cubemap '%s' must be a square 2D texture.\n", cString::To8Char(asFile).c_str());
				return false;
			}

			if(apTexture->numDimensions < 1 || apTexture->numDimensions > 3)
			{
				Error("Unsupported KTX2 texture dimension count %d in '%s'\n", apTexture->numDimensions, cString::To8Char(asFile).c_str());
				return false;
			}

			const ktx_uint32_t lMaxBitmapValue = (ktx_uint32_t)std::numeric_limits<int>::max();
			if(apTexture->baseWidth > lMaxBitmapValue ||
				apTexture->baseHeight > lMaxBitmapValue ||
				apTexture->baseDepth > lMaxBitmapValue ||
				apTexture->numLayers > lMaxBitmapValue ||
				apTexture->numLevels > lMaxBitmapValue)
			{
				Error("KTX2 texture '%s' exceeds HPL bitmap dimension or count limits.\n", cString::To8Char(asFile).c_str());
				return false;
			}

			return true;
		}

		bool CheckBitmapDataSizes(ktxTexture* apTexture, ePixelFormat aPixelFormat, const tWString& asFile)
		{
			int lNumOfMipMaps = apTexture->numLevels > 0 ? (int)apTexture->numLevels : 1;

			for(int mip=0; mip<lNumOfMipMaps; ++mip)
			{
				ktx_size_t imageSize = PixelFormatIsCompressed(aPixelFormat) ?
					ktxTexture_GetImageSize(apTexture, mip) :
					GetExpectedUncompressedImageSize(apTexture, mip, aPixelFormat);

				if(imageSize > (ktx_size_t)std::numeric_limits<int>::max())
				{
					Error("KTX2 texture '%s' mip %d requires %llu bytes per image, exceeding HPL bitmap limit of %d bytes.\n",
						cString::To8Char(asFile).c_str(),
						mip,
						(unsigned long long)imageSize,
						std::numeric_limits<int>::max());
					return false;
				}
			}

			return true;
		}
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cBitmapLoaderKTX2::cBitmapLoaderKTX2()
	{
#ifdef HPL2_ENABLE_KTX2
		AddSupportedExtension("ktx2");
#endif
	}

	cBitmapLoaderKTX2::~cBitmapLoaderKTX2()
	{
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cBitmap* cBitmapLoaderKTX2::LoadBitmap(const tWString& asFile, tBitmapLoadFlag aFlags)
	{
#ifndef HPL2_ENABLE_KTX2
		Error("KTX2 support was not enabled when building HPL2.\n");
		return NULL;
#else
		cFileGuard fileGuard(cPlatform::OpenFile(asFile, _W("rb")));
		if(fileGuard.Get() == NULL)
		{
			Error("Could not open KTX2 file '%s'\n", cString::To8Char(asFile).c_str());
			return NULL;
		}

		cKtxTextureGuard textureGuard;
		KTX_error_code result = ktxTexture_CreateFromStdioStream(fileGuard.Get(), KTX_TEXTURE_CREATE_NO_FLAGS, textureGuard.GetAddress());

		if(CheckKtxResult(result, "Reading header", asFile)==false) return NULL;

		ktxTexture* pTexture = textureGuard.Get();
		if(IsSupportedTextureShape(pTexture, asFile)==false) return NULL;

		bool bNeedsTranscoding = ktxTexture_NeedsTranscoding(pTexture) != 0;
		ePixelFormat pixelFormat = ePixelFormat_Unknown;
		if(bNeedsTranscoding==false)
		{
			pixelFormat = GetPixelFormatFromVkFormat(((ktxTexture2*)pTexture)->vkFormat);
			if(pixelFormat == ePixelFormat_Unknown)
			{
				Error("KTX2 texture '%s' uses unsupported Vulkan format %u.\n",
					cString::To8Char(asFile).c_str(),
					((ktxTexture2*)pTexture)->vkFormat);
				return NULL;
			}

			if(CheckBitmapDataSizes(pTexture, pixelFormat, asFile)==false) return NULL;
		}

		result = ktxTexture_LoadImageData(pTexture, NULL, 0);
		if(CheckKtxResult(result, "Loading image data", asFile)==false) return NULL;

		if(ktxTexture_NeedsTranscoding(pTexture))
		{
			ktx_transcode_fmt_e transcodeFormat = ShouldTranscodeToDXTC(aFlags) ? KTX_TTF_BC1_OR_3 : KTX_TTF_RGBA32;
			result = ktxTexture2_TranscodeBasis((ktxTexture2*)pTexture, transcodeFormat, 0);
			if(CheckKtxResult(result, "Transcoding", asFile)==false) return NULL;
		}

		pixelFormat = GetPixelFormatFromVkFormat(((ktxTexture2*)pTexture)->vkFormat);
		if(pixelFormat == ePixelFormat_Unknown)
		{
			Error("KTX2 texture '%s' uses unsupported Vulkan format %u.\n",
				cString::To8Char(asFile).c_str(),
				((ktxTexture2*)pTexture)->vkFormat);
			return NULL;
		}

		if(bNeedsTranscoding && CheckBitmapDataSizes(pTexture, pixelFormat, asFile)==false) return NULL;

		if(PixelFormatIsCompressed(pixelFormat) && (aFlags & eBitmapLoadFlag_ForceNoCompression))
		{
			Error("KTX2 texture '%s' is GPU-compressed and cannot be loaded with ForceNoCompression.\n",
				cString::To8Char(asFile).c_str());
			return NULL;
		}

		bool bIsArray = pTexture->isArray != 0;
		int lNumOfImages = bIsArray ? (int)pTexture->numLayers : (pTexture->numFaces == 6 ? 6 : 1);
		int lNumOfMipMaps = pTexture->numLevels > 0 ? (int)pTexture->numLevels : 1;

		cBitmap* pBitmap = hplNew(cBitmap, ());
		pBitmap->SetUpData(lNumOfImages, lNumOfMipMaps);
		pBitmap->SetSize(cVector3l((int)pTexture->baseWidth, (int)pTexture->baseHeight, (int)(pTexture->baseDepth > 0 ? pTexture->baseDepth : 1)));
		pBitmap->SetBytesPerPixel((char)GetBytesPerPixel(pixelFormat));
		pBitmap->SetIsCompressed(PixelFormatIsCompressed(pixelFormat));
		pBitmap->SetIsTextureArray(bIsArray);
		pBitmap->SetPixelFormat(pixelFormat);

		ktx_uint8_t* pTextureData = ktxTexture_GetData(pTexture);
		if(pTextureData == NULL)
		{
			Error("KTX2 texture '%s' did not contain image data.\n", cString::To8Char(asFile).c_str());
			hplDelete(pBitmap);
			return NULL;
		}
		ktx_size_t textureDataSize = ktxTexture_GetDataSize(pTexture);

		for(int image=0; image<lNumOfImages; ++image)
		for(int mip=0; mip<lNumOfMipMaps; ++mip)
		{
			ktx_size_t offset = 0;
			ktx_uint32_t layer = bIsArray ? (ktx_uint32_t)image : 0;
			ktx_uint32_t face = bIsArray ? 0 : (pTexture->numFaces == 6 ? (ktx_uint32_t)image : 0);
			result = ktxTexture_GetImageOffset(pTexture, mip, layer, face, &offset);
			if(CheckKtxResult(result, "Finding image data", asFile)==false)
			{
				hplDelete(pBitmap);
				return NULL;
			}

			ktx_size_t imageSize = ktxTexture_GetImageSize(pTexture, mip);
			if(offset > textureDataSize || imageSize > textureDataSize - offset)
			{
				Error("KTX2 texture '%s' mip %d image data is out of bounds.\n",
					cString::To8Char(asFile).c_str(),
					mip);
				hplDelete(pBitmap);
				return NULL;
			}

			if(PixelFormatIsCompressed(pixelFormat)==false)
			{
				ktx_size_t expectedSize = GetExpectedUncompressedImageSize(pTexture, mip, pixelFormat);
				if(imageSize != expectedSize)
				{
					Error("KTX2 texture '%s' mip %d has padded or unsupported uncompressed data.\n",
						cString::To8Char(asFile).c_str(),
						mip);
					hplDelete(pBitmap);
					return NULL;
				}
			}

			cBitmapData* pImage = pBitmap->GetData(image, mip);
			pImage->SetData(pTextureData + offset, (int)imageSize);
		}

		return pBitmap;
#endif
	}

	//-----------------------------------------------------------------------

	bool cBitmapLoaderKTX2::SaveBitmap(cBitmap* apBitmap, const tWString& asFile, tBitmapSaveFlag aFlags)
	{
		return false;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	ePixelFormat cBitmapLoaderKTX2::GetPixelFormatFromVkFormat(unsigned int alVkFormat)
	{
#ifndef HPL2_ENABLE_KTX2
		return ePixelFormat_Unknown;
#else
		switch(alVkFormat)
		{
		case kVkFormatR8Unorm:
		case kVkFormatR8Srgb:
			return ePixelFormat_Luminance;
		case kVkFormatR8G8Unorm:
		case kVkFormatR8G8Srgb:
			return ePixelFormat_LuminanceAlpha;
		case kVkFormatR8G8B8Unorm:
		case kVkFormatR8G8B8Srgb:
			return ePixelFormat_RGB;
		case kVkFormatB8G8R8Unorm:
		case kVkFormatB8G8R8Srgb:
			return ePixelFormat_BGR;
		case kVkFormatR8G8B8A8Unorm:
		case kVkFormatR8G8B8A8Srgb:
			return ePixelFormat_RGBA;
		case kVkFormatB8G8R8A8Unorm:
		case kVkFormatB8G8R8A8Srgb:
			return ePixelFormat_BGRA;
		case kVkFormatBC1RGBUnormBlock:
		case kVkFormatBC1RGBSrgbBlock:
		case kVkFormatBC1RGBAUnormBlock:
		case kVkFormatBC1RGBASrgbBlock:
			return ePixelFormat_DXT1;
		case kVkFormatBC2UnormBlock:
		case kVkFormatBC2SrgbBlock:
			return ePixelFormat_DXT3;
		case kVkFormatBC3UnormBlock:
		case kVkFormatBC3SrgbBlock:
			return ePixelFormat_DXT5;
		}

		return ePixelFormat_Unknown;
#endif
	}

	//-----------------------------------------------------------------------

	bool cBitmapLoaderKTX2::ShouldTranscodeToDXTC(tBitmapLoadFlag aFlags)
	{
#ifndef HPL2_ENABLE_KTX2
		return false;
#else
		if(aFlags & eBitmapLoadFlag_ForceNoCompression) return false;
		if(mpLowLevelGraphics == NULL) return false;
		return mpLowLevelGraphics->GetCaps(eGraphicCaps_TextureCompression_DXTC) != 0;
#endif
	}

	//-----------------------------------------------------------------------
}
