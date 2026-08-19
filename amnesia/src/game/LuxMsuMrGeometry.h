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

#ifndef LUX_MSU_MR_GEOMETRY_H
#define LUX_MSU_MR_GEOMETRY_H

#include <cstdint>

#include "LuxSatelliteTypes.h"

// Geometry result for a nominal MSU-MR Earth-view look. The ray and ground
// point use HPL's metre-space Earth-fixed axes; latitude and longitude are
// WGS-84 geodetic coordinates.
struct cLuxMsuMrGroundSample
{
	cLuxMsuMrGroundSample()
		: mlEarthSampleIndex(0), mfLookAngleDegrees(0.0),
		  mfSlantRangeMetres(0.0), mfLatitudeDegrees(0.0),
		  mfLongitudeDegrees(0.0)
	{
	}

	std::uint32_t mlEarthSampleIndex;
	double mfLookAngleDegrees;
	cLuxSatelliteVector3d mvRayDirection;
	cLuxSatelliteVector3d mvGroundPointMetres;
	double mfSlantRangeMetres;
	double mfLatitudeDegrees;
	double mfLongitudeDegrees;
};

class cLuxMsuMrGeometry
{
public:
	// Uses the nominal nadir-pointed local orbital frame in cLuxSatellitePose.
	// HRPT indices increase from -cross-track to +cross-track. Both physical
	// mirror faces share this ray law; face identity remains calibration metadata.
	static bool CalculateGroundSample(const cLuxSatellitePose& aPose,
									  std::uint32_t alEarthSampleIndex,
									  cLuxMsuMrGroundSample& aGroundSample);

	// Intersects an arbitrary normalized sensor ray with the WGS-84 ellipsoid.
	// This supports validation of renderer-generated rays against the nominal
	// discrete scan law without duplicating the ellipsoid math elsewhere.
	static bool CalculateGroundIntersection(
		const cLuxSatelliteVector3d& aOriginMetres,
		const cLuxSatelliteVector3d& aRayDirection,
		cLuxMsuMrGroundSample& aGroundSample);

	static bool CalculateGeodesicDistanceMetres(double afLatitudeDegreesA,
										 double afLongitudeDegreesA,
										 double afLatitudeDegreesB,
										 double afLongitudeDegreesB,
										 double& afDistanceMetres);

	static double GetOpticalPositionPitchDegrees();
	static double GetEarthFootprintWidthDegrees();
};

#endif // LUX_MSU_MR_GEOMETRY_H
