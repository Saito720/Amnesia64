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

#ifndef HPL_MATERIAL_TYPE_ATMOSPHERE_H
#define HPL_MATERIAL_TYPE_ATMOSPHERE_H

#include "graphics/MaterialType.h"
#include "graphics/Material.h"

namespace hpl {

	//--------------------------------------------------

	class cMaterialType_Atmosphere_Vars : public iMaterialVars
	{
	public:
		cMaterialType_Atmosphere_Vars();
		~cMaterialType_Atmosphere_Vars() {}

		cVector3f mvGroundRadii;
		cVector3f mvAtmosphereRadii;
		cVector3f mvRayleighScattering;
		float mfMieScattering;
		float mfMieExtinction;
		float mfRayleighScaleHeight;
		float mfMieScaleHeight;
		float mfMieAnisotropy;
		float mfAerosolDensity;
		float mfExposure;
		float mfMultipleScatteringStrength;

		cVector2f mvCloudArrayTileGrid;
		float mfCloudBaseHeight;
		float mfCloudMaxHeight;
		float mfCloudCoverageThreshold;
		float mfCloudExtinction;
		float mfCloudAmbient;
		float mfCloudSunIntensity;
		float mfCloudTwilightStrength;
		cVector3f mvCloudTwilightColor;
		float mfCloudSelfShadow;
		float mfCloudShadowStrength;
		float mfCloudShadowSoftness;
	};

	//--------------------------------------------------

	class cMaterialType_Atmosphere : public iMaterialType
	{
	public:
		cMaterialType_Atmosphere(cGraphics *apGraphics, cResources *apResources);
		~cMaterialType_Atmosphere();

		void DestroyProgram(cMaterial *apMaterial, eMaterialRenderMode aRenderMode,
							iGpuProgram* apProgram, char alSkeleton);

		bool SupportsHWSkinning() { return false; }

		iTexture* GetTextureForUnit(cMaterial *apMaterial, eMaterialRenderMode aRenderMode, int alUnit);
		iGpuProgram* GetGpuProgram(cMaterial *apMaterial, eMaterialRenderMode aRenderMode, char alSkeleton);

		void SetupTypeSpecificData(eMaterialRenderMode aRenderMode, iGpuProgram* apProgram,
								   iRenderer* apRenderer);
		void SetupMaterialSpecificData(eMaterialRenderMode aRenderMode, iGpuProgram* apProgram,
									   cMaterial* apMaterial, iRenderer* apRenderer);
		void SetupObjectSpecificData(eMaterialRenderMode aRenderMode, iGpuProgram* apProgram,
									 iRenderable* apObject, iRenderer* apRenderer);

		iMaterialVars* CreateSpecificVariables();
		void LoadVariables(cMaterial *apMaterial, cResourceVarsObject *apVars);
		void GetVariableValues(cMaterial *apMaterial, cResourceVarsObject *apVars);

		void CompileMaterialSpecifics(cMaterial *apMaterial);

	private:
		void LoadData();
		void DestroyData();

		iGpuProgram *mpTransmittanceProgram;
		iGpuProgram *mpScatteringProgram;
	};

	//--------------------------------------------------

}

#endif // HPL_MATERIAL_TYPE_ATMOSPHERE_H
