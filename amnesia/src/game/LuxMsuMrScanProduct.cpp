/*
 * Copyright © 2009-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "LuxMsuMrScanProduct.h"

#include "LuxMsuMrSimulation.h"

#include <algorithm>
#include <cstring>

#include "engine/Engine.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "gui/Gui.h"
#include "gui/GuiGfxElement.h"
#include "gui/GuiSet.h"

namespace
{
	const float kOverlayWidth = 786.0f;
	const float kOverlayMargin = 8.0f;
	const float kOverlayDepth = 2.85f;
}

static_assert(cLuxMsuMrScanProduct::kWidth ==
	cLuxMsuMrSimulation::kEarthViewSamplesPerLine,
	"The scan product width must match one complete HRPT Earth-view line");

cLuxMsuMrScanProduct::cLuxMsuMrScanProduct()
	: mpTexture(NULL), mpImageGfx(NULL), mpBackgroundGfx(NULL),
	  mbInitialized(false), mbHasLastLine(false), mbLineOpen(false),
	  mlOpenLineIndex(0), mlLastLineIndex(0),
	  mlLastGapLineCount(0), mlStoredLineCount(0),
	  mlWrittenSampleCount(0)
{
}

cLuxMsuMrScanProduct::~cLuxMsuMrScanProduct()
{
	Reset();
}

bool cLuxMsuMrScanProduct::Initialize()
{
	Reset();
	if(gpBase == NULL || gpBase->mpEngine == NULL ||
		gpBase->mpEngine->GetGraphics() == NULL ||
		gpBase->mpEngine->GetGui() == NULL)
		return false;

	mvLinePixels.assign(static_cast<size_t>(kWidth) * 4, 0);
	mvLineWritten.assign(static_cast<size_t>(kWidth), 0);
	mvPixels.assign(static_cast<size_t>(kWidth) * kHistoryLines * 4, 0);
	cGraphics *pGraphics = gpBase->mpEngine->GetGraphics();
	mpTexture = pGraphics->CreateTexture("MsuMrScanProduct",
		eTextureType_2D, eTextureUsage_Normal);
	if(mpTexture == NULL)
	{
		Reset();
		return false;
	}
	mpTexture->SetUseMipMaps(false);
	mpTexture->SetFilter(eTextureFilter_Nearest);
	mpTexture->SetWrapSTR(eTextureWrap_ClampToEdge);
	if(mpTexture->CreateFromRawData(
		cVector3l(kWidth, kHistoryLines, 0), ePixelFormat_RGBA,
		&mvPixels[0]) == false)
	{
		Reset();
		return false;
	}

	cGui *pGui = gpBase->mpEngine->GetGui();
	mpImageGfx = pGui->CreateGfxTexture(mpTexture, false,
		eGuiMaterial_Alpha);
	mpBackgroundGfx = pGui->CreateGfxFilledRect(
		cColor(0.0f, 0.0f, 0.0f, 1.0f), eGuiMaterial_Alpha);
	if(mpImageGfx == NULL || mpBackgroundGfx == NULL)
	{
		Reset();
		return false;
	}

	mbInitialized = true;
	return true;
}

void cLuxMsuMrScanProduct::Reset()
{
	if(gpBase && gpBase->mpEngine)
	{
		cGui *pGui = gpBase->mpEngine->GetGui();
		if(pGui && mpImageGfx) pGui->DestroyGfx(mpImageGfx);
		if(pGui && mpBackgroundGfx) pGui->DestroyGfx(mpBackgroundGfx);
		cGraphics *pGraphics = gpBase->mpEngine->GetGraphics();
		if(pGraphics && mpTexture) pGraphics->DestroyTexture(mpTexture);
	}

	mpTexture = NULL;
	mpImageGfx = NULL;
	mpBackgroundGfx = NULL;
	mvLinePixels.clear();
	mvLineWritten.clear();
	mvPixels.clear();
	mbInitialized = false;
	mbHasLastLine = false;
	mbLineOpen = false;
	mlOpenLineIndex = 0;
	mlLastLineIndex = 0;
	mlLastGapLineCount = 0;
	mlStoredLineCount = 0;
	mlWrittenSampleCount = 0;
}

bool cLuxMsuMrScanProduct::BeginLine(std::uint64_t alLineIndex)
{
	if(mbInitialized == false || mpTexture == NULL || mbLineOpen ||
		(mbHasLastLine && alLineIndex <= mlLastLineIndex))
		return false;

	std::fill(mvLinePixels.begin(), mvLinePixels.end(), 0);
	std::fill(mvLineWritten.begin(), mvLineWritten.end(), 0);
	mlWrittenSampleCount = 0;
	mlOpenLineIndex = alLineIndex;
	mbLineOpen = true;
	return true;
}

bool cLuxMsuMrScanProduct::SetLineSample(std::uint32_t alSampleIndex,
	const unsigned char *apRgba)
{
	if(mbLineOpen == false || apRgba == NULL || alSampleIndex >= kWidth)
		return false;

	unsigned char *pPixel = &mvLinePixels[static_cast<size_t>(alSampleIndex) * 4];
	pPixel[0] = apRgba[0];
	pPixel[1] = apRgba[1];
	pPixel[2] = apRgba[2];
	pPixel[3] = apRgba[3];
	if(mvLineWritten[alSampleIndex] == 0)
	{
		mvLineWritten[alSampleIndex] = 1;
		++mlWrittenSampleCount;
	}
	return true;
}

bool cLuxMsuMrScanProduct::CommitLine()
{
	if(mbLineOpen == false || mbInitialized == false || mpTexture == NULL ||
		mlWrittenSampleCount != kWidth)
		return false;

	mlLastGapLineCount = mbHasLastLine ?
		mlOpenLineIndex - mlLastLineIndex - 1 : mlOpenLineIndex;
	const std::uint32_t lRowsToAdvance =
		mlLastGapLineCount >= kHistoryLines - 1 ? kHistoryLines :
		static_cast<std::uint32_t>(mlLastGapLineCount + 1);
	const size_t lRowBytes = static_cast<size_t>(kWidth) * 4;
	if(lRowsToAdvance < kHistoryLines)
	{
		std::memmove(&mvPixels[static_cast<size_t>(lRowsToAdvance) * lRowBytes],
			&mvPixels[0],
			static_cast<size_t>(kHistoryLines - lRowsToAdvance) * lRowBytes);
	}
	std::memset(&mvPixels[0], 0,
		static_cast<size_t>(lRowsToAdvance) * lRowBytes);
	std::memcpy(&mvPixels[0], &mvLinePixels[0], lRowBytes);
	UploadPixels();

	mbHasLastLine = true;
	mlLastLineIndex = mlOpenLineIndex;
	mlStoredLineCount = static_cast<std::uint32_t>(
		std::min<std::uint32_t>(mlStoredLineCount + lRowsToAdvance,
			kHistoryLines));
	mbLineOpen = false;
	mlWrittenSampleCount = 0;
	return true;
}

void cLuxMsuMrScanProduct::CancelLine()
{
	mbLineOpen = false;
	mlWrittenSampleCount = 0;
}

void cLuxMsuMrScanProduct::UploadPixels()
{
	if(mpTexture == NULL || mvPixels.empty())
		return;
	mpTexture->SetRawData(0, cVector3l(0, 0, 0),
		cVector3l(kWidth, kHistoryLines, 1), ePixelFormat_RGBA,
		&mvPixels[0]);
}

void cLuxMsuMrScanProduct::Draw()
{
	if(mbInitialized == false || mpImageGfx == NULL ||
		mpBackgroundGfx == NULL || gpBase == NULL ||
		gpBase->mpGameHudSet == NULL)
		return;

	const float fWidth = std::min(kOverlayWidth,
		gpBase->mvHudVirtualSize.x - kOverlayMargin * 2.0f);
	if(fWidth <= 0.0f)
		return;
	const cVector2f vSize(fWidth,
		fWidth * static_cast<float>(kHistoryLines) /
		static_cast<float>(kWidth));
	cVector3f vPosition = gpBase->mvHudVirtualStartPos;
	vPosition.x += (gpBase->mvHudVirtualSize.x - vSize.x) * 0.5f;
	vPosition.y += gpBase->mvHudVirtualSize.y - vSize.y - kOverlayMargin;
	vPosition.z = kOverlayDepth;

	gpBase->mpGameHudSet->DrawGfx(mpBackgroundGfx,
		vPosition - cVector3f(1.0f, 1.0f, 0.01f),
		vSize + cVector2f(2.0f, 2.0f), cColor(1.0f, 1.0f));
	gpBase->mpGameHudSet->DrawGfx(mpImageGfx, vPosition,
		vSize, cColor(1.0f, 1.0f));
}
