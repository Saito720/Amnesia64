#ifndef HPL_TRUETYPE_FONTDATA_H
#define HPL_TRUETYPE_FONTDATA_H

#include "graphics/FontData.h"

namespace hpl {
	struct cTrueTypeFontDataImpl;

	// Rasterizes Unicode glyphs on demand into the image manager's atlases.
	class cTrueTypeFontData : public iFontData
	{
	public:
		cTrueTypeFontData(const tString &asName, iLowLevelGraphics* apLowLevelGraphics);
		~cTrueTypeFontData();

		bool CreateFromFontFile(const tWString &asFileName, int alSize,
			unsigned short alFirstChar, unsigned short alLastChar);
		bool CreateFromBitmapFile(const tWString &asFileName);
		cGlyph* GetGlyphForCodepoint(unsigned int alCodepoint);
		float GetKerning(unsigned int alLeft, unsigned int alRight) const;

	private:
		cTrueTypeFontDataImpl* mpImpl;
	};
}

#endif
