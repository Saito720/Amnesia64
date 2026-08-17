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

#ifndef LUX_SATELLITE_HANDLER_H
#define LUX_SATELLITE_HANDLER_H

#include <map>

#include "LuxBase.h"

class cLuxMap;
class cLuxEarthOrientationTable;
class cLuxSatelliteOrbit;

typedef std::map<tString, cLuxSatelliteOrbit*> tLuxSatelliteOrbitMap;

class cLuxSatelliteHandler
{
public:
	cLuxSatelliteHandler();
	~cLuxSatelliteHandler();

	// Registers one or more consecutive three-line element sets. Each name can
	// optionally identify a map entity whose transform will be driven by the orbit.
	bool RegisterTLE(const tString& asFile);
	bool RegisterEarthOrientationData(const tString& asFile);

	void Update();
	void Reset();
	void DestroyWorldEntities(cLuxMap *apMap);

private:
	bool LoadEarthOrientationData(const tString& asFile, bool abLogFailure);
	void EnsureEarthOrientationData();
	void DestroyOrbit(cLuxSatelliteOrbit *apOrbit, bool abDestroyBillboard);
	void UpdateOrbit(cLuxSatelliteOrbit *apOrbit, double afJulianDateUtc);

	tLuxSatelliteOrbitMap m_mapOrbits;
	cLuxEarthOrientationTable *mpEarthOrientationTable;
	bool mbDefaultEarthOrientationLoadAttempted;
	bool mbEarthOrientationWarningShown;
};

#endif // LUX_SATELLITE_HANDLER_H
