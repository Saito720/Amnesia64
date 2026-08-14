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

#ifndef HPLEDITOR_ENTITY_WRAPPER_LIGHT_SUN_H
#define HPLEDITOR_ENTITY_WRAPPER_LIGHT_SUN_H

#include "EntityWrapperLight.h"

//---------------------------------------------------------------------------

class cIconEntityLightSun : public iIconEntityLight
{
public:
	cIconEntityLightSun(iEntityWrapper* apParent);

	bool Create(const tString& asName);
};

//---------------------------------------------------------------------------

#define LightSunPropIdStart 100

enum eLightSunFloat
{
	eLightSunFloat_Intensity = LightSunPropIdStart,
	eLightSunFloat_HighlightKnee,

	eLightSunFloat_LastEnum,
};

enum eLightSunBool
{
	eLightSunBool_UseSystemTime = LightSunPropIdStart,
	eLightSunBool_ShowSunDisk,

	eLightSunBool_LastEnum,
};

enum eLightSunStr
{
	eLightSunStr_JulianDate = LightSunPropIdStart,

	eLightSunStr_LastEnum,
};

//---------------------------------------------------------------------------

class cEntityWrapperTypeLightSun : public iEntityWrapperTypeLight
{
public:
	cEntityWrapperTypeLightSun();

protected:
	iEntityWrapperData* CreateSpecificData();
};

//---------------------------------------------------------------------------

class cEntityWrapperDataLightSun : public iEntityWrapperDataLight
{
public:
	cEntityWrapperDataLightSun(iEntityWrapperType* apType);

protected:
	iEntityWrapper* CreateSpecificEntity();
};

//---------------------------------------------------------------------------

class cEntityWrapperLightSun : public iEntityWrapperLight
{
public:
	cEntityWrapperLightSun(iEntityWrapperData* apData);
	~cEntityWrapperLightSun();

	bool SetProperty(int alPropID, const float& afX);
	bool SetProperty(int alPropID, const bool& abX);
	bool SetProperty(int alPropID, const tString& asX);

	bool GetProperty(int alPropID, float& afX);
	bool GetProperty(int alPropID, bool& abX);
	bool GetProperty(int alPropID, tString& asX);

	void SetIntensity(float afX);
	float GetIntensity() const { return mfIntensity; }
	void SetHighlightKnee(float afX);
	float GetHighlightKnee() const { return mfHighlightKnee; }

	void SetUseSystemTime(bool abX);
	bool GetUseSystemTime() const { return mbUseSystemTime; }
	void SetShowSunDisk(bool abX);
	bool GetShowSunDisk() const { return mbShowSunDisk; }

	void SetJulianDate(const tString& asX);
	const tString& GetJulianDate() const { return msJulianDate; }

	void DrawLightTypeSpecific(cEditorWindowViewport* apViewport,
		cRendererCallbackFunctions* apFunctions, iEditorEditMode* apEditMode,
		bool abIsSelected);

protected:
	iEngineEntity* CreateSpecificEngineEntity();

	float mfIntensity;
	float mfHighlightKnee;
	bool mbUseSystemTime;
	bool mbShowSunDisk;
	tString msJulianDate;
};

//---------------------------------------------------------------------------

#endif // HPLEDITOR_ENTITY_WRAPPER_LIGHT_SUN_H
