# Window resize smoke test

Build the solution in **Debug / x64**, then run from the repository root:

```powershell
./HPL2/tests/WindowResize/build.ps1
```

The script finds the installed Visual Studio C++ tools and Windows SDK, builds a standalone native test against the solution's libraries, and runs it with a hidden console. Output and engine logs go to `bld/window-resize-test/x64`. No game assets, profiles, saves, or configuration files are loaded. SDL windows are hidden as soon as they are initialized; they may appear briefly during creation.

The test checks native window resize support, new screen and viewport dimensions after wider/taller/smaller/larger resizes, the minimum size, actual rendered pixels at all four corners and quadrant centers, framebuffer copy dimensions, OpenGL errors, a new launch returning to its requested resolution, and the `0,0` desktop-resolution case remaining windowed. It also checks that unchanged dimensions do not repeatedly request resource rebuilds.

Use `-BuildOnly` to compile without running, or `-Architecture x86` after building Debug / Win32. `-Fullscreen` additionally opens and closes a desktop fullscreen test window; this case is optional because it briefly changes focus and fullscreen state.

For the full renderer and resource lifecycle test, use:

```powershell
./HPL2/tests/WindowResize/build.ps1 -Engine -GameDirectory 'D:/path/to/Amnesia The Dark Descent'
```

The game directory defaults to `ATDD_DIR` if set. This copies only the game's `core` assets into the test output directory, then initializes an engine and an empty world. It checks G-buffer and depth dimensions, actual GPU texture/renderbuffer storage, full/quarter-size temporary buffer storage and stable borrowed pointers/texture handles, fixed-size targets, camera aspect, GUI virtual coordinates and mouse normalization, framebuffer completeness, and a rendered frame with SSAO, edge smoothing, bloom, and image trail after each resize, including odd dimensions. A known quadrant image also runs through bloom and image trail; output pixels at corners and centers catch stale texture coordinates or cropping. Logs and shader caches remain inside the test output directory. No game configuration is opened or changed.

## Full game verification

The native tests cover the low-level backend and, with `-Engine`, engine rendering resources. Verify game-specific screens and persistence with a normal game session:

1. Launch with fullscreen disabled and a fixed configured resolution. Resize wider, taller, smaller, and larger, then maximize and restore. Confirm the scene renders at the new resolution and that menu controls remain under the pointer.
2. Repeat from the main menu, a loaded map, the pause menu, inventory, journal, and an open options screen. Exercise effects such as blur, bloom, fog, reflections, and sanity effects; check for black regions, stale image sizes, and graphics errors.
3. Minimize and restore during a loaded map. Confirm rendering resumes without allocating zero-sized render targets or changing the configured resolution.
4. Quit normally and inspect the configured width and height. They should retain their launch values. Relaunch and confirm the window starts at those values. Repeat with width and height set to `0,0`: launch uses the monitor resolution while remaining windowed, and saving keeps `0,0`.
5. Change resolution deliberately through the graphics options, then quit and relaunch. Confirm that explicit options changes still persist. Launch fullscreen and confirm its existing behavior is unchanged.
