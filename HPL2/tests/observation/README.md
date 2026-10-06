# Scene observation rendering tests

These tests consume a prebuilt x64 Release `HPL2.lib` and its matching dependency
libraries. They initialize a real SDL/OpenGL context and Newton character body,
but generate every material, texture, mesh, and shader in memory. Retail game
assets and model files are unnecessary. The test window is hidden immediately
after context creation; a functioning graphics driver and desktop are required.

From the repository root, after building the Release engine:

```powershell
cmake -S HPL2/tests/observation -B bld/observation-tests -G "Visual Studio 18 2026" -A x64
cmake --build bld/observation-tests --config Release
ctest --test-dir bld/observation-tests -C Release --output-on-failure
```

Use `-DHPL2_LIBRARY_DIRECTORY=...` to consume a different matching library
directory. Optional CUDA runtime DLLs already deployed beside that engine are
copied automatically; this test does not call inference or load a model.

The tests check visible and occluded player/door masks, view-specific self mesh
exclusion, camera FOV, top-down packed RGB orientation, exact preview texture/RGB
agreement, opaque preview alpha, independent retained depth, framebuffer
restoration, repeated captures, and GPU resource destruction/recreation.
