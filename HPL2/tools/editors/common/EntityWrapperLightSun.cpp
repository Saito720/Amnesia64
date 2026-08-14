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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "EntityWrapperLightSun.h"

#include "EditorWorld.h"

#include <cstdlib>

//---------------------------------------------------------------------------

cIconEntityLightSun::cIconEntityLightSun(iEntityWrapper* apParent)
	: iIconEntityLight(apParent, "Spot")
{
}

bool cIconEntityLightSun::Create(const tString& asName)
{
	cWorld* pWorld = mpParent->GetEditorWorld()->GetWorld();
	mpEntity = pWorld->CreateLightSun(asName);

	return true;
}

//---------------------------------------------------------------------------

cEntityWrapperTypeLightSun::cEntityWrapperTypeLightSun()
	: iEntityWrapperTypeLight("SunLight", eEditorEntityLightType_Sun)
{
	mScaleType = eScaleType_None;

	AddFloat(eLightSunFloat_Intensity, "Intensity", 2.5f);
	AddFloat(eLightSunFloat_HighlightKnee, "HighlightKnee", 0.7f);
	AddBool(eLightSunBool_UseSystemTime, "UseSystemTime", true);
	AddBool(eLightSunBool_ShowSunDisk, "ShowSunDisk", true);
	AddString(eLightSunStr_JulianDate, "JulianDate", "2451545.0");
}

iEntityWrapperData* cEntityWrapperTypeLightSun::CreateSpecificData()
{
	return hplNew(cEntityWrapperDataLightSun,(this));
}

//---------------------------------------------------------------------------

cEntityWrapperDataLightSun::cEntityWrapperDataLightSun(iEntityWrapperType* apType)
	: iEntityWrapperDataLight(apType)
{
	SetName("SunLight");
}

iEntityWrapper* cEntityWrapperDataLightSun::CreateSpecificEntity()
{
	return hplNew(cEntityWrapperLightSun,(this));
}

//---------------------------------------------------------------------------

cEntityWrapperLightSun::cEntityWrapperLightSun(iEntityWrapperData* apData)
	: iEntityWrapperLight(apData), mfIntensity(1.0f), mfHighlightKnee(0.7f),
	  mbUseSystemTime(true), mbShowSunDisk(true),
	  msJulianDate("2451545.0")
{
}

cEntityWrapperLightSun::~cEntityWrapperLightSun()
{
}

//---------------------------------------------------------------------------

bool cEntityWrapperLightSun::SetProperty(int alPropID, const float& afX)
{
	if(alPropID == eLightSunFloat_Intensity)
	{
		SetIntensity(afX);
		return true;
	}
	if(alPropID == eLightSunFloat_HighlightKnee)
	{
		SetHighlightKnee(afX);
		return true;
	}

	return iEntityWrapperLight::SetProperty(alPropID, afX);
}

bool cEntityWrapperLightSun::SetProperty(int alPropID, const bool& abX)
{
	if(alPropID == eLightSunBool_UseSystemTime)
	{
		SetUseSystemTime(abX);
		return true;
	}
	if(alPropID == eLightSunBool_ShowSunDisk)
	{
		SetShowSunDisk(abX);
		return true;
	}

	return iEntityWrapperLight::SetProperty(alPropID, abX);
}

bool cEntityWrapperLightSun::SetProperty(int alPropID, const tString& asX)
{
	if(alPropID == eLightSunStr_JulianDate)
	{
		SetJulianDate(asX);
		return true;
	}

	return iEntityWrapperLight::SetProperty(alPropID, asX);
}

bool cEntityWrapperLightSun::GetProperty(int alPropID, float& afX)
{
	if(alPropID == eLightSunFloat_Intensity)
	{
		afX = GetIntensity();
		return true;
	}
	if(alPropID == eLightSunFloat_HighlightKnee)
	{
		afX = GetHighlightKnee();
		return true;
	}

	return iEntityWrapperLight::GetProperty(alPropID, afX);
}

bool cEntityWrapperLightSun::GetProperty(int alPropID, bool& abX)
{
	if(alPropID == eLightSunBool_UseSystemTime)
	{
		abX = GetUseSystemTime();
		return true;
	}
	if(alPropID == eLightSunBool_ShowSunDisk)
	{
		abX = GetShowSunDisk();
		return true;
	}

	return iEntityWrapperLight::GetProperty(alPropID, abX);
}

bool cEntityWrapperLightSun::GetProperty(int alPropID, tString& asX)
{
	if(alPropID == eLightSunStr_JulianDate)
	{
		asX = GetJulianDate();
		return true;
	}

	return iEntityWrapperLight::GetProperty(alPropID, asX);
}

//---------------------------------------------------------------------------

void cEntityWrapperLightSun::SetIntensity(float afX)
{
	mfIntensity = afX < 0.0f ? 0.0f : afX;
	((cLightSun*)mpEngineEntity->GetEntity())->SetIntensity(mfIntensity);
}

void cEntityWrapperLightSun::SetHighlightKnee(float afX)
{
	mfHighlightKnee = afX < 0.0f ? 0.0f : (afX > 1.0f ? 1.0f : afX);
	((cLightSun*)mpEngineEntity->GetEntity())->SetHighlightKnee(mfHighlightKnee);
}

void cEntityWrapperLightSun::SetUseSystemTime(bool abX)
{
	mbUseSystemTime = abX;
	((cLightSun*)mpEngineEntity->GetEntity())->SetUseSystemTime(mbUseSystemTime);
}

void cEntityWrapperLightSun::SetShowSunDisk(bool abX)
{
	mbShowSunDisk = abX;
	((cLightSun*)mpEngineEntity->GetEntity())->SetShowSunDisk(mbShowSunDisk);
}

void cEntityWrapperLightSun::SetJulianDate(const tString& asX)
{
	char* pEnd = NULL;
	const double fJulianDate = std::strtod(asX.c_str(), &pEnd);
	if(pEnd == asX.c_str() || *pEnd != '\0')
		return;

	msJulianDate = asX;
	((cLightSun*)mpEngineEntity->GetEntity())->SetJulianDate(fJulianDate);
}

//---------------------------------------------------------------------------

void cEntityWrapperLightSun::DrawLightTypeSpecific(
	cEditorWindowViewport* apViewport, cRendererCallbackFunctions* apFunctions,
	iEditorEditMode* apEditMode, bool abIsSelected)
{
}

iEngineEntity* cEntityWrapperLightSun::CreateSpecificEngineEntity()
{
	return hplNew(cIconEntityLightSun,(this));
}

//---------------------------------------------------------------------------
