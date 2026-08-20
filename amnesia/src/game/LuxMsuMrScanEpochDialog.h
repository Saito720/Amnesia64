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

#ifndef LUX_MSU_MR_SCAN_EPOCH_DIALOG_H
#define LUX_MSU_MR_SCAN_EPOCH_DIALOG_H

#include <string>

class cLuxMsuMrScanEpochDialog
{
public:
	enum eResult
	{
		eResult_Cancelled,
		eResult_Confirmed,
		eResult_Unavailable
	};

	// Displays a modal native UTC date/time picker owned by the main game
	// window. On confirmation, the selected instant is returned both as a
	// Julian date and as a display-ready ISO 8601 string.
	static eResult Show(const std::wstring& asSatelliteName,
		double& afJulianDateUtc, std::wstring& asUtcText);
};

#endif // LUX_MSU_MR_SCAN_EPOCH_DIALOG_H
