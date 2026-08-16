/*
 * Copyright © 2009-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent. If not, see <https://www.gnu.org/licenses/>.
 */

#include "graphics/MaterialType_Atmosphere.h"

#include "graphics/Graphics.h"
#include "graphics/GPUProgram.h"
#include "graphics/Renderer.h"
#include "graphics/Renderable.h"
#include "graphics/Texture.h"

#include "math/Math.h"
#include "math/Frustum.h"

#include "resources/Resources.h"

#include "scene/Light.h"
#include "scene/LightSun.h"
#include "scene/World.h"

#include "system/PreprocessParser.h"

namespace hpl {

	namespace {

		// Variable ids are local to each atmosphere GPU program.
		enum eAtmosphereProgramVariable
		{
			kVar_avCameraPosition = 0,
			kVar_avSunDirection,
			kVar_avSunColor,
			kVar_avGroundRadii,
			kVar_avAtmosphereRadii,
			kVar_avRayleighScattering,
			kVar_afMieScattering,
			kVar_afMieExtinction,
			kVar_afRayleighScaleHeight,
			kVar_afMieScaleHeight,
			kVar_afMieAnisotropy,
			kVar_afExposure,
			kVar_afCloudEnabled,
			kVar_avCloudTextureLayout,
			kVar_afCloudBaseHeight,
			kVar_afCloudMaxHeight,
			kVar_afCloudCoverageThreshold,
			kVar_afCloudExtinction,
			kVar_afCloudAmbient,
			kVar_afCloudSunIntensity,
			kVar_afCloudSelfShadow,
			kVar_afMultipleScatteringStrength,
			kVar_afAerosolDensity,
			kVar_afCloudShadowStrength,
			kVar_afCloudShadowSoftness,
			kVar_afCloudTwilightStrength,
			kVar_avCloudTwilightColor
		};

		// This is the validated Earth baseline. Keeping every fallback here makes
		// constructor, editor, loader, and documentation defaults unambiguous.
		const cVector3f kDefaultGroundRadii(6378150.0f, 6356750.0f, 6378150.0f);
		const cVector3f kDefaultAtmosphereRadii(6478150.0f, 6456750.0f, 6478150.0f);
		const cVector3f kDefaultRayleighScattering(
			0.000005802f, 0.000013558f, 0.000033100f);
		const float kDefaultMieScattering = 0.000003996f;
		const float kDefaultMieExtinction = 0.000004440f;
		const float kDefaultRayleighScaleHeight = 8000.0f;
		const float kDefaultMieScaleHeight = 1200.0f;
		const float kDefaultMieAnisotropy = 0.8f;
		const float kDefaultAerosolDensity = 2.0f;
		const float kDefaultExposure = 1.6f;
		const float kDefaultMultipleScatteringStrength = 0.85f;
		const cVector2f kDefaultCloudArrayTileGrid(3.0f, 2.0f);
		const float kDefaultCloudBaseHeight = 1200.0f;
		const float kDefaultCloudMaxHeight = 10000.0f;
		const float kDefaultCloudCoverageThreshold = 0.065f;
		const float kDefaultCloudExtinction = 0.00020f;
		const float kDefaultCloudAmbient = 0.2f;
		const float kDefaultCloudSunIntensity = 0.75f;
		const float kDefaultCloudTwilightStrength = 0.055f;
		const cVector3f kDefaultCloudTwilightColor(0.40f, 0.58f, 0.88f);
		const float kDefaultCloudSelfShadow = 1.4f;
		const float kDefaultCloudShadowStrength = 0.65f;
		const float kDefaultCloudShadowSoftness = 1.5f;

	}

	//////////////////////////////////////////////////////////////////////////
	// VARIABLES
	//////////////////////////////////////////////////////////////////////////

	cMaterialType_Atmosphere_Vars::cMaterialType_Atmosphere_Vars()
		: mvGroundRadii(kDefaultGroundRadii),
		  mvAtmosphereRadii(kDefaultAtmosphereRadii),
		  mvRayleighScattering(kDefaultRayleighScattering),
		  mfMieScattering(kDefaultMieScattering),
		  mfMieExtinction(kDefaultMieExtinction),
		  mfRayleighScaleHeight(kDefaultRayleighScaleHeight),
		  mfMieScaleHeight(kDefaultMieScaleHeight),
		  mfMieAnisotropy(kDefaultMieAnisotropy),
		  mfAerosolDensity(kDefaultAerosolDensity),
		  mfExposure(kDefaultExposure),
		  mfMultipleScatteringStrength(kDefaultMultipleScatteringStrength),
		  mvCloudArrayTileGrid(kDefaultCloudArrayTileGrid),
		  mfCloudBaseHeight(kDefaultCloudBaseHeight),
		  mfCloudMaxHeight(kDefaultCloudMaxHeight),
		  mfCloudCoverageThreshold(kDefaultCloudCoverageThreshold),
		  mfCloudExtinction(kDefaultCloudExtinction),
		  mfCloudAmbient(kDefaultCloudAmbient),
		  mfCloudSunIntensity(kDefaultCloudSunIntensity),
		  mfCloudTwilightStrength(kDefaultCloudTwilightStrength),
		  mvCloudTwilightColor(kDefaultCloudTwilightColor),
		  mfCloudSelfShadow(kDefaultCloudSelfShadow),
		  mfCloudShadowStrength(kDefaultCloudShadowStrength),
		  mfCloudShadowSoftness(kDefaultCloudShadowSoftness)
	{
	}

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTION
	//////////////////////////////////////////////////////////////////////////

	cMaterialType_Atmosphere::cMaterialType_Atmosphere(cGraphics *apGraphics,
											 cResources *apResources)
		: iMaterialType(apGraphics, apResources),
		  mpTransmittanceProgram(NULL),
		  mpScatteringProgram(NULL)
	{
		mbIsTranslucent = true;
		AddUsedTexture(eMaterialTexture_Diffuse);

		AddVarVec3("GroundRadii", kDefaultGroundRadii,
			"Local-space ground ellipsoid radii. HPL uses Y as the polar axis.");
		AddVarVec3("AtmosphereRadii", kDefaultAtmosphereRadii,
			"Local-space outer atmosphere ellipsoid radii; these should match the proxy mesh.");
		AddVarVec3("RayleighScattering", kDefaultRayleighScattering,
			"Sea-level Rayleigh scattering coefficients in inverse local distance units.");
		AddVarFloat("MieScattering", kDefaultMieScattering,
			"Sea-level Mie scattering coefficient in inverse local distance units.");
		AddVarFloat("MieExtinction", kDefaultMieExtinction,
			"Sea-level Mie extinction coefficient in inverse local distance units.");
		AddVarFloat("RayleighScaleHeight", kDefaultRayleighScaleHeight,
			"Rayleigh density scale height in local distance units.");
		AddVarFloat("MieScaleHeight", kDefaultMieScaleHeight,
			"Mie density scale height in local distance units.");
		AddVarFloat("MieAnisotropy", kDefaultMieAnisotropy,
			"Mie forward-scattering asymmetry, clamped to -0.99 through 0.99.");
		AddVarFloat("AerosolDensity", kDefaultAerosolDensity,
			"Multiplier for Mie density, coupling aerosol scattering and extinction.");
		AddVarFloat("Exposure", kDefaultExposure,
			"Final in-scattered light multiplier for the LDR accumulation buffer.");
		AddVarFloat("MultipleScatteringStrength", kDefaultMultipleScatteringStrength,
			"Low-order isotropic multiple-scattering approximation; zero disables it.");
		AddVarVec2("CloudArrayTileGrid", kDefaultCloudArrayTileGrid,
			"Horizontal and vertical tile count of the cloud 2D texture array.");
		AddVarFloat("CloudBaseHeight", kDefaultCloudBaseHeight,
			"Height of the cloud base above the ground ellipsoid.");
		AddVarFloat("CloudMaxHeight", kDefaultCloudMaxHeight,
			"Maximum map-driven cloud height above CloudBaseHeight.");
		AddVarFloat("CloudCoverageThreshold", kDefaultCloudCoverageThreshold,
			"Cloud-map luminance at or below which the volume is empty.");
		AddVarFloat("CloudExtinction", kDefaultCloudExtinction,
			"Cloud opacity per local distance unit; higher values make clouds denser.");
		AddVarFloat("CloudAmbient", kDefaultCloudAmbient,
			"Sun-tinted multiple-scattering approximation on the illuminated side.");
		AddVarFloat("CloudSunIntensity", kDefaultCloudSunIntensity,
			"Direct sunlight multiplier for clouds.");
		AddVarFloat("CloudTwilightStrength", kDefaultCloudTwilightStrength,
			"Atmospheric sky-light strength on clouds around the day-night terminator.");
		AddVarVec3("CloudTwilightColor", kDefaultCloudTwilightColor,
			"RGB tint of the indirect twilight illumination on clouds.");
		AddVarFloat("CloudSelfShadow", kDefaultCloudSelfShadow,
			"Approximate darkening below cloud tops; zero disables it.");
		AddVarFloat("CloudShadowStrength", kDefaultCloudShadowStrength,
			"Fraction of direct surface illumination removed by projected cloud shadows.");
		AddVarFloat("CloudShadowSoftness", kDefaultCloudShadowSoftness,
			"Additional cloud-map mip levels used for projected surface shadows.");
	}

	cMaterialType_Atmosphere::~cMaterialType_Atmosphere()
	{
	}

	//////////////////////////////////////////////////////////////////////////
	// PROGRAM
	//////////////////////////////////////////////////////////////////////////

	void cMaterialType_Atmosphere::LoadData()
	{
		cParserVarContainer transmittanceVars;
		cParserVarContainer scatteringVars;
		scatteringVars.Add("AtmosphereScatteringPass");

		mpTransmittanceProgram = mpGraphics->CreateGpuProgramFromShaders(
			"AtmosphereTransmittance", "atmosphere_vtx.glsl", "atmosphere_frag.glsl",
			&transmittanceVars);
		mpScatteringProgram = mpGraphics->CreateGpuProgramFromShaders(
			"AtmosphereScattering", "atmosphere_vtx.glsl", "atmosphere_frag.glsl",
			&scatteringVars);

		iGpuProgram *pPrograms[2] = {
			mpTransmittanceProgram, mpScatteringProgram };
		for(int i = 0; i < 2; ++i)
		{
			iGpuProgram *pProgram = pPrograms[i];
			if(pProgram == NULL) continue;

			pProgram->GetVariableAsId("avCameraPosition", kVar_avCameraPosition);
			pProgram->GetVariableAsId("avSunDirection", kVar_avSunDirection);
			pProgram->GetVariableAsId("avSunColor", kVar_avSunColor);
			pProgram->GetVariableAsId("avGroundRadii", kVar_avGroundRadii);
			pProgram->GetVariableAsId("avAtmosphereRadii", kVar_avAtmosphereRadii);
			pProgram->GetVariableAsId("avRayleighScattering", kVar_avRayleighScattering);
			pProgram->GetVariableAsId("afMieExtinction", kVar_afMieExtinction);
			pProgram->GetVariableAsId("afRayleighScaleHeight", kVar_afRayleighScaleHeight);
			pProgram->GetVariableAsId("afMieScaleHeight", kVar_afMieScaleHeight);
			pProgram->GetVariableAsId("afCloudEnabled", kVar_afCloudEnabled);
			pProgram->GetVariableAsId("avCloudTextureLayout", kVar_avCloudTextureLayout);
			pProgram->GetVariableAsId("afCloudBaseHeight", kVar_afCloudBaseHeight);
			pProgram->GetVariableAsId("afCloudMaxHeight", kVar_afCloudMaxHeight);
			pProgram->GetVariableAsId("afCloudCoverageThreshold", kVar_afCloudCoverageThreshold);
			pProgram->GetVariableAsId("afCloudExtinction", kVar_afCloudExtinction);
			pProgram->GetVariableAsId("afAerosolDensity", kVar_afAerosolDensity);
			pProgram->SetSamplerToUnit("aCloudMap", 0);
		}

		if(mpTransmittanceProgram)
		{
			mpTransmittanceProgram->GetVariableAsId("afCloudShadowStrength",
				kVar_afCloudShadowStrength);
			mpTransmittanceProgram->GetVariableAsId("afCloudShadowSoftness",
				kVar_afCloudShadowSoftness);
		}

		if(mpScatteringProgram)
		{
			mpScatteringProgram->GetVariableAsId("afMieScattering",
				kVar_afMieScattering);
			mpScatteringProgram->GetVariableAsId("afMieAnisotropy",
				kVar_afMieAnisotropy);
			mpScatteringProgram->GetVariableAsId("afExposure", kVar_afExposure);
			mpScatteringProgram->GetVariableAsId("afCloudAmbient", kVar_afCloudAmbient);
			mpScatteringProgram->GetVariableAsId("afCloudSunIntensity",
				kVar_afCloudSunIntensity);
			mpScatteringProgram->GetVariableAsId("afCloudSelfShadow",
				kVar_afCloudSelfShadow);
			mpScatteringProgram->GetVariableAsId("afMultipleScatteringStrength",
				kVar_afMultipleScatteringStrength);
			mpScatteringProgram->GetVariableAsId("afCloudTwilightStrength",
				kVar_afCloudTwilightStrength);
			mpScatteringProgram->GetVariableAsId("avCloudTwilightColor",
				kVar_avCloudTwilightColor);
		}
	}

	void cMaterialType_Atmosphere::DestroyData()
	{
		if(mpTransmittanceProgram)
		{
			mpGraphics->DestroyGpuProgram(mpTransmittanceProgram);
			mpTransmittanceProgram = NULL;
		}
		if(mpScatteringProgram)
		{
			mpGraphics->DestroyGpuProgram(mpScatteringProgram);
			mpScatteringProgram = NULL;
		}
	}

	void cMaterialType_Atmosphere::DestroyProgram(cMaterial *apMaterial,
											 eMaterialRenderMode aRenderMode,
											 iGpuProgram* apProgram, char alSkeleton)
	{
		// The atmosphere program is shared by every atmosphere material and is
		// owned by this material type, not by an individual cMaterial instance.
	}

	iGpuProgram* cMaterialType_Atmosphere::GetGpuProgram(cMaterial *apMaterial,
												eMaterialRenderMode aRenderMode,
												char alSkeleton)
	{
		if(aRenderMode == eMaterialRenderMode_Diffuse)
			return mpTransmittanceProgram;
		if(aRenderMode == eMaterialRenderMode_Illumination)
			return mpScatteringProgram;
		return NULL;
	}

	iTexture* cMaterialType_Atmosphere::GetTextureForUnit(cMaterial *apMaterial,
												 eMaterialRenderMode aRenderMode,
												 int alUnit)
	{
		if((aRenderMode == eMaterialRenderMode_Diffuse ||
			aRenderMode == eMaterialRenderMode_Illumination) && alUnit == 0)
			return apMaterial->GetTexture(eMaterialTexture_Diffuse);
		return NULL;
	}

	//////////////////////////////////////////////////////////////////////////
	// SETUP
	//////////////////////////////////////////////////////////////////////////

	void cMaterialType_Atmosphere::SetupTypeSpecificData(eMaterialRenderMode aRenderMode,
												 iGpuProgram* apProgram,
												 iRenderer* apRenderer)
	{
	}

	static cVector3f PositiveRadii(const cVector3f& avRadii)
	{
		return cVector3f(
			cMath::Max(cMath::Abs(avRadii.x), 0.0001f),
			cMath::Max(cMath::Abs(avRadii.y), 0.0001f),
			cMath::Max(cMath::Abs(avRadii.z), 0.0001f));
	}

	void cMaterialType_Atmosphere::SetupMaterialSpecificData(eMaterialRenderMode aRenderMode,
														 iGpuProgram* apProgram,
														 cMaterial* apMaterial,
														 iRenderer* apRenderer)
	{
		cMaterialType_Atmosphere_Vars *pVars =
			static_cast<cMaterialType_Atmosphere_Vars*>(apMaterial->GetVars());

		apProgram->SetVec3f(kVar_avGroundRadii, PositiveRadii(pVars->mvGroundRadii));
		apProgram->SetVec3f(kVar_avAtmosphereRadii, PositiveRadii(pVars->mvAtmosphereRadii));
		apProgram->SetVec3f(kVar_avRayleighScattering,
			cVector3f(cMath::Max(pVars->mvRayleighScattering.x, 0.0f),
					  cMath::Max(pVars->mvRayleighScattering.y, 0.0f),
					  cMath::Max(pVars->mvRayleighScattering.z, 0.0f)));
		apProgram->SetFloat(kVar_afMieExtinction, cMath::Max(pVars->mfMieExtinction, 0.0f));
		apProgram->SetFloat(kVar_afRayleighScaleHeight,
			cMath::Max(pVars->mfRayleighScaleHeight, 0.0001f));
		apProgram->SetFloat(kVar_afMieScaleHeight,
			cMath::Max(pVars->mfMieScaleHeight, 0.0001f));
		apProgram->SetFloat(kVar_afAerosolDensity,
			cMath::Max(pVars->mfAerosolDensity, 0.0f));

		iTexture *pCloudTexture = apMaterial->GetTexture(eMaterialTexture_Diffuse);
		const bool bCloudEnabled = pCloudTexture != NULL &&
			pCloudTexture->GetType() == eTextureType_2DArray;
		apProgram->SetFloat(kVar_afCloudEnabled, bCloudEnabled ? 1.0f : 0.0f);

		float fCloudTileWidth = 1.0f;
		float fCloudTileHeight = 1.0f;

		if(bCloudEnabled)
		{
			const cVector3l& vCloudSize = pCloudTexture->GetSize();
			fCloudTileWidth = (float)cMath::Max(vCloudSize.x, 1);
			fCloudTileHeight = (float)cMath::Max(vCloudSize.y, 1);
		}

		apProgram->SetVec4f(kVar_avCloudTextureLayout,
			cMath::Max(pVars->mvCloudArrayTileGrid.x, 1.0f),
			cMath::Max(pVars->mvCloudArrayTileGrid.y, 1.0f),
			fCloudTileWidth, fCloudTileHeight);

		apProgram->SetFloat(kVar_afCloudBaseHeight,
			cMath::Max(pVars->mfCloudBaseHeight, 0.0f));
		apProgram->SetFloat(kVar_afCloudMaxHeight,
			cMath::Max(pVars->mfCloudMaxHeight, 1.0f));
		apProgram->SetFloat(kVar_afCloudCoverageThreshold,
			cMath::Max(0.0f, cMath::Min(pVars->mfCloudCoverageThreshold, 0.999f)));
		apProgram->SetFloat(kVar_afCloudExtinction,
			cMath::Max(pVars->mfCloudExtinction, 0.0f));

		if(aRenderMode == eMaterialRenderMode_Illumination)
		{
			apProgram->SetFloat(kVar_afMieScattering,
				cMath::Max(pVars->mfMieScattering, 0.0f));
			apProgram->SetFloat(kVar_afMieAnisotropy,
				cMath::Max(-0.99f, cMath::Min(pVars->mfMieAnisotropy, 0.99f)));
			apProgram->SetFloat(kVar_afExposure,
				cMath::Max(pVars->mfExposure, 0.0f));
			apProgram->SetFloat(kVar_afMultipleScatteringStrength,
				cMath::Max(pVars->mfMultipleScatteringStrength, 0.0f));
			apProgram->SetFloat(kVar_afCloudAmbient,
				cMath::Max(pVars->mfCloudAmbient, 0.0f));
			apProgram->SetFloat(kVar_afCloudSunIntensity,
				cMath::Max(pVars->mfCloudSunIntensity, 0.0f));
			apProgram->SetFloat(kVar_afCloudTwilightStrength,
				cMath::Max(pVars->mfCloudTwilightStrength, 0.0f));
			apProgram->SetVec3f(kVar_avCloudTwilightColor,
				pVars->mvCloudTwilightColor);
			apProgram->SetFloat(kVar_afCloudSelfShadow,
				cMath::Max(pVars->mfCloudSelfShadow, 0.0f));
		}
		else
		{
			apProgram->SetFloat(kVar_afCloudShadowStrength,
				cMath::Max(0.0f, cMath::Min(pVars->mfCloudShadowStrength, 1.0f)));
			apProgram->SetFloat(kVar_afCloudShadowSoftness,
				cMath::Max(pVars->mfCloudShadowSoftness, 0.0f));
		}
	}

	void cMaterialType_Atmosphere::SetupObjectSpecificData(eMaterialRenderMode aRenderMode,
													   iGpuProgram* apProgram,
													   iRenderable* apObject,
													   iRenderer* apRenderer)
	{
		cFrustum *pFrustum = apRenderer->GetCurrentFrustum();
		cMatrixf *pModelMatrix = apObject->GetModelMatrix(pFrustum);
		// Static identity-transformed submeshes intentionally return NULL here;
		// the renderer uses that as its fast-path representation of identity.
		const cMatrixf mtxInvModel = pModelMatrix ?
			cMath::MatrixInverse(*pModelMatrix) : cMatrixf::Identity;

		const cVector3f vCameraLocal = cMath::MatrixMul(mtxInvModel, pFrustum->GetOrigin());
		apProgram->SetVec3f(kVar_avCameraPosition, vCameraLocal);

		cVector3f vSunDirectionWorld(1.0f, 0.0f, 0.0f);
		cColor sunColor(0.0f, 0.0f, 0.0f, 1.0f);

		tLightList *pLights = apRenderer->GetCurrentWorld()->GetLightList();
		for(tLightListIt it = pLights->begin(); it != pLights->end(); ++it)
		{
			iLight *pLight = *it;
			if(pLight->GetLightType() != eLightType_Sun ||
			   pLight->IsActive() == false || pLight->IsVisible() == false)
				continue;

			cLightSun *pSun = static_cast<cLightSun*>(pLight);
			vSunDirectionWorld = pSun->GetSunDirection();
			sunColor = pLight->GetDiffuseColor();
			sunColor.r *= pSun->GetIntensity();
			sunColor.g *= pSun->GetIntensity();
			sunColor.b *= pSun->GetIntensity();
			break;
		}

		cVector3f vSunDirectionLocal =
			cMath::MatrixMul3x3(mtxInvModel, vSunDirectionWorld);
		vSunDirectionLocal.Normalize();

		apProgram->SetVec3f(kVar_avSunDirection, vSunDirectionLocal);
		apProgram->SetColor3f(kVar_avSunColor, sunColor);
	}

	//////////////////////////////////////////////////////////////////////////
	// MATERIAL VARIABLES
	//////////////////////////////////////////////////////////////////////////

	iMaterialVars* cMaterialType_Atmosphere::CreateSpecificVariables()
	{
		return hplNew(cMaterialType_Atmosphere_Vars, ());
	}

	void cMaterialType_Atmosphere::LoadVariables(cMaterial *apMaterial,
											cResourceVarsObject *apVars)
	{
		cMaterialType_Atmosphere_Vars *pVars =
			static_cast<cMaterialType_Atmosphere_Vars*>(apMaterial->GetVars());
		if(pVars == NULL)
		{
			pVars = static_cast<cMaterialType_Atmosphere_Vars*>(CreateSpecificVariables());
			apMaterial->SetVars(pVars);
		}

		pVars->mvGroundRadii = apVars->GetVarVector3f("GroundRadii",
			kDefaultGroundRadii);
		pVars->mvAtmosphereRadii = apVars->GetVarVector3f("AtmosphereRadii",
			kDefaultAtmosphereRadii);
		pVars->mvRayleighScattering = apVars->GetVarVector3f("RayleighScattering",
			kDefaultRayleighScattering);
		pVars->mfMieScattering = apVars->GetVarFloat("MieScattering",
			kDefaultMieScattering);
		pVars->mfMieExtinction = apVars->GetVarFloat("MieExtinction",
			kDefaultMieExtinction);
		pVars->mfRayleighScaleHeight = apVars->GetVarFloat("RayleighScaleHeight",
			kDefaultRayleighScaleHeight);
		pVars->mfMieScaleHeight = apVars->GetVarFloat("MieScaleHeight",
			kDefaultMieScaleHeight);
		pVars->mfMieAnisotropy = apVars->GetVarFloat("MieAnisotropy",
			kDefaultMieAnisotropy);
		pVars->mfAerosolDensity = apVars->GetVarFloat("AerosolDensity",
			kDefaultAerosolDensity);
		pVars->mfExposure = apVars->GetVarFloat("Exposure", kDefaultExposure);
		pVars->mfMultipleScatteringStrength =
			apVars->GetVarFloat("MultipleScatteringStrength",
				kDefaultMultipleScatteringStrength);
		pVars->mvCloudArrayTileGrid = apVars->GetVarVector2f(
			"CloudArrayTileGrid", kDefaultCloudArrayTileGrid);
		pVars->mvCloudArrayTileGrid.x = cMath::Max(
			pVars->mvCloudArrayTileGrid.x, 1.0f);
		pVars->mvCloudArrayTileGrid.y = cMath::Max(
			pVars->mvCloudArrayTileGrid.y, 1.0f);
		pVars->mfCloudBaseHeight = apVars->GetVarFloat("CloudBaseHeight",
			kDefaultCloudBaseHeight);
		pVars->mfCloudMaxHeight = apVars->GetVarFloat("CloudMaxHeight",
			kDefaultCloudMaxHeight);
		pVars->mfCloudCoverageThreshold = apVars->GetVarFloat(
			"CloudCoverageThreshold", kDefaultCloudCoverageThreshold);
		pVars->mfCloudExtinction = apVars->GetVarFloat("CloudExtinction",
			kDefaultCloudExtinction);
		pVars->mfCloudAmbient = apVars->GetVarFloat("CloudAmbient",
			kDefaultCloudAmbient);
		pVars->mfCloudSunIntensity = apVars->GetVarFloat("CloudSunIntensity",
			kDefaultCloudSunIntensity);
		pVars->mfCloudTwilightStrength =
			apVars->GetVarFloat("CloudTwilightStrength",
				kDefaultCloudTwilightStrength);
		pVars->mvCloudTwilightColor = apVars->GetVarVector3f("CloudTwilightColor",
			kDefaultCloudTwilightColor);
		pVars->mfCloudSelfShadow = apVars->GetVarFloat("CloudSelfShadow",
			kDefaultCloudSelfShadow);
		pVars->mfCloudShadowStrength =
			apVars->GetVarFloat("CloudShadowStrength", kDefaultCloudShadowStrength);
		pVars->mfCloudShadowSoftness =
			apVars->GetVarFloat("CloudShadowSoftness", kDefaultCloudShadowSoftness);
	}

	void cMaterialType_Atmosphere::GetVariableValues(cMaterial *apMaterial,
												 cResourceVarsObject *apVars)
	{
		cMaterialType_Atmosphere_Vars *pVars =
			static_cast<cMaterialType_Atmosphere_Vars*>(apMaterial->GetVars());

		apVars->AddVarVector3f("GroundRadii", pVars->mvGroundRadii);
		apVars->AddVarVector3f("AtmosphereRadii", pVars->mvAtmosphereRadii);
		apVars->AddVarVector3f("RayleighScattering", pVars->mvRayleighScattering);
		apVars->AddVarFloat("MieScattering", pVars->mfMieScattering);
		apVars->AddVarFloat("MieExtinction", pVars->mfMieExtinction);
		apVars->AddVarFloat("RayleighScaleHeight", pVars->mfRayleighScaleHeight);
		apVars->AddVarFloat("MieScaleHeight", pVars->mfMieScaleHeight);
		apVars->AddVarFloat("MieAnisotropy", pVars->mfMieAnisotropy);
		apVars->AddVarFloat("AerosolDensity", pVars->mfAerosolDensity);
		apVars->AddVarFloat("Exposure", pVars->mfExposure);
		apVars->AddVarFloat("MultipleScatteringStrength",
			pVars->mfMultipleScatteringStrength);
		apVars->AddVarVector2f("CloudArrayTileGrid", pVars->mvCloudArrayTileGrid);
		apVars->AddVarFloat("CloudBaseHeight", pVars->mfCloudBaseHeight);
		apVars->AddVarFloat("CloudMaxHeight", pVars->mfCloudMaxHeight);
		apVars->AddVarFloat("CloudCoverageThreshold", pVars->mfCloudCoverageThreshold);
		apVars->AddVarFloat("CloudExtinction", pVars->mfCloudExtinction);
		apVars->AddVarFloat("CloudAmbient", pVars->mfCloudAmbient);
		apVars->AddVarFloat("CloudSunIntensity", pVars->mfCloudSunIntensity);
		apVars->AddVarFloat("CloudTwilightStrength", pVars->mfCloudTwilightStrength);
		apVars->AddVarVector3f("CloudTwilightColor", pVars->mvCloudTwilightColor);
		apVars->AddVarFloat("CloudSelfShadow", pVars->mfCloudSelfShadow);
		apVars->AddVarFloat("CloudShadowStrength", pVars->mfCloudShadowStrength);
		apVars->AddVarFloat("CloudShadowSoftness", pVars->mfCloudShadowSoftness);
	}

	void cMaterialType_Atmosphere::CompileMaterialSpecifics(cMaterial *apMaterial)
	{
		// The normal translucent pass multiplies the existing framebuffer by
		// wavelength-dependent transmission. HPL's built-in illumination pass then
		// adds atmospheric and cloud in-scattering over that attenuated surface.
		apMaterial->SetBlendMode(eMaterialBlendMode_Mul);
		apMaterial->SetHasTranslucentIllumination(true);
		apMaterial->SetAffectedByFog(false);
		apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse, true);
		apMaterial->SetHasObjectSpecificsSettings(eMaterialRenderMode_Diffuse, true);
		apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Illumination, true);
		apMaterial->SetHasObjectSpecificsSettings(eMaterialRenderMode_Illumination, true);
	}

}
