# Dear ImGui

Vendored unmodified from https://github.com/ocornut/imgui, tag **v1.91.9b**.
The MIT license is in `LICENSE.txt`. Only core files and the SDL2/OpenGL
backends needed by the multiplayer overlay are compiled.

Amnesia uses the SDL2 platform backend and OpenGL3 renderer with GLSL 1.20,
which works with HPL2's OpenGL compatibility context and preserves shader
and vertex-buffer state around the overlay. The engine owns event polling
and buffer swaps; the game registers callbacks instead of polling SDL a
second time or coupling HPL2 to Amnesia classes.
