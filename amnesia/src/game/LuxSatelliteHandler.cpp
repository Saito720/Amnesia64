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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "LuxEntity.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"

#include "SGP4/SGP4.h"

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
	bool Sample(double afJulianDateUtc, cLuxEarthOrientationSample& aSample) const;

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
	const double kMetresPerKilometre = 1000.0;
	const double kEarthRotationRadiansPerSecond = 7.29211514670698e-5;
	const double kArcsecondsToRadians = kDegreesToRadians / 3600.0;
	const double kJulianDateToModifiedJulianDate = 2400000.5;
	const char *kDefaultEarthOrientationFile = "core/eop/finals2000A.data";
	const char *kRepositoryEarthOrientationFile = "redist/core/eop/finals2000A.data";

	struct cVector3d
	{
		cVector3d() : x(0.0), y(0.0), z(0.0) {}
		cVector3d(double afX, double afY, double afZ) : x(afX), y(afY), z(afZ) {}

		double x;
		double y;
		double z;
	};

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

	static bool ReadThreeLineElement(const tString& asFile, cThreeLineElement& aTle)
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

		if(vLines.size() != 3)
		{
			Warning("Could not register TLE '%s': expected exactly three non-empty lines, found %u.\n",
					asFile.c_str(), (unsigned int)vLines.size());
			return false;
		}

		aTle.msName = TrimAscii(vLines[0]);
		if(aTle.msName.size() >= 2 && aTle.msName[0] == '0' && aTle.msName[1] == ' ')
			aTle.msName = TrimAscii(aTle.msName.substr(2));

		aTle.msLine1 = vLines[1];
		aTle.msLine2 = vLines[2];

		if(aTle.msName.empty())
		{
			Warning("Could not register TLE '%s': satellite name is empty.\n", asFile.c_str());
			return false;
		}

		if(aTle.msLine1.size() < 69 || aTle.msLine2.size() < 69 ||
			aTle.msLine1[0] != '1' || aTle.msLine1[1] != ' ' ||
			aTle.msLine2[0] != '2' || aTle.msLine2[1] != ' ')
		{
			Warning("Could not register TLE '%s': element lines are not valid fixed-column TLE records.\n", asFile.c_str());
			return false;
		}

		if(aTle.msLine1.compare(2, 5, aTle.msLine2, 2, 5) != 0)
		{
			Warning("Could not register TLE '%s': catalog numbers do not match.\n", asFile.c_str());
			return false;
		}

		if(HasValidTleChecksum(aTle.msLine1) == false || HasValidTleChecksum(aTle.msLine2) == false)
		{
			Warning("Could not register TLE '%s': checksum validation failed.\n", asFile.c_str());
			return false;
		}

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

	static void TemeToEarthFixed(double afJulianDateUtc,
							 const double avTemePositionKm[3], const double avTemeVelocityKmPerSecond[3],
							 const cLuxEarthOrientationSample& aEarthOrientation,
							 cVector3d& avEcefPositionKm, cVector3d& avEcefVelocityKmPerSecond)
	{
		const double fJulianDateUt1 =
			afJulianDateUtc + aEarthOrientation.mfUt1MinusUtcSeconds / kSecondsPerDay;
		double fSiderealAngle = SGP4Funcs::gstime_SGP4(fJulianDateUt1);

		// TEME's post-1997 kinematic equation-of-equinox terms, matching
		// Vallado's reference teme2ecef implementation. UT1 also approximates TT
		// here; their difference has a negligible effect on these tiny terms.
		if(fJulianDateUt1 > 2450449.5)
		{
			const double fJulianCenturies = (fJulianDateUt1 - 2451545.0) / 36525.0;
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

	static bool BuildHplTransform(const cVector3d& avPositionMetres, const cVector3d& avVelocityMetresPerSecond,
							  const cMatrixf& aCurrentTransform, cMatrixf& aTransform)
	{
		const cVector3d vUp = Normalize(avPositionMetres);
		const cVector3d vVelocity = Normalize(avVelocityMetresPerSecond);
		const cVector3d vRight = Normalize(Cross(vUp, vVelocity));
		const cVector3d vForward = Normalize(Cross(vRight, vUp));

		if(Dot(vUp, vUp) == 0.0 || Dot(vRight, vRight) == 0.0 || Dot(vForward, vForward) == 0.0)
			return false;

		const double fScaleX = MatrixColumnLength(aCurrentTransform, 0);
		const double fScaleY = MatrixColumnLength(aCurrentTransform, 1);
		const double fScaleZ = MatrixColumnLength(aCurrentTransform, 2);

		aTransform = cMatrixf::Identity;
		aTransform.m[0][0] = (float)(vRight.x * fScaleX);
		aTransform.m[1][0] = (float)(vRight.y * fScaleX);
		aTransform.m[2][0] = (float)(vRight.z * fScaleX);
		aTransform.m[0][1] = (float)(vUp.x * fScaleY);
		aTransform.m[1][1] = (float)(vUp.y * fScaleY);
		aTransform.m[2][1] = (float)(vUp.z * fScaleY);
		aTransform.m[0][2] = (float)(vForward.x * fScaleZ);
		aTransform.m[1][2] = (float)(vForward.y * fScaleZ);
		aTransform.m[2][2] = (float)(vForward.z * fScaleZ);
		aTransform.m[0][3] = (float)avPositionMetres.x;
		aTransform.m[1][3] = (float)avPositionMetres.y;
		aTransform.m[2][3] = (float)avPositionMetres.z;
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

bool cLuxEarthOrientationTable::Sample(double afJulianDateUtc, cLuxEarthOrientationSample& aSample) const
{
	if(mvRecords.empty() || std::isfinite(afJulianDateUtc) == false)
		return false;

	const double fModifiedJulianDate = afJulianDateUtc - kJulianDateToModifiedJulianDate;
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
		mbMissingEntityWarningShown = false;
		mlLastPropagationError = 0;
		std::memset(&mSatelliteRecord, 0, sizeof(mSatelliteRecord));
	}

	tString msName;
	tString msSourceFile;
	cLuxMap *mpMap;
	elsetrec mSatelliteRecord;
	bool mbMissingEntityWarningShown;
	int mlLastPropagationError;
};

cLuxSatelliteHandler::cLuxSatelliteHandler()
{
	mpEarthOrientationTable = hplNew(cLuxEarthOrientationTable, ());
	mbDefaultEarthOrientationLoadAttempted = false;
	mbEarthOrientationWarningShown = false;
}

cLuxSatelliteHandler::~cLuxSatelliteHandler()
{
	Reset();
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
	return LoadEarthOrientationData(asFile, true);
}

bool cLuxSatelliteHandler::RegisterTLE(const tString& asFile)
{
	if(gpBase == NULL || gpBase->mpMapHandler == NULL)
		return false;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap == NULL)
	{
		Warning("Could not register TLE '%s': no map is loaded.\n", asFile.c_str());
		return false;
	}
	EnsureEarthOrientationData();

	cThreeLineElement tle;
	if(ReadThreeLineElement(asFile, tle) == false)
		return false;

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
		Warning("Could not register TLE '%s': SGP4 initialization failed with error %d.\n",
				asFile.c_str(), pOrbit->mSatelliteRecord.error);
		hplDelete(pOrbit);
		return false;
	}

	const tString sKey = cString::ToLowerCase(pOrbit->msName);
	tLuxSatelliteOrbitMap::iterator itExisting = m_mapOrbits.find(sKey);
	if(itExisting != m_mapOrbits.end())
	{
		hplDelete(itExisting->second);
		m_mapOrbits.erase(itExisting);
	}
	m_mapOrbits[sKey] = pOrbit;

	Log("Registered SGP4 orbit '%s' from '%s'.\n", pOrbit->msName.c_str(), asFile.c_str());
	UpdateOrbit(pOrbit, GetSimulationJulianDateUtc(pMap));
	return true;
}

void cLuxSatelliteHandler::Update()
{
	if(m_mapOrbits.empty() || gpBase == NULL || gpBase->mpMapHandler == NULL)
		return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap == NULL)
	{
		Reset();
		return;
	}

	const double fJulianDateUtc = GetSimulationJulianDateUtc(pMap);
	tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin();
	while(it != m_mapOrbits.end())
	{
		cLuxSatelliteOrbit *pOrbit = it->second;
		if(pOrbit == NULL || pOrbit->mpMap != pMap)
		{
			tLuxSatelliteOrbitMap::iterator itDestroy = it++;
			hplDelete(itDestroy->second);
			m_mapOrbits.erase(itDestroy);
			continue;
		}

		UpdateOrbit(pOrbit, fJulianDateUtc);
		++it;
	}
}

void cLuxSatelliteHandler::Reset()
{
	for(tLuxSatelliteOrbitMap::iterator it = m_mapOrbits.begin(); it != m_mapOrbits.end(); ++it)
		hplDelete(it->second);
	m_mapOrbits.clear();
}

void cLuxSatelliteHandler::UpdateOrbit(cLuxSatelliteOrbit *apOrbit, double afJulianDateUtc)
{
	if(apOrbit == NULL || apOrbit->mpMap == NULL || std::isfinite(afJulianDateUtc) == false)
		return;

	const double fJulianDay = std::floor(afJulianDateUtc);
	const double fJulianFraction = afJulianDateUtc - fJulianDay;
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
					apOrbit->msName.c_str(), afJulianDateUtc, apOrbit->mSatelliteRecord.error);
			apOrbit->mlLastPropagationError = apOrbit->mSatelliteRecord.error;
		}
		return;
	}
	apOrbit->mlLastPropagationError = 0;

	cLuxEarthOrientationSample earthOrientation;
	const bool bHasEarthOrientation = mpEarthOrientationTable &&
		mpEarthOrientationTable->Sample(afJulianDateUtc, earthOrientation);
	if(bHasEarthOrientation)
		mbEarthOrientationWarningShown = false;
	else if(mbEarthOrientationWarningShown == false)
	{
		if(mpEarthOrientationTable && mpEarthOrientationTable->Empty() == false)
		{
			Warning("No IERS Earth-orientation record covers JD %.8f (available MJD %.2f through %.2f); "
					"falling back to UTC as UT1 with zero polar motion and LOD.\n",
					afJulianDateUtc,
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
	TemeToEarthFixed(afJulianDateUtc, vTemePositionKm, vTemeVelocityKmPerSecond,
					 earthOrientation,
					 vEcefPositionKm, vEcefVelocityKmPerSecond);

	cVector3d vHplPositionMetres;
	cVector3d vHplVelocityMetresPerSecond;
	EarthFixedToHpl(vEcefPositionKm, vHplPositionMetres);
	EarthFixedToHpl(vEcefVelocityKmPerSecond, vHplVelocityMetresPerSecond);
	if(IsFinite(vHplPositionMetres) == false || IsFinite(vHplVelocityMetresPerSecond) == false)
		return;

	iLuxEntity *pEntity = apOrbit->mpMap->GetEntityByName(apOrbit->msName);
	iEntity3D *pAttachEntity = pEntity ? pEntity->GetAttachEntity() : NULL;
	if(pAttachEntity == NULL)
	{
		if(apOrbit->mbMissingEntityWarningShown == false)
		{
			Warning("SGP4 orbit '%s' is registered but no same-named map entity with a transform exists.\n",
					apOrbit->msName.c_str());
			apOrbit->mbMissingEntityWarningShown = true;
		}
		return;
	}
	apOrbit->mbMissingEntityWarningShown = false;

	cMatrixf mtxTransform;
	if(BuildHplTransform(vHplPositionMetres, vHplVelocityMetresPerSecond,
						 pAttachEntity->GetWorldMatrix(), mtxTransform))
	{
		// This is the only precision boundary in the orbit path. SGP4, UTC/UT1,
		// EOP interpolation, TEME/ECEF conversion, units, axes, and orbital
		// attitude remain doubles.
		pAttachEntity->SetWorldMatrix(mtxTransform);
	}
}
