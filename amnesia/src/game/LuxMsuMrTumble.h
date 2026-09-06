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

#ifndef LUX_MSU_MR_TUMBLE_H
#define LUX_MSU_MR_TUMBLE_H

#include <cstdint>

#include "LuxSatelliteTypes.h"

// Instantaneous instrument axes in HPL's Earth-fixed world coordinates. The
// frame is orthonormal and right-handed: cross-track X, along-track Y, nadir Z.
struct cLuxMsuMrInstrumentFrame
{
	cLuxSatelliteVector3d mvCrossTrack;
	cLuxSatelliteVector3d mvAlongTrack;
	cLuxSatelliteVector3d mvNadir;
};

// A per-scan, deterministic torque-free attitude function. The model spins the
// body about its scan-start nadir/up principal axis, which remains fixed in
// inertial space, at one revolution per eight minutes. There is no
// Earth-pointing restoration after scan start.
//
// The profile is randomized once at scan start and evaluated from instrument
// time, so pausing or changing render frame rate cannot alter scan geometry.
class cLuxMsuMrTumbleProfile
{
public:
	cLuxMsuMrTumbleProfile();

	bool Generate(std::uint64_t alSeed, const cLuxSatellitePose& aInitialPose);
	bool CalculateInstrumentFrame(double afElapsedSeconds,
		cLuxMsuMrInstrumentFrame& aFrame) const;

	bool IsInitialized() const { return mbInitialized; }
	std::uint64_t GetSeed() const { return mlSeed; }
	double GetSpinRateDegreesPerSecond() const
	{
		return mfSpinRateDegreesPerSecond;
	}
	double GetSpinPeriodSeconds() const;
	double GetInitialPhaseDegrees() const { return mfInitialPhaseDegrees; }

private:
	bool mbInitialized;
	std::uint64_t mlSeed;
	double mfSpinRateDegreesPerSecond;
	double mfInitialPhaseDegrees;
	cLuxSatelliteVector3d mvInitialCrossTrackInertial;
	cLuxSatelliteVector3d mvInitialAlongTrackInertial;
	cLuxSatelliteVector3d mvInitialNadirInertial;
	cLuxSatelliteVector3d mvSpinAxisInertial;
};

#endif // LUX_MSU_MR_TUMBLE_H
