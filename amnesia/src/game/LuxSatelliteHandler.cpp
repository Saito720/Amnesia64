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

#include "LuxSatelliteHandler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "LuxEntity.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxMsuMrGeometry.h"
#include "LuxMsuMrScanProduct.h"
#include "LuxMsuMrSimulation.h"
#include "LuxMsuMrTumble.h"

#include "SGP4/SGP4.h"

#include "engine/Engine.h"
#include "graphics/Bitmap.h"
#include "graphics/FontData.h"
#include "graphics/FrameBuffer.h"
#include "graphics/FrameSubImage.h"
#include "graphics/Graphics.h"
#include "graphics/LowLevelGraphics.h"
#include "graphics/Renderer.h"
#include "graphics/Texture.h"
#include "gui/GuiGfxElement.h"
#include "resources/FontManager.h"
#include "scene/MeshEntity.h"

struct cLuxEarthOrientationRecord
{
	double mfModifiedJulianDateUtc;
	double mfPolarMotionXArcseconds;
	double mfPolarMotionYArcseconds;
	double mfUt1MinusUtcSeconds;
	double mfLengthOfDayMilliseconds;
	bool mbHasLengthOfDay;
};

struct cLuxEarthOrientationSample
{
	cLuxEarthOrientationSample()
		: mfPolarMotionXRadians(0.0), mfPolarMotionYRadians(0.0),
		  mfUt1MinusUtcSeconds(0.0), mfLengthOfDaySeconds(0.0)
	{
	}

	double mfPolarMotionXRadians;
	double mfPolarMotionYRadians;
	double mfUt1MinusUtcSeconds;
	double mfLengthOfDaySeconds;
};

class cLuxEarthOrientationTable
{
public:
	bool Load(const tWString& asPath, tString& asError);
	bool Sample(double afJulianDayUtc, double afJulianFractionUtc,
				cLuxEarthOrientationSample& aSample) const;

	bool Empty() const { return mvRecords.empty(); }
	size_t GetRecordCount() const { return mvRecords.size(); }
	double GetFirstModifiedJulianDate() const { return mvRecords.empty() ? 0.0 : mvRecords.front().mfModifiedJulianDateUtc; }
	double GetLastModifiedJulianDate() const { return mvRecords.empty() ? 0.0 : mvRecords.back().mfModifiedJulianDateUtc; }

private:
	std::vector<cLuxEarthOrientationRecord> mvRecords;
};

namespace
{
	const double kPi = 3.1415926535897932384626433832795;
	const double kTwoPi = 2.0 * kPi;
	const double kDegreesToRadians = kPi / 180.0;
	const double kMinutesPerDay = 1440.0;
	const double kSecondsPerDay = 86400.0;
	// Diagnostic threshold only; TLE accuracy has no universal hard cutoff.
	const double kTleEpochWarningDistanceDays = 14.0;
	const double kMetresPerKilometre = 1000.0;
	const double kEarthRotationRadiansPerSecond = 7.29211514670698e-5;
	const double kArcsecondsToRadians = kDegreesToRadians / 3600.0;
	const double kJulianDateToModifiedJulianDate = 2400000.5;
	const char *kDefaultEarthOrientationFile = "core/eop/finals2000A.data";
	const char *kRepositoryEarthOrientationFile = "redist/core/eop/finals2000A.data";
	const char *kSatelliteIconMaterial = "graphics/icon/satellite.mat";
	const float kSatelliteIconWorldSizeMetres = 400000.0f;
	const int kSatelliteIconTranslucentPriority = 100;
	const char *kSatelliteLabelFont = "JetBrainsMono-Bold.fnt";
	const char *kSatelliteLabelMaterial = "fonts/satellite_label.mat";
	const float kSatelliteLabelLineHeightMetres = 140000.0f;
	const float kSatelliteLabelGapMetres = 20000.0f;
	const int kSatelliteLabelTranslucentPriority = 101;
	enum
	{
		kMsuMrStripSampleCount = 16,
		kMsuMrFullStripCount =
			cLuxMsuMrSimulation::kEarthViewSamplesPerLine /
			kMsuMrStripSampleCount,
		kMsuMrTailStripSampleCount =
			cLuxMsuMrSimulation::kEarthViewSamplesPerLine %
			kMsuMrStripSampleCount,
		kMsuMrStripCount = kMsuMrFullStripCount + 1,
		kMsuMrFirstRenderBatchStripCount = 50,
		kMsuMrSensorBatchWidth =
			cLuxMsuMrSimulation::kEarthViewSamplesPerLine,
		kMsuMrRepresentativeSampleCount = 6
	};
	static_assert(kMsuMrTailStripSampleCount > 0,
		"The MSU-MR strip layout expects one non-empty rightmost tail view");
	static_assert(kMsuMrFirstRenderBatchStripCount > 0 &&
		kMsuMrFirstRenderBatchStripCount < kMsuMrStripCount,
		"The MSU-MR acquisition must be split into two non-empty render batches");
	const std::uint32_t
		kMsuMrRepresentativeSampleIndices[kMsuMrRepresentativeSampleCount] =
			{0, 393, 785, 786, 1178, 1571};
	static int GetMsuMrStripFirstSample(int alStripIndex)
	{
		return alStripIndex * kMsuMrStripSampleCount;
	}
	static int GetMsuMrStripSampleCount(int alStripIndex)
	{
		return alStripIndex < kMsuMrFullStripCount ?
			kMsuMrStripSampleCount : kMsuMrTailStripSampleCount;
	}
	const float kMsuMrSensorNearClipMetres = 100.0f;
	const float kMsuMrSensorFarClipMetres = 3000000.0f;
	// Billboard UVs display the source icon vertically flipped, placing its
	// signal bands along local +X,+Y in the rendered quad.
	const cVector2f kSatelliteIconSignalDirection(1.0f, 1.0f);

	typedef cLuxSatelliteVector3d cVector3d;

	struct cThreeLineElement
	{
		tString msName;
		std::string msLine1;
		std::string msLine2;
	};

	static double Dot(const cVector3d& avA, const cVector3d& avB)
	{
		return avA.x * avB.x + avA.y * avB.y + avA.z * avB.z;
	}

	static cVector3d Cross(const cVector3d& avA, const cVector3d& avB)
	{
		return cVector3d(avA.y * avB.z - avA.z * avB.y,
						avA.z * avB.x - avA.x * avB.z,
						avA.x * avB.y - avA.y * avB.x);
	}

	static cVector3d Normalize(const cVector3d& avValue)
	{
		const double fLengthSquared = Dot(avValue, avValue);
		if(fLengthSquared <= 0.0 || std::isfinite(fLengthSquared) == false)
			return cVector3d();

		const double fInverseLength = 1.0 / std::sqrt(fLengthSquared);
		return cVector3d(avValue.x * fInverseLength,
						avValue.y * fInverseLength,
						avValue.z * fInverseLength);
	}

	static bool IsFinite(const cVector3d& avValue)
	{
		return std::isfinite(avValue.x) && std::isfinite(avValue.y) && std::isfinite(avValue.z);
	}

	static double GetMsuMrElapsedSeconds(std::uint64_t alCircularPositionIndex)
	{
		return static_cast<double>(alCircularPositionIndex) /
			cLuxMsuMrSimulation::kCircularPositionsPerSecond;
	}

	static std::uint64_t GenerateMsuMrTumbleSeed(const tString& asSatelliteKey)
	{
		std::uint64_t lSeed = static_cast<std::uint64_t>(
			std::chrono::system_clock::now().time_since_epoch().count());
		lSeed ^= static_cast<std::uint64_t>(
			std::chrono::steady_clock::now().time_since_epoch().count()) +
			UINT64_C(0x9E3779B97F4A7C15);
		for(size_t i = 0; i < asSatelliteKey.size(); ++i)
		{
			lSeed ^= static_cast<unsigned char>(asSatelliteKey[i]);
			lSeed *= UINT64_C(1099511628211);
		}
		// One SplitMix64 finalizer prevents nearby start timestamps from
		// producing visibly related parameter sets.
		lSeed = (lSeed ^ (lSeed >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
		lSeed = (lSeed ^ (lSeed >> 27)) * UINT64_C(0x94D049BB133111EB);
		lSeed ^= lSeed >> 31;
		return lSeed;
	}

	static bool CalculateMsuMrGroundSampleForFrame(
		const cLuxSatellitePose& aPose,
		const cLuxMsuMrInstrumentFrame& aFrame,
		std::uint32_t alEarthSampleIndex,
		cLuxMsuMrGroundSample& aGroundSample,
		bool& abGroundHit)
	{
		if(alEarthSampleIndex >= cLuxMsuMrSimulation::kEarthViewSamplesPerLine)
			return false;
		const double fCentreSampleIndex =
			(cLuxMsuMrSimulation::kEarthViewSamplesPerLine - 1) * 0.5;
		const double fLookAngleDegrees =
			(static_cast<double>(alEarthSampleIndex) - fCentreSampleIndex) *
			cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees();
		const double fLookAngleRadians = fLookAngleDegrees * kDegreesToRadians;
		const cVector3d vDirection = Normalize(cVector3d(
			aFrame.mvNadir.x * std::cos(fLookAngleRadians) +
				aFrame.mvCrossTrack.x * std::sin(fLookAngleRadians),
			aFrame.mvNadir.y * std::cos(fLookAngleRadians) +
				aFrame.mvCrossTrack.y * std::sin(fLookAngleRadians),
			aFrame.mvNadir.z * std::cos(fLookAngleRadians) +
				aFrame.mvCrossTrack.z * std::sin(fLookAngleRadians)));
		if(Dot(vDirection, vDirection) == 0.0)
			return false;
		abGroundHit = cLuxMsuMrGeometry::CalculateGroundIntersection(
			aPose.mvPositionMetres, vDirection, aGroundSample);
		if(abGroundHit == false)
		{
			aGroundSample = cLuxMsuMrGroundSample();
			aGroundSample.mvRayDirection = vDirection;
			aGroundSample.mfSlantRangeMetres =
				std::numeric_limits<double>::quiet_NaN();
			aGroundSample.mfLatitudeDegrees =
				std::numeric_limits<double>::quiet_NaN();
			aGroundSample.mfLongitudeDegrees =
				std::numeric_limits<double>::quiet_NaN();
		}
		aGroundSample.mlEarthSampleIndex = alEarthSampleIndex;
		aGroundSample.mfLookAngleDegrees = fLookAngleDegrees;
		return true;
	}

	static bool CalculateMsuMrTumbledGroundSample(
		const cLuxMsuMrTumbleProfile& aProfile,
		const cLuxSatellitePose& aPose, double afElapsedSeconds,
		std::uint32_t alEarthSampleIndex,
		cLuxMsuMrGroundSample& aGroundSample,
		bool& abGroundHit,
		cLuxMsuMrInstrumentFrame *apFrame = NULL)
	{
		cLuxMsuMrInstrumentFrame frame;
		if(aProfile.CalculateInstrumentFrame(afElapsedSeconds, frame) == false ||
			CalculateMsuMrGroundSampleForFrame(aPose, frame,
				alEarthSampleIndex, aGroundSample, abGroundHit) == false)
			return false;
		if(apFrame)
			*apFrame = frame;
		return true;
	}

	static bool CalculateMsuMrGroundSolarElevationDegrees(
		const cLuxMsuMrGroundSample& aGroundSample,
		double afJulianDayUtc, double afJulianFractionUtc,
		double& afSolarElevationDegrees)
	{
		if(std::isfinite(aGroundSample.mfLatitudeDegrees) == false ||
			std::isfinite(aGroundSample.mfLongitudeDegrees) == false ||
			std::isfinite(afJulianDayUtc) == false ||
			std::isfinite(afJulianFractionUtc) == false)
			return false;

		const double fLatitudeRadians =
			aGroundSample.mfLatitudeDegrees * kDegreesToRadians;
		const double fLongitudeRadians =
			aGroundSample.mfLongitudeDegrees * kDegreesToRadians;
		// A geodetic latitude/longitude pair directly defines the outward WGS-84
		// surface normal. Convert that normal to HPL's Y-up Earth-fixed axes.
		const cVector3d vSurfaceNormal(
			std::cos(fLatitudeRadians) * std::cos(fLongitudeRadians),
			std::sin(fLatitudeRadians),
			-std::cos(fLatitudeRadians) * std::sin(fLongitudeRadians));
		const cVector3f vSunDirection = cLightSun::CalculateSunDirection(
			afJulianDayUtc + afJulianFractionUtc);
		double fSolarSine =
			vSurfaceNormal.x * static_cast<double>(vSunDirection.x) +
			vSurfaceNormal.y * static_cast<double>(vSunDirection.y) +
			vSurfaceNormal.z * static_cast<double>(vSunDirection.z);
		fSolarSine = std::max(-1.0, std::min(1.0, fSolarSine));
		afSolarElevationDegrees =
			std::asin(fSolarSine) / kDegreesToRadians;
		return std::isfinite(afSolarElevationDegrees);
	}

	static bool IsMsuMrPointSunEarthOccluded(
		const cLuxSatellitePose& aPose,
		double afJulianDayUtc, double afJulianFractionUtc)
	{
		const cVector3f vSunDirection = cLightSun::CalculateSunDirection(
			afJulianDayUtc + afJulianFractionUtc);
		const cVector3d vSunRay(
			static_cast<double>(vSunDirection.x),
			static_cast<double>(vSunDirection.y),
			static_cast<double>(vSunDirection.z));
		cLuxMsuMrGroundSample occultingGround;
		// This deliberately tests the Sun's centre as a point source. A future
		// radiometric model can distinguish penumbra using the finite solar disk.
		return cLuxMsuMrGeometry::CalculateGroundIntersection(
			aPose.mvPositionMetres, vSunRay, occultingGround);
	}

	static std::string TrimAscii(const std::string& asValue)
	{
		size_t lStart = 0;
		while(lStart < asValue.size() && (asValue[lStart] == ' ' || asValue[lStart] == '\t'))
			++lStart;

		size_t lEnd = asValue.size();
		while(lEnd > lStart && (asValue[lEnd - 1] == ' ' || asValue[lEnd - 1] == '\t'))
			--lEnd;

		return asValue.substr(lStart, lEnd - lStart);
	}

	static bool ParseFixedDouble(const std::string& asLine, size_t alStart, size_t alLength, double& afValue)
	{
		if(alStart >= asLine.size())
			return false;

		const std::string sField = TrimAscii(asLine.substr(alStart, alLength));
		if(sField.empty())
			return false;

		char *pEnd = NULL;
		afValue = std::strtod(sField.c_str(), &pEnd);
		return pEnd != sField.c_str() && *pEnd == '\0' && std::isfinite(afValue);
	}

	static double ContinuousUt1MinusUtcDelta(double afStartSeconds, double afEndSeconds)
	{
		double fDelta = afEndSeconds - afStartSeconds;
		if(std::fabs(fDelta) > 0.5)
			fDelta -= std::round(fDelta);
		return fDelta;
	}

	static double DerivedLengthOfDaySeconds(const cLuxEarthOrientationRecord& aStart,
										 const cLuxEarthOrientationRecord& aEnd)
	{
		const double fDaySpan = aEnd.mfModifiedJulianDateUtc - aStart.mfModifiedJulianDateUtc;
		if(fDaySpan <= 0.0)
			return 0.0;
		return -ContinuousUt1MinusUtcDelta(aStart.mfUt1MinusUtcSeconds,
											 aEnd.mfUt1MinusUtcSeconds) / fDaySpan;
	}

	static bool HasValidTleChecksum(const std::string& asLine)
	{
		if(asLine.size() < 69 || asLine[68] < '0' || asLine[68] > '9')
			return false;

		int lChecksum = 0;
		for(size_t i = 0; i < 68; ++i)
		{
			if(asLine[i] >= '0' && asLine[i] <= '9')
				lChecksum += asLine[i] - '0';
			else if(asLine[i] == '-')
				++lChecksum;
		}

		return (lChecksum % 10) == (asLine[68] - '0');
	}

	static bool ResolveDataPath(const tString& asFile, tWString& asResolvedPath)
	{
		if(gpBase == NULL || gpBase->mpEngine == NULL || gpBase->mpEngine->GetResources() == NULL)
			return false;

		cFileSearcher *pFileSearcher = gpBase->mpEngine->GetResources()->GetFileSearcher();
		if(pFileSearcher)
			asResolvedPath = pFileSearcher->GetFilePath(asFile);

		if(asResolvedPath.empty())
		{
			const tWString sDirectPath = cString::To16Char(asFile);
			if(cPlatform::FileExists(sDirectPath))
				asResolvedPath = sDirectPath;
		}

		return asResolvedPath.empty() == false;
	}

	static bool ParseThreeLineElement(const tString& asFile, const std::vector<std::string>& avLines,
								 size_t alFirstLine, cThreeLineElement& aTle)
	{
		aTle.msName = TrimAscii(avLines[alFirstLine]);
		if(aTle.msName.size() >= 2 && aTle.msName[0] == '0' && aTle.msName[1] == ' ')
			aTle.msName = TrimAscii(aTle.msName.substr(2));

		aTle.msLine1 = avLines[alFirstLine + 1];
		aTle.msLine2 = avLines[alFirstLine + 2];

		const unsigned int lRecordNumber = (unsigned int)(alFirstLine / 3 + 1);
		if(aTle.msName.empty())
		{
			Warning("Could not register TLE '%s': satellite name in record %u is empty.\n",
					asFile.c_str(), lRecordNumber);
			return false;
		}

		if(aTle.msLine1.size() < 69 || aTle.msLine2.size() < 69 ||
			aTle.msLine1[0] != '1' || aTle.msLine1[1] != ' ' ||
			aTle.msLine2[0] != '2' || aTle.msLine2[1] != ' ')
		{
			Warning("Could not register TLE '%s': record %u does not contain valid fixed-column element lines.\n",
					asFile.c_str(), lRecordNumber);
			return false;
		}

		if(aTle.msLine1.compare(2, 5, aTle.msLine2, 2, 5) != 0)
		{
			Warning("Could not register TLE '%s': catalog numbers in record %u do not match.\n",
					asFile.c_str(), lRecordNumber);
			return false;
		}

		if(HasValidTleChecksum(aTle.msLine1) == false || HasValidTleChecksum(aTle.msLine2) == false)
		{
			Warning("Could not register TLE '%s': checksum validation failed in record %u.\n",
					asFile.c_str(), lRecordNumber);
			return false;
		}

		return true;
	}

	static bool ReadThreeLineElements(const tString& asFile, std::vector<cThreeLineElement>& avTles)
	{
		tWString sResolvedPath;
		if(ResolveDataPath(asFile, sResolvedPath) == false)
		{
			Warning("Could not register TLE '%s': file was not found.\n", asFile.c_str());
			return false;
		}

		FILE *pFile = cPlatform::OpenFile(sResolvedPath, _W("rb"));
		if(pFile == NULL)
		{
			Warning("Could not register TLE '%s': file could not be opened.\n", asFile.c_str());
			return false;
		}

		std::vector<std::string> vLines;
		char sBuffer[256];
		while(std::fgets(sBuffer, sizeof(sBuffer), pFile))
		{
			const size_t lLength = std::strlen(sBuffer);
			if(lLength == sizeof(sBuffer) - 1 && sBuffer[lLength - 1] != '\n' && std::feof(pFile) == 0)
			{
				std::fclose(pFile);
				Warning("Could not register TLE '%s': a line is too long.\n", asFile.c_str());
				return false;
			}

			std::string sLine(sBuffer, lLength);
			while(sLine.empty() == false && (sLine[sLine.size() - 1] == '\r' || sLine[sLine.size() - 1] == '\n'))
				sLine.erase(sLine.size() - 1);

			if(vLines.empty() && sLine.size() >= 3 &&
				(unsigned char)sLine[0] == 0xef && (unsigned char)sLine[1] == 0xbb && (unsigned char)sLine[2] == 0xbf)
				sLine.erase(0, 3);

			if(TrimAscii(sLine).empty())
				continue;

			vLines.push_back(sLine);
		}
		std::fclose(pFile);

		if(vLines.empty() || (vLines.size() % 3) != 0)
		{
			Warning("Could not register TLE '%s': expected one or more complete three-line records, found %u non-empty lines.\n",
					asFile.c_str(), (unsigned int)vLines.size());
			return false;
		}

		std::vector<cThreeLineElement> vTles;
		vTles.reserve(vLines.size() / 3);
		for(size_t i = 0; i < vLines.size(); i += 3)
		{
			cThreeLineElement tle;
			if(ParseThreeLineElement(asFile, vLines, i, tle) == false)
				return false;

			const tString sKey = cString::ToLowerCase(tle.msName);
			for(size_t j = 0; j < vTles.size(); ++j)
			{
				if(cString::ToLowerCase(vTles[j].msName) == sKey)
				{
					Warning("Could not register TLE '%s': satellite name '%s' appears more than once.\n",
							asFile.c_str(), tle.msName.c_str());
					return false;
				}
			}

			vTles.push_back(tle);
		}

		avTles.swap(vTles);
		return true;
	}

	static double GetSimulationJulianDateUtc(cLuxMap *apMap)
	{
		// The first active Sun is the map's astronomical clock. This keeps fixed
		// test dates and wall-clock dates identical for lighting and propagation.
		if(apMap && apMap->GetWorld())
		{
			cLightListIterator it = apMap->GetWorld()->GetLightIterator();
			while(it.HasNext())
			{
				iLight *pLight = it.Next();
				if(pLight && pLight->GetLightType() == eLightType_Sun && pLight->IsActive())
					return static_cast<cLightSun*>(pLight)->GetJulianDate();
			}
		}

		return cLightSun::GetSystemJulianDate();
	}

	static int GetFixedUpdatesPerSecond()
	{
		if(gpBase == NULL || gpBase->mpEngine == NULL)
			return 0;

		const double fStepSeconds = gpBase->mpEngine->GetStepSize();
		if(std::isfinite(fStepSeconds) == false || fStepSeconds <= 0.0)
			return 0;

		// cLogicTimer::GetUpdatesPerSec truncates a reconstructed floating-point
		// reciprocal. For a stored 1000/60 ms step that reciprocal can be just
		// below 60, so recover the configured integer rate by rounding instead.
		return static_cast<int>(std::floor(1.0 / fStepSeconds + 0.5));
	}

	static bool NormalizeJulianDate(double afJulianDay, double afJulianFraction,
									double& afNormalizedDay, double& afNormalizedFraction)
	{
		if(std::isfinite(afJulianDay) == false || std::isfinite(afJulianFraction) == false)
			return false;

		afNormalizedDay = std::floor(afJulianDay);
		afNormalizedFraction = (afJulianDay - afNormalizedDay) + afJulianFraction;
		const double fAdditionalDays = std::floor(afNormalizedFraction);
		afNormalizedDay += fAdditionalDays;
		afNormalizedFraction -= fAdditionalDays;
		return std::isfinite(afNormalizedDay) && std::isfinite(afNormalizedFraction);
	}

	static double GreenwichSiderealTime(double afJulianDayUt1, double afJulianFractionUt1)
	{
		// Vallado 2013, equation 3-45, evaluated from a split Julian date so a
		// 30-microsecond instrument phase tick survives the subtraction from J2000.
		const double fJulianCenturies =
			((afJulianDayUt1 - 2451545.0) + afJulianFractionUt1) / 36525.0;
		double fSiderealSeconds =
			-6.2e-6 * fJulianCenturies * fJulianCenturies * fJulianCenturies +
			0.093104 * fJulianCenturies * fJulianCenturies +
			(876600.0 * 3600.0 + 8640184.812866) * fJulianCenturies + 67310.54841;
		double fSiderealAngle = std::fmod(
			fSiderealSeconds * kDegreesToRadians / 240.0, kTwoPi);
		if(fSiderealAngle < 0.0)
			fSiderealAngle += kTwoPi;
		return fSiderealAngle;
	}

	static void TemeToEarthFixed(double afJulianDayUtc, double afJulianFractionUtc,
							 const double avTemePositionKm[3], const double avTemeVelocityKmPerSecond[3],
							 const cLuxEarthOrientationSample& aEarthOrientation,
							 cVector3d& avEcefPositionKm, cVector3d& avEcefVelocityKmPerSecond,
							 cVector3d& avEcefInertialVelocityKmPerSecond)
	{
		double fJulianDayUt1 = 0.0;
		double fJulianFractionUt1 = 0.0;
		NormalizeJulianDate(afJulianDayUtc,
			afJulianFractionUtc + aEarthOrientation.mfUt1MinusUtcSeconds / kSecondsPerDay,
			fJulianDayUt1, fJulianFractionUt1);
		double fSiderealAngle = GreenwichSiderealTime(fJulianDayUt1, fJulianFractionUt1);

		// TEME's post-1997 kinematic equation-of-equinox terms, matching
		// Vallado's reference teme2ecef implementation. UT1 also approximates TT
		// here; their difference has a negligible effect on these tiny terms.
		if(fJulianDayUt1 > 2450449.0 ||
			(fJulianDayUt1 == 2450449.0 && fJulianFractionUt1 > 0.5))
		{
			const double fJulianCenturies =
				((fJulianDayUt1 - 2451545.0) + fJulianFractionUt1) / 36525.0;
			const double fOmegaDegrees = 125.04452222 +
				(-6962890.5390 * fJulianCenturies +
				 7.455 * fJulianCenturies * fJulianCenturies +
				 0.008 * fJulianCenturies * fJulianCenturies * fJulianCenturies) / 3600.0;
			const double fOmega = std::fmod(fOmegaDegrees, 360.0) * kDegreesToRadians;
			fSiderealAngle += (0.00264 * std::sin(fOmega) + 0.000063 * std::sin(2.0 * fOmega)) *
				kDegreesToRadians / 3600.0;
		}

		fSiderealAngle = std::fmod(fSiderealAngle, kTwoPi);
		if(fSiderealAngle < 0.0)
			fSiderealAngle += kTwoPi;

		const double fCos = std::cos(fSiderealAngle);
		const double fSin = std::sin(fSiderealAngle);

		// Transpose of the PEF-to-TEME sidereal rotation.
		const cVector3d vPefPositionKm(fCos * avTemePositionKm[0] + fSin * avTemePositionKm[1],
									 -fSin * avTemePositionKm[0] + fCos * avTemePositionKm[1],
									 avTemePositionKm[2]);

		const cVector3d vRotatedVelocity(fCos * avTemeVelocityKmPerSecond[0] + fSin * avTemeVelocityKmPerSecond[1],
									  -fSin * avTemeVelocityKmPerSecond[0] + fCos * avTemeVelocityKmPerSecond[1],
									  avTemeVelocityKmPerSecond[2]);

		// Velocity in PEF is R*v - omega x r. IERS LOD corrects the nominal
		// angular velocity for the current excess length of day.
		const double fEarthRotationRate = kEarthRotationRadiansPerSecond *
			(1.0 - aEarthOrientation.mfLengthOfDaySeconds / kSecondsPerDay);
		const cVector3d vPefVelocityKmPerSecond(
			vRotatedVelocity.x + fEarthRotationRate * vPefPositionKm.y,
			vRotatedVelocity.y - fEarthRotationRate * vPefPositionKm.x,
			vRotatedVelocity.z);

		// Vallado's IAU-1980 polar-motion matrix maps ITRF/ECEF to PEF. Apply
		// its transpose to both PEF vectors using the IERS xp and yp values.
		const double fCosXp = std::cos(aEarthOrientation.mfPolarMotionXRadians);
		const double fSinXp = std::sin(aEarthOrientation.mfPolarMotionXRadians);
		const double fCosYp = std::cos(aEarthOrientation.mfPolarMotionYRadians);
		const double fSinYp = std::sin(aEarthOrientation.mfPolarMotionYRadians);

		avEcefPositionKm.x = fCosXp * vPefPositionKm.x +
			fSinXp * fSinYp * vPefPositionKm.y + fSinXp * fCosYp * vPefPositionKm.z;
		avEcefPositionKm.y = fCosYp * vPefPositionKm.y - fSinYp * vPefPositionKm.z;
		avEcefPositionKm.z = -fSinXp * vPefPositionKm.x +
			fCosXp * fSinYp * vPefPositionKm.y + fCosXp * fCosYp * vPefPositionKm.z;

		avEcefVelocityKmPerSecond.x = fCosXp * vPefVelocityKmPerSecond.x +
			fSinXp * fSinYp * vPefVelocityKmPerSecond.y + fSinXp * fCosYp * vPefVelocityKmPerSecond.z;
		avEcefVelocityKmPerSecond.y = fCosYp * vPefVelocityKmPerSecond.y - fSinYp * vPefVelocityKmPerSecond.z;
		avEcefVelocityKmPerSecond.z = -fSinXp * vPefVelocityKmPerSecond.x +
			fCosXp * fSinYp * vPefVelocityKmPerSecond.y + fCosXp * fCosYp * vPefVelocityKmPerSecond.z;

		// The inertial TEME velocity rotated into Earth-fixed axes is kept
		// separately. It defines the spacecraft's LVLH/orbital plane; the
		// coordinate derivative above includes the rotating Earth's omega x r
		// correction and instead follows the ground track.
		avEcefInertialVelocityKmPerSecond.x = fCosXp * vRotatedVelocity.x +
			fSinXp * fSinYp * vRotatedVelocity.y + fSinXp * fCosYp * vRotatedVelocity.z;
		avEcefInertialVelocityKmPerSecond.y = fCosYp * vRotatedVelocity.y -
			fSinYp * vRotatedVelocity.z;
		avEcefInertialVelocityKmPerSecond.z = -fSinXp * vRotatedVelocity.x +
			fCosXp * fSinYp * vRotatedVelocity.y + fCosXp * fCosYp * vRotatedVelocity.z;
	}

	static void EarthFixedToHpl(const cVector3d& avEcef, cVector3d& avHpl)
	{
		// ECEF +Z is north and +Y is 90 degrees east. HPL is Y-up with
		// Greenwich on +X and east on -Z: (X, Y, Z) -> (X, Z, -Y).
		avHpl.x = avEcef.x * kMetresPerKilometre;
		avHpl.y = avEcef.z * kMetresPerKilometre;
		avHpl.z = -avEcef.y * kMetresPerKilometre;
	}

	static double MatrixColumnLength(const cMatrixf& aMatrix, int alColumn)
	{
		const double fX = aMatrix.m[0][alColumn];
		const double fY = aMatrix.m[1][alColumn];
		const double fZ = aMatrix.m[2][alColumn];
		const double fLength = std::sqrt(fX * fX + fY * fY + fZ * fZ);
		return fLength > 0.0 && std::isfinite(fLength) ? fLength : 1.0;
	}

	static bool BuildOrbitFrame(const cVector3d& avPositionMetres,
							const cVector3d& avVelocityMetresPerSecond,
							cVector3d& avCrossTrack, cVector3d& avRadialOut,
							cVector3d& avAlongTrack)
	{
		avRadialOut = Normalize(avPositionMetres);
		const cVector3d vVelocity = Normalize(avVelocityMetresPerSecond);
		avCrossTrack = Normalize(Cross(avRadialOut, vVelocity));
		avAlongTrack = Normalize(Cross(avCrossTrack, avRadialOut));

		return Dot(avRadialOut, avRadialOut) != 0.0 &&
			Dot(avCrossTrack, avCrossTrack) != 0.0 && Dot(avAlongTrack, avAlongTrack) != 0.0;
	}

	static bool BuildHplTransform(const cLuxSatellitePose& aPose,
							  const cMatrixf& aCurrentTransform, cMatrixf& aTransform)
	{
		if(IsFinite(aPose.mvPositionMetres) == false || IsFinite(aPose.mvCrossTrack) == false ||
			IsFinite(aPose.mvRadialOut) == false || IsFinite(aPose.mvAlongTrack) == false)
			return false;

		const double fScaleX = MatrixColumnLength(aCurrentTransform, 0);
		const double fScaleY = MatrixColumnLength(aCurrentTransform, 1);
		const double fScaleZ = MatrixColumnLength(aCurrentTransform, 2);

		aTransform = cMatrixf::Identity;
		aTransform.m[0][0] = (float)(aPose.mvCrossTrack.x * fScaleX);
		aTransform.m[1][0] = (float)(aPose.mvCrossTrack.y * fScaleX);
		aTransform.m[2][0] = (float)(aPose.mvCrossTrack.z * fScaleX);
		aTransform.m[0][1] = (float)(aPose.mvRadialOut.x * fScaleY);
		aTransform.m[1][1] = (float)(aPose.mvRadialOut.y * fScaleY);
		aTransform.m[2][1] = (float)(aPose.mvRadialOut.z * fScaleY);
		aTransform.m[0][2] = (float)(aPose.mvAlongTrack.x * fScaleZ);
		aTransform.m[1][2] = (float)(aPose.mvAlongTrack.y * fScaleZ);
		aTransform.m[2][2] = (float)(aPose.mvAlongTrack.z * fScaleZ);
		aTransform.m[0][3] = (float)aPose.mvPositionMetres.x;
		aTransform.m[1][3] = (float)aPose.mvPositionMetres.y;
		aTransform.m[2][3] = (float)aPose.mvPositionMetres.z;
		return true;
	}
}

bool cLuxEarthOrientationTable::Load(const tWString& asPath, tString& asError)
{
	FILE *pFile = cPlatform::OpenFile(asPath, _W("rb"));
	if(pFile == NULL)
	{
		asError = "file could not be opened";
		return false;
	}

	std::vector<cLuxEarthOrientationRecord> vRecords;
	char sBuffer[512];
	while(std::fgets(sBuffer, sizeof(sBuffer), pFile))
	{
		const size_t lLength = std::strlen(sBuffer);
		if(lLength == sizeof(sBuffer) - 1 && sBuffer[lLength - 1] != '\n' && std::feof(pFile) == 0)
		{
			std::fclose(pFile);
			asError = "a line is too long";
			return false;
		}

		std::string sLine(sBuffer, lLength);
		while(sLine.empty() == false && (sLine[sLine.size() - 1] == '\r' || sLine[sLine.size() - 1] == '\n'))
			sLine.erase(sLine.size() - 1);

		// finals2000A uses a fixed-column MJD at columns 8-15. Lines after
		// the prediction horizon contain a date but intentionally omit EOP.
		double fModifiedJulianDate = 0.0;
		if(ParseFixedDouble(sLine, 7, 8, fModifiedJulianDate) == false)
			continue;

		double fBulletinAX = 0.0;
		double fBulletinAY = 0.0;
		double fBulletinAUt1MinusUtc = 0.0;
		const bool bHasBulletinA =
			ParseFixedDouble(sLine, 18, 9, fBulletinAX) &&
			ParseFixedDouble(sLine, 37, 9, fBulletinAY) &&
			ParseFixedDouble(sLine, 58, 10, fBulletinAUt1MinusUtc);

		double fBulletinBX = 0.0;
		double fBulletinBY = 0.0;
		double fBulletinBUt1MinusUtc = 0.0;
		const bool bHasBulletinB =
			ParseFixedDouble(sLine, 134, 10, fBulletinBX) &&
			ParseFixedDouble(sLine, 144, 10, fBulletinBY) &&
			ParseFixedDouble(sLine, 154, 11, fBulletinBUt1MinusUtc);

		if(bHasBulletinA == false && bHasBulletinB == false)
			continue;

		cLuxEarthOrientationRecord record;
		record.mfModifiedJulianDateUtc = fModifiedJulianDate;
		// Bulletin B contains the final combined values when available;
		// otherwise use Bulletin A rapid values or predictions.
		record.mfPolarMotionXArcseconds = bHasBulletinB ? fBulletinBX : fBulletinAX;
		record.mfPolarMotionYArcseconds = bHasBulletinB ? fBulletinBY : fBulletinAY;
		record.mfUt1MinusUtcSeconds = bHasBulletinB ? fBulletinBUt1MinusUtc : fBulletinAUt1MinusUtc;
		record.mbHasLengthOfDay = ParseFixedDouble(sLine, 79, 7, record.mfLengthOfDayMilliseconds);
		if(record.mbHasLengthOfDay == false)
			record.mfLengthOfDayMilliseconds = 0.0;
		vRecords.push_back(record);
	}
	std::fclose(pFile);

	if(vRecords.size() < 2)
	{
		asError = "fewer than two complete finals2000A records were found";
		return false;
	}

	std::sort(vRecords.begin(), vRecords.end(),
		[](const cLuxEarthOrientationRecord& aA, const cLuxEarthOrientationRecord& aB)
		{
			return aA.mfModifiedJulianDateUtc < aB.mfModifiedJulianDateUtc;
		});

	for(size_t i = 1; i < vRecords.size(); ++i)
	{
		if(vRecords[i].mfModifiedJulianDateUtc <= vRecords[i - 1].mfModifiedJulianDateUtc)
		{
			asError = "duplicate or non-increasing MJD records were found";
			return false;
		}
	}

	mvRecords.swap(vRecords);
	asError.clear();
	return true;
}

bool cLuxEarthOrientationTable::Sample(double afJulianDayUtc, double afJulianFractionUtc,
									   cLuxEarthOrientationSample& aSample) const
{
	double fJulianDayUtc = 0.0;
	double fJulianFractionUtc = 0.0;
	if(mvRecords.empty() ||
		NormalizeJulianDate(afJulianDayUtc, afJulianFractionUtc,
			fJulianDayUtc, fJulianFractionUtc) == false)
		return false;

	const double fModifiedJulianDate =
		(fJulianDayUtc - kJulianDateToModifiedJulianDate) + fJulianFractionUtc;
	std::vector<cLuxEarthOrientationRecord>::const_iterator itUpper =
		std::lower_bound(mvRecords.begin(), mvRecords.end(), fModifiedJulianDate,
			[](const cLuxEarthOrientationRecord& aRecord, double afDate)
			{
				return aRecord.mfModifiedJulianDateUtc < afDate;
			});

	if(itUpper == mvRecords.end())
		return false;

	const double fDateTolerance = 1.0e-9;
	if(std::fabs(itUpper->mfModifiedJulianDateUtc - fModifiedJulianDate) <= fDateTolerance)
	{
		aSample.mfPolarMotionXRadians = itUpper->mfPolarMotionXArcseconds * kArcsecondsToRadians;
		aSample.mfPolarMotionYRadians = itUpper->mfPolarMotionYArcseconds * kArcsecondsToRadians;
		aSample.mfUt1MinusUtcSeconds = itUpper->mfUt1MinusUtcSeconds;
		if(itUpper->mbHasLengthOfDay)
			aSample.mfLengthOfDaySeconds = itUpper->mfLengthOfDayMilliseconds * 0.001;
		else if(itUpper + 1 != mvRecords.end())
			aSample.mfLengthOfDaySeconds = DerivedLengthOfDaySeconds(*itUpper, *(itUpper + 1));
		else if(itUpper != mvRecords.begin())
			aSample.mfLengthOfDaySeconds = DerivedLengthOfDaySeconds(*(itUpper - 1), *itUpper);
		else
			aSample.mfLengthOfDaySeconds = 0.0;
		return true;
	}

	if(itUpper == mvRecords.begin())
		return false;

	const cLuxEarthOrientationRecord& lower = *(itUpper - 1);
	const cLuxEarthOrientationRecord& upper = *itUpper;
	const double fDaySpan = upper.mfModifiedJulianDateUtc - lower.mfModifiedJulianDateUtc;
	if(fDaySpan <= 0.0)
		return false;

	const double fFraction = (fModifiedJulianDate - lower.mfModifiedJulianDateUtc) / fDaySpan;
	aSample.mfPolarMotionXRadians =
		(lower.mfPolarMotionXArcseconds +
		 (upper.mfPolarMotionXArcseconds - lower.mfPolarMotionXArcseconds) * fFraction) * kArcsecondsToRadians;
	aSample.mfPolarMotionYRadians =
		(lower.mfPolarMotionYArcseconds +
		 (upper.mfPolarMotionYArcseconds - lower.mfPolarMotionYArcseconds) * fFraction) * kArcsecondsToRadians;

	// UT1-UTC has an integer-second step at a leap second. Interpolate its
	// continuous branch, then take the new record's value exactly at midnight.
	const double fContinuousUt1Delta =
		ContinuousUt1MinusUtcDelta(lower.mfUt1MinusUtcSeconds, upper.mfUt1MinusUtcSeconds);
	aSample.mfUt1MinusUtcSeconds = lower.mfUt1MinusUtcSeconds + fContinuousUt1Delta * fFraction;

	if(lower.mbHasLengthOfDay && upper.mbHasLengthOfDay)
	{
		aSample.mfLengthOfDaySeconds =
			(lower.mfLengthOfDayMilliseconds +
			 (upper.mfLengthOfDayMilliseconds - lower.mfLengthOfDayMilliseconds) * fFraction) * 0.001;
	}
	else if(lower.mbHasLengthOfDay)
		aSample.mfLengthOfDaySeconds = lower.mfLengthOfDayMilliseconds * 0.001;
	else if(upper.mbHasLengthOfDay)
		aSample.mfLengthOfDaySeconds = upper.mfLengthOfDayMilliseconds * 0.001;
	else
		aSample.mfLengthOfDaySeconds = -fContinuousUt1Delta / fDaySpan;

	return true;
}

class cLuxSatelliteOrbit
{
public:
	cLuxSatelliteOrbit()
	{
		mpMap = NULL;
		mpIconBillboard = NULL;
		mpScanMeshEntity = NULL;
		mbScanVisibilityCaptured = false;
		mbScanIconWasVisible = false;
		mbScanMeshWasVisible = false;
		mlLastPropagationError = 0;
		std::memset(&mSatelliteRecord, 0, sizeof(mSatelliteRecord));
	}

	tString msName;
	tString msSourceFile;
	cLuxMap *mpMap;
	cBillboard *mpIconBillboard;
	std::vector<cBillboard*> mvLabelBillboards;
	cMeshEntity *mpScanMeshEntity;
	std::vector<bool> mvScanLabelWasVisible;
	bool mbScanVisibilityCaptured;
	bool mbScanIconWasVisible;
	bool mbScanMeshWasVisible;
	elsetrec mSatelliteRecord;
	int mlLastPropagationError;
};

struct cLuxSatelliteScanSunState
{
	cLuxSatelliteScanSunState()
		: mpSun(NULL), mbUsedSystemTime(false), mfFixedJulianDate(0.0)
	{
	}

	cLightSun *mpSun;
	bool mbUsedSystemTime;
	double mfFixedJulianDate;
};

// The sensor and player cameras share one world. This callback suppresses the
// spacecraft's presentation-only renderables and applies the individual
// sample's Sun time solely while its viewport is drawing, then restores every
// captured state before the next sensor or player view runs.
class cLuxMsuMrSensorViewportCallback : public iViewportCallback
{
public:
	cLuxMsuMrSensorViewportCallback(cLuxSatelliteOrbit *apOrbit)
		: mpOrbit(apOrbit), mbSuppressed(false), mbIconWasVisible(false),
		  mbMeshWasVisible(false), mpWorld(NULL), mbSkyBoxWasActive(false),
		  mfSampleJulianDate(0.0), mbHasSampleJulianDate(false),
		  mbRenderTimingActive(false), mbRenderTimingValid(false),
		  mfLastWorldDrawMilliseconds(0.0)
	{
	}

	void SetSampleJulianDate(double afJulianDayUtc,
							 double afJulianFractionUtc)
	{
		mfSampleJulianDate = afJulianDayUtc + afJulianFractionUtc;
		mbHasSampleJulianDate = std::isfinite(mfSampleJulianDate);
	}

	void ResetRenderTiming()
	{
		mbRenderTimingActive = false;
		mbRenderTimingValid = false;
		mfLastWorldDrawMilliseconds = 0.0;
	}

	bool HasRenderTiming() const { return mbRenderTimingValid; }
	double GetLastWorldDrawMilliseconds() const
	{
		return mfLastWorldDrawMilliseconds;
	}

	void OnPreWorldDraw()
	{
		if(mpOrbit == NULL || mbSuppressed)
			return;

		mbIconWasVisible = mpOrbit->mpIconBillboard &&
			mpOrbit->mpIconBillboard->GetVisibleVar();
		mvLabelWasVisible.clear();
		mvLabelWasVisible.reserve(mpOrbit->mvLabelBillboards.size());
		for(size_t i = 0; i < mpOrbit->mvLabelBillboards.size(); ++i)
		{
			mvLabelWasVisible.push_back(mpOrbit->mvLabelBillboards[i] &&
				mpOrbit->mvLabelBillboards[i]->GetVisibleVar());
		}
		mbMeshWasVisible = mpOrbit->mpScanMeshEntity &&
			mpOrbit->mpScanMeshEntity->IsVisible();
		mpWorld = mpOrbit->mpMap ? mpOrbit->mpMap->GetWorld() : NULL;
		mbSkyBoxWasActive = mpWorld && mpWorld->GetSkyBoxActive();
		mvSunStates.clear();
		if(mpWorld && mbHasSampleJulianDate)
		{
			cLightListIterator itLight = mpWorld->GetLightIterator();
			while(itLight.HasNext())
			{
				iLight *pLight = itLight.Next();
				if(pLight == NULL || pLight->GetLightType() != eLightType_Sun)
					continue;

				cLightSun *pSun = static_cast<cLightSun*>(pLight);
				cLuxSatelliteScanSunState state;
				state.mpSun = pSun;
				state.mbUsedSystemTime = pSun->GetUseSystemTime();
				state.mfFixedJulianDate = pSun->GetJulianDate();
				mvSunStates.push_back(state);
				pSun->SetUseSystemTime(false);
				pSun->SetJulianDate(mfSampleJulianDate);
			}
		}

		if(mpOrbit->mpIconBillboard)
			mpOrbit->mpIconBillboard->SetVisible(false);
		for(size_t i = 0; i < mpOrbit->mvLabelBillboards.size(); ++i)
		{
			if(mpOrbit->mvLabelBillboards[i])
				mpOrbit->mvLabelBillboards[i]->SetVisible(false);
		}
		if(mpOrbit->mpScanMeshEntity)
			mpOrbit->mpScanMeshEntity->SetVisible(false);
		if(mpWorld)
			mpWorld->SetSkyBoxActive(false);
		mbSuppressed = true;
		mRenderStart = std::chrono::steady_clock::now();
		mbRenderTimingActive = true;
	}

	void OnPostWorldDraw()
	{
		if(mbRenderTimingActive)
		{
			const std::chrono::steady_clock::time_point end =
				std::chrono::steady_clock::now();
			mfLastWorldDrawMilliseconds =
				std::chrono::duration<double, std::milli>(end - mRenderStart).count();
			mbRenderTimingValid = true;
			mbRenderTimingActive = false;
		}
		Restore();
	}

	void Restore()
	{
		if(mpOrbit == NULL || mbSuppressed == false)
			return;

		if(mpOrbit->mpIconBillboard)
			mpOrbit->mpIconBillboard->SetVisible(mbIconWasVisible);
		for(size_t i = 0; i < mpOrbit->mvLabelBillboards.size(); ++i)
		{
			if(mpOrbit->mvLabelBillboards[i] && i < mvLabelWasVisible.size())
				mpOrbit->mvLabelBillboards[i]->SetVisible(mvLabelWasVisible[i]);
		}
		if(mpOrbit->mpScanMeshEntity)
			mpOrbit->mpScanMeshEntity->SetVisible(mbMeshWasVisible);
		for(size_t i = 0; i < mvSunStates.size(); ++i)
		{
			if(mvSunStates[i].mpSun)
			{
				mvSunStates[i].mpSun->SetJulianDate(
					mvSunStates[i].mfFixedJulianDate);
				mvSunStates[i].mpSun->SetUseSystemTime(
					mvSunStates[i].mbUsedSystemTime);
			}
		}
		if(mpWorld)
			mpWorld->SetSkyBoxActive(mbSkyBoxWasActive);

		mvLabelWasVisible.clear();
		mvSunStates.clear();
		mpWorld = NULL;
		mbSuppressed = false;
	}

private:
	cLuxSatelliteOrbit *mpOrbit;
	bool mbSuppressed;
	bool mbIconWasVisible;
	bool mbMeshWasVisible;
	cWorld *mpWorld;
	bool mbSkyBoxWasActive;
	double mfSampleJulianDate;
	bool mbHasSampleJulianDate;
	std::chrono::steady_clock::time_point mRenderStart;
	bool mbRenderTimingActive;
	bool mbRenderTimingValid;
	double mfLastWorldDrawMilliseconds;
	std::vector<bool> mvLabelWasVisible;
	std::vector<cLuxSatelliteScanSunState> mvSunStates;
};

struct cLuxMsuMrSensorSampleState
{
	cLuxMsuMrSensorSampleState()
		: mpView(NULL), mpViewportCallback(NULL), mlSampleIndex(0),
		  mfJulianDayUtc(0.0), mfJulianFractionUtc(0.0),
		  mfLookAngleDegrees(0.0), mfSlantRangeMetres(0.0),
		  mfFootprintMetres(0.0), mfLatitudeDegrees(0.0),
		  mfLongitudeDegrees(0.0), mfAimErrorArcseconds(0.0),
		  mfOriginQuantizationMetres(0.0)
	{
	}

	cLuxCameraView *mpView;
	cLuxMsuMrSensorViewportCallback *mpViewportCallback;
	std::uint32_t mlSampleIndex;
	double mfJulianDayUtc;
	double mfJulianFractionUtc;
	double mfLookAngleDegrees;
	double mfSlantRangeMetres;
	double mfFootprintMetres;
	double mfLatitudeDegrees;
	double mfLongitudeDegrees;
	double mfAimErrorArcseconds;
	double mfOriginQuantizationMetres;
};

class cLuxSatelliteScanPresentationState
{
public:
	cLuxSatelliteScanPresentationState()
		: mpMap(NULL), mpSensorBatchFrameBuffer(NULL),
		  mpSensorBatchTexture(NULL), mpSensorBatchDepthStencil(NULL),
		  mpScanProduct(NULL),
		  mbSensorLinePending(false),
		  mlSensorLineIndex(0), mlSensorMirrorFaceIndex(0),
		  mlCurrentStripBatchStart(0), mlCurrentStripBatchCount(0),
		  mlOverwrittenSensorLines(0), mlPerformanceBatchCount(0),
		  mfPreparationWallMilliseconds(0.0),
		  mfCurrentLineRenderWallMilliseconds(0.0),
		  mfCurrentLineMaxViewRenderWallMilliseconds(0.0),
		  mfFirstBatchPackSubmitWallMilliseconds(0.0),
		  mfPreparationWallSumMilliseconds(0.0),
		  mfPreparationWallMaxMilliseconds(0.0),
		  mfFirstBatchPackSubmitWallSumMilliseconds(0.0),
		  mfFirstBatchPackSubmitWallMaxMilliseconds(0.0),
		  mfRenderWallSumMilliseconds(0.0),
		  mfRenderWallMaxMilliseconds(0.0),
		  mfReadbackWallSumMilliseconds(0.0),
		  mfReadbackWallMaxMilliseconds(0.0),
		  mfLineLatencyWallSumMilliseconds(0.0),
		  mfLineLatencyWallMaxMilliseconds(0.0),
		  mfCommitWallSumMilliseconds(0.0),
		  mfCommitWallMaxMilliseconds(0.0),
		  mfStripMaxProjectionRayErrorArcseconds(0.0),
		  mfStripMaxGroundDisplacementMetres(0.0),
		  mfStripMaxTimeOffsetMilliseconds(0.0),
		  mbIlluminationDiagnosticsValid(false),
		  mbPointSunEarthOccluded(false),
		  mfGroundSolarElevationMinDegrees(0.0),
		  mfGroundSolarElevationMaxDegrees(0.0),
		  mlDaylightEndpointCount(0),
		  mlIlluminationEndpointCount(0),
		  mlActiveSunCount(0),
		  mfMaxActiveSunIntensity(0.0)
	{
		for(int i = 0; i < kMsuMrRepresentativeSampleCount; ++i)
			mvRepresentativeSolarElevationDegrees[i] = 0.0;
	}

	cLuxMap *mpMap;
	tString msTargetKey;
	cLuxMsuMrTumbleProfile mTumbleProfile;
	std::vector<cLuxSatelliteScanSunState> mvSuns;
	cLuxMsuMrSensorSampleState mvSensorStrips[kMsuMrStripCount];
	iFrameBuffer *mpSensorBatchFrameBuffer;
	iTexture *mpSensorBatchTexture;
	iDepthStencilBuffer *mpSensorBatchDepthStencil;
	cLuxMsuMrScanProduct *mpScanProduct;
	bool mbSensorLinePending;
	std::uint64_t mlSensorLineIndex;
	std::uint32_t mlSensorMirrorFaceIndex;
	int mlCurrentStripBatchStart;
	int mlCurrentStripBatchCount;
	std::uint64_t mlOverwrittenSensorLines;
	std::uint64_t mlPerformanceBatchCount;
	double mfPreparationWallMilliseconds;
	double mfCurrentLineRenderWallMilliseconds;
	double mfCurrentLineMaxViewRenderWallMilliseconds;
	double mfFirstBatchPackSubmitWallMilliseconds;
	std::chrono::steady_clock::time_point mLineAcquisitionStart;
	double mfPreparationWallSumMilliseconds;
	double mfPreparationWallMaxMilliseconds;
	double mfFirstBatchPackSubmitWallSumMilliseconds;
	double mfFirstBatchPackSubmitWallMaxMilliseconds;
	double mfRenderWallSumMilliseconds;
	double mfRenderWallMaxMilliseconds;
	double mfReadbackWallSumMilliseconds;
	double mfReadbackWallMaxMilliseconds;
	double mfLineLatencyWallSumMilliseconds;
	double mfLineLatencyWallMaxMilliseconds;
	double mfCommitWallSumMilliseconds;
	double mfCommitWallMaxMilliseconds;
	double mfStripMaxProjectionRayErrorArcseconds;
	double mfStripMaxGroundDisplacementMetres;
	double mfStripMaxTimeOffsetMilliseconds;
	bool mbIlluminationDiagnosticsValid;
	bool mbPointSunEarthOccluded;
	double mfGroundSolarElevationMinDegrees;
	double mfGroundSolarElevationMaxDegrees;
	double mvRepresentativeSolarElevationDegrees[kMsuMrRepresentativeSampleCount];
	unsigned int mlDaylightEndpointCount;
	unsigned int mlIlluminationEndpointCount;
	unsigned int mlActiveSunCount;
	double mfMaxActiveSunIntensity;
};

static void ResetMsuMrPendingSensorLine(
	cLuxSatelliteScanPresentationState *apState)
{
	if(apState == NULL)
		return;

	for(int i = 0; i < kMsuMrStripCount; ++i)
	{
		if(apState->mvSensorStrips[i].mpView)
			apState->mvSensorStrips[i].mpView->SetVisible(false);
	}
	apState->mbSensorLinePending = false;
	apState->mlCurrentStripBatchStart = 0;
	apState->mlCurrentStripBatchCount = 0;
	apState->mlOverwrittenSensorLines = 0;
	apState->mfCurrentLineRenderWallMilliseconds = 0.0;
	apState->mfCurrentLineMaxViewRenderWallMilliseconds = 0.0;
	apState->mfFirstBatchPackSubmitWallMilliseconds = 0.0;
	apState->mbIlluminationDiagnosticsValid = false;
}

static bool AimMsuMrSensorSample(cLuxMsuMrSensorSampleState& aSample,
	const cLuxMsuMrEarthSampleEvent& aEvent,
	const cLuxSatellitePose& aPose,
	const cLuxMsuMrInstrumentFrame& aInstrumentFrame,
	const cLuxMsuMrGroundSample& aGroundSample)
{
	if(aSample.mpView == NULL || aSample.mpViewportCallback == NULL)
		return false;

	cVector3f vForward(
		static_cast<float>(aGroundSample.mvRayDirection.x),
		static_cast<float>(aGroundSample.mvRayDirection.y),
		static_cast<float>(aGroundSample.mvRayDirection.z));
	cVector3f vUp(
		static_cast<float>(aInstrumentFrame.mvAlongTrack.x),
		static_cast<float>(aInstrumentFrame.mvAlongTrack.y),
		static_cast<float>(aInstrumentFrame.mvAlongTrack.z));
	vForward.Normalize();
	cVector3f vRight = cMath::Vector3Cross(vForward, vUp);
	vRight.Normalize();
	vUp = cMath::Vector3Cross(vRight, vForward);
	vUp.Normalize();

	cMatrixf mtxViewRotation = cMatrixf::Identity;
	mtxViewRotation.SetRight(vRight);
	mtxViewRotation.SetUp(vUp);
	mtxViewRotation.SetForward(vForward * -1.0f);
	const cVector3f vSensorPosition(
		static_cast<float>(aPose.mvPositionMetres.x),
		static_cast<float>(aPose.mvPositionMetres.y),
		static_cast<float>(aPose.mvPositionMetres.z));
	aSample.mpView->SetTransform(vSensorPosition, mtxViewRotation);
	aSample.mpViewportCallback->SetSampleJulianDate(
		aEvent.mfJulianDayUtc, aEvent.mfJulianFractionUtc);
	aSample.mpViewportCallback->ResetRenderTiming();

	aSample.mlSampleIndex = aEvent.mlEarthSampleIndex;
	aSample.mfJulianDayUtc = aEvent.mfJulianDayUtc;
	aSample.mfJulianFractionUtc = aEvent.mfJulianFractionUtc;
	aSample.mfLookAngleDegrees = aGroundSample.mfLookAngleDegrees;
	aSample.mfSlantRangeMetres = aGroundSample.mfSlantRangeMetres;
	aSample.mfFootprintMetres = 2.0 * aGroundSample.mfSlantRangeMetres *
		std::tan(cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees() *
			kDegreesToRadians * 0.5);
	aSample.mfLatitudeDegrees = aGroundSample.mfLatitudeDegrees;
	aSample.mfLongitudeDegrees = aGroundSample.mfLongitudeDegrees;

	const cVector3f vCameraForward = aSample.mpView->GetCamera()->GetForward();
	const double fAimDot =
		static_cast<double>(vCameraForward.x) * aGroundSample.mvRayDirection.x +
		static_cast<double>(vCameraForward.y) * aGroundSample.mvRayDirection.y +
		static_cast<double>(vCameraForward.z) * aGroundSample.mvRayDirection.z;
	const double fAimCrossX =
		static_cast<double>(vCameraForward.y) * aGroundSample.mvRayDirection.z -
		static_cast<double>(vCameraForward.z) * aGroundSample.mvRayDirection.y;
	const double fAimCrossY =
		static_cast<double>(vCameraForward.z) * aGroundSample.mvRayDirection.x -
		static_cast<double>(vCameraForward.x) * aGroundSample.mvRayDirection.z;
	const double fAimCrossZ =
		static_cast<double>(vCameraForward.x) * aGroundSample.mvRayDirection.y -
		static_cast<double>(vCameraForward.y) * aGroundSample.mvRayDirection.x;
	const double fAimCrossLength = std::sqrt(
		fAimCrossX * fAimCrossX + fAimCrossY * fAimCrossY +
		fAimCrossZ * fAimCrossZ);
	aSample.mfAimErrorArcseconds = std::atan2(fAimCrossLength, fAimDot) /
		kDegreesToRadians * 3600.0;

	const cVector3f vCameraPosition = aSample.mpView->GetCamera()->GetPosition();
	const double fOriginErrorX =
		static_cast<double>(vCameraPosition.x) - aPose.mvPositionMetres.x;
	const double fOriginErrorY =
		static_cast<double>(vCameraPosition.y) - aPose.mvPositionMetres.y;
	const double fOriginErrorZ =
		static_cast<double>(vCameraPosition.z) - aPose.mvPositionMetres.z;
	aSample.mfOriginQuantizationMetres = std::sqrt(
		fOriginErrorX * fOriginErrorX + fOriginErrorY * fOriginErrorY +
		fOriginErrorZ * fOriginErrorZ);
	return std::isfinite(aSample.mfAimErrorArcseconds) &&
		std::isfinite(aSample.mfOriginQuantizationMetres);
}

static bool CreateSatelliteLabel(cLuxSatelliteOrbit *apOrbit, iFontData *apFont)
{
	if(apOrbit == NULL || apFont == NULL || apOrbit->mpMap == NULL ||
		apOrbit->mpMap->GetWorld() == NULL)
	{
		return false;
	}

	cWorld *pWorld = apOrbit->mpMap->GetWorld();
	const tWString sLabel = cString::To16Char(apOrbit->msName);
	const cVector2f vFontScale(kSatelliteLabelLineHeightMetres);
	float fCursorX = -apFont->GetLength(vFontScale, sLabel.c_str()) * 0.5f;
	const float fLineTopY = -(kSatelliteIconWorldSizeMetres * 0.5f +
		kSatelliteLabelGapMetres);

	for(size_t i = 0; i < sLabel.size(); ++i)
	{
		const wchar_t lCharacter = sLabel[i];
		if(lCharacter < apFont->GetFirstChar() || lCharacter > apFont->GetLastChar())
			continue;

		cGlyph *pGlyph = apFont->GetGlyph(lCharacter - apFont->GetFirstChar());
		if(pGlyph == NULL)
			continue;

		const float fAdvance = pGlyph->mfAdvance * kSatelliteLabelLineHeightMetres;
		if(lCharacter != L' ' && pGlyph->mpGuiGfx)
		{
			cFrameSubImage *pImage = pGlyph->mpGuiGfx->GetImage(0);
			if(pImage && pImage->GetVertexVec().size() >= 4)
			{
				const cVector2f vGlyphSize = pGlyph->mvSize * vFontScale;
				const cVector2f vGlyphOffset(
					fCursorX + pGlyph->mvOffset.x * kSatelliteLabelLineHeightMetres + vGlyphSize.x * 0.5f,
					fLineTopY - pGlyph->mvOffset.y * kSatelliteLabelLineHeightMetres - vGlyphSize.y * 0.5f);
				cBillboard *pGlyphBillboard = pWorld->CreateBillboard(
					"__SGP4Label_" + apOrbit->msName + "_" + cString::ToString((int)i),
					vGlyphSize, eBillboardType_Point, kSatelliteLabelMaterial, false);
				if(pGlyphBillboard == NULL || pGlyphBillboard->GetMaterial() == NULL)
				{
					if(pGlyphBillboard) pWorld->DestroyBillboard(pGlyphBillboard);
					for(size_t j = 0; j < apOrbit->mvLabelBillboards.size(); ++j)
						pWorld->DestroyBillboard(apOrbit->mvLabelBillboards[j]);
					apOrbit->mvLabelBillboards.clear();
					return false;
				}

				const tVertexVec& vImageVertices = pImage->GetVertexVec();
				pGlyphBillboard->SetUVRect(
					cVector2f(vImageVertices[0].tex.x, vImageVertices[0].tex.y),
					cVector2f(vImageVertices[2].tex.x, vImageVertices[2].tex.y), true);
				pGlyphBillboard->SetCameraSpaceOffset(vGlyphOffset);
				pGlyphBillboard->SetTranslucentSortPriority(kSatelliteLabelTranslucentPriority);
				apOrbit->mvLabelBillboards.push_back(pGlyphBillboard);
			}
		}

		fCursorX += fAdvance;
	}

	return apOrbit->mvLabelBillboards.empty() == false;
}

cLuxSatelliteHandler::cLuxSatelliteHandler()
{
	mpMsuMrSimulation = hplNew(cLuxMsuMrSimulation, ());
	mpScanPresentationState = NULL;
	mpEarthOrientationTable = hplNew(cLuxEarthOrientationTable, ());
	mbDefaultEarthOrientationLoadAttempted = false;
	mbEarthOrientationWarningShown = false;
}

cLuxSatelliteHandler::~cLuxSatelliteHandler()
{
	Reset();
	if(mpScanPresentationState) hplDelete(mpScanPresentationState);
	hplDelete(mpMsuMrSimulation);
	hplDelete(mpEarthOrientationTable);
}

bool cLuxSatelliteHandler::LoadEarthOrientationData(const tString& asFile, bool abLogFailure)
{
	tWString sResolvedPath;
	if(ResolveDataPath(asFile, sResolvedPath) == false)
	{
		if(abLogFailure)
			Warning("Could not register Earth-orientation data '%s': file was not found.\n", asFile.c_str());
		return false;
	}

	cLuxEarthOrientationTable table;
	tString sError;
	if(table.Load(sResolvedPath, sError) == false)
	{
		if(abLogFailure)
			Warning("Could not register Earth-orientation data '%s': %s.\n", asFile.c_str(), sError.c_str());
		return false;
	}

	*mpEarthOrientationTable = table;
	mbDefaultEarthOrientationLoadAttempted = true;
	mbEarthOrientationWarningShown = false;
	Log("Registered %u IERS Earth-orientation records from '%s' (MJD %.2f through %.2f).\n",
		(unsigned int)mpEarthOrientationTable->GetRecordCount(), asFile.c_str(),
		mpEarthOrientationTable->GetFirstModifiedJulianDate(),
		mpEarthOrientationTable->GetLastModifiedJulianDate());
	return true;
}

void cLuxSatelliteHandler::EnsureEarthOrientationData()
{
	if(mpEarthOrientationTable == NULL || mpEarthOrientationTable->Empty() == false ||
		mbDefaultEarthOrientationLoadAttempted)
		return;

	mbDefaultEarthOrientationLoadAttempted = true;
	if(LoadEarthOrientationData(kDefaultEarthOrientationFile, false) == false)
		LoadEarthOrientationData(kRepositoryEarthOrientationFile, false);
}

bool cLuxSatelliteHandler::RegisterEarthOrientationData(const tString& asFile)
{
	if((mpMsuMrSimulation && mpMsuMrSimulation->IsActive()) ||
		mpScanPresentationState)
	{
		Warning("Could not register Earth-orientation data '%s' while an MSU-MR scan is active.\n",
			asFile.c_str());
		return false;
	}
	return LoadEarthOrientationData(asFile, true);
}

void cLuxSatelliteHandler::GetSatelliteNames(tStringVec& avNames) const
{
	avNames.clear();
	avNames.reserve(m_mapOrbits.size());
	for(tLuxSatelliteOrbitMap::const_iterator it = m_mapOrbits.begin(); it != m_mapOrbits.end(); ++it)
	{
		if(it->second)
			avNames.push_back(it->second->msName);
	}
}

bool cLuxSatelliteHandler::SelectSatellite(const tString& asName)
{
	const tString sSelectedKey = cString::ToLowerCase(asName);
	bool bFound = false;

	for(tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin(); it != m_mapOrbits.end(); ++it)
	{
		cLuxSatelliteOrbit *pOrbit = it->second;
		if(pOrbit == NULL)
			continue;

		const bool bSelected = it->first == sSelectedKey;
		bFound = bFound || bSelected;
		if(pOrbit->mpIconBillboard)
			pOrbit->mpIconBillboard->SetColor(bSelected ? cColor(0, 1, 0, 1) : cColor(1, 1, 1, 1));
	}

	msSelectedSatelliteKey = bFound ? sSelectedKey : "";
	return bFound;
}

void cLuxSatelliteHandler::SetOrbitScanVisibility(cLuxSatelliteOrbit *apOrbit,
												   bool abTargetVisible)
{
	if(apOrbit == NULL || apOrbit->mpMap == NULL)
		return;

	if(apOrbit->mbScanVisibilityCaptured == false)
	{
		apOrbit->mbScanIconWasVisible = apOrbit->mpIconBillboard &&
			apOrbit->mpIconBillboard->GetVisibleVar();
		apOrbit->mvScanLabelWasVisible.clear();
		apOrbit->mvScanLabelWasVisible.reserve(apOrbit->mvLabelBillboards.size());
		for(size_t i = 0; i < apOrbit->mvLabelBillboards.size(); ++i)
		{
			apOrbit->mvScanLabelWasVisible.push_back(
				apOrbit->mvLabelBillboards[i] && apOrbit->mvLabelBillboards[i]->GetVisibleVar());
		}

		iLuxEntity *pEntity = apOrbit->mpMap->GetEntityByName(apOrbit->msName);
		apOrbit->mpScanMeshEntity = pEntity ? pEntity->GetMeshEntity() : NULL;
		apOrbit->mbScanMeshWasVisible = apOrbit->mpScanMeshEntity &&
			apOrbit->mpScanMeshEntity->IsVisible();
		apOrbit->mbScanVisibilityCaptured = true;
	}

	if(apOrbit->mpIconBillboard)
		apOrbit->mpIconBillboard->SetVisible(abTargetVisible && apOrbit->mbScanIconWasVisible);
	for(size_t i = 0; i < apOrbit->mvLabelBillboards.size(); ++i)
	{
		const bool bWasVisible = i < apOrbit->mvScanLabelWasVisible.size() &&
			apOrbit->mvScanLabelWasVisible[i];
		if(apOrbit->mvLabelBillboards[i])
			apOrbit->mvLabelBillboards[i]->SetVisible(abTargetVisible && bWasVisible);
	}
	if(apOrbit->mpScanMeshEntity)
		apOrbit->mpScanMeshEntity->SetVisible(abTargetVisible && apOrbit->mbScanMeshWasVisible);
}

void cLuxSatelliteHandler::RestoreOrbitScanVisibility(cLuxSatelliteOrbit *apOrbit)
{
	if(apOrbit == NULL || apOrbit->mbScanVisibilityCaptured == false)
		return;

	if(apOrbit->mpIconBillboard)
		apOrbit->mpIconBillboard->SetVisible(apOrbit->mbScanIconWasVisible);
	for(size_t i = 0; i < apOrbit->mvLabelBillboards.size(); ++i)
	{
		if(apOrbit->mvLabelBillboards[i] && i < apOrbit->mvScanLabelWasVisible.size())
			apOrbit->mvLabelBillboards[i]->SetVisible(apOrbit->mvScanLabelWasVisible[i]);
	}
	if(apOrbit->mpScanMeshEntity)
		apOrbit->mpScanMeshEntity->SetVisible(apOrbit->mbScanMeshWasVisible);

	apOrbit->mpScanMeshEntity = NULL;
	apOrbit->mvScanLabelWasVisible.clear();
	apOrbit->mbScanVisibilityCaptured = false;
	apOrbit->mbScanIconWasVisible = false;
	apOrbit->mbScanMeshWasVisible = false;
}

bool cLuxSatelliteHandler::BeginScanPresentation(cLuxMap *apMap, const tString& asTargetKey)
{
	if(mpScanPresentationState || apMap == NULL || apMap->GetWorld() == NULL ||
		mpMsuMrSimulation == NULL || mpMsuMrSimulation->IsActive() == false ||
		gpBase == NULL || gpBase->mpMapHandler == NULL ||
		gpBase->mpEngine == NULL || gpBase->mpEngine->GetGraphics() == NULL)
		return false;
	tLuxSatelliteOrbitMap::iterator itTarget = m_mapOrbits.find(asTargetKey);
	if(itTarget == m_mapOrbits.end() || itTarget->second == NULL ||
		itTarget->second->mpMap != apMap)
		return false;

	mpScanPresentationState = hplNew(cLuxSatelliteScanPresentationState, ());
	mpScanPresentationState->mpMap = apMap;
	mpScanPresentationState->msTargetKey = asTargetKey;
	if(mpMsuMrSimulation->HasSatellitePose() == false ||
		mpScanPresentationState->mTumbleProfile.Generate(
			GenerateMsuMrTumbleSeed(asTargetKey),
			mpMsuMrSimulation->GetSatellitePose()) == false)
	{
		EndScanPresentation(false);
		return false;
	}

	// Each strip retains a private render target. Ninety-eight 16x1 views and
	// one 4x1 tail are packed into this full-width atlas for one readback;
	// direct offset sub-viewports into a shared HPL2 deferred target are not
	// reliable.
	cGraphics *pGraphics = gpBase->mpEngine->GetGraphics();
	const cVector2l vSensorBatchSize(kMsuMrSensorBatchWidth, 1);
	mpScanPresentationState->mpSensorBatchTexture = pGraphics->CreateTexture(
		"MsuMrSensorBatchTarget", eTextureType_Rect,
		eTextureUsage_RenderTarget);
	if(mpScanPresentationState->mpSensorBatchTexture == NULL)
	{
		EndScanPresentation(false);
		return false;
	}
	mpScanPresentationState->mpSensorBatchTexture->SetWrapSTR(
		eTextureWrap_ClampToEdge);
	if(mpScanPresentationState->mpSensorBatchTexture->CreateFromRawData(
		cVector3l(vSensorBatchSize.x, vSensorBatchSize.y, 0),
		ePixelFormat_RGBA, NULL) == false)
	{
		EndScanPresentation(false);
		return false;
	}

	mpScanPresentationState->mpSensorBatchFrameBuffer =
		pGraphics->CreateFrameBuffer("MsuMrSensorBatch");
	if(mpScanPresentationState->mpSensorBatchFrameBuffer == NULL)
	{
		EndScanPresentation(false);
		return false;
	}
	mpScanPresentationState->mpSensorBatchFrameBuffer->SetTexture2D(
		0, mpScanPresentationState->mpSensorBatchTexture);
	mpScanPresentationState->mpSensorBatchDepthStencil =
		pGraphics->CreateDepthStencilBuffer(vSensorBatchSize, 24, 8, false);
	if(mpScanPresentationState->mpSensorBatchDepthStencil == NULL)
	{
		EndScanPresentation(false);
		return false;
	}
	mpScanPresentationState->mpSensorBatchFrameBuffer->SetDepthStencilBuffer(
		mpScanPresentationState->mpSensorBatchDepthStencil);
	if(mpScanPresentationState->mpSensorBatchFrameBuffer->CompileAndValidate() ==
		false)
	{
		EndScanPresentation(false);
		return false;
	}

	cLuxCameraViewDesc stripDesc;
	stripDesc.mvResolution = cVector2l(kMsuMrStripSampleCount, 1);
	stripDesc.mfFOV = static_cast<float>(
		cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees() * kDegreesToRadians);
	stripDesc.mfNearClipPlane = kMsuMrSensorNearClipMetres;
	stripDesc.mfFarClipPlane = kMsuMrSensorFarClipMetres;
	stripDesc.mpWorld = apMap->GetWorld();
	stripDesc.mRenderer = eRenderer_Main;
	stripDesc.mbActive = true;
	// The world renderer only needs to visit these viewports when a completed
	// line has queued the complete strip acquisition set.
	stripDesc.mbVisible = false;
	stripDesc.mbPushFront = true;
	bool bAnyTargetMipmaps =
		mpScanPresentationState->mpSensorBatchTexture->UsesMipMaps();
	for(int i = 0; i < kMsuMrStripCount; ++i)
	{
		const int lStripSampleCount = GetMsuMrStripSampleCount(i);
		stripDesc.mvResolution = cVector2l(lStripSampleCount, 1);
		cLuxMsuMrSensorSampleState& strip =
			mpScanPresentationState->mvSensorStrips[i];
		strip.mlSampleIndex = GetMsuMrStripFirstSample(i);
		strip.mpView = gpBase->mpMapHandler->CreateCameraView(stripDesc);
		if(strip.mpView == NULL || strip.mpView->GetViewport() == NULL)
		{
			EndScanPresentation(false);
			return false;
		}
		if(strip.mpView->GetRenderTexture() &&
			strip.mpView->GetRenderTexture()->UsesMipMaps())
		{
			bAnyTargetMipmaps = true;
		}
		strip.mpView->GetViewport()->SetPosition(cVector2l(0, 0));
		strip.mpView->GetViewport()->SetSize(
			cVector2l(lStripSampleCount, 1));

		strip.mpViewportCallback =
			hplNew(cLuxMsuMrSensorViewportCallback, (itTarget->second));
		strip.mpView->GetViewport()->AddViewportCallback(
			strip.mpViewportCallback);

		// Detector strips do not benefit from screen-space passes, reflections,
		// shadows, or occlusion queries.
		cRenderSettings *pRenderSettings =
			strip.mpView->GetViewport()->GetRenderSettings();
		if(pRenderSettings)
		{
			pRenderSettings->mbUseOcclusionCulling = false;
			pRenderSettings->mbUseEdgeSmooth = false;
			pRenderSettings->mbRenderWorldReflection = false;
			pRenderSettings->mbRenderShadows = false;
			pRenderSettings->mbSSAOActive = false;
		}
	}

	mpScanPresentationState->mpScanProduct = hplNew(cLuxMsuMrScanProduct, ());
	if(mpScanPresentationState->mpScanProduct->Initialize() == false)
	{
		EndScanPresentation(false);
		return false;
	}

	cLightListIterator itLight = apMap->GetWorld()->GetLightIterator();
	while(itLight.HasNext())
	{
		iLight *pLight = itLight.Next();
		if(pLight == NULL || pLight->GetLightType() != eLightType_Sun)
			continue;

		cLightSun *pSun = static_cast<cLightSun*>(pLight);
		cLuxSatelliteScanSunState state;
		state.mpSun = pSun;
		state.mbUsedSystemTime = pSun->GetUseSystemTime();
		state.mfFixedJulianDate = pSun->GetJulianDate();
		mpScanPresentationState->mvSuns.push_back(state);
		pSun->SetUseSystemTime(false);
	}

	for(tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin(); it != m_mapOrbits.end(); ++it)
	{
		if(it->second && it->second->mpMap == apMap)
			SetOrbitScanVisibility(it->second, it->first == asTargetKey);
	}

	UpdateScanPresentation();
	const double fPitchRadians =
		cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees() * kDegreesToRadians;
	const double fStripHorizontalFovDegrees = 2.0 * std::atan(
		kMsuMrStripSampleCount * std::tan(fPitchRadians * 0.5)) /
		kDegreesToRadians;
	const double fTailHorizontalFovDegrees = 2.0 * std::atan(
		kMsuMrTailStripSampleCount * std::tan(fPitchRadians * 0.5)) /
		kDegreesToRadians;
	Log("MSU-MR full-strip acquisition initialized: fullStrips=%dx(16x1 RGBA), "
		"tail=1x(4x1 RGBA) for rightmost samples 1568..1571, "
		"totalViews=%d, samples=0..1571, "
		"perStripPixelOrder=reversed, "
		"packedTarget=%dx1 RGBA, gpuCopiesPerLine=%d, "
		"renderBatches=50+49 across two frames, readbackCallsPerLine=1, "
		"renderer=mainDeferred, "
		"verticalFOV=%.8f deg, fullHorizontalFOV=%.8f deg, "
		"tailHorizontalFOV=%.8f deg, "
		"clip=%.0f..%.0f m, targetMipmaps=%s, stripMidpointSunTime=on, "
		"endpointGeometryValidation=on, void=engineDefaultBlack, "
		"spacecraftRenderables=excluded.\n",
		kMsuMrFullStripCount, kMsuMrStripCount,
		kMsuMrSensorBatchWidth, kMsuMrStripCount,
		cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees(),
		fStripHorizontalFovDegrees, fTailHorizontalFovDegrees,
		kMsuMrSensorNearClipMetres, kMsuMrSensorFarClipMetres,
		bAnyTargetMipmaps ? "on" : "off");
	Log("MSU-MR strip timing: fullSpan=%.9f ms, "
		"fullMaximumMidpointOffset=%.9f ms, tailSpan=%.9f ms, "
		"tailMaximumMidpointOffset=%.9f ms; complete 1572-sample lines "
		"are published atomically.\n",
		(kMsuMrStripSampleCount - 1) * 1000.0 /
			cLuxMsuMrSimulation::kCircularPositionsPerSecond,
		(kMsuMrStripSampleCount - 1) * 500.0 /
			cLuxMsuMrSimulation::kCircularPositionsPerSecond,
		(kMsuMrTailStripSampleCount - 1) * 1000.0 /
			cLuxMsuMrSimulation::kCircularPositionsPerSecond,
		(kMsuMrTailStripSampleCount - 1) * 500.0 /
			cLuxMsuMrSimulation::kCircularPositionsPerSecond);
	const cLuxMsuMrTumbleProfile& tumbleProfile =
		mpScanPresentationState->mTumbleProfile;
	const double fTumbleRate =
		std::fabs(tumbleProfile.GetSpinRateDegreesPerSecond());
	const double fEarthSweepSeconds =
		(cLuxMsuMrSimulation::kEarthViewSamplesPerLine - 1) /
		static_cast<double>(cLuxMsuMrSimulation::kCircularPositionsPerSecond);
	const double fLinePeriodSeconds =
		cLuxMsuMrSimulation::kCircularPositionsPerLine /
		static_cast<double>(cLuxMsuMrSimulation::kCircularPositionsPerSecond);
	Log("MSU-MR tumble profile assigned: seed=%llu, "
		"model=torqueFreeScanStartNadirAxis, spinRate=%+.4f deg/s, "
		"spinPeriod=%.2f s, initialPhase=%+.2f deg, "
		"EarthSweepMotion=%.3f deg/%.3f ms, lineStep=%.3f deg/%.3f ms; "
		"the scan-start nadir axis is fixed inertially, "
		"and attitude is sampled from the instrument clock.\n",
		static_cast<unsigned long long>(tumbleProfile.GetSeed()),
		tumbleProfile.GetSpinRateDegreesPerSecond(),
		tumbleProfile.GetSpinPeriodSeconds(),
		tumbleProfile.GetInitialPhaseDegrees(),
		fTumbleRate * fEarthSweepSeconds,
		fEarthSweepSeconds * 1000.0,
		fTumbleRate * fLinePeriodSeconds,
		fLinePeriodSeconds * 1000.0);
	Log("MSU-MR product mapping: HRPT acquires all 99 views; LRPT width 1568 "
		"omits the final 4x1 view containing rightmost HRPT samples 1568..1571.\n");
	Log("MSU-MR scan product initialized: lineBuffer=%ux1 RGBA, "
		"rollingHistory=%ux%u RGBA, unwrittenAlpha=0, "
		"presentation=separateSDLWindow, newestLine=row0, activeSuns=%u, "
		"maxActiveSunIntensity=%.3f.\n",
		cLuxMsuMrScanProduct::kWidth,
		cLuxMsuMrScanProduct::kWidth,
		mpScanPresentationState->mpScanProduct->GetHistoryLineCapacity(),
		mpScanPresentationState->mlActiveSunCount,
		mpScanPresentationState->mfMaxActiveSunIntensity);
	return true;
}

void cLuxSatelliteHandler::UpdateScanPresentation()
{
	if(mpScanPresentationState == NULL || mpMsuMrSimulation == NULL ||
		mpMsuMrSimulation->IsActive() == false)
		return;
	if(mpScanPresentationState->mpScanProduct &&
		mpScanPresentationState->mpScanProduct->UpdateWindow() == false)
	{
		Log("Stopping MSU-MR simulation because its product window was closed.\n");
		StopMsuMrScan();
		return;
	}

	double fJulianDayUtc = 0.0;
	double fJulianFractionUtc = 0.0;
	mpMsuMrSimulation->GetJulianDateUtc(fJulianDayUtc, fJulianFractionUtc);
	const double fJulianDateUtc = fJulianDayUtc + fJulianFractionUtc;
	mpScanPresentationState->mlActiveSunCount = 0;
	mpScanPresentationState->mfMaxActiveSunIntensity = 0.0;
	for(size_t i = 0; i < mpScanPresentationState->mvSuns.size(); ++i)
	{
		cLightSun *pSun = mpScanPresentationState->mvSuns[i].mpSun;
		if(pSun)
		{
			if(pSun->IsActive())
			{
				++mpScanPresentationState->mlActiveSunCount;
				mpScanPresentationState->mfMaxActiveSunIntensity = std::max(
					mpScanPresentationState->mfMaxActiveSunIntensity,
					static_cast<double>(pSun->GetIntensity()));
			}
			pSun->SetUseSystemTime(false);
			pSun->SetJulianDate(fJulianDateUtc);
		}
	}

	for(tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin(); it != m_mapOrbits.end(); ++it)
	{
		cLuxSatelliteOrbit *pOrbit = it->second;
		if(pOrbit == NULL || pOrbit->mpMap != mpScanPresentationState->mpMap)
			continue;

		const bool bIsTarget = it->first == mpScanPresentationState->msTargetKey;
		SetOrbitScanVisibility(pOrbit, bIsTarget);
		if(bIsTarget && mpMsuMrSimulation->HasSatellitePose())
		{
			const cLuxSatellitePose& nominalPose =
				mpMsuMrSimulation->GetSatellitePose();
			cLuxMsuMrInstrumentFrame instrumentFrame;
			if(mpScanPresentationState->mTumbleProfile.CalculateInstrumentFrame(
				mpMsuMrSimulation->GetElapsedSeconds(),
				instrumentFrame))
			{
				cLuxSatellitePose displayPose = nominalPose;
				displayPose.mvCrossTrack = instrumentFrame.mvCrossTrack;
				displayPose.mvAlongTrack = instrumentFrame.mvAlongTrack;
				displayPose.mvRadialOut = cVector3d(
					-instrumentFrame.mvNadir.x,
					-instrumentFrame.mvNadir.y,
					-instrumentFrame.mvNadir.z);
				ApplyOrbitPose(pOrbit, displayPose);
			}
		}
	}
}

void cLuxSatelliteHandler::EndScanPresentation(bool abResyncLiveOrbits)
{
	if(mpScanPresentationState == NULL)
		return;

	cLuxSatelliteScanPresentationState *pState = mpScanPresentationState;
	mpScanPresentationState = NULL;
	if(pState->mpScanProduct)
	{
		hplDelete(pState->mpScanProduct);
		pState->mpScanProduct = NULL;
	}
	for(int i = 0; i < kMsuMrStripCount; ++i)
	{
		cLuxMsuMrSensorSampleState& strip = pState->mvSensorStrips[i];
		if(strip.mpViewportCallback)
		{
			strip.mpViewportCallback->Restore();
			if(strip.mpView && strip.mpView->GetViewport())
			{
				strip.mpView->GetViewport()->RemoveViewportCallback(
					strip.mpViewportCallback);
			}
			hplDelete(strip.mpViewportCallback);
			strip.mpViewportCallback = NULL;
		}
		if(strip.mpView && gpBase && gpBase->mpMapHandler)
		{
			gpBase->mpMapHandler->DestroyCameraView(strip.mpView);
			strip.mpView = NULL;
		}
	}
	if(gpBase && gpBase->mpEngine && gpBase->mpEngine->GetGraphics())
	{
		cGraphics *pGraphics = gpBase->mpEngine->GetGraphics();
		if(pState->mpSensorBatchFrameBuffer)
		{
			pGraphics->DestroyFrameBuffer(pState->mpSensorBatchFrameBuffer);
			pState->mpSensorBatchFrameBuffer = NULL;
		}
		if(pState->mpSensorBatchDepthStencil)
		{
			pGraphics->DestoroyDepthStencilBuffer(
				pState->mpSensorBatchDepthStencil);
			pState->mpSensorBatchDepthStencil = NULL;
		}
		if(pState->mpSensorBatchTexture)
		{
			pGraphics->DestroyTexture(pState->mpSensorBatchTexture);
			pState->mpSensorBatchTexture = NULL;
		}
	}
	for(size_t i = 0; i < pState->mvSuns.size(); ++i)
	{
		cLuxSatelliteScanSunState& state = pState->mvSuns[i];
		if(state.mpSun)
		{
			state.mpSun->SetJulianDate(state.mfFixedJulianDate);
			state.mpSun->SetUseSystemTime(state.mbUsedSystemTime);
		}
	}

	if(abResyncLiveOrbits && pState->mpMap && pState->mpMap->GetWorld())
	{
		const double fLiveJulianDateUtc = GetSimulationJulianDateUtc(pState->mpMap);
		for(tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin(); it != m_mapOrbits.end(); ++it)
		{
			if(it->second && it->second->mpMap == pState->mpMap)
				UpdateOrbit(it->second, fLiveJulianDateUtc);
		}
	}

	for(tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin(); it != m_mapOrbits.end(); ++it)
	{
		if(it->second && it->second->mpMap == pState->mpMap)
			RestoreOrbitScanVisibility(it->second);
	}

	hplDelete(pState);
}

void cLuxSatelliteHandler::LogMsuMrSampleDiagnostics(cLuxSatelliteOrbit *apOrbit)
{
	if(mpMsuMrSimulation == NULL || apOrbit == NULL ||
		mpScanPresentationState == NULL ||
		mpScanPresentationState->mTumbleProfile.IsInitialized() == false)
		return;
	const cLuxMsuMrTumbleProfile& tumbleProfile =
		mpScanPresentationState->mTumbleProfile;

	const tLuxMsuMrEarthSampleEventVec& vEvents =
		mpMsuMrSimulation->GetLastEarthSampleEvents();
	if(vEvents.empty())
		return;

	const cLuxMsuMrEarthSampleEvent& lastEvent = vEvents.back();
	if(lastEvent.mlEarthSampleIndex != cLuxMsuMrSimulation::kEarthViewSamplesPerLine - 1)
		return;

	const cLuxMsuMrEarthSampleEvent& firstEvent = vEvents.front();
	Log("MSU-MR HRPT line %llu complete: face=%u, updatePhase=%u, updateEarth=%u, "
		"HRPT=%u..%u, circular=%u..%u, totalEarth=%llu, nextPhase=%u, "
		"UTC(first)=%.0f+%.15f, UTC(last)=%.0f+%.15f.\n",
		static_cast<unsigned long long>(lastEvent.mlLineIndex),
		lastEvent.mlMirrorFaceIndex,
		mpMsuMrSimulation->GetLastPhasePositionsAdvanced(),
		static_cast<unsigned int>(vEvents.size()),
		firstEvent.mlEarthSampleIndex, lastEvent.mlEarthSampleIndex,
		firstEvent.mlCircularPhase, lastEvent.mlCircularPhase,
		static_cast<unsigned long long>(mpMsuMrSimulation->GetTotalEarthSampleCount()),
		mpMsuMrSimulation->GetCircularPhasePosition(),
		firstEvent.mfJulianDayUtc, firstEvent.mfJulianFractionUtc,
		lastEvent.mfJulianDayUtc, lastEvent.mfJulianFractionUtc);

	cLuxMsuMrEarthSampleEvent
		vRepresentativeEvents[kMsuMrRepresentativeSampleCount];
	cLuxSatellitePose vRepresentativePoses[kMsuMrRepresentativeSampleCount];
	cLuxMsuMrGroundSample vGroundSamples[kMsuMrRepresentativeSampleCount];
	bool vRepresentativeGroundHits[kMsuMrRepresentativeSampleCount] = {};
	double vRepresentativeSolarElevationDegrees[kMsuMrRepresentativeSampleCount];
	for(int i = 0; i < kMsuMrRepresentativeSampleCount; ++i)
	{
		vRepresentativeSolarElevationDegrees[i] =
			std::numeric_limits<double>::quiet_NaN();
	}
	bool bIlluminationDiagnosticsValid = true;
	unsigned int lRepresentativeGroundHitCount = 0;
	for(int i = 0; i < kMsuMrRepresentativeSampleCount; ++i)
	{
		if(mpMsuMrSimulation->GetEarthSampleEvent(lastEvent.mlLineIndex,
			kMsuMrRepresentativeSampleIndices[i],
			vRepresentativeEvents[i]) == false ||
			GetOrbitPose(apOrbit, vRepresentativeEvents[i].mfJulianDayUtc,
				vRepresentativeEvents[i].mfJulianFractionUtc,
				vRepresentativePoses[i]) == false ||
			CalculateMsuMrTumbledGroundSample(tumbleProfile,
				vRepresentativePoses[i],
				GetMsuMrElapsedSeconds(
					vRepresentativeEvents[i].mlCircularPositionIndex),
				kMsuMrRepresentativeSampleIndices[i],
				vGroundSamples[i], vRepresentativeGroundHits[i]) == false)
		{
			Warning("Could not calculate MSU-MR WGS-84 geometry for line %llu, HRPT sample %u.\n",
				static_cast<unsigned long long>(lastEvent.mlLineIndex),
				kMsuMrRepresentativeSampleIndices[i]);
			return;
		}
		if(vRepresentativeGroundHits[i])
			++lRepresentativeGroundHitCount;
		if(vRepresentativeGroundHits[i] &&
			CalculateMsuMrGroundSolarElevationDegrees(vGroundSamples[i],
			vRepresentativeEvents[i].mfJulianDayUtc,
			vRepresentativeEvents[i].mfJulianFractionUtc,
			vRepresentativeSolarElevationDegrees[i]) == false)
		{
			bIlluminationDiagnosticsValid = false;
		}
	}
	const bool bPointSunEarthOccluded = IsMsuMrPointSunEarthOccluded(
		vRepresentativePoses[2],
		vRepresentativeEvents[2].mfJulianDayUtc,
		vRepresentativeEvents[2].mfJulianFractionUtc);

	double fSwathMetres = 0.0;
	double fCentreSampleSpacingMetres = 0.0;
	const bool bHasSwathDistance = vRepresentativeGroundHits[0] &&
		vRepresentativeGroundHits[5] &&
		cLuxMsuMrGeometry::CalculateGeodesicDistanceMetres(
		vGroundSamples[0].mfLatitudeDegrees, vGroundSamples[0].mfLongitudeDegrees,
		vGroundSamples[5].mfLatitudeDegrees, vGroundSamples[5].mfLongitudeDegrees,
		fSwathMetres);
	const bool bHasCentreSpacing = vRepresentativeGroundHits[2] &&
		vRepresentativeGroundHits[3] &&
		cLuxMsuMrGeometry::CalculateGeodesicDistanceMetres(
		vGroundSamples[2].mfLatitudeDegrees, vGroundSamples[2].mfLongitudeDegrees,
		vGroundSamples[3].mfLatitudeDegrees, vGroundSamples[3].mfLongitudeDegrees,
		fCentreSampleSpacingMetres);
	if(bHasSwathDistance == false)
		fSwathMetres = std::numeric_limits<double>::quiet_NaN();
	if(bHasCentreSpacing == false)
		fCentreSampleSpacingMetres = std::numeric_limits<double>::quiet_NaN();

	Log("MSU-MR WGS84 line %llu face=%u: "
		"s0[a=%.8f lat=%.7f lon=%.7f range=%.3fkm], "
		"s785[a=%.8f lat=%.7f lon=%.7f range=%.3fkm], "
		"s786[a=%.8f lat=%.7f lon=%.7f range=%.3fkm], "
		"s1571[a=%.8f lat=%.7f lon=%.7f range=%.3fkm], "
		"edgeDistance=%.3fkm, centreSpacing=%.3fkm, earthHits=%u/%u.\n",
		static_cast<unsigned long long>(lastEvent.mlLineIndex),
		lastEvent.mlMirrorFaceIndex,
		vGroundSamples[0].mfLookAngleDegrees,
		vGroundSamples[0].mfLatitudeDegrees,
		vGroundSamples[0].mfLongitudeDegrees,
		vGroundSamples[0].mfSlantRangeMetres / 1000.0,
		vGroundSamples[2].mfLookAngleDegrees,
		vGroundSamples[2].mfLatitudeDegrees,
		vGroundSamples[2].mfLongitudeDegrees,
		vGroundSamples[2].mfSlantRangeMetres / 1000.0,
		vGroundSamples[3].mfLookAngleDegrees,
		vGroundSamples[3].mfLatitudeDegrees,
		vGroundSamples[3].mfLongitudeDegrees,
		vGroundSamples[3].mfSlantRangeMetres / 1000.0,
		vGroundSamples[5].mfLookAngleDegrees,
		vGroundSamples[5].mfLatitudeDegrees,
		vGroundSamples[5].mfLongitudeDegrees,
		vGroundSamples[5].mfSlantRangeMetres / 1000.0,
		fSwathMetres / 1000.0, fCentreSampleSpacingMetres / 1000.0,
		lRepresentativeGroundHitCount, kMsuMrRepresentativeSampleCount);

	// Prepare 98 full strips and the four-sample HRPT tail at their temporal and
	// angular midpoints. Endpoint checks bound the projection and motion
	// approximation at both swath edges as well as the centre before any view
	// becomes visible.
	if(mpScanPresentationState)
	{
		if(mpScanPresentationState->mbSensorLinePending)
		{
			++mpScanPresentationState->mlOverwrittenSensorLines;
			Log("MSU-MR product line %llu skipped while two-frame acquisition "
				"of line %llu is still active; simulation timing continues.\n",
				static_cast<unsigned long long>(lastEvent.mlLineIndex),
				static_cast<unsigned long long>(
					mpScanPresentationState->mlSensorLineIndex));
			return;
		}
		const std::chrono::steady_clock::time_point preparationStart =
			std::chrono::steady_clock::now();
		const double fPitchRadians =
			cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees() *
			kDegreesToRadians;
		const double fCentreSampleIndex =
			(cLuxMsuMrSimulation::kEarthViewSamplesPerLine - 1) * 0.5;
		double fMaxProjectionRayErrorArcseconds = 0.0;
		double fMaxGroundDisplacementMetres = 0.0;
		double fMaxTimeOffsetMilliseconds = 0.0;
		double fGroundSolarElevationMinDegrees = 90.0;
		double fGroundSolarElevationMaxDegrees = -90.0;
		unsigned int lDaylightEndpointCount = 0;
		unsigned int lIlluminationEndpointCount = 0;
		bool bPreparationSucceeded = true;
		int lFailedStrip = -1;
		for(int stripIndex = 0; stripIndex < kMsuMrStripCount; ++stripIndex)
		{
			const int lStripSampleCount =
				GetMsuMrStripSampleCount(stripIndex);
			const std::uint32_t lFirstSample =
				GetMsuMrStripFirstSample(stripIndex);
			const std::uint32_t lLowerCentreSample =
				lFirstSample + lStripSampleCount / 2 - 1;
			const std::uint32_t lUpperCentreSample = lLowerCentreSample + 1;
			cLuxMsuMrEarthSampleEvent lowerCentreEvent;
			cLuxMsuMrEarthSampleEvent upperCentreEvent;
			if(mpMsuMrSimulation->GetEarthSampleEvent(lastEvent.mlLineIndex,
				lLowerCentreSample, lowerCentreEvent) == false ||
				mpMsuMrSimulation->GetEarthSampleEvent(lastEvent.mlLineIndex,
					lUpperCentreSample, upperCentreEvent) == false)
			{
				bPreparationSucceeded = false;
				lFailedStrip = stripIndex;
				break;
			}

			cLuxMsuMrEarthSampleEvent stripEvent = lowerCentreEvent;
			stripEvent.mlEarthSampleIndex = lFirstSample;
			const double fCentreEventDeltaDays =
				(upperCentreEvent.mfJulianDayUtc -
				 lowerCentreEvent.mfJulianDayUtc) +
				(upperCentreEvent.mfJulianFractionUtc -
				 lowerCentreEvent.mfJulianFractionUtc);
			stripEvent.mfJulianFractionUtc += fCentreEventDeltaDays * 0.5;

			cLuxSatellitePose stripPose;
			if(GetOrbitPose(apOrbit, stripEvent.mfJulianDayUtc,
				stripEvent.mfJulianFractionUtc, stripPose) == false)
			{
				bPreparationSucceeded = false;
				lFailedStrip = stripIndex;
				break;
			}
			const double fStripCentreSample =
				lFirstSample + (lStripSampleCount - 1) * 0.5;
			const double fStripLookAngleDegrees =
				(fStripCentreSample - fCentreSampleIndex) *
				cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees();
			const double fStripLookAngleRadians =
				fStripLookAngleDegrees * kDegreesToRadians;
			const double fStripElapsedSeconds =
				(static_cast<double>(lowerCentreEvent.mlCircularPositionIndex) +
				 0.5) /
				cLuxMsuMrSimulation::kCircularPositionsPerSecond;
			cLuxMsuMrInstrumentFrame stripInstrumentFrame;
			if(tumbleProfile.CalculateInstrumentFrame(
				fStripElapsedSeconds, stripInstrumentFrame) == false)
			{
				bPreparationSucceeded = false;
				lFailedStrip = stripIndex;
				break;
			}
			const cVector3d vStripForward = Normalize(cVector3d(
				stripInstrumentFrame.mvNadir.x * std::cos(fStripLookAngleRadians) +
					stripInstrumentFrame.mvCrossTrack.x * std::sin(fStripLookAngleRadians),
				stripInstrumentFrame.mvNadir.y * std::cos(fStripLookAngleRadians) +
					stripInstrumentFrame.mvCrossTrack.y * std::sin(fStripLookAngleRadians),
				stripInstrumentFrame.mvNadir.z * std::cos(fStripLookAngleRadians) +
					stripInstrumentFrame.mvCrossTrack.z * std::sin(fStripLookAngleRadians)));
			cLuxMsuMrGroundSample stripCentreGround;
			const bool bStripCentreGroundHit =
				cLuxMsuMrGeometry::CalculateGroundIntersection(
				stripPose.mvPositionMetres, vStripForward,
				stripCentreGround);
			if(bStripCentreGroundHit == false)
			{
				// Space is a valid detector result during tumble. Preserve the ray
				// so the view renders black rather than discarding the whole line.
				stripCentreGround = cLuxMsuMrGroundSample();
				stripCentreGround.mvRayDirection = vStripForward;
				stripCentreGround.mfSlantRangeMetres =
					std::numeric_limits<double>::quiet_NaN();
			}
			stripCentreGround.mlEarthSampleIndex = lFirstSample;
			stripCentreGround.mfLookAngleDegrees = fStripLookAngleDegrees;

			const cVector3d vStripRight = Normalize(Cross(
				vStripForward, stripInstrumentFrame.mvAlongTrack));
			for(int endpoint = 0; endpoint < 2; ++endpoint)
			{
				const int lSampleOffset = endpoint == 0 ?
					0 : lStripSampleCount - 1;
				const std::uint32_t lSampleIndex = lFirstSample + lSampleOffset;
				cLuxMsuMrEarthSampleEvent exactEvent;
				cLuxSatellitePose exactPose;
				cLuxMsuMrGroundSample exactGround;
				cLuxMsuMrGroundSample idealMidpointGround;
				cLuxMsuMrGroundSample projectedStripGround;
				bool bExactGroundHit = false;
				bool bIdealMidpointGroundHit = false;
				const int lStripPixel =
					lStripSampleCount - 1 - lSampleOffset;
				const double fHorizontalTangent =
					(2.0 * lStripPixel + 1.0 - lStripSampleCount) *
					std::tan(fPitchRadians * 0.5);
				const cVector3d vStripRay = Normalize(cVector3d(
					vStripForward.x + vStripRight.x * fHorizontalTangent,
					vStripForward.y + vStripRight.y * fHorizontalTangent,
					vStripForward.z + vStripRight.z * fHorizontalTangent));
				if(mpMsuMrSimulation->GetEarthSampleEvent(
						lastEvent.mlLineIndex, lSampleIndex, exactEvent) == false ||
					GetOrbitPose(apOrbit, exactEvent.mfJulianDayUtc,
						exactEvent.mfJulianFractionUtc, exactPose) == false ||
					CalculateMsuMrTumbledGroundSample(tumbleProfile,
						exactPose,
						GetMsuMrElapsedSeconds(
							exactEvent.mlCircularPositionIndex),
						lSampleIndex, exactGround, bExactGroundHit) == false ||
					CalculateMsuMrGroundSampleForFrame(stripPose,
						stripInstrumentFrame, lSampleIndex,
						idealMidpointGround, bIdealMidpointGroundHit) == false)
				{
					bPreparationSucceeded = false;
					lFailedStrip = stripIndex;
					break;
				}
				const bool bProjectedStripGroundHit =
					cLuxMsuMrGeometry::CalculateGroundIntersection(
						stripPose.mvPositionMetres, vStripRay,
						projectedStripGround);

				const cVector3d vRayCross = Cross(
					vStripRay, idealMidpointGround.mvRayDirection);
				const double fProjectionRayErrorArcseconds = std::atan2(
					std::sqrt(Dot(vRayCross, vRayCross)),
					Dot(vStripRay, idealMidpointGround.mvRayDirection)) /
					kDegreesToRadians * 3600.0;
				const double fTimeOffsetMilliseconds = std::fabs(
					((exactEvent.mfJulianDayUtc - stripEvent.mfJulianDayUtc) +
					 (exactEvent.mfJulianFractionUtc -
					  stripEvent.mfJulianFractionUtc)) *
					kSecondsPerDay * 1000.0);
				fMaxProjectionRayErrorArcseconds = std::max(
					fMaxProjectionRayErrorArcseconds,
					fProjectionRayErrorArcseconds);
				if(bExactGroundHit && bProjectedStripGroundHit)
				{
					double fGroundDisplacementMetres = 0.0;
					if(cLuxMsuMrGeometry::CalculateGeodesicDistanceMetres(
						projectedStripGround.mfLatitudeDegrees,
						projectedStripGround.mfLongitudeDegrees,
						exactGround.mfLatitudeDegrees,
						exactGround.mfLongitudeDegrees,
						fGroundDisplacementMetres) == false)
					{
						bPreparationSucceeded = false;
						lFailedStrip = stripIndex;
						break;
					}
					fMaxGroundDisplacementMetres = std::max(
						fMaxGroundDisplacementMetres,
						fGroundDisplacementMetres);
				}
				fMaxTimeOffsetMilliseconds = std::max(
					fMaxTimeOffsetMilliseconds,
					fTimeOffsetMilliseconds);
				double fSolarElevationDegrees = 0.0;
				if(bExactGroundHit &&
					CalculateMsuMrGroundSolarElevationDegrees(exactGround,
					exactEvent.mfJulianDayUtc,
					exactEvent.mfJulianFractionUtc,
					fSolarElevationDegrees))
				{
					fGroundSolarElevationMinDegrees = std::min(
						fGroundSolarElevationMinDegrees,
						fSolarElevationDegrees);
					fGroundSolarElevationMaxDegrees = std::max(
						fGroundSolarElevationMaxDegrees,
						fSolarElevationDegrees);
					++lIlluminationEndpointCount;
					if(fSolarElevationDegrees >= 0.0)
						++lDaylightEndpointCount;
				}
				else if(bExactGroundHit)
				{
					bIlluminationDiagnosticsValid = false;
				}
			}
			if(bPreparationSucceeded == false)
				break;

			cLuxMsuMrSensorSampleState& strip =
				mpScanPresentationState->mvSensorStrips[stripIndex];
			if(AimMsuMrSensorSample(strip, stripEvent, stripPose,
				stripInstrumentFrame, stripCentreGround) == false)
			{
				bPreparationSucceeded = false;
				lFailedStrip = stripIndex;
				break;
			}
			strip.mlSampleIndex = lFirstSample;
		}

		if(bPreparationSucceeded == false)
		{
			Warning("Could not prepare complete MSU-MR strip line %llu at strip %d "
				"(first HRPT sample %d).\n",
				static_cast<unsigned long long>(lastEvent.mlLineIndex),
				lFailedStrip,
				lFailedStrip >= 0 ?
					GetMsuMrStripFirstSample(lFailedStrip) : -1);
			ResetMsuMrPendingSensorLine(mpScanPresentationState);
			return;
		}

		mpScanPresentationState->mlSensorLineIndex = lastEvent.mlLineIndex;
		mpScanPresentationState->mlSensorMirrorFaceIndex =
			lastEvent.mlMirrorFaceIndex;
		mpScanPresentationState->mfStripMaxProjectionRayErrorArcseconds =
			fMaxProjectionRayErrorArcseconds;
		mpScanPresentationState->mfStripMaxGroundDisplacementMetres =
			fMaxGroundDisplacementMetres;
		mpScanPresentationState->mfStripMaxTimeOffsetMilliseconds =
			fMaxTimeOffsetMilliseconds;
		mpScanPresentationState->mbIlluminationDiagnosticsValid =
			bIlluminationDiagnosticsValid &&
			lIlluminationEndpointCount > 0;
		mpScanPresentationState->mbPointSunEarthOccluded =
			bPointSunEarthOccluded;
		mpScanPresentationState->mfGroundSolarElevationMinDegrees =
			fGroundSolarElevationMinDegrees;
		mpScanPresentationState->mfGroundSolarElevationMaxDegrees =
			fGroundSolarElevationMaxDegrees;
		mpScanPresentationState->mlDaylightEndpointCount =
			lDaylightEndpointCount;
		mpScanPresentationState->mlIlluminationEndpointCount =
			lIlluminationEndpointCount;
		for(int i = 0; i < kMsuMrRepresentativeSampleCount; ++i)
		{
			mpScanPresentationState->mvRepresentativeSolarElevationDegrees[i] =
				vRepresentativeSolarElevationDegrees[i];
		}
		mpScanPresentationState->mfPreparationWallMilliseconds =
			std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - preparationStart).count();
		mpScanPresentationState->mfCurrentLineRenderWallMilliseconds = 0.0;
		mpScanPresentationState->mfCurrentLineMaxViewRenderWallMilliseconds = 0.0;
		mpScanPresentationState->mfFirstBatchPackSubmitWallMilliseconds = 0.0;
		mpScanPresentationState->mLineAcquisitionStart = preparationStart;
		mpScanPresentationState->mlCurrentStripBatchStart = 0;
		mpScanPresentationState->mlCurrentStripBatchCount =
			kMsuMrFirstRenderBatchStripCount;
		mpScanPresentationState->mbSensorLinePending = true;
		for(int i = 0; i < kMsuMrFirstRenderBatchStripCount; ++i)
			mpScanPresentationState->mvSensorStrips[i].mpView->SetVisible(true);
	}
}

void cLuxSatelliteHandler::OnPostRender()
{
	if(mpScanPresentationState == NULL ||
		mpScanPresentationState->mbSensorLinePending == false)
		return;
	if(gpBase == NULL || gpBase->mpEngine == NULL ||
		gpBase->mpEngine->GetGraphics() == NULL)
	{
		Warning("Discarded pending MSU-MR line %llu because the graphics system is unavailable.\n",
			static_cast<unsigned long long>(
				mpScanPresentationState->mlSensorLineIndex));
		ResetMsuMrPendingSensorLine(mpScanPresentationState);
		return;
	}

	iLowLevelGraphics *pLowLevelGraphics =
		gpBase->mpEngine->GetGraphics()->GetLowLevel();
	if(pLowLevelGraphics == NULL)
	{
		Warning("Discarded pending MSU-MR line %llu because low-level graphics are unavailable.\n",
			static_cast<unsigned long long>(
				mpScanPresentationState->mlSensorLineIndex));
		ResetMsuMrPendingSensorLine(mpScanPresentationState);
		return;
	}

	cBitmap *pSensorBatchBitmap = NULL;
	unsigned char vLinePixels[kMsuMrSensorBatchWidth][4] = {};
	bool bReadSucceeded = true;
	int lFailedStrip = -1;
	const int lBatchStart =
		mpScanPresentationState->mlCurrentStripBatchStart;
	const int lBatchCount =
		mpScanPresentationState->mlCurrentStripBatchCount;
	const int lBatchEnd = lBatchStart + lBatchCount;
	const bool bFinalBatch = lBatchEnd == kMsuMrStripCount;
	if(lBatchStart < 0 || lBatchCount <= 0 ||
		lBatchEnd > kMsuMrStripCount)
	{
		Warning("Discarded MSU-MR line %llu after invalid strip batch %d+%d.\n",
			static_cast<unsigned long long>(
				mpScanPresentationState->mlSensorLineIndex),
			lBatchStart, lBatchCount);
		ResetMsuMrPendingSensorLine(mpScanPresentationState);
		return;
	}

	double fBatchRenderWallMilliseconds = 0.0;
	double fBatchMaxViewRenderWallMilliseconds = 0.0;
	for(int i = lBatchStart; i < lBatchEnd; ++i)
	{
		cLuxMsuMrSensorViewportCallback *pCallback =
			mpScanPresentationState->mvSensorStrips[i].mpViewportCallback;
		if(pCallback == NULL || pCallback->HasRenderTiming() == false)
		{
			bReadSucceeded = false;
			lFailedStrip = i;
			break;
		}
		const double fViewRenderMilliseconds =
			pCallback->GetLastWorldDrawMilliseconds();
		fBatchRenderWallMilliseconds += fViewRenderMilliseconds;
		fBatchMaxViewRenderWallMilliseconds = std::max(
			fBatchMaxViewRenderWallMilliseconds, fViewRenderMilliseconds);
	}
	const std::chrono::steady_clock::time_point packReadbackStart =
		std::chrono::steady_clock::now();
	iFrameBuffer *pPreviousFrameBuffer =
		pLowLevelGraphics->GetCurrentFrameBuffer();
	for(int i = lBatchStart; bReadSucceeded && i < lBatchEnd; ++i)
	{
		const int lStripSampleCount = GetMsuMrStripSampleCount(i);
		cLuxMsuMrSensorSampleState& strip =
			mpScanPresentationState->mvSensorStrips[i];
		if(strip.mpView == NULL || strip.mpView->GetViewport() == NULL ||
			strip.mpView->GetViewport()->IsVisible() == false ||
			strip.mpView->GetViewport()->GetFrameBuffer() !=
				strip.mpView->GetFrameBuffer() ||
			strip.mpView->GetViewport()->GetPosition() != cVector2l(0, 0) ||
			strip.mpView->GetViewport()->GetSize() !=
				cVector2l(lStripSampleCount, 1))
		{
			bReadSucceeded = false;
			lFailedStrip = i;
			break;
		}
	}
	if(bReadSucceeded &&
		(mpScanPresentationState->mpSensorBatchTexture == NULL ||
		 mpScanPresentationState->mpSensorBatchFrameBuffer == NULL))
	{
		bReadSucceeded = false;
		lFailedStrip = 0;
	}
	// Pack all private strip targets on the GPU, then synchronize only once for
	// the complete line. HPL's nominal roll reverses screen X relative to HRPT
	// sample order; that reversal is corrected after the atlas readback.
	for(int i = lBatchStart; bReadSucceeded && i < lBatchEnd; ++i)
	{
		const cVector2l vStripSize(GetMsuMrStripSampleCount(i), 1);
		cLuxMsuMrSensorSampleState& strip =
			mpScanPresentationState->mvSensorStrips[i];
		pLowLevelGraphics->SetCurrentFrameBuffer(
			strip.mpView->GetFrameBuffer(), cVector2l(0, 0), vStripSize);
		pLowLevelGraphics->CopyFrameBufferToTexure(
			mpScanPresentationState->mpSensorBatchTexture,
			cVector2l(0, 0), vStripSize,
			cVector2l(GetMsuMrStripFirstSample(i), 0));
	}
	if(bReadSucceeded && bFinalBatch &&
		mpScanPresentationState->mpSensorBatchFrameBuffer != NULL)
	{
		const cVector2l vSensorBatchSize(kMsuMrSensorBatchWidth, 1);
		pLowLevelGraphics->SetCurrentFrameBuffer(
			mpScanPresentationState->mpSensorBatchFrameBuffer,
			cVector2l(0, 0), vSensorBatchSize);
		pSensorBatchBitmap = pLowLevelGraphics->CopyFrameBufferToBitmap(
			cVector2l(0, 0), vSensorBatchSize);
		cBitmapData *pBitmapData = pSensorBatchBitmap ?
			pSensorBatchBitmap->GetData(0, 0) : NULL;
		if(pSensorBatchBitmap == NULL ||
			pSensorBatchBitmap->GetWidth() != vSensorBatchSize.x ||
			pSensorBatchBitmap->GetHeight() != vSensorBatchSize.y ||
			pSensorBatchBitmap->GetBytesPerPixel() < 4 ||
			pBitmapData == NULL || pBitmapData->mpData == NULL ||
			pBitmapData->mlSize < kMsuMrSensorBatchWidth * 4)
		{
			bReadSucceeded = false;
			lFailedStrip = 0;
		}
		else
		{
			const int lBytesPerPixel =
				pSensorBatchBitmap->GetBytesPerPixel();
			for(int stripIndex = 0;
				stripIndex < kMsuMrStripCount; ++stripIndex)
			{
				const int lStripSampleCount =
					GetMsuMrStripSampleCount(stripIndex);
				const int lFirstSample =
					GetMsuMrStripFirstSample(stripIndex);
				for(int sampleOffset = 0;
					sampleOffset < lStripSampleCount; ++sampleOffset)
				{
					const int lSourcePixel = lFirstSample +
						(lStripSampleCount - 1 - sampleOffset);
					const unsigned char *pSource = pBitmapData->mpData +
						lSourcePixel * lBytesPerPixel;
					std::memcpy(vLinePixels[lFirstSample + sampleOffset],
						pSource, 4);
				}
			}
		}
	}
	else if(bReadSucceeded && bFinalBatch)
	{
		bReadSucceeded = false;
		lFailedStrip = 0;
	}
	pLowLevelGraphics->SetCurrentFrameBuffer(pPreviousFrameBuffer);

	if(pSensorBatchBitmap) hplDelete(pSensorBatchBitmap);
	for(int i = lBatchStart; i < lBatchEnd; ++i)
	{
		if(mpScanPresentationState->mvSensorStrips[i].mpView)
			mpScanPresentationState->mvSensorStrips[i].mpView->SetVisible(false);
	}
	const double fBatchPackWallMilliseconds =
		std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - packReadbackStart).count();
	if(bReadSucceeded == false)
	{
		Warning("Discarded incomplete MSU-MR full-strip line %llu after strip "
			"%d (first HRPT sample %d) could not be rendered, packed, or read.\n",
			static_cast<unsigned long long>(mpScanPresentationState->mlSensorLineIndex),
			lFailedStrip,
			lFailedStrip >= 0 ?
				GetMsuMrStripFirstSample(lFailedStrip) : -1);
		ResetMsuMrPendingSensorLine(mpScanPresentationState);
		return;
	}

	mpScanPresentationState->mfCurrentLineRenderWallMilliseconds +=
		fBatchRenderWallMilliseconds;
	mpScanPresentationState->mfCurrentLineMaxViewRenderWallMilliseconds = std::max(
		mpScanPresentationState->mfCurrentLineMaxViewRenderWallMilliseconds,
		fBatchMaxViewRenderWallMilliseconds);
	if(bFinalBatch == false)
	{
		mpScanPresentationState->mfFirstBatchPackSubmitWallMilliseconds =
			fBatchPackWallMilliseconds;
		mpScanPresentationState->mlCurrentStripBatchStart = lBatchEnd;
		mpScanPresentationState->mlCurrentStripBatchCount =
			kMsuMrStripCount - lBatchEnd;
		for(int i = lBatchEnd; i < kMsuMrStripCount; ++i)
			mpScanPresentationState->mvSensorStrips[i].mpView->SetVisible(true);
		Log("MSU-MR line %llu render batch 1/2 packed: strips=%d..%d, "
			"samples=0..799, renderCpuWall=%.3f ms, "
			"packSubmitCpuWall=%.3f ms, nextBatchStrips=%d..%d, "
			"fps=%.1f, avgFrame=%.3f ms.\n",
			static_cast<unsigned long long>(
				mpScanPresentationState->mlSensorLineIndex),
			lBatchStart, lBatchEnd - 1,
			fBatchRenderWallMilliseconds, fBatchPackWallMilliseconds,
			lBatchEnd, kMsuMrStripCount - 1,
			gpBase->mpEngine->GetFPS(),
			gpBase->mpEngine->GetAvgFrameTimeInMS());
		return;
	}

	const double fRenderWallMilliseconds =
		mpScanPresentationState->mfCurrentLineRenderWallMilliseconds;
	const double fMaxViewRenderWallMilliseconds =
		mpScanPresentationState->mfCurrentLineMaxViewRenderWallMilliseconds;
	const double fPackReadbackWallMilliseconds = fBatchPackWallMilliseconds;
	const double fLineLatencyWallMilliseconds =
		std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() -
			mpScanPresentationState->mLineAcquisitionStart).count();

	double fMaxAimErrorArcseconds = 0.0;
	double fMaxOriginErrorMetres = 0.0;
	for(int i = 0; i < kMsuMrStripCount; ++i)
	{
		const cLuxMsuMrSensorSampleState& strip =
			mpScanPresentationState->mvSensorStrips[i];
		fMaxAimErrorArcseconds = std::max(fMaxAimErrorArcseconds,
			strip.mfAimErrorArcseconds);
		fMaxOriginErrorMetres = std::max(fMaxOriginErrorMetres,
			strip.mfOriginQuantizationMetres);
	}
	std::uint32_t lLineRgbaHash = 2166136261u;
	unsigned int lNonOpaquePixels = 0;
	unsigned int lBlackRgbPixels = 0;
	for(int i = 0; i < kMsuMrSensorBatchWidth; ++i)
	{
		for(int channel = 0; channel < 4; ++channel)
		{
			lLineRgbaHash ^= vLinePixels[i][channel];
			lLineRgbaHash *= 16777619u;
		}
		if(vLinePixels[i][3] != 255) ++lNonOpaquePixels;
		if(vLinePixels[i][0] == 0 && vLinePixels[i][1] == 0 &&
			vLinePixels[i][2] == 0) ++lBlackRgbPixels;
	}

	Log("MSU-MR full-strip line %llu face=%u: "
		"s0=(%u,%u,%u,%u), s393=(%u,%u,%u,%u), "
		"s785=(%u,%u,%u,%u), s786=(%u,%u,%u,%u), "
		"s1178=(%u,%u,%u,%u), s1571=(%u,%u,%u,%u), "
		"lineHash=%08x, blackRGB=%u/1572, nonOpaque=%u, "
		"maxProjectionRayError=%.6f arcsec, "
		"maxGroundDisplacement=%.3f m, maxTimeOffset=%.6f ms, "
		"preparationCpuWall=%.3f ms, stripAimError=%.3f arcsec, "
		"stripOriginFloatError=%.3f m, renderCpuWall=%.3f ms "
		"(maxView=%.3f ms), firstBatchPackSubmitWall=%.3f ms, "
		"finalBatchPackReadbackWall=%.3f ms, "
		"twoFrameLatencyWall=%.3f ms, fps=%.1f, avgFrame=%.3f ms, "
		"overwrittenLinesBeforeRead=%llu.\n",
		static_cast<unsigned long long>(mpScanPresentationState->mlSensorLineIndex),
		mpScanPresentationState->mlSensorMirrorFaceIndex,
		static_cast<unsigned int>(vLinePixels[0][0]),
		static_cast<unsigned int>(vLinePixels[0][1]),
		static_cast<unsigned int>(vLinePixels[0][2]),
		static_cast<unsigned int>(vLinePixels[0][3]),
		static_cast<unsigned int>(vLinePixels[393][0]),
		static_cast<unsigned int>(vLinePixels[393][1]),
		static_cast<unsigned int>(vLinePixels[393][2]),
		static_cast<unsigned int>(vLinePixels[393][3]),
		static_cast<unsigned int>(vLinePixels[785][0]),
		static_cast<unsigned int>(vLinePixels[785][1]),
		static_cast<unsigned int>(vLinePixels[785][2]),
		static_cast<unsigned int>(vLinePixels[785][3]),
		static_cast<unsigned int>(vLinePixels[786][0]),
		static_cast<unsigned int>(vLinePixels[786][1]),
		static_cast<unsigned int>(vLinePixels[786][2]),
		static_cast<unsigned int>(vLinePixels[786][3]),
		static_cast<unsigned int>(vLinePixels[1178][0]),
		static_cast<unsigned int>(vLinePixels[1178][1]),
		static_cast<unsigned int>(vLinePixels[1178][2]),
		static_cast<unsigned int>(vLinePixels[1178][3]),
		static_cast<unsigned int>(vLinePixels[1571][0]),
		static_cast<unsigned int>(vLinePixels[1571][1]),
		static_cast<unsigned int>(vLinePixels[1571][2]),
		static_cast<unsigned int>(vLinePixels[1571][3]),
		static_cast<unsigned int>(lLineRgbaHash),
		lBlackRgbPixels, lNonOpaquePixels,
		mpScanPresentationState->mfStripMaxProjectionRayErrorArcseconds,
		mpScanPresentationState->mfStripMaxGroundDisplacementMetres,
		mpScanPresentationState->mfStripMaxTimeOffsetMilliseconds,
		mpScanPresentationState->mfPreparationWallMilliseconds,
		fMaxAimErrorArcseconds, fMaxOriginErrorMetres,
		fRenderWallMilliseconds, fMaxViewRenderWallMilliseconds,
		mpScanPresentationState->mfFirstBatchPackSubmitWallMilliseconds,
		fPackReadbackWallMilliseconds,
		fLineLatencyWallMilliseconds,
		gpBase->mpEngine->GetFPS(),
		gpBase->mpEngine->GetAvgFrameTimeInMS(),
		static_cast<unsigned long long>(
			mpScanPresentationState->mlOverwrittenSensorLines));

	if(mpScanPresentationState->mbIlluminationDiagnosticsValid)
	{
		const bool bGroundNight =
			mpScanPresentationState->mfGroundSolarElevationMaxDegrees < 0.0;
		const char *pGroundLighting = bGroundNight ? "night" :
			(mpScanPresentationState->mfGroundSolarElevationMinDegrees >= 0.0 ?
				"daylight" : "terminator");
		const char *pBlackAssessment =
			lNonOpaquePixels != 0 ? "incompleteAcquisition" :
			(lBlackRgbPixels != kMsuMrSensorBatchWidth ? "notAllBlack" :
			(mpScanPresentationState->mlActiveSunCount == 0 ||
			 mpScanPresentationState->mfMaxActiveSunIntensity <= 0.0 ?
				"consistentWithNoActiveSun" :
			(bGroundNight ? "consistentWithNight" :
				"unexpectedForGroundLighting")));
		Log("MSU-MR illumination line %llu: ground=%s, "
			"spacecraftPointSun=%s, activeSuns=%u, "
			"maxActiveSunIntensity=%.3f, "
			"solarElevationDeg[min=%.3f max=%.3f, "
			"s0=%.3f s393=%.3f s785=%.3f s786=%.3f "
			"s1178=%.3f s1571=%.3f], "
			"daylightStripEndpoints=%u/%u, blackAssessment=%s; "
			"point-Sun occultation excludes penumbra.\n",
			static_cast<unsigned long long>(
				mpScanPresentationState->mlSensorLineIndex),
			pGroundLighting,
			mpScanPresentationState->mbPointSunEarthOccluded ?
				"earthOccluded" : "visible",
			mpScanPresentationState->mlActiveSunCount,
			mpScanPresentationState->mfMaxActiveSunIntensity,
			mpScanPresentationState->mfGroundSolarElevationMinDegrees,
			mpScanPresentationState->mfGroundSolarElevationMaxDegrees,
			mpScanPresentationState->mvRepresentativeSolarElevationDegrees[0],
			mpScanPresentationState->mvRepresentativeSolarElevationDegrees[1],
			mpScanPresentationState->mvRepresentativeSolarElevationDegrees[2],
			mpScanPresentationState->mvRepresentativeSolarElevationDegrees[3],
			mpScanPresentationState->mvRepresentativeSolarElevationDegrees[4],
			mpScanPresentationState->mvRepresentativeSolarElevationDegrees[5],
			mpScanPresentationState->mlDaylightEndpointCount,
			mpScanPresentationState->mlIlluminationEndpointCount,
			pBlackAssessment);
	}
	else
	{
		Warning("MSU-MR illumination diagnostics unavailable for line %llu; "
			"pixel acquisition validity still follows alpha/completeness checks.\n",
			static_cast<unsigned long long>(
				mpScanPresentationState->mlSensorLineIndex));
	}

	const std::chrono::steady_clock::time_point commitStart =
		std::chrono::steady_clock::now();
	bool bCommitted = false;
	cLuxMsuMrScanProduct *pScanProduct = mpScanPresentationState->mpScanProduct;
	if(pScanProduct && pScanProduct->BeginLine(
		mpScanPresentationState->mlSensorLineIndex))
	{
		bool bLineValid = true;
		for(int i = 0; i < kMsuMrSensorBatchWidth; ++i)
		{
			if(pScanProduct->SetLineSample(i, vLinePixels[i]) == false)
			{
				bLineValid = false;
				break;
			}
		}
		if(bLineValid)
			bCommitted = pScanProduct->CommitLine();
		if(bCommitted == false)
			pScanProduct->CancelLine();
	}
	const double fCommitWallMilliseconds =
		std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - commitStart).count();

	if(bCommitted == false)
	{
		Warning("Could not atomically commit complete MSU-MR strip line %llu to the scan product.\n",
			static_cast<unsigned long long>(mpScanPresentationState->mlSensorLineIndex));
	}
	else
	{
		Log("MSU-MR scan product line %llu committed: acquiredSamples=%d, "
			"historyRows=%u, insertedGapRows=%llu, commitWall=%.3f ms.\n",
			static_cast<unsigned long long>(mpScanPresentationState->mlSensorLineIndex),
			kMsuMrSensorBatchWidth,
			pScanProduct->GetStoredLineCount(),
			static_cast<unsigned long long>(
				pScanProduct->GetLastGapLineCount()),
			fCommitWallMilliseconds);

		++mpScanPresentationState->mlPerformanceBatchCount;
		mpScanPresentationState->mfPreparationWallSumMilliseconds +=
			mpScanPresentationState->mfPreparationWallMilliseconds;
		mpScanPresentationState->mfPreparationWallMaxMilliseconds = std::max(
			mpScanPresentationState->mfPreparationWallMaxMilliseconds,
			mpScanPresentationState->mfPreparationWallMilliseconds);
		mpScanPresentationState->mfFirstBatchPackSubmitWallSumMilliseconds +=
			mpScanPresentationState->mfFirstBatchPackSubmitWallMilliseconds;
		mpScanPresentationState->mfFirstBatchPackSubmitWallMaxMilliseconds =
			std::max(
				mpScanPresentationState->mfFirstBatchPackSubmitWallMaxMilliseconds,
				mpScanPresentationState->mfFirstBatchPackSubmitWallMilliseconds);
		mpScanPresentationState->mfRenderWallSumMilliseconds +=
			fRenderWallMilliseconds;
		mpScanPresentationState->mfRenderWallMaxMilliseconds = std::max(
			mpScanPresentationState->mfRenderWallMaxMilliseconds,
			fRenderWallMilliseconds);
		mpScanPresentationState->mfReadbackWallSumMilliseconds +=
			fPackReadbackWallMilliseconds;
		mpScanPresentationState->mfReadbackWallMaxMilliseconds = std::max(
			mpScanPresentationState->mfReadbackWallMaxMilliseconds,
			fPackReadbackWallMilliseconds);
		mpScanPresentationState->mfLineLatencyWallSumMilliseconds +=
			fLineLatencyWallMilliseconds;
		mpScanPresentationState->mfLineLatencyWallMaxMilliseconds = std::max(
			mpScanPresentationState->mfLineLatencyWallMaxMilliseconds,
			fLineLatencyWallMilliseconds);
		mpScanPresentationState->mfCommitWallSumMilliseconds +=
			fCommitWallMilliseconds;
		mpScanPresentationState->mfCommitWallMaxMilliseconds = std::max(
			mpScanPresentationState->mfCommitWallMaxMilliseconds,
			fCommitWallMilliseconds);

		if(mpScanPresentationState->mlPerformanceBatchCount % 16 == 0)
		{
			const double fBatchCount = static_cast<double>(
				mpScanPresentationState->mlPerformanceBatchCount);
			Log("MSU-MR full-strip performance after %llu lines "
				"(%d 16x1 views + one 4x1 tail/line, %d total views, "
				"%d samples/line): "
				"preparationCpuWall avg=%.3f max=%.3f ms, "
				"renderCpuWall avg=%.3f max=%.3f ms, "
				"firstBatchPackSubmitWall avg=%.3f max=%.3f ms, "
				"finalBatchPackReadbackWall avg=%.3f max=%.3f ms, "
				"twoFrameLatencyWall avg=%.3f max=%.3f ms "
				"(GPU copies=50+49, readbackCalls/line=1), "
				"commitWall avg=%.3f max=%.3f ms, fps=%.1f, "
				"avgFrame=%.3f ms.\n",
				static_cast<unsigned long long>(
					mpScanPresentationState->mlPerformanceBatchCount),
				kMsuMrFullStripCount, kMsuMrStripCount,
				kMsuMrSensorBatchWidth,
				mpScanPresentationState->mfPreparationWallSumMilliseconds /
					fBatchCount,
				mpScanPresentationState->mfPreparationWallMaxMilliseconds,
				mpScanPresentationState->mfRenderWallSumMilliseconds / fBatchCount,
				mpScanPresentationState->mfRenderWallMaxMilliseconds,
				mpScanPresentationState->mfFirstBatchPackSubmitWallSumMilliseconds /
					fBatchCount,
				mpScanPresentationState->mfFirstBatchPackSubmitWallMaxMilliseconds,
				mpScanPresentationState->mfReadbackWallSumMilliseconds / fBatchCount,
				mpScanPresentationState->mfReadbackWallMaxMilliseconds,
				mpScanPresentationState->mfLineLatencyWallSumMilliseconds /
					fBatchCount,
				mpScanPresentationState->mfLineLatencyWallMaxMilliseconds,
				mpScanPresentationState->mfCommitWallSumMilliseconds / fBatchCount,
				mpScanPresentationState->mfCommitWallMaxMilliseconds,
				gpBase->mpEngine->GetFPS(),
				gpBase->mpEngine->GetAvgFrameTimeInMS());
		}
	}
	ResetMsuMrPendingSensorLine(mpScanPresentationState);
}

bool cLuxSatelliteHandler::StartMsuMrScan(const tString& asName)
{
	if(gpBase == NULL || gpBase->mpMapHandler == NULL)
		return false;
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap == NULL)
		return false;
	return StartMsuMrScanAtEpoch(asName, GetSimulationJulianDateUtc(pMap),
		"current map astronomical UTC");
}

bool cLuxSatelliteHandler::StartMsuMrScan(const tString& asName,
	double afEpochJulianDateUtc)
{
	return StartMsuMrScanAtEpoch(asName, afEpochJulianDateUtc,
		"user-selected UTC");
}

bool cLuxSatelliteHandler::StartMsuMrScanAtEpoch(const tString& asName,
	double afEpochJulianDateUtc, const char *asEpochSource)
{
	if(mpMsuMrSimulation == NULL || gpBase == NULL || gpBase->mpMapHandler == NULL ||
		gpBase->mpEngine == NULL)
		return false;
	if(mpMsuMrSimulation->IsActive() || mpScanPresentationState)
	{
		Warning("Could not start MSU-MR simulation for '%s': another scan is already active.\n",
			asName.c_str());
		return false;
	}

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	const tString sSatelliteKey = cString::ToLowerCase(asName);
	tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.find(sSatelliteKey);
	if(pMap == NULL || it == m_mapOrbits.end() || it->second == NULL || it->second->mpMap != pMap)
	{
		Warning("Could not start MSU-MR simulation: satellite '%s' is not registered in the current map.\n",
				asName.c_str());
		return false;
	}
	if(std::isfinite(afEpochJulianDateUtc) == false)
	{
		Warning("Could not start MSU-MR simulation for satellite '%s': "
			"the requested UTC epoch is not finite.\n", asName.c_str());
		return false;
	}

	const int lUpdatesPerSecond = GetFixedUpdatesPerSecond();
	if(mpMsuMrSimulation->Start(it->second->msName, afEpochJulianDateUtc,
		lUpdatesPerSecond) == false)
	{
		Warning("Could not start MSU-MR simulation for satellite '%s': invalid clock state.\n",
				asName.c_str());
		return false;
	}

	const double fTleEpochJulianDateUtc =
		it->second->mSatelliteRecord.jdsatepoch +
		it->second->mSatelliteRecord.jdsatepochF;
	const double fTleEpochOffsetDays =
		afEpochJulianDateUtc - fTleEpochJulianDateUtc;
	Log("MSU-MR selected UTC is %+.6f days from the loaded TLE epoch "
		"(JD %.12f).\n", fTleEpochOffsetDays, fTleEpochJulianDateUtc);
	if(std::fabs(fTleEpochOffsetDays) > kTleEpochWarningDistanceDays)
	{
		Warning("MSU-MR scan UTC is %.2f days from the loaded TLE epoch; "
			"SGP4 position accuracy may be degraded.\n",
			std::fabs(fTleEpochOffsetDays));
	}

	cLuxSatellitePose initialPose;
	const double fEpochJulianDayUtc = std::floor(afEpochJulianDateUtc);
	if(GetOrbitPose(it->second, fEpochJulianDayUtc,
		afEpochJulianDateUtc - fEpochJulianDayUtc, initialPose) == false)
	{
		mpMsuMrSimulation->Stop();
		Warning("Could not start MSU-MR simulation for satellite '%s': its initial pose could not be propagated.\n",
				asName.c_str());
		return false;
	}
	mpMsuMrSimulation->SetSatellitePose(initialPose);
	if(BeginScanPresentation(pMap, sSatelliteKey) == false)
	{
		mpMsuMrSimulation->Stop();
		Warning("Could not start MSU-MR simulation for satellite '%s': scan presentation mode could not be initialized.\n",
				asName.c_str());
		return false;
	}

	SelectSatellite(asName);
	Log("MSU-MR scan epoch source: %s.\n",
		asEpochSource ? asEpochSource : "unspecified UTC");
	Log("Started MSU-MR simulation for '%s' at UTC JD %.12f (%d updates/s, %d phase positions/s).\n",
		it->second->msName.c_str(), afEpochJulianDateUtc, lUpdatesPerSecond,
		cLuxMsuMrSimulation::kCircularPositionsPerSecond);
	Log("MSU-MR scheduler convention: epoch is circular phase 0 / HRPT sample 0; "
		"Earth window is phase 0..1571 and non-Earth timing is phase 1572..5119.\n");
	Log("MSU-MR geometry convention: pitch=%.8f deg, footprint=%.8f deg, "
		"samples increase -cross-track to +cross-track in a nominal nadir-pointed "
		"LVLH frame; intersections use the WGS-84 ellipsoid without terrain.\n",
		cLuxMsuMrGeometry::GetOpticalPositionPitchDegrees(),
		cLuxMsuMrGeometry::GetEarthFootprintWidthDegrees());
	return true;
}

bool cLuxSatelliteHandler::StopMsuMrScan()
{
	if(mpMsuMrSimulation == NULL ||
		(mpMsuMrSimulation->IsActive() == false && mpScanPresentationState == NULL))
		return false;

	tString sSatelliteName = mpMsuMrSimulation->GetSatelliteName();
	if(sSatelliteName.empty() && mpScanPresentationState)
		sSatelliteName = mpScanPresentationState->msTargetKey;
	EndScanPresentation(true);
	mpMsuMrSimulation->Stop();
	Log("Stopped MSU-MR simulation for '%s'.\n", sSatelliteName.c_str());
	return true;
}

bool cLuxSatelliteHandler::IsMsuMrScanActive() const
{
	return mpMsuMrSimulation && mpMsuMrSimulation->IsActive();
}

bool cLuxSatelliteHandler::RegisterTLE(const tString& asFile)
{
	if(gpBase == NULL || gpBase->mpMapHandler == NULL)
		return false;
	if((mpMsuMrSimulation && mpMsuMrSimulation->IsActive()) ||
		mpScanPresentationState)
	{
		Warning("Could not register TLE '%s' while an MSU-MR scan is active.\n",
			asFile.c_str());
		return false;
	}

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap == NULL)
	{
		Warning("Could not register TLE '%s': no map is loaded.\n", asFile.c_str());
		return false;
	}
	EnsureEarthOrientationData();

	std::vector<cThreeLineElement> vTles;
	if(ReadThreeLineElements(asFile, vTles) == false)
		return false;

	// Initialize every record before changing the live orbit map. A malformed
	// catalog therefore cannot leave only part of the file registered.
	std::vector<cLuxSatelliteOrbit*> vNewOrbits;
	vNewOrbits.reserve(vTles.size());
	for(size_t i = 0; i < vTles.size(); ++i)
	{
		const cThreeLineElement& tle = vTles[i];
		cLuxSatelliteOrbit *pOrbit = hplNew(cLuxSatelliteOrbit, ());
		pOrbit->msName = tle.msName;
		pOrbit->msSourceFile = asFile;
		pOrbit->mpMap = pMap;

		char sLine1[130] = {0};
		char sLine2[130] = {0};
		const size_t lLine1Length = tle.msLine1.size() < sizeof(sLine1) - 1 ? tle.msLine1.size() : sizeof(sLine1) - 1;
		const size_t lLine2Length = tle.msLine2.size() < sizeof(sLine2) - 1 ? tle.msLine2.size() : sizeof(sLine2) - 1;
		std::memcpy(sLine1, tle.msLine1.data(), lLine1Length);
		std::memcpy(sLine2, tle.msLine2.data(), lLine2Length);

		double fStartMinutes = 0.0;
		double fStopMinutes = 0.0;
		double fStepMinutes = 0.0;
		SGP4Funcs::twoline2rv(sLine1, sLine2, 'c', 'm', 'a', wgs72,
							 fStartMinutes, fStopMinutes, fStepMinutes, pOrbit->mSatelliteRecord);

		if(pOrbit->mSatelliteRecord.error != 0 ||
			std::isfinite(pOrbit->mSatelliteRecord.jdsatepoch) == false ||
			std::isfinite(pOrbit->mSatelliteRecord.jdsatepochF) == false)
		{
			Warning("Could not register TLE '%s': SGP4 initialization for '%s' failed with error %d.\n",
					asFile.c_str(), pOrbit->msName.c_str(), pOrbit->mSatelliteRecord.error);
			hplDelete(pOrbit);
			for(size_t j = 0; j < vNewOrbits.size(); ++j)
				hplDelete(vNewOrbits[j]);
			return false;
		}

		vNewOrbits.push_back(pOrbit);
	}

	const double fJulianDateUtc = GetSimulationJulianDateUtc(pMap);
	cFontManager *pFontManager = gpBase->mpEngine && gpBase->mpEngine->GetResources() ?
		gpBase->mpEngine->GetResources()->GetFontManager() : NULL;
	iFontData *pLabelFont = pFontManager ?
		pFontManager->CreateFontData(kSatelliteLabelFont) : NULL;
	if(pLabelFont == NULL)
		Warning("Could not load satellite label font '%s'.\n", kSatelliteLabelFont);

	for(size_t i = 0; i < vNewOrbits.size(); ++i)
	{
		cLuxSatelliteOrbit *pOrbit = vNewOrbits[i];
		const tString sKey = cString::ToLowerCase(pOrbit->msName);
		tLuxSatelliteOrbitMap::iterator itExisting = m_mapOrbits.find(sKey);
		if(itExisting != m_mapOrbits.end())
		{
			DestroyOrbit(itExisting->second, true);
			m_mapOrbits.erase(itExisting);
		}

		if(pMap->GetWorld())
		{
			pOrbit->mpIconBillboard = pMap->GetWorld()->CreateBillboard(
				"__SGP4Icon_" + pOrbit->msName, cVector2f(kSatelliteIconWorldSizeMetres),
				eBillboardType_Point, kSatelliteIconMaterial, false);
			if(pOrbit->mpIconBillboard)
			{
				pOrbit->mpIconBillboard->SetTranslucentSortPriority(kSatelliteIconTranslucentPriority);
				pOrbit->mpIconBillboard->SetPointRollTarget(cVector3f(0.0f), kSatelliteIconSignalDirection);
				pOrbit->mpIconBillboard->SetColor(sKey == msSelectedSatelliteKey ?
					cColor(0, 1, 0, 1) : cColor(1, 1, 1, 1));
			}
			if(pOrbit->mpIconBillboard && pOrbit->mpIconBillboard->GetMaterial() == NULL)
			{
				Warning("Could not create satellite icon for '%s': material '%s' could not be loaded.\n",
						pOrbit->msName.c_str(), kSatelliteIconMaterial);
				pMap->GetWorld()->DestroyBillboard(pOrbit->mpIconBillboard);
				pOrbit->mpIconBillboard = NULL;
			}

			if(pLabelFont && CreateSatelliteLabel(pOrbit, pLabelFont) == false)
			{
				Warning("Could not create satellite label for '%s' with material '%s'.\n",
						pOrbit->msName.c_str(), kSatelliteLabelMaterial);
			}
		}

		m_mapOrbits[sKey] = pOrbit;
		Log("Registered SGP4 orbit '%s' from '%s'.\n", pOrbit->msName.c_str(), asFile.c_str());
		UpdateOrbit(pOrbit, fJulianDateUtc);
	}
	if(pLabelFont) pFontManager->Destroy(pLabelFont);

	return true;
}

void cLuxSatelliteHandler::Update()
{
	if(gpBase == NULL || gpBase->mpMapHandler == NULL)
		return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap == NULL)
	{
		Reset();
		return;
	}

	// LuxScriptHandler is a global updateable and therefore still receives
	// Update calls in menu, inventory, and journal containers. The map world is
	// inactive in those pause states (and during an in-world pause message), so
	// only advance instrument time while the world simulation itself is active.
	const bool bWorldSimulationRunning = pMap->GetWorld() && pMap->GetWorld()->IsActive();
	if(mpMsuMrSimulation && mpMsuMrSimulation->IsActive() && bWorldSimulationRunning)
	{
		const tString sSimulationKey = cString::ToLowerCase(mpMsuMrSimulation->GetSatelliteName());
		tLuxSatelliteOrbitMap::iterator itSimulationOrbit = m_mapOrbits.find(sSimulationKey);
		const bool bTargetIsValid = itSimulationOrbit != m_mapOrbits.end() &&
			itSimulationOrbit->second != NULL && itSimulationOrbit->second->mpMap == pMap;
		const int lUpdatesPerSecond = GetFixedUpdatesPerSecond();

		if(bTargetIsValid == false)
		{
			Warning("Stopping MSU-MR simulation because its target satellite is no longer available.\n");
			StopMsuMrScan();
		}
		else if(lUpdatesPerSecond != mpMsuMrSimulation->GetUpdatesPerSecond())
		{
			// The integer DDA is exact for the fixed update rate captured at Start.
			// Silently changing its denominator would introduce a time discontinuity.
			Warning("Stopping MSU-MR simulation because the game update rate changed from %d to %d Hz.\n",
					mpMsuMrSimulation->GetUpdatesPerSecond(), lUpdatesPerSecond);
			StopMsuMrScan();
		}
		else
		{
			mpMsuMrSimulation->AdvanceOneUpdate();
			LogMsuMrSampleDiagnostics(itSimulationOrbit->second);
			if(mpMsuMrSimulation->IsActive() == false)
			{
				Warning("Stopped MSU-MR simulation after its phase counter overflowed.\n");
				EndScanPresentation(true);
			}
			else
			{
				double fJulianDayUtc = 0.0;
				double fJulianFractionUtc = 0.0;
				mpMsuMrSimulation->GetJulianDateUtc(fJulianDayUtc, fJulianFractionUtc);

				cLuxSatellitePose pose;
				if(GetOrbitPose(itSimulationOrbit->second, fJulianDayUtc,
					fJulianFractionUtc, pose))
				{
					mpMsuMrSimulation->SetSatellitePose(pose);
				}
				else
				{
					Warning("Stopping MSU-MR simulation because its target pose could not be propagated.\n");
					StopMsuMrScan();
				}
			}
		}
	}
	if(mpMsuMrSimulation && mpMsuMrSimulation->IsActive())
		UpdateScanPresentation();

	if(m_mapOrbits.empty())
		return;

	const bool bScanModeActive = mpScanPresentationState && mpMsuMrSimulation &&
		mpMsuMrSimulation->IsActive();
	const double fJulianDateUtc = bScanModeActive ? 0.0 : GetSimulationJulianDateUtc(pMap);
	tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin();
	while(it != m_mapOrbits.end())
	{
		cLuxSatelliteOrbit *pOrbit = it->second;
		if(pOrbit == NULL || pOrbit->mpMap != pMap)
		{
			tLuxSatelliteOrbitMap::iterator itDestroy = it++;
			if(itDestroy->first == msSelectedSatelliteKey)
				msSelectedSatelliteKey.clear();
			DestroyOrbit(itDestroy->second, false);
			m_mapOrbits.erase(itDestroy);
			continue;
		}

		if(bScanModeActive == false)
			UpdateOrbit(pOrbit, fJulianDateUtc);
		++it;
	}
}

void cLuxSatelliteHandler::Reset()
{
	EndScanPresentation(false);
	if(mpMsuMrSimulation)
		mpMsuMrSimulation->Stop();

	cLuxMap *pCurrentMap = gpBase && gpBase->mpMapHandler ?
		gpBase->mpMapHandler->GetCurrentMap() : NULL;
	for(tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin(); it != m_mapOrbits.end(); ++it)
		DestroyOrbit(it->second, it->second && it->second->mpMap == pCurrentMap);
	m_mapOrbits.clear();
	msSelectedSatelliteKey.clear();
}

void cLuxSatelliteHandler::DestroyWorldEntities(cLuxMap *apMap)
{
	if(apMap == NULL)
		return;

	if(mpScanPresentationState && mpScanPresentationState->mpMap == apMap)
	{
		EndScanPresentation(false);
		if(mpMsuMrSimulation) mpMsuMrSimulation->Stop();
	}

	tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin();
	while(it != m_mapOrbits.end())
	{
		if(it->second && it->second->mpMap == apMap)
		{
			tLuxSatelliteOrbitMap::iterator itDestroy = it++;
			if(itDestroy->first == msSelectedSatelliteKey)
				msSelectedSatelliteKey.clear();
			DestroyOrbit(itDestroy->second, true);
			m_mapOrbits.erase(itDestroy);
		}
		else
			++it;
	}
}

void cLuxSatelliteHandler::DestroyOrbit(cLuxSatelliteOrbit *apOrbit, bool abDestroyBillboard)
{
	if(apOrbit == NULL)
		return;
	RestoreOrbitScanVisibility(apOrbit);

	if(abDestroyBillboard && apOrbit->mpIconBillboard && apOrbit->mpMap && apOrbit->mpMap->GetWorld())
		apOrbit->mpMap->GetWorld()->DestroyBillboard(apOrbit->mpIconBillboard);
	if(abDestroyBillboard && apOrbit->mpMap && apOrbit->mpMap->GetWorld())
	{
		for(size_t i = 0; i < apOrbit->mvLabelBillboards.size(); ++i)
			apOrbit->mpMap->GetWorld()->DestroyBillboard(apOrbit->mvLabelBillboards[i]);
	}
	apOrbit->mpIconBillboard = NULL;
	apOrbit->mvLabelBillboards.clear();
	hplDelete(apOrbit);
}

bool cLuxSatelliteHandler::GetOrbitPose(cLuxSatelliteOrbit *apOrbit,
										double afJulianDayUtc,
										double afJulianFractionUtc,
										cLuxSatellitePose& aPose)
{
	if(apOrbit == NULL)
		return false;

	double fJulianDay = 0.0;
	double fJulianFraction = 0.0;
	if(NormalizeJulianDate(afJulianDayUtc, afJulianFractionUtc,
		fJulianDay, fJulianFraction) == false)
		return false;

	const double fJulianDateForDiagnostics = fJulianDay + fJulianFraction;
	const double fMinutesSinceEpoch =
		((fJulianDay - apOrbit->mSatelliteRecord.jdsatepoch) +
		 (fJulianFraction - apOrbit->mSatelliteRecord.jdsatepochF)) * kMinutesPerDay;

	double vTemePositionKm[3] = {0.0, 0.0, 0.0};
	double vTemeVelocityKmPerSecond[3] = {0.0, 0.0, 0.0};
	const bool bPropagated = SGP4Funcs::sgp4(apOrbit->mSatelliteRecord, fMinutesSinceEpoch,
										vTemePositionKm, vTemeVelocityKmPerSecond);

	if(bPropagated == false || apOrbit->mSatelliteRecord.error != 0)
	{
		if(apOrbit->mlLastPropagationError != apOrbit->mSatelliteRecord.error)
		{
			Warning("Could not propagate SGP4 orbit '%s' at JD %.8f (error %d).\n",
					apOrbit->msName.c_str(), fJulianDateForDiagnostics,
					apOrbit->mSatelliteRecord.error);
			apOrbit->mlLastPropagationError = apOrbit->mSatelliteRecord.error;
		}
		return false;
	}
	apOrbit->mlLastPropagationError = 0;

	cLuxEarthOrientationSample earthOrientation;
	const bool bHasEarthOrientation = mpEarthOrientationTable &&
		mpEarthOrientationTable->Sample(fJulianDay, fJulianFraction, earthOrientation);
	if(bHasEarthOrientation)
		mbEarthOrientationWarningShown = false;
	else if(mbEarthOrientationWarningShown == false)
	{
		if(mpEarthOrientationTable && mpEarthOrientationTable->Empty() == false)
		{
			Warning("No IERS Earth-orientation record covers JD %.8f (available MJD %.2f through %.2f); "
					"falling back to UTC as UT1 with zero polar motion and LOD.\n",
					fJulianDateForDiagnostics,
					mpEarthOrientationTable->GetFirstModifiedJulianDate(),
					mpEarthOrientationTable->GetLastModifiedJulianDate());
		}
		else
		{
			Warning("No IERS Earth-orientation data is loaded; falling back to UTC as UT1 with zero polar motion and LOD. "
					"Call RegisterEarthOrientationData before RegisterTLE.\n");
		}
		mbEarthOrientationWarningShown = true;
	}

	cVector3d vEcefPositionKm;
	cVector3d vEcefVelocityKmPerSecond;
	cVector3d vEcefInertialVelocityKmPerSecond;
	TemeToEarthFixed(fJulianDay, fJulianFraction,
					 vTemePositionKm, vTemeVelocityKmPerSecond,
					 earthOrientation,
					 vEcefPositionKm, vEcefVelocityKmPerSecond,
					 vEcefInertialVelocityKmPerSecond);

	aPose = cLuxSatellitePose();
	aPose.mfJulianDayUtc = fJulianDay;
	aPose.mfJulianFractionUtc = fJulianFraction;
	aPose.mbUsedEarthOrientationData = bHasEarthOrientation;
	EarthFixedToHpl(vEcefPositionKm, aPose.mvPositionMetres);
	EarthFixedToHpl(vEcefVelocityKmPerSecond, aPose.mvVelocityMetresPerSecond);
	EarthFixedToHpl(vEcefInertialVelocityKmPerSecond,
		aPose.mvInertialVelocityMetresPerSecond);
	if(IsFinite(aPose.mvPositionMetres) == false ||
		IsFinite(aPose.mvVelocityMetresPerSecond) == false ||
		IsFinite(aPose.mvInertialVelocityMetresPerSecond) == false)
		return false;

	return BuildOrbitFrame(aPose.mvPositionMetres,
		aPose.mvInertialVelocityMetresPerSecond,
		aPose.mvCrossTrack, aPose.mvRadialOut, aPose.mvAlongTrack);
}

void cLuxSatelliteHandler::UpdateOrbit(cLuxSatelliteOrbit *apOrbit, double afJulianDateUtc)
{
	if(apOrbit == NULL || apOrbit->mpMap == NULL || std::isfinite(afJulianDateUtc) == false)
		return;

	const double fJulianDayUtc = std::floor(afJulianDateUtc);
	cLuxSatellitePose pose;
	if(GetOrbitPose(apOrbit, fJulianDayUtc,
		afJulianDateUtc - fJulianDayUtc, pose) == false)
		return;
	ApplyOrbitPose(apOrbit, pose);
}

void cLuxSatelliteHandler::ApplyOrbitPose(cLuxSatelliteOrbit *apOrbit,
											 const cLuxSatellitePose& aPose)
{
	if(apOrbit == NULL || apOrbit->mpMap == NULL)
		return;

	if(apOrbit->mpIconBillboard || apOrbit->mvLabelBillboards.empty() == false)
	{
		const cVector3f vIconPosition((float)aPose.mvPositionMetres.x,
								  (float)aPose.mvPositionMetres.y,
								  (float)aPose.mvPositionMetres.z);
		if(apOrbit->mpIconBillboard)
			apOrbit->mpIconBillboard->SetWorldPosition(vIconPosition);
		for(size_t i = 0; i < apOrbit->mvLabelBillboards.size(); ++i)
			apOrbit->mvLabelBillboards[i]->SetWorldPosition(vIconPosition);
	}

	iLuxEntity *pEntity = apOrbit->mpMap->GetEntityByName(apOrbit->msName);
	iEntity3D *pAttachEntity = pEntity ? pEntity->GetAttachEntity() : NULL;
	if(pAttachEntity == NULL)
		return;

	cMatrixf mtxTransform;
	if(BuildHplTransform(aPose, pAttachEntity->GetWorldMatrix(), mtxTransform))
	{
		// This is the only precision boundary in the orbit path. SGP4, UTC/UT1,
		// EOP interpolation, TEME/ECEF conversion, units, axes, and orbital
		// attitude remain doubles.
		pAttachEntity->SetWorldMatrix(mtxTransform);
	}
}
