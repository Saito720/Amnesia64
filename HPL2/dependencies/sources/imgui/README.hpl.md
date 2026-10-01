# Dear ImGui

Vendored from https://github.com/ocornut/imgui, tag **v1.91.9b**.
The MIT license is in `LICENSE.txt`. Only core files and the SDL2/OpenGL
backends needed by the multiplayer overlay are compiled.

Amnesia uses the SDL2 platform backend and OpenGL3 renderer with GLSL 1.20,
which works with HPL2's OpenGL compatibility context and preserves shader
and vertex-buffer state around the overlay. The engine owns event polling
and buffer swaps; the game registers callbacks instead of polling SDL a
second time or coupling HPL2 to Amnesia classes.

The local `imconfig.h` enables `IMGUI_USE_WCHAR32` so chat editing preserves
supplementary Unicode characters in UTF-8. Keep this setting consistent in
the game and test harnesses when updating ImGui. Font coverage still controls
which characters can be displayed.

`InputText` also has an opt-in `ImGuiInputTextFlags_CallbackBeforeEdit` extension.
It calls the widget callback after activation and before mouse hit-testing,
queued characters, cut/paste or other keyboard edits. Like `CallbackAlways`,
it exposes UTF-8 byte cursor/selection positions and supports buffer edits with
undo reconciliation. A nonzero return consumes native mouse/focus selection
and preserves queued text on refocus. Multiplayer chat uses this to hit-test
color emoji/shortcode cells and insert picker choices before subsequent typing;
its restored activation can also honor queued clipboard/undo shortcuts before
the normal shortcut routing catches up on the next frame.
Widgets without the flag retain upstream behavior. Preserve this hook when
updating the vendored ImGui sources.
