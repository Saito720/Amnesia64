/*
 * Copyright (C) 2009-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HPL_BITMAP_LOADER_KTX2_H
#define HPL_BITMAP_LOADER_KTX2_H

#include "resources/BitmapLoader.h"

namespace hpl {

	class cBitmapLoaderKTX2 : public iBitmapLoader
	{
	public:
		cBitmapLoaderKTX2();
		~cBitmapLoaderKTX2();

		cBitmap* LoadBitmap(const tWString& asFile, tBitmapLoadFlag aFlags);
		bool SaveBitmap(cBitmap* apBitmap, const tWString& asFile, tBitmapSaveFlag aFlags);

	private:
		ePixelFormat GetPixelFormatFromVkFormat(unsigned int alVkFormat);
		bool ShouldTranscodeToDXTC(tBitmapLoadFlag aFlags);
	};

};
#endif // HPL_BITMAP_LOADER_KTX2_H
