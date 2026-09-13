# Editor resize regression tests

Build `Amnesia.sln` in Debug x64, then run:

```powershell
./tests/editors/run-resize.ps1 -RetailDirectory 'D:/Steam/steamapps/common/Amnesia The Dark Descent'
./tests/editors/run-viewers.ps1 -RetailDirectory 'D:/Steam/steamapps/common/Amnesia The Dark Descent'
```

The harness links each editor's production objects and creates a hidden SDL/OpenGL
window with retail resources. It repeatedly grows and shrinks the native window,
including odd dimensions, broadcasts the engine resize notification, and renders
the result. It checks framebuffer completeness, color/depth dimensions, camera
aspect, document/viewport identity, and enlarged-viewport state in Model, Level,
Particle, and Material Editor. Logs and a rendered screenshot for each editor are
written under `bld/editor-resize-tests`.

The editor harness also posts SDL quit events and checks confirmation, cancel,
repeat-close suppression, and the existing unsaved-document flow for editors with
a document world. Material Editor retains its existing confirmation-only behavior.
The viewer harness compiles ModelView and MapView's production implementation and
checks the top/right menu margins after native resizes and exit on an SDL quit
event. Its output is under `bld/viewer-window-tests`.

Editor preferences and recent files are isolated in the test output. The harness
exits without invoking editor shutdown so it does not persist editor settings.
