# Font rendering integration test

Build the solution in Debug/x64, then run from the repository root:

```powershell
./HPL2/tests/FontRendering/build.ps1 -FontPath 'C:/Windows/Fonts/arial.ttf'
```

Set `ATDD_DIR` to an installed Amnesia directory, or pass `-GameDirectory`.
Use a font with Latin, Greek, Cyrillic, U+FFFD, and AV kerning. The test stages
the installed game's small `core` directory and the supplied font
under `bld/font-rendering-test/x64`. It does not write to the installed game or
include the font in any build. The hidden engine window renders through the real
font manager, image atlas, GUI, and OpenGL path. The test checks glyph creation,
Unicode decoding, kerning, font caching by size, and rendered pixels. Inspect
`bld/font-rendering-test/x64/font-rendering.bmp` for the visual result.

To render a TTF inside the live game menu using an isolated test profile:

```powershell
./tests/multiplayer/run-game.ps1 -FontOnly -FontPath 'C:/Windows/Fonts/arial.ttf' -Backend Steamworks -RetailDirectory 'D:/path/to/Amnesia The Dark Descent'
```

The game test writes `font-in-game.png` in its reported test artifact directory.
It loads the temporary font from its original location and leaves the installed
game files untouched.
