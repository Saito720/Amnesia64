# Enemy_Llama native editor integration tests

These tests link the current x64 Release ModelEditor object files and engine
libraries. Build ModelEditor first, then configure this standalone consumer:

```powershell
cmake -S HPL2/tests/editor-llama -B bld/llama-editor-tests -G "Visual Studio 18 2026" -A x64
cmake --build bld/llama-editor-tests --config Release
ctest --test-dir bld/llama-editor-tests -C Release --output-on-failure
```

`AMNESIA_DATA_DIRECTORY` defaults to `ATDD_DIR`. Set it explicitly if needed.
`MODEL_EDITOR_OBJECT_DIRECTORY` and `HPL2_LIBRARY_DIRECTORY` can point to another
matching Release build. Rebuild the harness after rebuilding editor objects.

A working graphics driver, installed editor assets, and the four source `.ent`
files listed in `amnesia/doc/Enemy_Llama.md` are required. Core/editor
assets are copied to the build's scratch directory; other retail assets are read
through absolute resource paths. Mesh cache writes are disabled. Configs, logs,
thumbnails and the saved `.ent` stay in scratch. The standard personal `Amnesia`
directory must already exist, because the production directory constructor checks
it before the test redirects the editor home. The test window is created offscreen
and hidden immediately.

The test initializes the real ModelEditor and settings popup, checks all 69 grouped
fields for each of four rigs, exercises dropdown/input/preset callbacks and stale
input rejection, and verifies exact settings and animation undo/redo through the
same action used by animation dialog confirmation. It checks horizontal FOV
defaults, editing and undo/redo, preservation through a rig preset, and entity
save/load. The actual animation popup also follows global undo/redo of committed
clip lists, synchronizes confirmation before the next editor frame, and preserves
its pending draft across unrelated settings changes. It compares all
stock clip paths, playback settings and events with the source entities, loads
available Grunt/Brute/Suitor clips through the engine's AnimationManager,
saves/reopens a rigged entity under scratch `entities/enemy/servant_brute`,
and checks that an existing custom Enemy_Llama schema remains authoritative.
The referenced ManPig clip is absent from the supplied data, so its saved animation
record is checked without loading that asset. Retail configs are not modified.
