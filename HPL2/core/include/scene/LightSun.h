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

#ifndef HPL_LIGHT_SUN_H
#define HPL_LIGHT_SUN_H

#include "scene/Light.h"

namespace hpl {

	//------------------------------------------

	// Global astronomical sunlight. The returned direction points from Earth
	// toward the Sun in the engine's Y-up Earth-fixed frame:
	// +Y north, +X Greenwich, -Z 90 degrees east.
	class cLightSun : public iLight
	{
	#ifdef __GNUC__
		typedef iLight __super;
	#endif
	public:
		cLightSun(tString asName, cResources *apResources);

		void SetUseSystemTime(bool abX) { mbUseSystemTime = abX; }
		bool GetUseSystemTime() const { return mbUseSystemTime; }

		void SetJulianDate(double afJulianDate) { mfJulianDate = afJulianDate; }
		double GetJulianDate() const;

		void SetIntensity(float afIntensity) { mfIntensity = afIntensity < 0.0f ? 0.0f : afIntensity; }
		float GetIntensity() const { return mfIntensity; }

		void SetHighlightKnee(float afHighlightKnee)
		{
			mfHighlightKnee = afHighlightKnee < 0.0f ? 0.0f :
				(afHighlightKnee > 1.0f ? 1.0f : afHighlightKnee);
		}
		float GetHighlightKnee() const { return mfHighlightKnee; }

		void SetShowSunDisk(bool abX) { mbShowSunDisk = abX; }
		bool GetShowSunDisk() const { return mbShowSunDisk; }

		cVector3f GetSunDirection() const;

		static double GetSystemJulianDate();
		static cVector3f CalculateSunDirection(double afJulianDate);

		void SetRadius(float afX);

		bool CollidesWithBV(cBoundingVolume *apBV);
		bool CollidesWithFrustum(cFrustum *apFrustum);

	private:
		void UpdateBoundingVolume();

		bool mbUseSystemTime;
		double mfJulianDate;
		float mfIntensity;
		float mfHighlightKnee;
		bool mbShowSunDisk;
	};

};
#endif // HPL_LIGHT_SUN_H
