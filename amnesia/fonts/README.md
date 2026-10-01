# Multiplayer chat font

Noto Sans Regular is distributed with the game for readable chat text. Both
Visual Studio and CMake copy the font and its license to `fonts/` beside the
executable (inside `Contents/Resources` for a macOS bundle). Keep that directory
when packaging the game. No system font or runtime download is required. The chat falls back to ImGui's built-in font if
the packaged font is absent.

Chat rasterizes the font at its displayed pixel size and the window's framebuffer
density. A resize or display-density change rebuilds the atlas between frames
without replacing the font objects or the active text editor state.

Source: [Noto fonts](https://github.com/notofonts/noto-fonts), commit
`ffebf8c1ee449e544955a7e813c54f9b73848eac`,
`hinted/ttf/NotoSans/NotoSans-Regular.ttf`.

SHA-256: `b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5`.

The unmodified font is licensed under the SIL Open Font License 1.1; see
[OFL.txt](OFL.txt). It contains Latin, Greek, and Cyrillic glyphs. Characters
outside this font's coverage use a replacement glyph; chat does not provide
complex-script shaping or bidirectional layout. Supported emoji use the bundled
[Twemoji artwork](twemoji/README.md) in messages and the entry field.
