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

#include "LuxMsuMrTumble.h"

#include <cmath>

namespace
{
	const double kPi = 3.1415926535897932384626433832795;
	const double kDegreesToRadians = kPi / 180.0;
	// Nominal sidereal rotation. LOD and polar-motion corrections are far below
	// the visual attitude scale over a scan lasting minutes.
	const double kEarthRotationRadiansPerSecond = 7.29211514670698e-5;

	class cTumbleRandom
	{
	public:
		explicit cTumbleRandom(std::uint64_t alSeed) : mlState(alSeed) {}

		double Uniform(double afMinimum, double afMaximum)
		{
			std::uint64_t z = (mlState += UINT64_C(0x9E3779B97F4A7C15));
			z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
			z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
			z ^= z >> 31;
			const double fUnit = static_cast<double>(z >> 11) *
				(1.0 / 9007199254740992.0);
			return afMinimum + (afMaximum - afMinimum) * fUnit;
		}

	private:
		std::uint64_t mlState;
	};

	double Dot(const cLuxSatelliteVector3d& aA,
		const cLuxSatelliteVector3d& aB)
	{
		return aA.x * aB.x + aA.y * aB.y + aA.z * aB.z;
	}

	cLuxSatelliteVector3d Cross(const cLuxSatelliteVector3d& aA,
		const cLuxSatelliteVector3d& aB)
	{
		return cLuxSatelliteVector3d(
			aA.y * aB.z - aA.z * aB.y,
			aA.z * aB.x - aA.x * aB.z,
			aA.x * aB.y - aA.y * aB.x);
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

	bool IsFinite(const cLuxSatelliteVector3d& aVector)
	{
		return std::isfinite(aVector.x) && std::isfinite(aVector.y) &&
			std::isfinite(aVector.z);
	}

	cLuxSatelliteVector3d Scale(const cLuxSatelliteVector3d& aVector,
		double afScale)
	{
		return cLuxSatelliteVector3d(aVector.x * afScale,
			aVector.y * afScale, aVector.z * afScale);
	}

	cLuxSatelliteVector3d Add(const cLuxSatelliteVector3d& aA,
		const cLuxSatelliteVector3d& aB)
	{
		return cLuxSatelliteVector3d(aA.x + aB.x,
			aA.y + aB.y, aA.z + aB.z);
	}

	cLuxSatelliteVector3d RotateAroundAxis(
		const cLuxSatelliteVector3d& aVector,
		const cLuxSatelliteVector3d& aUnitAxis,
		double afAngleRadians)
	{
		// Rodrigues' formula. The axis is fixed in the scan-start inertial frame,
		// which is exactly the torque-free principal-axis solution.
		const double fCosine = std::cos(afAngleRadians);
		const double fSine = std::sin(afAngleRadians);
		return Add(Add(Scale(aVector, fCosine),
			Scale(Cross(aUnitAxis, aVector), fSine)),
			Scale(aUnitAxis, Dot(aUnitAxis, aVector) * (1.0 - fCosine)));
	}

	cLuxSatelliteVector3d InertialToEarthFixedHpl(
		const cLuxSatelliteVector3d& aInertialVector,
		double afElapsedSeconds)
	{
		// HPL maps geographic east to -Z. Consequently an inertially fixed
		// vector's Earth-fixed components rotate from +X toward +Z as Earth turns.
		const double fAngle = kEarthRotationRadiansPerSecond * afElapsedSeconds;
		const double fCosine = std::cos(fAngle);
		const double fSine = std::sin(fAngle);
		return cLuxSatelliteVector3d(
			fCosine * aInertialVector.x - fSine * aInertialVector.z,
			aInertialVector.y,
			fSine * aInertialVector.x + fCosine * aInertialVector.z);
	}
}

cLuxMsuMrTumbleProfile::cLuxMsuMrTumbleProfile()
	: mbInitialized(false), mlSeed(0),
	  mfSpinRateDegreesPerSecond(0.0), mfInitialPhaseDegrees(0.0)
{
}

bool cLuxMsuMrTumbleProfile::Generate(std::uint64_t alSeed,
	const cLuxSatellitePose& aInitialPose)
{
	if(IsFinite(aInitialPose.mvCrossTrack) == false ||
		IsFinite(aInitialPose.mvAlongTrack) == false ||
		IsFinite(aInitialPose.mvRadialOut) == false)
		return false;

	mvInitialCrossTrackInertial = Normalize(aInitialPose.mvCrossTrack);
	mvInitialAlongTrackInertial = Normalize(aInitialPose.mvAlongTrack);
	mvInitialNadirInertial = Normalize(Scale(aInitialPose.mvRadialOut, -1.0));
	if(Dot(mvInitialCrossTrackInertial, mvInitialCrossTrackInertial) == 0.0 ||
		Dot(mvInitialAlongTrackInertial, mvInitialAlongTrackInertial) == 0.0 ||
		Dot(mvInitialNadirInertial, mvInitialNadirInertial) == 0.0)
		return false;

	cTumbleRandom random(alSeed);
	mlSeed = alSeed;
	// Exactly one revolution per eight minutes. Direction and starting phase
	// are the only per-scan variations.
	mfSpinRateDegreesPerSecond = (360.0 / (8.0 * 60.0)) *
		(random.Uniform(0.0, 1.0) < 0.5 ? -1.0 : 1.0);
	mfInitialPhaseDegrees = random.Uniform(-180.0, 180.0);
	mvSpinAxisInertial = mvInitialNadirInertial;
	mbInitialized = Dot(mvSpinAxisInertial, mvSpinAxisInertial) != 0.0;
	return mbInitialized;
}

bool cLuxMsuMrTumbleProfile::CalculateInstrumentFrame(
	double afElapsedSeconds, cLuxMsuMrInstrumentFrame& aFrame) const
{
	if(mbInitialized == false || std::isfinite(afElapsedSeconds) == false ||
		afElapsedSeconds < 0.0)
		return false;

	const double fSpinAngleRadians =
		(mfInitialPhaseDegrees + mfSpinRateDegreesPerSecond * afElapsedSeconds) *
		kDegreesToRadians;
	const cLuxSatelliteVector3d vCrossTrackInertial = RotateAroundAxis(
		mvInitialCrossTrackInertial, mvSpinAxisInertial, fSpinAngleRadians);
	const cLuxSatelliteVector3d vAlongTrackInertial = RotateAroundAxis(
		mvInitialAlongTrackInertial, mvSpinAxisInertial, fSpinAngleRadians);
	const cLuxSatelliteVector3d vNadirInertial = RotateAroundAxis(
		mvInitialNadirInertial, mvSpinAxisInertial, fSpinAngleRadians);

	aFrame.mvCrossTrack = InertialToEarthFixedHpl(
		vCrossTrackInertial, afElapsedSeconds);
	aFrame.mvAlongTrack = InertialToEarthFixedHpl(
		vAlongTrackInertial, afElapsedSeconds);
	aFrame.mvNadir = InertialToEarthFixedHpl(
		vNadirInertial, afElapsedSeconds);
	return IsFinite(aFrame.mvCrossTrack) && IsFinite(aFrame.mvAlongTrack) &&
		IsFinite(aFrame.mvNadir);
}

double cLuxMsuMrTumbleProfile::GetSpinPeriodSeconds() const
{
	return mbInitialized && mfSpinRateDegreesPerSecond != 0.0 ?
		360.0 / std::fabs(mfSpinRateDegreesPerSecond) : 0.0;
}
