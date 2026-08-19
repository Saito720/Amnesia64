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

#ifndef LUX_MSU_MR_SCAN_PRODUCT_H
#define LUX_MSU_MR_SCAN_PRODUCT_H

#include <cstdint>
#include <vector>

#include "LuxBase.h"

namespace hpl
{
	class iTexture;
	class cGuiGfxElement;
}

// Owns the accumulating rendered scan image independently of instrument
// scheduling and sensor acquisition. Gap rows have zero alpha; the overlay
// draws them over a black backing panel so acquired black remains valid data.
class cLuxMsuMrScanProduct
{
public:
	static const std::uint32_t kWidth = 1572;
	static const std::uint32_t kHistoryLines = 256;

	cLuxMsuMrScanProduct();
	~cLuxMsuMrScanProduct();

	bool Initialize();
	void Reset();
	bool BeginLine(std::uint64_t alLineIndex);
	bool SetLineSample(std::uint32_t alSampleIndex,
					   const unsigned char *apRgba);
	bool CommitLine();
	void CancelLine();
	void Draw();

	bool IsInitialized() const { return mbInitialized; }
	std::uint32_t GetStoredLineCount() const { return mlStoredLineCount; }
	std::uint64_t GetLastLineIndex() const { return mlLastLineIndex; }
	std::uint64_t GetLastGapLineCount() const { return mlLastGapLineCount; }

private:
	void UploadPixels();

	std::vector<unsigned char> mvLinePixels;
	std::vector<unsigned char> mvLineWritten;
	std::vector<unsigned char> mvPixels;
	hpl::iTexture *mpTexture;
	hpl::cGuiGfxElement *mpImageGfx;
	hpl::cGuiGfxElement *mpBackgroundGfx;
	bool mbInitialized;
	bool mbHasLastLine;
	bool mbLineOpen;
	std::uint64_t mlOpenLineIndex;
	std::uint64_t mlLastLineIndex;
	std::uint64_t mlLastGapLineCount;
	std::uint32_t mlStoredLineCount;
	std::uint32_t mlWrittenSampleCount;
};

#endif // LUX_MSU_MR_SCAN_PRODUCT_H
