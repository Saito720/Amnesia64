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

#include "graphics/PostEffect_ImageTrail.h"

#include "graphics/Graphics.h"

#include "graphics/LowLevelGraphics.h"
#include "graphics/PostEffectComposite.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Texture.h"
#include "graphics/GPUProgram.h"
#include "graphics/GPUShader.h"

#include "system/PreprocessParser.h"

#include <cmath>

namespace hpl {
	
	//////////////////////////////////////////////////////////////////////////
	// PROGRAM VARS
	//////////////////////////////////////////////////////////////////////////

	#define kVar_afAlpha	0

	//////////////////////////////////////////////////////////////////////////
	// POST EFFECT BASE
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	cPostEffectType_ImageTrail::cPostEffectType_ImageTrail(cGraphics *apGraphics, cResources *apResources) : iPostEffectType("ImageTrail",apGraphics,apResources)
	{
		cParserVarContainer vars;
		vars.Add("UseUv");

		mpProgram = mpGraphics->CreateGpuProgramFromShaders("ImageTrail","deferred_base_vtx.glsl", "posteffect_image_trail_frag.glsl", &vars);
		if(mpProgram)
		{
			mpProgram->GetVariableAsId("afAlpha",kVar_afAlpha);
		}
	}
	
	//-----------------------------------------------------------------------

	cPostEffectType_ImageTrail::~cPostEffectType_ImageTrail()
	{

	}

	//-----------------------------------------------------------------------

	iPostEffect * cPostEffectType_ImageTrail::CreatePostEffect(iPostEffectParams *apParams)
	{
		cPostEffect_ImageTrail *pEffect = hplNew(cPostEffect_ImageTrail, (mpGraphics,mpResources,this));
		cPostEffectParams_ImageTrail *pImageTrailParams = static_cast<cPostEffectParams_ImageTrail*>(apParams);

		return pEffect;
	}
	
	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// POST EFFECT
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cPostEffect_ImageTrail::cPostEffect_ImageTrail(cGraphics *apGraphics, cResources *apResources, iPostEffectType *apType) : iPostEffect(apGraphics,apResources,apType)
	{
		cVector2l vSize = mpLowLevelGraphics->GetScreenSizeInt();
		
		mpAccumTexture = mpGraphics->CreateTexture("ImageTrailTexture", eTextureType_Rect, eTextureUsage_RenderTarget);	
		// Small high-refresh blend weights must survive between frames. An 8-bit
		// history can round them away and leave a permanent ghost of old images.
		mpAccumBuffer = mpGraphics->CreateFrameBuffer("ImageTrailBuffer");
		const ePixelFormat historyFormats[] = {ePixelFormat_RGB32, ePixelFormat_RGB16, ePixelFormat_RGB};
		const int firstFormat = mpLowLevelGraphics->GetCaps(eGraphicCaps_TextureFloat) ? 0 : 2;
		bool bBufferValid = false;
		for(int i=firstFormat; i<3 && !bBufferValid; ++i)
		{
			// Some older drivers support float textures but not float render
			// targets. Validate each fallback, retaining their original path.
			const bool bTextureCreated = mpAccumTexture->CreateFromRawData(
				cVector3l(vSize.x,vSize.y,1), historyFormats[i], NULL);
			mpAccumBuffer->SetTexture2D(0, mpAccumTexture);
			bBufferValid = bTextureCreated && mpAccumBuffer->CompileAndValidate();
		}
		if(!bBufferValid)
		{
			Error("Could not compile and validate image trail frame buffer!\n");
		}

		mpImageTrailType = static_cast<cPostEffectType_ImageTrail*>(mpType);

		mbClearFrameBuffer = true;
	}

	//-----------------------------------------------------------------------

	cPostEffect_ImageTrail::~cPostEffect_ImageTrail()
	{

	}

	//-----------------------------------------------------------------------

	void cPostEffect_ImageTrail::Reset()
	{
		mbClearFrameBuffer = true;
	}

	//-----------------------------------------------------------------------

	bool cPostEffect_ImageTrail::ResizeScreenBuffers()
	{
		const cVector2l vSize = mpLowLevelGraphics->GetScreenSizeInt();
		if(!mpGraphics->ResizeRenderTexture(mpAccumTexture, vSize)) return false;
		mpAccumBuffer->SetSize(vSize);
		Reset();
		return mpAccumBuffer->CompileAndValidate();
	}

	//-----------------------------------------------------------------------

	void cPostEffect_ImageTrail::OnSetParams()
	{
		
	}

	//-----------------------------------------------------------------------

	void cPostEffect_ImageTrail::OnSetActive(bool abX)
	{
		if(abX == false)
		{
			Reset();
		}
	}

	//-----------------------------------------------------------------------

	float cPostEffect_ImageTrail::GetFrameBlendAlpha(float afAmount, float afFrameTime)
	{
		// Repeated renders with no elapsed time must not advance the history.
		if(!(afFrameTime > 0.0f)) return 0.0f;
		if(!(afAmount > 0.0f) || !std::isfinite(afAmount) || !std::isfinite(afFrameTime))
			return 1.0f;

		// Retail blends exp(-amount * 0.015 / dt) of the current image into
		// history. Preserve that weight at 60 Hz, then decay the retained history
		// exponentially with elapsed time. Scaling the exponent by FPS instead
		// makes trails arbitrarily persistent as rendering gets faster.
		const double fReferenceAlpha = std::exp(-static_cast<double>(afAmount) * 0.9);
		return static_cast<float>(-std::expm1(std::log1p(-fReferenceAlpha) *
			static_cast<double>(afFrameTime) * 60.0));
	}

	//-----------------------------------------------------------------------


	iTexture* cPostEffect_ImageTrail::RenderEffect(iTexture *apInputTexture, iFrameBuffer *apFinalTempBuffer)
	{
		/////////////////////////
		// Init render states
		mpCurrentComposite->SetFlatProjection();
		mpCurrentComposite->SetBlendMode(eMaterialBlendMode_Alpha);
		mpCurrentComposite->SetChannelMode(eMaterialChannelMode_RGBA);
		mpCurrentComposite->SetTextureRange(NULL,0);

		/////////////////////////
		// Render to accumulation buffer
		mpCurrentComposite->SetFrameBuffer(mpAccumBuffer);
		
		mpCurrentComposite->SetProgram(mpImageTrailType->mpProgram);
		if(mbClearFrameBuffer)
		{
			mpCurrentComposite->ClearFrameBuffer(eClearFrameBufferFlag_Color, true);
			mbClearFrameBuffer = false;
			if(mpImageTrailType->mpProgram)
				mpImageTrailType->mpProgram->SetFloat(kVar_afAlpha, 1.0f);
		}
		else
		{
			const float fAmount = GetFrameBlendAlpha(mParams.mfAmount,
				mpCurrentComposite->GetCurrentFrameTime());
			if(mpImageTrailType->mpProgram)
				mpImageTrailType->mpProgram->SetFloat(kVar_afAlpha, fAmount);
		}
		
		mpCurrentComposite->SetTexture(0, apInputTexture);
				
		DrawQuad(0, 1, apInputTexture, true);

		mpCurrentComposite->SetProgram(NULL);
		mpCurrentComposite->SetBlendMode(eMaterialBlendMode_None);
		
		return mpAccumTexture;
	}

	//-----------------------------------------------------------------------

}
