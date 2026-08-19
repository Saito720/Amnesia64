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

#ifndef LUX_SATELLITE_TYPES_H
#define LUX_SATELLITE_TYPES_H

struct cLuxSatelliteVector3d
{
	cLuxSatelliteVector3d() : x(0.0), y(0.0), z(0.0) {}
	cLuxSatelliteVector3d(double afX, double afY, double afZ) : x(afX), y(afY), z(afZ) {}

	double x;
	double y;
	double z;
};

// Earth-fixed kinematic pose expressed in HPL's Y-up world axes. The nominal
// local orbital basis is orthonormal and right-handed: X is position cross
// inertial velocity (cross-track), Y is radial-out, and Z is projected
// along-track. Its components are expressed in the rotating Earth-fixed axes.
// This is a reference frame, not telemetry-derived spacecraft attitude. The
// instrument can derive nadir directly as -radial-out.
struct cLuxSatellitePose
{
	cLuxSatellitePose()
		: mfJulianDayUtc(0.0), mfJulianFractionUtc(0.0),
		  mbUsedEarthOrientationData(false)
	{
	}

	double mfJulianDayUtc;
	double mfJulianFractionUtc;
	cLuxSatelliteVector3d mvPositionMetres;
	// Coordinate derivative in the rotating Earth-fixed frame.
	cLuxSatelliteVector3d mvVelocityMetresPerSecond;
	// Inertial orbital velocity with components expressed in Earth-fixed HPL
	// axes. This, rather than ground-track velocity, defines the LVLH frame.
	cLuxSatelliteVector3d mvInertialVelocityMetresPerSecond;
	cLuxSatelliteVector3d mvCrossTrack;
	cLuxSatelliteVector3d mvRadialOut;
	cLuxSatelliteVector3d mvAlongTrack;
	bool mbUsedEarthOrientationData;
};

#endif // LUX_SATELLITE_TYPES_H
