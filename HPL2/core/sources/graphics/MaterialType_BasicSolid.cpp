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

#include "graphics/MaterialType_BasicSolid.h"

#include "system/LowLevelSystem.h"
#include "system/PreprocessParser.h"

#include "resources/Resources.h"
#include "resources/TextureManager.h"

#include "math/Frustum.h"
#include "math/Math.h"

#include "graphics/Graphics.h"
#include "graphics/GPUShader.h"
#include "graphics/GPUProgram.h"
#include "graphics/LowLevelGraphics.h"
#include "graphics/Renderer.h"
#include "graphics/Material.h"
#include "graphics/ProgramComboManager.h"
#include "graphics/Renderable.h"
#include "graphics/RendererDeferred.h"
#include "graphics/Texture.h"


namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// DEFINES
	//////////////////////////////////////////////////////////////////////////
	
	//------------------------------
	// Variables
	//------------------------------
	#define kVar_afInvFarPlane					0
	#define kVar_avHeightMapScaleAndBias		1
	#define kVar_a_mtxUV						2	
	#define kVar_afColorMul						3
	#define kVar_afDissolveAmount				4
	#define kVar_avFrenselBiasPow				5
	#define kVar_a_mtxInvViewRotation			6
	#define kVar_avDiffuseArrayTileGrid			7
	#define kVar_avDiffuseArrayTileSize			8
	#define kVar_avEllipsoidNormalScale			9
	#define kVar_avOceanSpecularParams			10
	#define kVar_afPlanetaryTwilightStrength	11
	#define kVar_avWaterTint					12
	#define kVar_afWaterTintStrength			13
	#define kVar_afWaterSaturation			14
	#define kVar_afWaterBrightness			15


	//------------------------------
	//Diffuse Features and data
	//------------------------------
	#define eFeature_Diffuse_NormalMaps		eFlagBit_0
	#define eFeature_Diffuse_Specular		eFlagBit_1
	#define eFeature_Diffuse_Parallax		eFlagBit_2
	#define eFeature_Diffuse_UvAnimation	eFlagBit_3
	#define eFeature_Diffuse_Skeleton		eFlagBit_4
	#define eFeature_Diffuse_EnvMap			eFlagBit_5
	#define eFeature_Diffuse_CubeMapAlpha	eFlagBit_6
	#define eFeature_Diffuse_Array			eFlagBit_7
	#define eFeature_Diffuse_Unlit			eFlagBit_8
	#define eFeature_Diffuse_EllipsoidNormals	eFlagBit_9
	#define eFeature_Diffuse_OceanSpecular		eFlagBit_10
	#define eFeature_Diffuse_PlanetaryTwilight	eFlagBit_11
	#define eFeature_Diffuse_WaterMask			eFlagBit_12
		
	#define kDiffuseFeatureNum 13

	static cProgramComboFeature vDiffuseFeatureVec[] =
	{
		cProgramComboFeature("UseNormalMapping", kPC_VertexBit | kPC_FragmentBit),
		cProgramComboFeature("UseSpecular", kPC_FragmentBit),		
		cProgramComboFeature("UseParallax", kPC_VertexBit | kPC_FragmentBit, eFeature_Diffuse_NormalMaps),							
		cProgramComboFeature("UseUvAnimation", kPC_VertexBit),							
		cProgramComboFeature("UseSkeleton",	kPC_VertexBit),	
		cProgramComboFeature("UseEnvMap", kPC_VertexBit | kPC_FragmentBit),
		cProgramComboFeature("UseCubeMapAlpha", kPC_FragmentBit),
		cProgramComboFeature("UseDiffuseArray", kPC_FragmentBit),
		cProgramComboFeature("UseUnlit", kPC_FragmentBit),
		cProgramComboFeature("UseEllipsoidNormals", kPC_VertexBit | kPC_FragmentBit),
		cProgramComboFeature("UseOceanSpecular", kPC_FragmentBit, eFeature_Diffuse_Specular),
		cProgramComboFeature("UsePlanetaryTwilight", kPC_FragmentBit),
		cProgramComboFeature("UseWaterMask", kPC_FragmentBit),
	};

	static bool UsesOceanSpecular(cMaterial *apMaterial,
		const cMaterialType_SolidDiffuse_Vars *apVars)
	{
		return apMaterial->GetTexture(eMaterialTexture_Specular) != NULL &&
			(cMath::Abs(apVars->mfSpecularIntensityScale - 1.0f) > kEpsilonf ||
			 cMath::Abs(apVars->mfSpecularGlossBias) > kEpsilonf ||
			 apVars->mfOceanSpecularBroadStrength > kEpsilonf);
	}

	//------------------------------
	//Illumination Features and data
	//------------------------------
	#define eFeature_Illum_UvAnimation	eFlagBit_0
	#define eFeature_Illum_Skeleton		eFlagBit_1
	#define eFeature_Illum_Array		eFlagBit_2

	#define kIllumFeatureNum 3

	cProgramComboFeature vIllumFeatureVec[] =
	{
		cProgramComboFeature("UseUvAnimation", kPC_VertexBit),							
		cProgramComboFeature("UseSkeleton",	kPC_VertexBit),							
		cProgramComboFeature("UseDiffuseArray", kPC_FragmentBit),
	};

	//------------------------------
	//Z Features and data
	//------------------------------
	#define eFeature_Z_UseAlpha					eFlagBit_0
	#define eFeature_Z_UvAnimation				eFlagBit_1
	#define eFeature_Z_Dissolve					eFlagBit_2
	#define eFeature_Z_DissolveAlpha			eFlagBit_3
	#define eFeature_Z_UseAlphaDissolveFilter	eFlagBit_4
	
	#define kZFeatureNum 5

	cProgramComboFeature vZFeatureVec[] =
	{
			cProgramComboFeature("UseAlphaMap",					kPC_FragmentBit),
			cProgramComboFeature("UseUvAnimation",				kPC_VertexBit),
			cProgramComboFeature("UseDissolve",					kPC_FragmentBit),
			cProgramComboFeature("UseDissolveAlphaMap",			kPC_FragmentBit),
			cProgramComboFeature("UseAlphaUseDissolveFilter",	kPC_FragmentBit)
	};

	//------------------------------
	
	//////////////////////////////////////////////////////////////////////////
	// STATIC OBJECTS
	//////////////////////////////////////////////////////////////////////////

	//--------------------------------------------------------------------------

	bool iMaterialType_SolidBase::mbGlobalDataCreated = false;
	cProgramComboManager* iMaterialType_SolidBase::mpGlobalProgramManager;

	float iMaterialType_SolidBase::mfVirtualPositionAddScale = 0.03f;

	//--------------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// SOLID BASE
	//////////////////////////////////////////////////////////////////////////

	//--------------------------------------------------------------------------

	iMaterialType_SolidBase::iMaterialType_SolidBase(cGraphics *apGraphics, cResources *apResources) : iMaterialType(apGraphics,apResources)
	{
		mbIsGlobalDataCreator = false;
	}

	//--------------------------------------------------------------------------

	iMaterialType_SolidBase::~iMaterialType_SolidBase()
	{

	}

	//--------------------------------------------------------------------------

	void iMaterialType_SolidBase::DestroyProgram(cMaterial *apMaterial, eMaterialRenderMode aRenderMode, iGpuProgram* apProgram, char alSkeleton)
	{
		/////////////////////////////
		// Remove from global manager
		if(aRenderMode == eMaterialRenderMode_Z || aRenderMode ==  eMaterialRenderMode_Z_Dissolve)
		{
			mpGlobalProgramManager->DestroyGeneratedProgram(eMaterialRenderMode_Z, apProgram);
		}
		/////////////////////////////
		// Remove from normal manager
		else
		{
			mpProgramManager->DestroyGeneratedProgram(aRenderMode, apProgram);
		}
	}
	
	//--------------------------------------------------------------------------

	void iMaterialType_SolidBase::CreateGlobalPrograms()
	{
		if(mbGlobalDataCreated) return;

		mbGlobalDataCreated = true;
		mbIsGlobalDataCreator = true;

		/////////////////////////////
		//Load programs
		//This makes this material's program manager responsible for managing the global programs!
		cParserVarContainer defaultVars;
		defaultVars.Add("UseUv");
		
		mpProgramManager->SetupGenerateProgramData(	eMaterialRenderMode_Z,"Z","deferred_base_vtx.glsl", "deferred_base_frag.glsl", 
													vZFeatureVec,kZFeatureNum, defaultVars);
		
		mpProgramManager->AddGenerateProgramVariableId("a_mtxUV",kVar_a_mtxUV,eMaterialRenderMode_Z);
		mpProgramManager->AddGenerateProgramVariableId("afDissolveAmount",kVar_afDissolveAmount,eMaterialRenderMode_Z);

		mpGlobalProgramManager = mpProgramManager;
	}

	//--------------------------------------------------------------------------

	void iMaterialType_SolidBase::LoadData()
	{
		/////////////////////////////
		//Global data init (that is shared between Solid materials)
		CreateGlobalPrograms();

		//////////////
		// Create textures
		mpDissolveTexture = mpResources->GetTextureManager()->Create2D("core_dissolve.tga",true);


		LoadSpecificData();
	}

	//--------------------------------------------------------------------------

	void iMaterialType_SolidBase::DestroyData()
	{
		if(mpDissolveTexture) mpResources->GetTextureManager()->Destroy(mpDissolveTexture);

		//If this instace was global data creator, then it needs to be recreated.
		if(mbIsGlobalDataCreator)
		{
			mbGlobalDataCreated = false;
		}
		mpProgramManager->DestroyShadersAndPrograms();
	}

	//--------------------------------------------------------------------------

	void iMaterialType_SolidBase::LoadVariables(cMaterial *apMaterial, cResourceVarsObject *apVars)
	{

	}

	void iMaterialType_SolidBase::GetVariableValues(cMaterial *apMaterial, cResourceVarsObject *apVars)
	{

	}

	//--------------------------------------------------------------------------

	void iMaterialType_SolidBase::CompileMaterialSpecifics(cMaterial *apMaterial)
	{
		////////////////////////
		//If there is an alpha texture, set alpha mode to trans, else solid.
		if(apMaterial->GetTexture(eMaterialTexture_Alpha))
		{
			apMaterial->SetAlphaMode(eMaterialAlphaMode_Trans);
		}
		else
		{
			apMaterial->SetAlphaMode(eMaterialAlphaMode_Solid);
		}

		CompileSolidSpecifics(apMaterial);
	}

	//--------------------------------------------------------------------------


	//////////////////////////////////////////////////////////////////////////
	// SOLID DIFFUSE
	//////////////////////////////////////////////////////////////////////////
	
	//--------------------------------------------------------------------------
	
	cMaterialType_SolidDiffuse::cMaterialType_SolidDiffuse(cGraphics *apGraphics, cResources *apResources) : iMaterialType_SolidBase(apGraphics, apResources)
	{
		AddUsedTexture(eMaterialTexture_Diffuse);
		AddUsedTexture(eMaterialTexture_NMap);
		AddUsedTexture(eMaterialTexture_Alpha);
		AddUsedTexture(eMaterialTexture_Specular);
		AddUsedTexture(eMaterialTexture_Height);
		AddUsedTexture(eMaterialTexture_Illumination);
		AddUsedTexture(eMaterialTexture_DissolveAlpha);
		AddUsedTexture(eMaterialTexture_CubeMap);
		AddUsedTexture(eMaterialTexture_CubeMapAlpha);
		AddUsedTexture(eMaterialTexture_WaterMask);

		mbHasTypeSpecifics[eMaterialRenderMode_Diffuse] = true;

		AddVarFloat("HeightMapScale", 0.05f, "");
		AddVarFloat("HeightMapBias", 0, "");
		AddVarFloat("FrenselBias", 0.2f, "Bias for Fresnel term. values: 0-1. Higher means that more of reflection is seen when looking straight at object.");
		AddVarFloat("FrenselPow", 8.0f, "The higher the 'sharper' the reflection is, meaning that it is only clearly seen at sharp angles.");
		AddVarBool("AlphaDissolveFilter", false, "If alpha values between 0 and 1 should be used and dissolve the texture. This can be useful for things like hair.");
		AddVarBool("Unlit", false, "Render the diffuse texture without scene lighting.");
		AddVarBool("UseEllipsoidNormals", false, "Derive smooth normals from an origin-centered ellipsoid instead of using mesh normals.");
		AddVarVec3("EllipsoidRadii", cVector3f(1.0f), "Relative local-space ellipsoid radii. Common scale does not matter.");
		AddVarInt("DiffuseArrayTileColumns", 3, "Number of horizontal tiles in a 2D array diffuse texture.");
		AddVarInt("DiffuseArrayTileRows", 2, "Number of vertical tiles in a 2D array diffuse texture.");
		AddVarFloat("SpecularIntensityScale", 1.0f, "Multiplier applied to the specular map intensity channel.");
		AddVarFloat("SpecularGlossBias", 0.0f, "Offset applied to the specular map gloss channel before lighting.");
		AddVarFloat("OceanSpecularBroadStrength", 0.0f, "Strength of the broad low-energy ocean glint shoulder.");
		AddVarFloat("PlanetaryTwilightStrength", 0.0f, "Low-energy atmospheric sky irradiance around a planetary terminator; zero disables it.");
		AddVarVec3("WaterTint", cVector3f(1.0f), "Multiplicative RGB tint applied only where the water mask is white.");
		AddVarFloat("WaterTintStrength", 0.0f, "Blend strength of WaterTint; zero applies no tint and one applies the full tint.");
		AddVarFloat("WaterSaturation", 1.0f, "Water-only color saturation; zero is grayscale and one preserves the source saturation.");
		AddVarFloat("WaterBrightness", 1.0f, "Water-only diffuse brightness multiplier.");
	}
	
	//--------------------------------------------------------------------------

	cMaterialType_SolidDiffuse::~cMaterialType_SolidDiffuse()
	{
	}

	//--------------------------------------------------------------------------


	void cMaterialType_SolidDiffuse::LoadSpecificData()
	{
		/////////////////////////////
		//Load Diffuse programs
		cParserVarContainer defaultVars;
		defaultVars.Add("UseUv");
		defaultVars.Add("UseNormals");
		defaultVars.Add("UseDepth");
		defaultVars.Add("VirtualPositionAddScale",mfVirtualPositionAddScale);
		
		//Get the G-buffer type
		if(cRendererDeferred::GetGBufferType() == eDeferredGBuffer_32Bit)	defaultVars.Add("Deferred_32bit");
		else																defaultVars.Add("Deferred_64bit");

		//Set up number of gbuffer textures used
		if(cRendererDeferred::GetNumOfGBufferTextures() == 4)	defaultVars.Add("RenderTargets_4");
		else													defaultVars.Add("RenderTargets_3");

		//Set up relief mapping method
		if(	iRenderer::GetParallaxQuality() != eParallaxQuality_Low &&
			mpGraphics->GetLowLevel()->GetCaps(eGraphicCaps_ShaderModel_3)!=0) 
		{
			defaultVars.Add("ParallaxMethod_Relief");
		}
		else														
		{
			defaultVars.Add("ParallaxMethod_Simple");
		}

		
		
		mpProgramManager->SetupGenerateProgramData(	eMaterialRenderMode_Diffuse,"Diffuse","deferred_base_vtx.glsl", "deferred_gbuffer_solid_frag.glsl", 
													vDiffuseFeatureVec,kDiffuseFeatureNum, defaultVars);

		/////////////////////////////
		//Load Illumination programs
		defaultVars.Clear();
		defaultVars.Add("UseUv");
		mpProgramManager->SetupGenerateProgramData(	eMaterialRenderMode_Illumination,"Illum","deferred_base_vtx.glsl", "deferred_illumination_frag.glsl", 
													vIllumFeatureVec,kIllumFeatureNum, defaultVars);

		
		////////////////////////////////
		//Set up variable ids
		mpProgramManager->AddGenerateProgramVariableId("afInvFarPlane",kVar_afInvFarPlane,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("avHeightMapScaleAndBias",kVar_avHeightMapScaleAndBias, eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("a_mtxUV",kVar_a_mtxUV,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("avFrenselBiasPow", kVar_avFrenselBiasPow,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("a_mtxInvViewRotation", kVar_a_mtxInvViewRotation,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("avDiffuseArrayTileGrid", kVar_avDiffuseArrayTileGrid,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("avDiffuseArrayTileSize", kVar_avDiffuseArrayTileSize,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("avEllipsoidNormalScale", kVar_avEllipsoidNormalScale,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("avOceanSpecularParams", kVar_avOceanSpecularParams,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("afPlanetaryTwilightStrength", kVar_afPlanetaryTwilightStrength,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("avWaterTint", kVar_avWaterTint,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("afWaterTintStrength", kVar_afWaterTintStrength,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("afWaterSaturation", kVar_afWaterSaturation,eMaterialRenderMode_Diffuse);
		mpProgramManager->AddGenerateProgramVariableId("afWaterBrightness", kVar_afWaterBrightness,eMaterialRenderMode_Diffuse);

		mpProgramManager->AddGenerateProgramVariableId("a_mtxUV",kVar_a_mtxUV,eMaterialRenderMode_Illumination);
		mpProgramManager->AddGenerateProgramVariableId("afColorMul",kVar_afColorMul,eMaterialRenderMode_Illumination);
		mpProgramManager->AddGenerateProgramVariableId("avDiffuseArrayTileGrid", kVar_avDiffuseArrayTileGrid,eMaterialRenderMode_Illumination);
		mpProgramManager->AddGenerateProgramVariableId("avDiffuseArrayTileSize", kVar_avDiffuseArrayTileSize,eMaterialRenderMode_Illumination);
	}

	//--------------------------------------------------------------------------

	void cMaterialType_SolidDiffuse::CompileSolidSpecifics(cMaterial *apMaterial)
	{
		cMaterialType_SolidDiffuse_Vars *pVars = (cMaterialType_SolidDiffuse_Vars*)apMaterial->GetVars();

		//////////////////////////////////
		//Z specifics
		apMaterial->SetHasObjectSpecificsSettings(eMaterialRenderMode_Z_Dissolve,true);
		apMaterial->SetUseAlphaDissolveFilter(pVars->mbAlphaDissolveFilter);
		
		//////////////////////////////////
		//Normal map and height specifics
		if(apMaterial->GetTexture(eMaterialTexture_NMap))
		{
			if(apMaterial->GetTexture(eMaterialTexture_Height))
			{
				apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
			}
		}

		//////////////////////////////////
		//Uv animation specifics
		if(apMaterial->HasUvAnimation())
		{
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Z,true);
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Illumination,true);
		}

		//////////////////////////////////
		//Cubemap
		if(apMaterial->GetTexture(eMaterialTexture_CubeMap))
		{
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
		}

		//////////////////////////////////
		//2D texture array diffuse map
		if(apMaterial->GetTexture(eMaterialTexture_Diffuse) &&
			apMaterial->GetTexture(eMaterialTexture_Diffuse)->GetType() == eTextureType_2DArray)
		{
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
		}

		//////////////////////////////////
		//Unlit diffuse pass
		if(pVars->mbUnlit && apMaterial->GetTexture(eMaterialTexture_Diffuse))
		{
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
			apMaterial->SetHasObjectSpecificsSettings(eMaterialRenderMode_Illumination,true);

			if(apMaterial->GetTexture(eMaterialTexture_Diffuse)->GetType() == eTextureType_2DArray)
			{
				apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Illumination,true);
			}
		}

		//////////////////////////////////
		//Analytic ellipsoid normals
		if(pVars->mbUseEllipsoidNormals)
		{
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
		}

		//////////////////////////////////
		//Sun-only ocean specular model
		if(UsesOceanSpecular(apMaterial, pVars))
		{
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
		}

		//////////////////////////////////
		//Opt-in planetary twilight sky fill
		if(pVars->mfPlanetaryTwilightStrength > kEpsilonf)
		{
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
		}

		//////////////////////////////////
		//Optional water-only diffuse adjustments
		if(apMaterial->GetTexture(eMaterialTexture_WaterMask))
		{
			apMaterial->SetHasSpecificSettings(eMaterialRenderMode_Diffuse,true);
		}

		//////////////////////////////////
		//Illuminations specifics
		if(apMaterial->GetTexture(eMaterialTexture_Illumination))
		{
			apMaterial->SetHasObjectSpecificsSettings(eMaterialRenderMode_Illumination,true);
		}
	}

	//--------------------------------------------------------------------------

	
	iTexture* cMaterialType_SolidDiffuse::GetTextureForUnit(cMaterial *apMaterial,eMaterialRenderMode aRenderMode, int alUnit)
	{
		cMaterialType_SolidDiffuse_Vars *pVars = (cMaterialType_SolidDiffuse_Vars*)apMaterial->GetVars();

		////////////////////////////
		//Z
		if(aRenderMode == eMaterialRenderMode_Z)
		{
			switch(alUnit)
			{
			case 0: return apMaterial->GetTexture(eMaterialTexture_Alpha);
			case 1: return mpDissolveTexture;
			}
		}
		////////////////////////////
		//Z Dissolve
		else if(aRenderMode == eMaterialRenderMode_Z_Dissolve)
		{
			switch(alUnit)
			{
			case 0: return apMaterial->GetTexture(eMaterialTexture_Alpha);
			case 1: return mpDissolveTexture;
			case 2: return apMaterial->GetTexture(eMaterialTexture_DissolveAlpha);
			}
		}
		////////////////////////////
		//Diffuse
		else if(aRenderMode == eMaterialRenderMode_Diffuse)
		{
			switch(alUnit)
			{
			case 0: return apMaterial->GetTexture(eMaterialTexture_Diffuse);
			case 1: return apMaterial->GetTexture(eMaterialTexture_NMap);
			case 2: return apMaterial->GetTexture(eMaterialTexture_Specular);
			case 3: return apMaterial->GetTexture(eMaterialTexture_Height);
			case 4: return apMaterial->GetTexture(eMaterialTexture_CubeMap);
			case 5: return apMaterial->GetTexture(eMaterialTexture_CubeMapAlpha);
			case 6: return apMaterial->GetTexture(eMaterialTexture_WaterMask);
			}
		}
		////////////////////////////
		//Illumination
		else if(aRenderMode == eMaterialRenderMode_Illumination)
		{
			switch(alUnit)
			{
			case 0:
				if(pVars->mbUnlit && apMaterial->GetTexture(eMaterialTexture_Diffuse))
				{
					return apMaterial->GetTexture(eMaterialTexture_Diffuse);
				}
				return apMaterial->GetTexture(eMaterialTexture_Illumination);
			}
		}

		return NULL;
	}
	//--------------------------------------------------------------------------

	iTexture* cMaterialType_SolidDiffuse::GetSpecialTexture(cMaterial *apMaterial, eMaterialRenderMode aRenderMode,iRenderer *apRenderer, int alUnit)
	{
		return NULL;
	}
	
	//--------------------------------------------------------------------------
	
	iGpuProgram* cMaterialType_SolidDiffuse::GetGpuProgram(cMaterial *apMaterial, eMaterialRenderMode aRenderMode, char alSkeleton)
	{
		cMaterialType_SolidDiffuse_Vars *pVars = (cMaterialType_SolidDiffuse_Vars*)apMaterial->GetVars();

		////////////////////////////
		//Z
		if(aRenderMode == eMaterialRenderMode_Z)
		{
			tFlag lFlags =0;
			if(apMaterial->GetTexture(eMaterialTexture_Alpha))	lFlags |= eFeature_Z_UseAlpha;
			if(apMaterial->HasUvAnimation())					lFlags |= eFeature_Z_UvAnimation;
			if(pVars->mbAlphaDissolveFilter)					lFlags |= eFeature_Z_UseAlphaDissolveFilter;

			return mpGlobalProgramManager->GenerateProgram(eMaterialRenderMode_Z, lFlags);
		}
		////////////////////////////
		//Z Dissolve
		else if(aRenderMode == eMaterialRenderMode_Z_Dissolve)
		{
			tFlag lFlags =0;
			lFlags |= eFeature_Z_Dissolve;
			if(apMaterial->GetTexture(eMaterialTexture_Alpha))			lFlags |= eFeature_Z_UseAlpha;
			if(apMaterial->GetTexture(eMaterialTexture_DissolveAlpha))	lFlags |= eFeature_Z_DissolveAlpha;
			if(apMaterial->HasUvAnimation())							lFlags |= eFeature_Z_UvAnimation;
			if(pVars->mbAlphaDissolveFilter)							lFlags |= eFeature_Z_UseAlphaDissolveFilter;

			return mpGlobalProgramManager->GenerateProgram(eMaterialRenderMode_Z, lFlags);
		}
		////////////////////////////
		//Diffuse
		else if(aRenderMode == eMaterialRenderMode_Diffuse)
		{
			tFlag lFlags =0;
			if(apMaterial->GetTexture(eMaterialTexture_NMap))			lFlags |= eFeature_Diffuse_NormalMaps;
			if(apMaterial->GetTexture(eMaterialTexture_Specular))		lFlags |= eFeature_Diffuse_Specular;
			if(	apMaterial->GetTexture(eMaterialTexture_Height) && 
				iRenderer::GetParallaxEnabled())			    		lFlags |= eFeature_Diffuse_Parallax;
			if(apMaterial->GetTexture(eMaterialTexture_CubeMap))
			{	
				lFlags |= eFeature_Diffuse_EnvMap;
				if(apMaterial->GetTexture(eMaterialTexture_CubeMapAlpha))	lFlags |= eFeature_Diffuse_CubeMapAlpha;
			}
			if(apMaterial->HasUvAnimation())							lFlags |= eFeature_Diffuse_UvAnimation;
			if(apMaterial->GetTexture(eMaterialTexture_Diffuse) &&
				apMaterial->GetTexture(eMaterialTexture_Diffuse)->GetType() == eTextureType_2DArray)
			{
				lFlags |= eFeature_Diffuse_Array;
			}
			if(pVars->mbUnlit && apMaterial->GetTexture(eMaterialTexture_Diffuse))
			{
				lFlags |= eFeature_Diffuse_Unlit;
			}
			if(pVars->mbUseEllipsoidNormals)
			{
				lFlags |= eFeature_Diffuse_EllipsoidNormals;
			}
			if(UsesOceanSpecular(apMaterial, pVars))
			{
				lFlags |= eFeature_Diffuse_OceanSpecular;
			}
			if(pVars->mfPlanetaryTwilightStrength > kEpsilonf)
			{
				lFlags |= eFeature_Diffuse_PlanetaryTwilight;
			}
			if(apMaterial->GetTexture(eMaterialTexture_WaterMask))
			{
				lFlags |= eFeature_Diffuse_WaterMask;
			}
			

			return mpProgramManager->GenerateProgram(aRenderMode,lFlags);
		}
		////////////////////////////
		//Illumination
		else if(aRenderMode == eMaterialRenderMode_Illumination)
		{
			tFlag lFlags =0;
			if(apMaterial->HasUvAnimation())	lFlags |= eFeature_Illum_UvAnimation;
			if(pVars->mbUnlit &&
				apMaterial->GetTexture(eMaterialTexture_Diffuse) &&
				apMaterial->GetTexture(eMaterialTexture_Diffuse)->GetType() == eTextureType_2DArray)
			{
				lFlags |= eFeature_Illum_Array;
			}

			return mpProgramManager->GenerateProgram(aRenderMode,lFlags);
		}

		return NULL;
	}

	//--------------------------------------------------------------------------

	void cMaterialType_SolidDiffuse::SetupTypeSpecificData(eMaterialRenderMode aRenderMode, iGpuProgram* apProgram, iRenderer *apRenderer)
	{
		////////////////////////////
		//Diffuse
		if(aRenderMode == eMaterialRenderMode_Diffuse)
		{
			cFrustum *pFrustum = apRenderer->GetCurrentFrustum();

			apProgram->SetFloat(kVar_afInvFarPlane, 1.0f/pFrustum->GetFarPlane());
		}
		
	}

	//--------------------------------------------------------------------------

	void cMaterialType_SolidDiffuse::SetupMaterialSpecificData(	eMaterialRenderMode aRenderMode, iGpuProgram* apProgram, cMaterial *apMaterial,
																iRenderer *apRenderer)
	{
		if(	aRenderMode == eMaterialRenderMode_Diffuse || 
			aRenderMode == eMaterialRenderMode_Z || 
			aRenderMode == eMaterialRenderMode_Z_Dissolve || 
			aRenderMode == eMaterialRenderMode_Illumination)
		{
			/////////////////////////
			//UV Animation
			if(apMaterial->HasUvAnimation())
			{
				apProgram->SetMatrixf(kVar_a_mtxUV, apMaterial->GetUvMatrix());
			}
			
			if(aRenderMode == eMaterialRenderMode_Diffuse)
			{
				cMaterialType_SolidDiffuse_Vars* pVars = (cMaterialType_SolidDiffuse_Vars*)apMaterial->GetVars();

				/////////////////////////
				//Diffuse texture array
				iTexture* pDiffuseTexture = apMaterial->GetTexture(eMaterialTexture_Diffuse);
				if(pDiffuseTexture && pDiffuseTexture->GetType() == eTextureType_2DArray)
				{
					apProgram->SetVec2f(kVar_avDiffuseArrayTileGrid, (float)pVars->mlDiffuseArrayTileColumns, (float)pVars->mlDiffuseArrayTileRows);
					const cVector3l& vDiffuseSize = pDiffuseTexture->GetSize();
					apProgram->SetVec2f(kVar_avDiffuseArrayTileSize, (float)cMath::Max(1, vDiffuseSize.x), (float)cMath::Max(1, vDiffuseSize.y));
				}

				/////////////////////////
				//Analytic ellipsoid normals
				if(pVars->mbUseEllipsoidNormals)
				{
					cVector3f vRadii(
						cMath::Max(cMath::Abs(pVars->mvEllipsoidRadii.x), kEpsilonf),
						cMath::Max(cMath::Abs(pVars->mvEllipsoidRadii.y), kEpsilonf),
						cMath::Max(cMath::Abs(pVars->mvEllipsoidRadii.z), kEpsilonf));
					const float fMaxRadius = cMath::Max(vRadii.x, cMath::Max(vRadii.y, vRadii.z));
					vRadii.x = cMath::Max(vRadii.x / fMaxRadius, kEpsilonf);
					vRadii.y = cMath::Max(vRadii.y / fMaxRadius, kEpsilonf);
					vRadii.z = cMath::Max(vRadii.z / fMaxRadius, kEpsilonf);

					apProgram->SetVec3f(kVar_avEllipsoidNormalScale,
						1.0f / (vRadii.x * vRadii.x),
						1.0f / (vRadii.y * vRadii.y),
						1.0f / (vRadii.z * vRadii.z));
				}

				if(UsesOceanSpecular(apMaterial, pVars))
				{
					apProgram->SetVec3f(kVar_avOceanSpecularParams,
						cMath::Max(pVars->mfSpecularIntensityScale, 0.0f),
						cMath::Max(-1.0f, cMath::Min(pVars->mfSpecularGlossBias, 1.0f)),
						cMath::Max(0.0f, cMath::Min(pVars->mfOceanSpecularBroadStrength, 1.0f)));
				}

				if(pVars->mfPlanetaryTwilightStrength > kEpsilonf)
				{
					apProgram->SetFloat(kVar_afPlanetaryTwilightStrength,
						cMath::Max(0.0f, cMath::Min(pVars->mfPlanetaryTwilightStrength, 1.0f)));
				}

				if(apMaterial->GetTexture(eMaterialTexture_WaterMask))
				{
					apProgram->SetVec3f(kVar_avWaterTint, pVars->mvWaterTint);
					apProgram->SetFloat(kVar_afWaterTintStrength,
						cMath::Max(0.0f, cMath::Min(pVars->mfWaterTintStrength, 1.0f)));
					apProgram->SetFloat(kVar_afWaterSaturation,
						cMath::Max(0.0f, cMath::Min(pVars->mfWaterSaturation, 2.0f)));
					apProgram->SetFloat(kVar_afWaterBrightness,
						cMath::Max(0.0f, pVars->mfWaterBrightness));
				}

				/////////////////////////
				//Parallax
				if(apMaterial->GetTexture(eMaterialTexture_Height) && iRenderer::GetParallaxEnabled())
				{
					apProgram->SetVec2f(kVar_avHeightMapScaleAndBias, pVars->mfHeightMapScale, pVars->mfHeightMapBias);
				}

				/////////////////////////
				//Cube Map
				if(apMaterial->GetTexture(eMaterialTexture_CubeMap))
				{
					apProgram->SetVec2f(kVar_avFrenselBiasPow, pVars->mfFrenselBias, pVars->mfFrenselPow);
					
					cMatrixf mtxInvView = apRenderer->GetCurrentFrustum()->GetViewMatrix().GetTranspose();
					apProgram->SetMatrixf(kVar_a_mtxInvViewRotation, mtxInvView.GetRotation());
				}
			}
			else if(aRenderMode == eMaterialRenderMode_Illumination)
			{
				cMaterialType_SolidDiffuse_Vars* pVars = (cMaterialType_SolidDiffuse_Vars*)apMaterial->GetVars();
				iTexture* pDiffuseTexture = pVars->mbUnlit ? apMaterial->GetTexture(eMaterialTexture_Diffuse) : NULL;
				if(pDiffuseTexture && pDiffuseTexture->GetType() == eTextureType_2DArray)
				{
					apProgram->SetVec2f(kVar_avDiffuseArrayTileGrid, (float)pVars->mlDiffuseArrayTileColumns, (float)pVars->mlDiffuseArrayTileRows);
					const cVector3l& vDiffuseSize = pDiffuseTexture->GetSize();
					apProgram->SetVec2f(kVar_avDiffuseArrayTileSize, (float)cMath::Max(1, vDiffuseSize.x), (float)cMath::Max(1, vDiffuseSize.y));
				}
			}
		}
	}
	
	//--------------------------------------------------------------------------

	void cMaterialType_SolidDiffuse::SetupObjectSpecificData(	eMaterialRenderMode aRenderMode, iGpuProgram* apProgram, iRenderable *apObject,
																iRenderer *apRenderer)
	{
		
		////////////////////////////
		//Z Dissolve
		if(aRenderMode == eMaterialRenderMode_Z_Dissolve)
		{
			bool bRet = apProgram->SetFloat(kVar_afDissolveAmount, apObject->GetCoverageAmount());
			if(bRet==false)Error("Could not set variable!\n");
		}
		////////////////////////////
		//Illumination
		else if(aRenderMode == eMaterialRenderMode_Illumination)
		{
			bool bRet = apProgram->SetFloat(kVar_afColorMul, apObject->GetIlluminationAmount());
		}
	}


	//--------------------------------------------------------------------------

	iMaterialVars* cMaterialType_SolidDiffuse::CreateSpecificVariables()
	{
		return hplNew(cMaterialType_SolidDiffuse_Vars,());
	}

	//--------------------------------------------------------------------------

	void cMaterialType_SolidDiffuse::LoadVariables(cMaterial* apMaterial, cResourceVarsObject *apVars)
	{
		cMaterialType_SolidDiffuse_Vars *pVars = (cMaterialType_SolidDiffuse_Vars*)apMaterial->GetVars();
		if(pVars==NULL)
		{
			pVars = (cMaterialType_SolidDiffuse_Vars*)CreateSpecificVariables();
			apMaterial->SetVars(pVars);
		}
				
		pVars->mfHeightMapScale = apVars->GetVarFloat("HeightMapScale", 0.1f);
		pVars->mfHeightMapBias = apVars->GetVarFloat("HeightMapBias", 0);
		pVars->mfFrenselBias = apVars->GetVarFloat("FrenselBias", 0.2f);
		pVars->mfFrenselPow = apVars->GetVarFloat("FrenselPow", 8.0f);
		pVars->mbAlphaDissolveFilter = apVars->GetVarBool("AlphaDissolveFilter", false);
		pVars->mbUnlit = apVars->GetVarBool("Unlit", false);
		apMaterial->SetUnlit(pVars->mbUnlit);
		pVars->mbUseEllipsoidNormals = apVars->GetVarBool("UseEllipsoidNormals", false);
		pVars->mvEllipsoidRadii = apVars->GetVarVector3f("EllipsoidRadii", cVector3f(1.0f));
		pVars->mlDiffuseArrayTileColumns = cMath::Max(1, apVars->GetVarInt("DiffuseArrayTileColumns", 3));
		pVars->mlDiffuseArrayTileRows = cMath::Max(1, apVars->GetVarInt("DiffuseArrayTileRows", 2));
		pVars->mfSpecularIntensityScale = apVars->GetVarFloat("SpecularIntensityScale", 1.0f);
		pVars->mfSpecularGlossBias = apVars->GetVarFloat("SpecularGlossBias", 0.0f);
		pVars->mfOceanSpecularBroadStrength = apVars->GetVarFloat("OceanSpecularBroadStrength", 0.0f);
		pVars->mfPlanetaryTwilightStrength = apVars->GetVarFloat("PlanetaryTwilightStrength", 0.0f);
		pVars->mvWaterTint = apVars->GetVarVector3f("WaterTint", cVector3f(1.0f));
		pVars->mfWaterTintStrength = apVars->GetVarFloat("WaterTintStrength", 0.0f);
		pVars->mfWaterSaturation = apVars->GetVarFloat("WaterSaturation", 1.0f);
		pVars->mfWaterBrightness = apVars->GetVarFloat("WaterBrightness", 1.0f);

	}

	//--------------------------------------------------------------------------

	void cMaterialType_SolidDiffuse::GetVariableValues(cMaterial* apMaterial, cResourceVarsObject* apVars)
	{
		cMaterialType_SolidDiffuse_Vars* pVars = (cMaterialType_SolidDiffuse_Vars*)apMaterial->GetVars();

		apVars->AddVarFloat("HeightMapScale", pVars->mfHeightMapScale);
		apVars->AddVarFloat("HeightMapBias", pVars->mfHeightMapBias);
		apVars->AddVarFloat("FrenselBias", pVars->mfFrenselBias);
		apVars->AddVarFloat("FrenselPow", pVars->mfFrenselPow);
		apVars->AddVarBool("AlphaDissolveFilter", pVars->mbAlphaDissolveFilter);
		apVars->AddVarBool("Unlit", pVars->mbUnlit);
		apVars->AddVarBool("UseEllipsoidNormals", pVars->mbUseEllipsoidNormals);
		apVars->AddVarVector3f("EllipsoidRadii", pVars->mvEllipsoidRadii);
		apVars->AddVarInt("DiffuseArrayTileColumns", pVars->mlDiffuseArrayTileColumns);
		apVars->AddVarInt("DiffuseArrayTileRows", pVars->mlDiffuseArrayTileRows);
		apVars->AddVarFloat("SpecularIntensityScale", pVars->mfSpecularIntensityScale);
		apVars->AddVarFloat("SpecularGlossBias", pVars->mfSpecularGlossBias);
		apVars->AddVarFloat("OceanSpecularBroadStrength", pVars->mfOceanSpecularBroadStrength);
		apVars->AddVarFloat("PlanetaryTwilightStrength", pVars->mfPlanetaryTwilightStrength);
		apVars->AddVarVector3f("WaterTint", pVars->mvWaterTint);
		apVars->AddVarFloat("WaterTintStrength", pVars->mfWaterTintStrength);
		apVars->AddVarFloat("WaterSaturation", pVars->mfWaterSaturation);
		apVars->AddVarFloat("WaterBrightness", pVars->mfWaterBrightness);
	}

	//--------------------------------------------------------------------------
}
