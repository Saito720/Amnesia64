# Font rendering integration test

Run from the repository root (Visual Studio C++ tools and a Windows SDK are required):

```powershell
./HPL2/tests/FontRendering/build.ps1 -FontPath 'C:/Windows/Fonts/arial.ttf'
```

Set `ATDD_DIR` to an installed Amnesia directory, or pass `-GameDirectory`.
Use a font with Latin, Greek, Cyrillic, U+FFFD, and AV kerning. The script builds
the engine and its dependencies in Debug/x64 with a consistent output directory.
Use `-SkipBuild` only after a current build with `SolutionDir` set to the repository root.
The test stages the installed game's `core` directory, English bitmap fonts, and supplied TTF
under `bld/font-rendering-test/x64`. It does not write to the installed game or
include the font in any build. The hidden engine window renders through the real
font manager, image atlas, GUI, and OpenGL path. The test checks glyph creation,
UTF-8/UTF-16 conversion, wrapping, TTF kerning, bitmap font compatibility,
rejection of malformed font headers, font caching by size, and rendered pixels. Inspect
`bld/font-rendering-test/x64/font-rendering.bmp` for the visual result.

To render a TTF inside the live game menu using an isolated test profile:

```powershell
./tests/multiplayer/run-game.ps1 -FontOnly -FontPath 'C:/Windows/Fonts/arial.ttf' -Backend Steamworks -RetailDirectory 'D:/path/to/Amnesia The Dark Descent'
```

The game test also checks supplementary-character input, deletion, selection,
length limits, scrolling, kerned text-box clipping, and gamepad hint placement.
It writes `font-in-game.png` (including a selected, kerned text sample)
in its reported test artifact directory.
It loads the temporary font from its original location and leaves the installed
game files untouched.

TrueType support rasterizes Unicode glyphs and applies pair kerning. It does not
provide complex-script shaping, bidirectional layout, or fallback across font
families. Font assets should come from trusted sources: the bundled stb_truetype
rasterizer is not a complete validator for hostile font data.
