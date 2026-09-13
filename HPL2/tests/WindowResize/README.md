# Window resize smoke test

Build the solution in **Debug / x64**, then run from the repository root:

```powershell
./HPL2/tests/WindowResize/build.ps1
```

The script finds the installed Visual Studio C++ tools and Windows SDK, builds a standalone native test against the solution's libraries, and runs it with a hidden console. Output and engine logs go to `bld/window-resize-test/x64`. Without `-Engine`, no game assets, profiles, saves, or configuration files are loaded. SDL windows are hidden as soon as they are initialized; they may appear briefly during creation.

The test checks native window resize support, new screen and viewport dimensions after wider/taller/smaller/larger resizes, the minimum size, actual rendered pixels at all four corners and quadrant centers, framebuffer copy dimensions, OpenGL errors, a newly initialized graphics backend using its requested resolution, and the `0,0` desktop-resolution case remaining windowed. It also checks that unchanged dimensions do not repeatedly request resource rebuilds. The backend reinitialization case does not load or save game configuration.

On Windows, the hidden-window suite also sends native sizing messages to its own window without synthesizing physical mouse or keyboard input. It checks the border-click entry path, all eight resize edges, opposite-edge anchoring, minimum/maximum client sizes, keyboard sizing, Escape restoration, and cancellation on capture or focus loss. It checks that sampled resize presses and motion do not reach SDL gameplay input, that requested mouse modes can change during sizing, and that the key ending sizing remains usable after focus loss without a key release. The outer event loop must continue while sizing remains active. A watchdog cancels an unexpected native modal loop so a regression fails instead of hanging.

Use `-BuildOnly` to compile without running, or `-Architecture x86` after building Debug / Win32. `-Fullscreen` additionally opens and closes a desktop fullscreen test window and, on Windows, checks native maximize/minimize/restore and sizing rejection in those states. These cases are optional because they briefly change focus and window/fullscreen state.

For the full renderer and resource lifecycle test, use:

```powershell
./HPL2/tests/WindowResize/build.ps1 -Engine -GameDirectory 'D:/path/to/Amnesia The Dark Descent'
```

The game directory defaults to `ATDD_DIR` if set. This copies only the game's `core` assets into the test output directory, then initializes an engine and an empty world. It checks G-buffer and depth dimensions, actual GPU texture/renderbuffer storage, full/quarter-size temporary buffer storage and stable borrowed pointers/texture handles, fixed-size targets, camera aspect, GUI virtual coordinates and mouse normalization, and framebuffer completeness. It renders an empty-world frame with SSAO, edge smoothing, bloom, and image trail enabled after each resize, including odd dimensions. A known quadrant image also runs through bloom and image trail; output pixels at corners and centers catch stale texture coordinates or cropping. Logs and shader caches remain inside the test output directory. No game configuration is opened or changed.

On September 12, 2026, the Windows x64 suite passed **890 assertions** with both `-Engine` and `-Fullscreen`, using the Steamworks-enabled Debug engine libraries. This count includes repeated geometry, resource, and pixel assertions across resolutions; it is not a count of independent game scenarios. The recorded run is `bld/window-resize-fixed-engine-tests.log`.

## Combined game and multiplayer regression

The [multiplayer game harness](../../../tests/multiplayer/README.md) can also exercise resizing through the real game's outer engine loop. Enable its additional cases with:

```powershell
$env:CODEX_MP_RESIZE='1'
try {
    ./tests/multiplayer/run-game.ps1 -Backend Steamworks -RetailDirectory 'D:/path/to/Amnesia The Dark Descent'
}
finally { Remove-Item Env:CODEX_MP_RESIZE -ErrorAction SilentlyContinue }
```

This is a separate suite using retail game assets and two local game instances. It checks generic popup layout, shrinks the main menu from 1920x1080 to 640x480 while a confirmation remains open, and verifies its controls remain visible without losing modal focus. It validates the game's outline framebuffer dimensions, completeness, and borrowed depth/accumulation attachments after resolution changes. On Windows, each peer holds a native sizing operation open for two seconds; gameplay updates, outgoing pose sequences, and incoming remote poses must continue. These checks then proceed into the normal multiplayer regression covering menus, death recovery, map transitions, and other session behavior. The sizing hold verifies continued updates; it does not drag every menu or render every game effect at every resolution.

Follow the harness README for prerequisites, backend selection, build reuse, output locations, and profile isolation. Neither these local transport checks nor the standalone engine suite establishes cross-account Steam relay behavior.

## Full game verification

The native and combined suites cover the cases described above. Verify the remaining visual behavior, native desktop interaction, and configuration persistence with a normal game session:

1. Launch with fullscreen disabled and a fixed configured resolution. Resize wider, taller, smaller, and larger, then maximize and restore. Confirm the scene renders at the new resolution and that menu controls remain under the pointer.
2. Repeat from the main menu, a loaded map, the pause menu, inventory, journal, and an open options screen. Exercise effects such as blur, bloom, fog, reflections, and sanity effects; check for black regions, stale image sizes, and graphics errors.
3. Minimize and restore during a loaded map. Confirm rendering resumes without allocating zero-sized render targets or changing the configured resolution.
4. Quit normally and inspect the configured width and height. They should retain their launch values. Relaunch and confirm the window starts at those values. Repeat with width and height set to `0,0`: launch uses the monitor resolution while remaining windowed, and saving keeps `0,0`.
5. Change resolution deliberately through the graphics options, then quit and relaunch. Confirm that explicit options changes still persist. Launch fullscreen and confirm its existing behavior is unchanged.
