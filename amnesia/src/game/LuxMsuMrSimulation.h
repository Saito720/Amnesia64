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

#ifndef LUX_MSU_MR_SIMULATION_H
#define LUX_MSU_MR_SIMULATION_H

#include <cstdint>
#include <vector>

#include "LuxBase.h"
#include "LuxSatelliteTypes.h"

// One physically timed Earth-view sample produced while advancing the native
// circular scan clock. Indices are zero-based and never reset during a scan.
struct cLuxMsuMrEarthSampleEvent
{
	cLuxMsuMrEarthSampleEvent()
		: mlCircularPositionIndex(0), mlLineIndex(0), mlCircularPhase(0),
		  mlEarthSampleIndex(0), mlMirrorFaceIndex(0), mfJulianDayUtc(0.0),
		  mfJulianFractionUtc(0.0)
	{
	}

	std::uint64_t mlCircularPositionIndex;
	std::uint64_t mlLineIndex;
	std::uint32_t mlCircularPhase;
	std::uint32_t mlEarthSampleIndex;
	std::uint32_t mlMirrorFaceIndex;
	double mfJulianDayUtc;
	double mfJulianFractionUtc;
};

typedef std::vector<cLuxMsuMrEarthSampleEvent> tLuxMsuMrEarthSampleEventVec;

// Owns the instrument's deterministic clock and sample-event state. Rendering
// and orbit propagation remain outside so scheduling stays independent of HPL2
// resources and floating-point scene coordinates.
class cLuxMsuMrSimulation
{
public:
	// Nominal MSU-MR timing constants. Keeping these as exact integer ratios
	// prevents the line and mirror clocks from drifting apart.
	enum
	{
		kCircularPositionsPerLine = 5120,
		kEarthViewSamplesPerLine = 1572,
		// Simulation phase convention: the epoch is the first Earth sample,
		// therefore phase zero maps to HRPT sample zero. This does not assert an
		// absolute physical encoder index and can be offset later without changing
		// the scheduler.
		kEarthViewStartCircularPosition = 0,
		kMirrorFaceCount = 2,
		kNominalLineRateNumerator = 13,
		kNominalLineRateDenominator = 2,
		kCircularPositionsPerSecond = 33280,
		kEarthViewSamplesPerSecond = 10218
	};

	cLuxMsuMrSimulation();

	bool Start(const tString& asSatelliteName, double afEpochJulianDateUtc,
			   int alUpdatesPerSecond);
	void Stop();

	// Advances one fixed game update and returns the number of circular phase
	// positions that became due during it. At 60 Hz this repeats 554, 555, 555.
	std::uint32_t AdvanceOneUpdate();

	bool IsActive() const { return mbActive; }
	const tString& GetSatelliteName() const { return msSatelliteName; }
	int GetUpdatesPerSecond() const { return mlUpdatesPerSecond; }

	std::uint64_t GetTotalPhasePositionCount() const { return mlTotalPhasePositionCount; }
	std::uint64_t GetCompletedLineCount() const;
	std::uint32_t GetCircularPhasePosition() const;
	std::uint32_t GetLastPhasePositionsAdvanced() const { return mlLastPhasePositionsAdvanced; }
	std::uint64_t GetTotalEarthSampleCount() const { return mlTotalEarthSampleCount; }
	const tLuxMsuMrEarthSampleEventVec& GetLastEarthSampleEvents() const
	{
		return mvLastEarthSampleEvents;
	}
	bool GetEarthSampleEvent(std::uint64_t alLineIndex,
						 std::uint32_t alEarthSampleIndex,
						 cLuxMsuMrEarthSampleEvent& aEvent) const;
	double GetElapsedSeconds() const;

	// The split form preserves sub-millisecond scan timing. Adding a small
	// fraction directly to a modern, approximately 2.46-million-day Julian date
	// would otherwise discard precision before propagation ever sees it.
	void GetJulianDateUtc(double& afJulianDay, double& afJulianFraction) const;
	double GetJulianDateUtc() const;

	void SetSatellitePose(const cLuxSatellitePose& aPose);
	void ClearSatellitePose();
	bool HasSatellitePose() const { return mbHasSatellitePose; }
	const cLuxSatellitePose& GetSatellitePose() const { return mSatellitePose; }

private:
	void GetJulianDateUtcForPositionCount(std::uint64_t alPositionCount,
										  double& afJulianDay,
										  double& afJulianFraction) const;
	std::uint64_t GetEarthSampleCountForPositionCount(std::uint64_t alPositionCount) const;

	bool mbActive;
	tString msSatelliteName;
	double mfEpochJulianDayUtc;
	double mfEpochJulianFractionUtc;
	int mlUpdatesPerSecond;
	std::uint64_t mlPhaseStepRemainder;
	std::uint64_t mlTotalPhasePositionCount;
	std::uint64_t mlTotalEarthSampleCount;
	std::uint32_t mlLastPhasePositionsAdvanced;
	tLuxMsuMrEarthSampleEventVec mvLastEarthSampleEvents;
	bool mbHasSatellitePose;
	cLuxSatellitePose mSatellitePose;
};

#endif // LUX_MSU_MR_SIMULATION_H
