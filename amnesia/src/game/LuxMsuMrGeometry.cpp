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

#include "LuxMsuMrGeometry.h"

#include "LuxMsuMrSimulation.h"

#include <cmath>

namespace
{
	const double kPi = 3.1415926535897932384626433832795;
	const double kDegreesToRadians = kPi / 180.0;
	const double kRadiansToDegrees = 180.0 / kPi;
	const double kWgs84EquatorialRadiusMetres = 6378137.0;
	const double kWgs84InverseFlattening = 298.257223563;
	const double kWgs84Flattening = 1.0 / kWgs84InverseFlattening;
	const double kWgs84PolarRadiusMetres =
		kWgs84EquatorialRadiusMetres * (1.0 - kWgs84Flattening);

	bool IsFinite(const cLuxSatelliteVector3d& aVector)
	{
		return std::isfinite(aVector.x) && std::isfinite(aVector.y) &&
			std::isfinite(aVector.z);
	}

	double Dot(const cLuxSatelliteVector3d& aA, const cLuxSatelliteVector3d& aB)
	{
		return aA.x * aB.x + aA.y * aB.y + aA.z * aB.z;
	}

	cLuxSatelliteVector3d Normalize(const cLuxSatelliteVector3d& aVector)
	{
		const double fLengthSquared = Dot(aVector, aVector);
		if(std::isfinite(fLengthSquared) == false || fLengthSquared <= 0.0)
			return cLuxSatelliteVector3d();

		const double fInverseLength = 1.0 / std::sqrt(fLengthSquared);
		return cLuxSatelliteVector3d(aVector.x * fInverseLength,
			aVector.y * fInverseLength, aVector.z * fInverseLength);
	}

	bool IntersectWgs84(const cLuxSatelliteVector3d& aOrigin,
					 const cLuxSatelliteVector3d& aDirection,
					 cLuxSatelliteVector3d& aGroundPoint,
					 double& afSlantRangeMetres)
	{
		const double fInverseEquatorialRadiusSquared = 1.0 /
			(kWgs84EquatorialRadiusMetres * kWgs84EquatorialRadiusMetres);
		const double fInversePolarRadiusSquared = 1.0 /
			(kWgs84PolarRadiusMetres * kWgs84PolarRadiusMetres);

		// HPL is Y-up, so X/Z are equatorial and Y is the polar axis.
		const double fA =
			(aDirection.x * aDirection.x + aDirection.z * aDirection.z) *
				fInverseEquatorialRadiusSquared +
			aDirection.y * aDirection.y * fInversePolarRadiusSquared;
		const double fB = 2.0 * (
			(aOrigin.x * aDirection.x + aOrigin.z * aDirection.z) *
				fInverseEquatorialRadiusSquared +
			aOrigin.y * aDirection.y * fInversePolarRadiusSquared);
		const double fC =
			(aOrigin.x * aOrigin.x + aOrigin.z * aOrigin.z) *
				fInverseEquatorialRadiusSquared +
			aOrigin.y * aOrigin.y * fInversePolarRadiusSquared - 1.0;
		const double fDiscriminant = fB * fB - 4.0 * fA * fC;
		if(std::isfinite(fA) == false || fA <= 0.0 ||
			std::isfinite(fDiscriminant) == false || fDiscriminant < 0.0)
			return false;

		const double fRoot = std::sqrt(fDiscriminant);
		const double fNear = (-fB - fRoot) / (2.0 * fA);
		const double fFar = (-fB + fRoot) / (2.0 * fA);
		afSlantRangeMetres = fNear > 0.0 ? fNear : fFar;
		if(std::isfinite(afSlantRangeMetres) == false || afSlantRangeMetres <= 0.0)
			return false;

		aGroundPoint = cLuxSatelliteVector3d(
			aOrigin.x + aDirection.x * afSlantRangeMetres,
			aOrigin.y + aDirection.y * afSlantRangeMetres,
			aOrigin.z + aDirection.z * afSlantRangeMetres);
		return IsFinite(aGroundPoint);
	}

	bool HplGroundPointToGeodetic(const cLuxSatelliteVector3d& aGroundPoint,
								 double& afLatitudeDegrees,
								 double& afLongitudeDegrees)
	{
		if(IsFinite(aGroundPoint) == false)
			return false;

		// Inverse of EarthFixedToHpl: ECEF (X,Y,Z) = (HPL.x,-HPL.z,HPL.y).
		const double fEcefX = aGroundPoint.x;
		const double fEcefY = -aGroundPoint.z;
		const double fEcefZ = aGroundPoint.y;
		const double fEquatorialDistance = std::sqrt(
			fEcefX * fEcefX + fEcefY * fEcefY);
		if(std::isfinite(fEquatorialDistance) == false)
			return false;

		// The point is analytically on the ellipsoid, so its surface normal gives
		// geodetic latitude directly without a height iteration.
		afLatitudeDegrees = std::atan2(
			fEcefZ / (kWgs84PolarRadiusMetres * kWgs84PolarRadiusMetres),
			fEquatorialDistance /
				(kWgs84EquatorialRadiusMetres * kWgs84EquatorialRadiusMetres)) *
			kRadiansToDegrees;
		afLongitudeDegrees = std::atan2(fEcefY, fEcefX) * kRadiansToDegrees;
		return std::isfinite(afLatitudeDegrees) && std::isfinite(afLongitudeDegrees);
	}
}

double cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees()
{
	return 360.0 / cLuxMsuMrSimulation::kCircularPositionsPerLine;
}

double cLuxMsuMrGeometry::GetEarthFootprintWidthDegrees()
{
	return cLuxMsuMrSimulation::kEarthViewSamplesPerLine *
		GetOpticalPositionPitchDegrees();
}

bool cLuxMsuMrGeometry::CalculateGroundSample(const cLuxSatellitePose& aPose,
	std::uint32_t alEarthSampleIndex, cLuxMsuMrGroundSample& aGroundSample)
{
	if(alEarthSampleIndex >= cLuxMsuMrSimulation::kEarthViewSamplesPerLine ||
		IsFinite(aPose.mvPositionMetres) == false ||
		IsFinite(aPose.mvRadialOut) == false || IsFinite(aPose.mvCrossTrack) == false)
		return false;

	const double fCentreSampleIndex =
		(cLuxMsuMrSimulation::kEarthViewSamplesPerLine - 1) * 0.5;
	const double fLookAngleDegrees =
		(static_cast<double>(alEarthSampleIndex) - fCentreSampleIndex) *
		GetOpticalPositionPitchDegrees();
	const double fLookAngleRadians = fLookAngleDegrees * kDegreesToRadians;
	const cLuxSatelliteVector3d vDirection = Normalize(cLuxSatelliteVector3d(
		-aPose.mvRadialOut.x * std::cos(fLookAngleRadians) +
			aPose.mvCrossTrack.x * std::sin(fLookAngleRadians),
		-aPose.mvRadialOut.y * std::cos(fLookAngleRadians) +
			aPose.mvCrossTrack.y * std::sin(fLookAngleRadians),
		-aPose.mvRadialOut.z * std::cos(fLookAngleRadians) +
			aPose.mvCrossTrack.z * std::sin(fLookAngleRadians)));
	if(Dot(vDirection, vDirection) == 0.0)
		return false;

	if(CalculateGroundIntersection(aPose.mvPositionMetres, vDirection,
		aGroundSample) == false)
		return false;
	aGroundSample.mlEarthSampleIndex = alEarthSampleIndex;
	aGroundSample.mfLookAngleDegrees = fLookAngleDegrees;
	return true;
}

bool cLuxMsuMrGeometry::CalculateGroundIntersection(
	const cLuxSatelliteVector3d& aOriginMetres,
	const cLuxSatelliteVector3d& aRayDirection,
	cLuxMsuMrGroundSample& aGroundSample)
{
	if(IsFinite(aOriginMetres) == false || IsFinite(aRayDirection) == false)
		return false;

	const cLuxSatelliteVector3d vDirection = Normalize(aRayDirection);
	if(Dot(vDirection, vDirection) == 0.0)
		return false;

	aGroundSample = cLuxMsuMrGroundSample();
	aGroundSample.mvRayDirection = vDirection;
	if(IntersectWgs84(aOriginMetres, vDirection,
		aGroundSample.mvGroundPointMetres,
		aGroundSample.mfSlantRangeMetres) == false)
		return false;

	return HplGroundPointToGeodetic(aGroundSample.mvGroundPointMetres,
		aGroundSample.mfLatitudeDegrees, aGroundSample.mfLongitudeDegrees);
}

bool cLuxMsuMrGeometry::CalculateGeodesicDistanceMetres(
	double afLatitudeDegreesA, double afLongitudeDegreesA,
	double afLatitudeDegreesB, double afLongitudeDegreesB,
	double& afDistanceMetres)
{
	if(std::isfinite(afLatitudeDegreesA) == false ||
		std::isfinite(afLongitudeDegreesA) == false ||
		std::isfinite(afLatitudeDegreesB) == false ||
		std::isfinite(afLongitudeDegreesB) == false)
		return false;

	const double fLatitudeA = afLatitudeDegreesA * kDegreesToRadians;
	const double fLatitudeB = afLatitudeDegreesB * kDegreesToRadians;
	const double fReducedLatitudeA = std::atan(
		(1.0 - kWgs84Flattening) * std::tan(fLatitudeA));
	const double fReducedLatitudeB = std::atan(
		(1.0 - kWgs84Flattening) * std::tan(fLatitudeB));
	const double fSinReducedA = std::sin(fReducedLatitudeA);
	const double fCosReducedA = std::cos(fReducedLatitudeA);
	const double fSinReducedB = std::sin(fReducedLatitudeB);
	const double fCosReducedB = std::cos(fReducedLatitudeB);
	double fLongitudeDifference =
		(afLongitudeDegreesB - afLongitudeDegreesA) * kDegreesToRadians;
	while(fLongitudeDifference > kPi) fLongitudeDifference -= 2.0 * kPi;
	while(fLongitudeDifference < -kPi) fLongitudeDifference += 2.0 * kPi;

	double fLambda = fLongitudeDifference;
	double fSinSigma = 0.0;
	double fCosSigma = 0.0;
	double fSigma = 0.0;
	double fSinAlpha = 0.0;
	double fCosSquaredAlpha = 0.0;
	double fCosTwoSigmaM = 0.0;
	bool bConverged = false;
	for(int i = 0; i < 100; ++i)
	{
		const double fSinLambda = std::sin(fLambda);
		const double fCosLambda = std::cos(fLambda);
		const double fFirst = fCosReducedB * fSinLambda;
		const double fSecond = fCosReducedA * fSinReducedB -
			fSinReducedA * fCosReducedB * fCosLambda;
		fSinSigma = std::sqrt(fFirst * fFirst + fSecond * fSecond);
		if(fSinSigma == 0.0)
		{
			afDistanceMetres = 0.0;
			return true;
		}

		fCosSigma = fSinReducedA * fSinReducedB +
			fCosReducedA * fCosReducedB * fCosLambda;
		fSigma = std::atan2(fSinSigma, fCosSigma);
		fSinAlpha = fCosReducedA * fCosReducedB * fSinLambda / fSinSigma;
		fCosSquaredAlpha = 1.0 - fSinAlpha * fSinAlpha;
		fCosTwoSigmaM = fCosSquaredAlpha > 1e-15 ?
			fCosSigma - 2.0 * fSinReducedA * fSinReducedB / fCosSquaredAlpha : 0.0;
		const double fC = kWgs84Flattening / 16.0 * fCosSquaredAlpha *
			(4.0 + kWgs84Flattening * (4.0 - 3.0 * fCosSquaredAlpha));
		const double fPreviousLambda = fLambda;
		fLambda = fLongitudeDifference + (1.0 - fC) * kWgs84Flattening *
			fSinAlpha * (fSigma + fC * fSinSigma *
			(fCosTwoSigmaM + fC * fCosSigma *
			(-1.0 + 2.0 * fCosTwoSigmaM * fCosTwoSigmaM)));
		if(std::fabs(fLambda - fPreviousLambda) <= 1e-12)
		{
			bConverged = true;
			break;
		}
	}
	if(bConverged == false)
		return false;

	const double fUSquared = fCosSquaredAlpha *
		(kWgs84EquatorialRadiusMetres * kWgs84EquatorialRadiusMetres -
		 kWgs84PolarRadiusMetres * kWgs84PolarRadiusMetres) /
		(kWgs84PolarRadiusMetres * kWgs84PolarRadiusMetres);
	const double fCoefficientA = 1.0 + fUSquared / 16384.0 *
		(4096.0 + fUSquared * (-768.0 + fUSquared *
		(320.0 - 175.0 * fUSquared)));
	const double fCoefficientB = fUSquared / 1024.0 *
		(256.0 + fUSquared * (-128.0 + fUSquared *
		(74.0 - 47.0 * fUSquared)));
	const double fDeltaSigma = fCoefficientB * fSinSigma *
		(fCosTwoSigmaM + fCoefficientB / 4.0 *
		(fCosSigma * (-1.0 + 2.0 * fCosTwoSigmaM * fCosTwoSigmaM) -
		 fCoefficientB / 6.0 * fCosTwoSigmaM *
		 (-3.0 + 4.0 * fSinSigma * fSinSigma) *
		 (-3.0 + 4.0 * fCosTwoSigmaM * fCosTwoSigmaM)));
	afDistanceMetres = kWgs84PolarRadiusMetres * fCoefficientA *
		(fSigma - fDeltaSigma);
	return std::isfinite(afDistanceMetres) && afDistanceMetres >= 0.0;
}
