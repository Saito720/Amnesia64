/*
 * Copyright © 2009-2010 Frictional Games
 *
 * This file is part of HPL2.
 *
 * HPL2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HPL2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HPL2.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "scene/LightSun.h"

#include <chrono>
#include <cmath>

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// HELPERS
	//////////////////////////////////////////////////////////////////////////

	static double NormalizeDegrees(double afDegrees)
	{
		double fResult = std::fmod(afDegrees, 360.0);
		return fResult < 0.0 ? fResult + 360.0 : fResult;
	}

	static double ToRadians(double afDegrees)
	{
		return afDegrees * 0.01745329251994329576923690768489;
	}

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cLightSun::cLightSun(tString asName, cResources *apResources) : iLight(asName,apResources)
	{
		mLightType = eLightType_Sun;
		mbUseSystemTime = true;
		mfJulianDate = 2451545.0;
		mfIntensity = 1.0f;
		mfHighlightKnee = 0.7f;
		mbShowSunDisk = true;

		// iLight::IsVisible uses radius as a general enabled-state check. A Sun
		// has no spatial radius, so keep this sentinel positive and ignore later
		// attempts to resize it.
		mfRadius = 1.0f;
		UpdateBoundingVolume();
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	double cLightSun::GetJulianDate() const
	{
		return mbUseSystemTime ? GetSystemJulianDate() : mfJulianDate;
	}

	//-----------------------------------------------------------------------

	cVector3f cLightSun::GetSunDirection() const
	{
		return CalculateSunDirection(GetJulianDate());
	}

	//-----------------------------------------------------------------------

	double cLightSun::GetSystemJulianDate()
	{
		const std::chrono::duration<double> elapsed =
			std::chrono::system_clock::now().time_since_epoch();
		return 2440587.5 + elapsed.count() / 86400.0;
	}

	//-----------------------------------------------------------------------

	cVector3f cLightSun::CalculateSunDirection(double afJulianDate)
	{
		// Approximate geocentric solar coordinates from the Astronomical
		// Almanac/USNO model. Its error is about one arcminute from 1800-2200,
		// far below the Sun's approximately 0.53 degree apparent diameter.
		const double fDaysSinceJ2000 = afJulianDate - 2451545.0;
		const double fMeanAnomaly = ToRadians(NormalizeDegrees(
			357.529 + 0.98560028 * fDaysSinceJ2000));
		const double fMeanLongitude = NormalizeDegrees(
			280.459 + 0.98564736 * fDaysSinceJ2000);
		const double fEclipticLongitude = ToRadians(NormalizeDegrees(
			fMeanLongitude + 1.915 * std::sin(fMeanAnomaly) +
			0.020 * std::sin(2.0 * fMeanAnomaly)));
		const double fObliquity = ToRadians(
			23.439 - 0.00000036 * fDaysSinceJ2000);

		// Earth-centered inertial equatorial direction toward the Sun.
		const double fEciX = std::cos(fEclipticLongitude);
		const double fEciY = std::cos(fObliquity) * std::sin(fEclipticLongitude);
		const double fEciZ = std::sin(fObliquity) * std::sin(fEclipticLongitude);

		// Rotate ECI into Earth-fixed coordinates with Greenwich mean sidereal
		// time. UTC is used as the UT1 approximation, whose sub-second error is
		// immaterial for visual sunlight.
		const double fCenturiesSinceJ2000 = fDaysSinceJ2000 / 36525.0;
		const double fGmst = ToRadians(NormalizeDegrees(
			280.46061837 + 360.98564736629 * fDaysSinceJ2000 +
			0.000387933 * fCenturiesSinceJ2000 * fCenturiesSinceJ2000 -
			fCenturiesSinceJ2000 * fCenturiesSinceJ2000 * fCenturiesSinceJ2000 / 38710000.0));
		const double fCosGmst = std::cos(fGmst);
		const double fSinGmst = std::sin(fGmst);
		const double fEcefX = fCosGmst * fEciX + fSinGmst * fEciY;
		const double fEcefY = -fSinGmst * fEciX + fCosGmst * fEciY;
		const double fEcefZ = fEciZ;

		// Standard ECEF is Z-up. Rotate it into HPL's right-handed Y-up frame:
		// world X = ECEF X, world Y = ECEF Z, world Z = -ECEF Y.
		cVector3f vDirection((float)fEcefX, (float)fEcefZ, (float)-fEcefY);
		vDirection.Normalize();
		return vDirection;
	}

	//-----------------------------------------------------------------------

	void cLightSun::SetRadius(float afX)
	{
		// Directional sunlight is global and has no finite radius.
	}

	//-----------------------------------------------------------------------

	bool cLightSun::CollidesWithBV(cBoundingVolume *apBV)
	{
		return true;
	}

	//-----------------------------------------------------------------------

	bool cLightSun::CollidesWithFrustum(cFrustum *apFrustum)
	{
		return true;
	}

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	void cLightSun::UpdateBoundingVolume()
	{
		// Sun lights never enter a spatial render container. Keep a benign
		// volume for generic light code that may still inspect one.
		mBoundingVolume.SetSize(1.0f);
		mBoundingVolume.SetPosition(0);
	}

	//-----------------------------------------------------------------------

}
