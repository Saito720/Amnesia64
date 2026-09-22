// Exercises the same font manager, glyph atlas, GUI drawing and OpenGL path as the game.
#define SDL_MAIN_HANDLED
#include "engine/Engine.h"
#include "engine/EngineInitVars.h"
#include "graphics/FontData.h"
#include "graphics/Graphics.h"
#include "graphics/LowLevelGraphics.h"
#include "gui/Gui.h"
#include "gui/GuiSet.h"
#include "resources/FontManager.h"
#include "resources/Resources.h"
#include "system/String.h"
#include "SDL2/SDL.h"
#include "GL/glew.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace hpl;

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void CheckUnicodeConversion()
{
    const tString utf8 = "A\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF";
    const tWString wide = cString::UTF8ToWChar(utf8);
    Check(cString::S16BitToUTF8(wide) == utf8, "UTF-8 scalar boundaries did not round-trip");
    const tString embeddedNull("A\0B", 3);
    Check(cString::S16BitToUTF8(cString::UTF8ToWChar(embeddedNull)) == embeddedNull,
          "Unicode conversion lost an embedded null");
    const char* invalid[] = { "\xF0\x9F", "\xE2", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80" };
    for(size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
    {
        const tWString result = cString::UTF8ToWChar(invalid[i]);
        Check(!result.empty(), "invalid UTF-8 disappeared");
        for(size_t j = 0; j < result.size(); ++j)
            Check(result[j] == 0xFFFD, "invalid UTF-8 was not replaced");
    }
    Check(cString::UTF8ToWChar("\xE2" "A") == _W("\xFFFD" "A"),
          "invalid UTF-8 consumed a following ASCII character");
    Check(cString::S16BitToUTF8(_W("\xD800")) == "\xEF\xBF\xBD",
          "isolated surrogate was encoded as invalid UTF-8");
}

static void WriteInvalidFont()
{
    // A complete SFNT directory whose required tables have no contents.
    // Directory bounds alone must not allow this file into stb_truetype.
    const char* tags[] = { "head", "hhea", "maxp", "cmap", "hmtx", "loca", "glyf" };
    std::vector<unsigned char> data(12 + 7 * 16, 0);
    data[1] = 1;
    data[5] = 7;
    for(size_t i = 0; i < 7; ++i)
    {
        std::memcpy(&data[12 + i * 16], tags[i], 4);
        data[12 + i * 16 + 11] = (unsigned char)data.size();
    }
    FILE* file = fopen("InvalidFontTest.ttf", "wb");
    Check(file != NULL, "cannot create malformed-font fixture");
    const bool saved = fwrite(&data[0], 1, data.size(), file) == data.size();
    fclose(file);
    Check(saved, "cannot save malformed-font fixture");
}

static void SaveBackBuffer(const char* path, int width, int height)
{
    const int stride = (width * 3 + 3) & ~3;
    std::vector<unsigned char> pixels((size_t)stride * height, 0);
    std::vector<unsigned char> rgba((size_t)width * height * 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const size_t source = ((size_t)y * width + x) * 4;
            const size_t target = (size_t)y * stride + x * 3;
            pixels[target + 0] = rgba[source + 2];
            pixels[target + 1] = rgba[source + 1];
            pixels[target + 2] = rgba[source + 0];
        }

    unsigned char header[54] = {
        'B','M', 0,0,0,0, 0,0,0,0, 54,0,0,0,
        40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 24,0
    };
    const unsigned int fileSize = 54 + (unsigned int)pixels.size();
    for (int i = 0; i < 4; ++i)
    {
        header[2 + i] = (unsigned char)(fileSize >> (8 * i));
        header[18 + i] = (unsigned char)((unsigned int)width >> (8 * i));
        header[22 + i] = (unsigned char)((unsigned int)height >> (8 * i));
    }
    FILE* output = fopen(path, "wb");
    Check(output != NULL, "cannot create screenshot");
    const bool saved = fwrite(header, 1, sizeof(header), output) == sizeof(header)
        && fwrite(&pixels[0], 1, pixels.size(), output) == pixels.size();
    fclose(output);
    Check(saved, "cannot save screenshot");
}

static int CountBrightPixels(int yMin, int yMax, int width, int height)
{
    std::vector<unsigned char> rgba((size_t)width * height * 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
    int count = 0;
    for (int y = yMin; y < yMax; ++y)
        for (int x = 0; x < width; ++x)
        {
            const size_t pixel = ((size_t)(height - 1 - y) * width + x) * 4;
            if (rgba[pixel] > 60 || rgba[pixel + 1] > 60 || rgba[pixel + 2] > 60) ++count;
        }
    return count;
}

// HPL2's Windows entry point shares an object file with the logging functions.
int hplMain(const hpl::tString&) { return 0; }

int main()
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    cEngine* engine = NULL;
    try
    {
        CheckUnicodeConversion();
        WriteInvalidFont();
        cEngineInitVars variables;
        variables.mGraphics.mvScreenSize = cVector2l(900, 330);
        variables.mGraphics.mbFullscreen = false;
        variables.mGraphics.mWindowBorderMode = eWindowBorderMode_Bordered;
        variables.mGraphics.msWindowCaption = "Amnesia font rendering test";
        variables.mSound.mbUseHRTF = false;
        variables.mSound.mbUseThreading = false;
        engine = CreateHPLEngine(eHplAPI_OpenGL, eHplSetup_Screen, &variables);
        Check(engine != NULL, "engine initialization failed");
        SDL_Window* window = SDL_GL_GetCurrentWindow();
        Check(window != NULL, "OpenGL window missing");
        SDL_HideWindow(window);

        cResources* resources = engine->GetResources();
        resources->AddResourceDir(_W("."), false);
        resources->AddResourceDir(_W("retail-fonts"), false);
        cFontManager* manager = resources->GetFontManager();
        Check(manager->CreateFontData("InvalidFontTest.ttf", 32) == NULL,
              "malformed TTF with empty required tables was accepted");
        Check(manager->CreateFontData("FontRenderingTest.ttf", 0) == NULL &&
              manager->CreateFontData("FontRenderingTest.ttf", 513) == NULL,
              "invalid font rasterization size was accepted");
        iFontData* font = manager->CreateFontData("FontRenderingTest.ttf", 32);
        Check(font != NULL, "font manager could not load the test TTF");
        Check(manager->CreateFontData("FontRenderingTest.ttf", 32) == font,
              "same TTF and size should reuse the cached font");
        iFontData* smaller = manager->CreateFontData("FontRenderingTest.ttf", 18);
        Check(smaller != NULL && smaller != font, "different TTF sizes need separate font objects");

        const unsigned int codepoints[] = { 'A', 'V', 0x00E9, 0x0395, 0x0416, 0x20AC };
        cGlyph* replacement = font->GetGlyphForCodepoint(0xFFFD);
        Check(replacement != NULL, "font replacement glyph missing");
        for (size_t i = 0; i < sizeof(codepoints) / sizeof(codepoints[0]); ++i)
        {
            cGlyph* glyph = font->GetGlyphForCodepoint(codepoints[i]);
            Check(glyph != NULL && glyph->mpGuiGfx != NULL && glyph != replacement,
                  "expected Unicode glyph missing or replaced");
        }
        const float kern = font->GetKerning('A', 'V');
        Check(kern < 0, "test font AV kerning should tighten the pair");
        const cVector2f size(32, 32);
        const float pairWidth = font->GetLength(size, _W("AV"));
        const float isolatedWidth = font->GetLength(size, _W("A")) + font->GetLength(size, _W("V"));
        Check(pairWidth < isolatedWidth - 0.1f, "text measurement did not apply kerning");
        const tWString astral = cString::UTF8ToWChar("\xF0\x9F\x98\x80");
        const wchar_t* surrogate = astral.c_str();
        Check(DecodeFontCodepoint(surrogate) == 0x1F600 && *surrogate == 0,
              "UTF-16 surrogate pair was not decoded as one codepoint");
        const wchar_t* isolatedSurrogate = _W("\xD800");
        Check(DecodeFontCodepoint(isolatedSurrogate) == 0xFFFD,
              "isolated surrogate was not replaced");
        Check(font->GetGlyphForCodepoint(0x10FFFF) == replacement &&
              font->GetGlyphForCodepoint(0xD800) == replacement,
              "missing or invalid scalar did not use the replacement glyph");
        Check(font->GetGlyphForCodepoint('\n') == NULL && font->GetGlyphForCodepoint('\r') == NULL,
              "line terminator rendered a replacement box");
        tWStringVec rows;
        font->GetWordWrapRows(font->GetLength(size, _W(" ")) * 0.5f, 32, size,
                             _W(" A"), &rows);
        if(rows.size() != 1 || rows[0] != _W("A"))
            std::fprintf(stderr, "wrap rows: %zu, first length: %zu\n", rows.size(),
                         rows.empty() ? 0 : rows[0].size());
        Check(rows.size() == 1 && rows[0] == _W("A"),
              "word wrap emitted a blank row for leading whitespace");
        rows.push_back(_W("existing"));
        font->GetWordWrapRows(font->GetLength(size, _W(" ")) * 0.5f, 32, size,
                             _W(" "), &rows);
        Check(rows.size() == 3 && rows[2].empty(),
              "word wrap did not append a row for whitespace-only text");
        rows.clear();
        const tWString cjk = _W("\u4E00\u4E00\u4E00\u4E00");
        font->GetWordWrapRows(font->GetLength(size, _W("\u4E00")) * 1.5f, 32, size, cjk, &rows);
        tWString joined;
        for(size_t i = 0; i < rows.size(); ++i) joined += rows[i];
        Check(rows.size() == 4 && joined == cjk, "CJK wrapping dropped source characters");
        rows.clear();
        font->GetWordWrapRows(font->GetLength(size, astral.c_str()) * 1.5f, 32, size, astral + astral, &rows);
        Check(rows.size() == 2 && rows[0] == astral && rows[1] == astral,
              "wrapping split a supplementary character");
        rows.clear();
        font->GetWordWrapRows(10000, 32, size, _W("A\r\n\nB\n"), &rows);
        Check(rows.size() == 4 && rows[0] == _W("A") && rows[1].empty() &&
              rows[2] == _W("B") && rows[3].empty(), "explicit line breaks were not preserved");

        iFontData* bitmap = manager->CreateFontData("menu_gui_default.fnt");
        Check(bitmap != NULL, "shipped bitmap font did not load");
        const cVector2f bitmapSize(23,23);
        Check(bitmap->GetKerning('A','V') == 0 &&
              std::fabs(bitmap->GetLength(bitmapSize, _W("AV")) - 26.0f) < 0.001f,
              "shipped bitmap font spacing changed");
        rows.clear();
        bitmap->GetWordWrapRows(60, 23, bitmapSize, _W("AV AV AV"), &rows);
        Check(rows.size() == 2 && rows[0] == _W("AV AV") && rows[1] == _W("AV"),
              "bitmap font word boundaries changed");
        rows.clear();
        bitmap->GetWordWrapRows(26, 23, bitmapSize, _W("AVAVAV"), &rows);
        Check(rows.size() == 1 && rows[0] == _W("AVAVAV"), "bitmap wrapping dropped an overlong word");

        cGuiSet* gui = engine->GetGui()->CreateSet("FontTest", NULL);
        gui->SetDrawMouse(false);
        iLowLevelGraphics* graphics = engine->GetGraphics()->GetLowLevel();
        graphics->SetCurrentFrameBuffer(NULL);
        graphics->SetClearColor(cColor(0, 0, 0, 1));
        graphics->ClearFrameBuffer(eClearFrameBufferFlag_Color);
        gui->DrawFont(_W("AVATAR  To Wa Yo"), font, cVector3f(30, 35, 0), size, cColor(1, 1));
        gui->DrawFont(_W("café naïve € — Ελληνικά"), font, cVector3f(30, 105, 0), size, cColor(1, 1));
        gui->DrawFont(_W("Привет мир  Zażółć gęślą jaźń"), font,
                      cVector3f(30, 175, 0), size, cColor(1, 1));
        gui->DrawFont(_W("Small: AV café Ελληνικά Привет"), smaller,
                      cVector3f(30, 255, 0), cVector2f(18, 18), cColor(1, 1));
        gui->DrawFont(_W("Bitmap: AV To Wa"), bitmap,
                      cVector3f(30, 295, 0), bitmapSize, cColor(1, 1));
        gui->Render(NULL);
        Check(glGetError() == GL_NO_ERROR, "OpenGL error while rendering font");
        Check(CountBrightPixels(30, 85, 900, 330) > 150, "Latin line did not render");
        Check(CountBrightPixels(100, 155, 900, 330) > 150, "Greek and accented line did not render");
        Check(CountBrightPixels(170, 225, 900, 330) > 150, "Cyrillic line did not render");
        Check(CountBrightPixels(250, 295, 900, 330) > 80, "smaller font size did not render");
        Check(CountBrightPixels(295, 330, 900, 330) > 80, "bitmap font did not render");
        SaveBackBuffer("font-rendering.bmp", 900, 330);
        std::printf("PASS: Unicode conversion, wrapping, bitmap compatibility, invalid-font rejection, caching, and rendering; TTF AV kerning %g vs %g pixels.\n",
                    pairWidth, isolatedWidth);
        DestroyHPLEngine(engine);
        SDL_Quit();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        if (engine) DestroyHPLEngine(engine);
        SDL_Quit();
        return 1;
    }
}
