Build the x64 Release game first. This test links the same game objects and engine
libraries, and needs an installed game's read-only `core`, GUI resources and
configuration files. It creates its own synthetic map, character cylinders,
meshes and animation clip; no authored enemy or map is changed.

```powershell
cmake -S HPL2/tests/enemy-llama -B bld/enemy-llama-runtime-tests -G "Visual Studio 18 2026" -A x64 -DAMNESIA_DATA_DIRECTORY="D:/Steam/steamapps/common/Amnesia The Dark Descent"
cmake --build bld/enemy-llama-runtime-tests --config Release
ctest --test-dir bld/enemy-llama-runtime-tests -C Release --output-on-failure
```

The test covers deterministic controller ownership, inactive/disabled/dead owner
handoff, rejected commands for extra enemies, raw entity FOV loading (default,
custom, clamped and nonfinite values), actual horizontal camera projections at
square, wide and portrait image sizes, actual RGB capture, sound event
quantization and expiry, capture-consistent sound snapshots, coalescing across
turns and queue ordering, immediate-turn sound/camera alignment, custom camera
offsets, manual input timeout, gravity, single-clip idle playback,
death animation reuse, and restoration through the native save-data objects.
Save coverage exercises the in-memory snapshot/restore methods, rather than a
complete game save file. Rigged assets, authored maps and visual animation quality
still need in-game validation.

Generated resources, logs and test output stay in the build directory's `scratch`
folder. The executable rejects scratch directories within the retail asset tree.
Resource cache writing is disabled for the test.
