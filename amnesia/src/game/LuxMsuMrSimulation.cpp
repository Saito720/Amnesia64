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

#include "LuxMsuMrSimulation.h"

#include <cmath>
#include <limits>

namespace
{
	const std::uint64_t kSecondsPerDay = 86400;
	const std::uint64_t kPhasePositionsPerDay =
		static_cast<std::uint64_t>(cLuxMsuMrSimulation::kCircularPositionsPerSecond) * kSecondsPerDay;
}

static_assert(cLuxMsuMrSimulation::kCircularPositionsPerLine *
			  cLuxMsuMrSimulation::kNominalLineRateNumerator ==
			  cLuxMsuMrSimulation::kCircularPositionsPerSecond *
			  cLuxMsuMrSimulation::kNominalLineRateDenominator,
			  "MSU-MR circular phase rate must remain exactly 5120 * 6.5 Hz");
static_assert(cLuxMsuMrSimulation::kEarthViewSamplesPerLine *
			  cLuxMsuMrSimulation::kNominalLineRateNumerator ==
			  cLuxMsuMrSimulation::kEarthViewSamplesPerSecond *
			  cLuxMsuMrSimulation::kNominalLineRateDenominator,
			  "MSU-MR Earth-view rate must remain exactly 1572 * 6.5 Hz");
static_assert(cLuxMsuMrSimulation::kEarthViewStartCircularPosition >= 0 &&
			  cLuxMsuMrSimulation::kEarthViewStartCircularPosition +
			  cLuxMsuMrSimulation::kEarthViewSamplesPerLine <=
			  cLuxMsuMrSimulation::kCircularPositionsPerLine,
			  "MSU-MR Earth-view window must fit inside one circular scan line");

cLuxMsuMrSimulation::cLuxMsuMrSimulation()
	: mbActive(false),
	  mfEpochJulianDayUtc(0.0),
	  mfEpochJulianFractionUtc(0.0),
	  mlUpdatesPerSecond(0),
	  mlPhaseStepRemainder(0),
	  mlTotalPhasePositionCount(0),
	  mlTotalEarthSampleCount(0),
	  mlLastPhasePositionsAdvanced(0),
	  mbHasSatellitePose(false)
{
}

bool cLuxMsuMrSimulation::Start(const tString& asSatelliteName,
								 double afEpochJulianDateUtc,
								 int alUpdatesPerSecond)
{
	if(asSatelliteName.empty() || std::isfinite(afEpochJulianDateUtc) == false ||
		alUpdatesPerSecond <= 0)
		return false;

	msSatelliteName = asSatelliteName;
	mfEpochJulianDayUtc = std::floor(afEpochJulianDateUtc);
	mfEpochJulianFractionUtc = afEpochJulianDateUtc - mfEpochJulianDayUtc;
	mlUpdatesPerSecond = alUpdatesPerSecond;
	mlPhaseStepRemainder = 0;
	mlTotalPhasePositionCount = 0;
	mlTotalEarthSampleCount = 0;
	mlLastPhasePositionsAdvanced = 0;
	mvLastEarthSampleEvents.clear();
	ClearSatellitePose();
	mbActive = true;
	return true;
}

void cLuxMsuMrSimulation::Stop()
{
	mbActive = false;
	msSatelliteName.clear();
	mfEpochJulianDayUtc = 0.0;
	mfEpochJulianFractionUtc = 0.0;
	mlUpdatesPerSecond = 0;
	mlPhaseStepRemainder = 0;
	mlTotalPhasePositionCount = 0;
	mlTotalEarthSampleCount = 0;
	mlLastPhasePositionsAdvanced = 0;
	mvLastEarthSampleEvents.clear();
	ClearSatellitePose();
}

std::uint32_t cLuxMsuMrSimulation::AdvanceOneUpdate()
{
	mlLastPhasePositionsAdvanced = 0;
	mvLastEarthSampleEvents.clear();
	if(mbActive == false || mlUpdatesPerSecond <= 0)
		return 0;

	mlPhaseStepRemainder += kCircularPositionsPerSecond;
	const std::uint64_t lPositionsToAdvance =
		mlPhaseStepRemainder / static_cast<std::uint64_t>(mlUpdatesPerSecond);
	mlPhaseStepRemainder %= static_cast<std::uint64_t>(mlUpdatesPerSecond);

	if(lPositionsToAdvance > std::numeric_limits<std::uint32_t>::max() ||
		mlTotalPhasePositionCount > std::numeric_limits<std::uint64_t>::max() - lPositionsToAdvance)
	{
		Stop();
		return 0;
	}

	const std::uint64_t lFirstPositionIndex = mlTotalPhasePositionCount;
	const std::uint64_t lEndPositionIndex = lFirstPositionIndex + lPositionsToAdvance;
	const std::uint64_t lExpectedEarthSamples =
		GetEarthSampleCountForPositionCount(lEndPositionIndex) -
		GetEarthSampleCountForPositionCount(lFirstPositionIndex);
	if(mvLastEarthSampleEvents.capacity() < static_cast<size_t>(lExpectedEarthSamples))
		mvLastEarthSampleEvents.reserve(static_cast<size_t>(lExpectedEarthSamples));

	for(std::uint64_t lPositionIndex = lFirstPositionIndex;
		lPositionIndex < lEndPositionIndex; ++lPositionIndex)
	{
		const std::uint32_t lCircularPhase = static_cast<std::uint32_t>(
			lPositionIndex % kCircularPositionsPerLine);
		if(lCircularPhase < kEarthViewStartCircularPosition ||
			lCircularPhase >= kEarthViewStartCircularPosition + kEarthViewSamplesPerLine)
			continue;

		cLuxMsuMrEarthSampleEvent event;
		event.mlCircularPositionIndex = lPositionIndex;
		event.mlLineIndex = lPositionIndex / kCircularPositionsPerLine;
		event.mlCircularPhase = lCircularPhase;
		event.mlEarthSampleIndex = lCircularPhase - kEarthViewStartCircularPosition;
		event.mlMirrorFaceIndex = static_cast<std::uint32_t>(
			event.mlLineIndex % kMirrorFaceCount);
		GetJulianDateUtcForPositionCount(lPositionIndex,
			event.mfJulianDayUtc, event.mfJulianFractionUtc);
		mvLastEarthSampleEvents.push_back(event);
	}

	if(mvLastEarthSampleEvents.size() != static_cast<size_t>(lExpectedEarthSamples))
	{
		Stop();
		return 0;
	}

	mlLastPhasePositionsAdvanced = static_cast<std::uint32_t>(lPositionsToAdvance);
	mlTotalPhasePositionCount = lEndPositionIndex;
	mlTotalEarthSampleCount = GetEarthSampleCountForPositionCount(lEndPositionIndex);
	return mlLastPhasePositionsAdvanced;
}

std::uint64_t cLuxMsuMrSimulation::GetCompletedLineCount() const
{
	return mlTotalPhasePositionCount / kCircularPositionsPerLine;
}

std::uint32_t cLuxMsuMrSimulation::GetCircularPhasePosition() const
{
	return static_cast<std::uint32_t>(
		mlTotalPhasePositionCount % kCircularPositionsPerLine);
}

bool cLuxMsuMrSimulation::GetEarthSampleEvent(
	std::uint64_t alLineIndex, std::uint32_t alEarthSampleIndex,
	cLuxMsuMrEarthSampleEvent& aEvent) const
{
	if(alEarthSampleIndex >= kEarthViewSamplesPerLine ||
		alLineIndex > (std::numeric_limits<std::uint64_t>::max() -
			kEarthViewStartCircularPosition - alEarthSampleIndex) /
			kCircularPositionsPerLine)
		return false;

	const std::uint64_t lPositionIndex =
		alLineIndex * static_cast<std::uint64_t>(kCircularPositionsPerLine) +
		kEarthViewStartCircularPosition + alEarthSampleIndex;
	if(lPositionIndex >= mlTotalPhasePositionCount)
		return false;

	aEvent = cLuxMsuMrEarthSampleEvent();
	aEvent.mlCircularPositionIndex = lPositionIndex;
	aEvent.mlLineIndex = alLineIndex;
	aEvent.mlCircularPhase = kEarthViewStartCircularPosition + alEarthSampleIndex;
	aEvent.mlEarthSampleIndex = alEarthSampleIndex;
	aEvent.mlMirrorFaceIndex = static_cast<std::uint32_t>(
		alLineIndex % kMirrorFaceCount);
	GetJulianDateUtcForPositionCount(lPositionIndex,
		aEvent.mfJulianDayUtc, aEvent.mfJulianFractionUtc);
	return true;
}

double cLuxMsuMrSimulation::GetElapsedSeconds() const
{
	return static_cast<double>(mlTotalPhasePositionCount) /
		static_cast<double>(kCircularPositionsPerSecond);
}

void cLuxMsuMrSimulation::GetJulianDateUtc(double& afJulianDay,
											double& afJulianFraction) const
{
	GetJulianDateUtcForPositionCount(mlTotalPhasePositionCount,
		afJulianDay, afJulianFraction);
}

void cLuxMsuMrSimulation::GetJulianDateUtcForPositionCount(
	std::uint64_t alPositionCount, double& afJulianDay, double& afJulianFraction) const
{
	const std::uint64_t lWholeDays = alPositionCount / kPhasePositionsPerDay;
	const std::uint64_t lRemainingPositions = alPositionCount % kPhasePositionsPerDay;

	afJulianDay = mfEpochJulianDayUtc + static_cast<double>(lWholeDays);
	afJulianFraction = mfEpochJulianFractionUtc +
		static_cast<double>(lRemainingPositions) / static_cast<double>(kPhasePositionsPerDay);

	if(afJulianFraction >= 1.0)
	{
		const double fAdditionalDays = std::floor(afJulianFraction);
		afJulianDay += fAdditionalDays;
		afJulianFraction -= fAdditionalDays;
	}
}

std::uint64_t cLuxMsuMrSimulation::GetEarthSampleCountForPositionCount(
	std::uint64_t alPositionCount) const
{
	const std::uint64_t lCompletedLines = alPositionCount / kCircularPositionsPerLine;
	const std::uint32_t lCircularPositionsInCurrentLine = static_cast<std::uint32_t>(
		alPositionCount % kCircularPositionsPerLine);

	std::uint32_t lEarthSamplesInCurrentLine = 0;
	if(lCircularPositionsInCurrentLine > kEarthViewStartCircularPosition)
	{
		lEarthSamplesInCurrentLine = lCircularPositionsInCurrentLine -
			kEarthViewStartCircularPosition;
		if(lEarthSamplesInCurrentLine > kEarthViewSamplesPerLine)
			lEarthSamplesInCurrentLine = kEarthViewSamplesPerLine;
	}

	return lCompletedLines * static_cast<std::uint64_t>(kEarthViewSamplesPerLine) +
		lEarthSamplesInCurrentLine;
}

double cLuxMsuMrSimulation::GetJulianDateUtc() const
{
	double fJulianDay = 0.0;
	double fJulianFraction = 0.0;
	GetJulianDateUtc(fJulianDay, fJulianFraction);
	return fJulianDay + fJulianFraction;
}

void cLuxMsuMrSimulation::SetSatellitePose(const cLuxSatellitePose& aPose)
{
	mSatellitePose = aPose;
	mbHasSatellitePose = true;
}

void cLuxMsuMrSimulation::ClearSatellitePose()
{
	mSatellitePose = cLuxSatellitePose();
	mbHasSatellitePose = false;
}
