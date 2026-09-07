# Amnesia 64
64-bit Windows port of Amnesia: The Dark Descent

## Key changes:
- Can be compiled in both 32-bit and 64-bit modes using Visual Studio 2026 with the v145 build tools.
- Single solution file for all projects (main game, HPL2, dependencies and editors). No need to compile the engine separately.
- Produces self-contained .exe files without dependency on 3rd party dlls (this prevents cluttering user's game folder with 64-bit dlls).
- Some libraries were changed, most notably:
	- SDL2 was upgraded from 2.0.4 to 2.0.12
	- alut was replaced with freealut
	- OpenAL Soft 1.25.2 is built from its bundled source and linked statically.
	- Newton Dynamics was upgraded from 2.08 to 2.32 (I simply couldn't find the source code for 2.08)
	- Fbx support is temporarily removed (I'm planning to re-implement it using OpenFBX)

## Building on Windows

Install Visual Studio 2026 with Desktop development with C++, the v145 toolset,
a Windows 10 or 11 SDK, and C++ CMake tools for Windows. A CMake version supporting
Visual Studio 18 2026 on `PATH` can also be used.

Open `Amnesia.sln` and build Debug or Release for x64 or x86. OpenAL Soft builds
automatically with the engine and needs no separate download or prebuilt library.
Executables and libraries are written to `x64/Debug` or `x64/Release` for x64,
and `Debug` or `Release` for x86.

See [OpenAL Soft build and update details](HPL2/dependencies/sources/OPENAL_SOFT.md).

## In-game entity spawning

While a map is loaded:

| Control | Action |
| --- | --- |
| Hold **X** | Show the spawn menu and release the mouse cursor. Releasing X closes it. |
| Left click a thumbnail | Spawn that entity at the centre of the camera's aim. |
| **Z** | Remove the newest surviving entity you spawned. Press again to work backwards. |
| **V** | Toggle the existing free camera. Spawning also works from this camera. |
| **B** | Toggle the crosshair (moved from X; old X crosshair bindings migrate automatically). |

The menu has one **Entities** tab, a directory tree, and a scrolling thumbnail grid.
It recursively indexes `.ent` files under `entities/`, including an active custom
story's `entities/` folder. Selecting a folder also shows its descendants;
double-click a folder to expand or collapse it. **Refresh** picks up added files.
Thumbnails use the engine's mesh renderer and a bounded in-memory cache. Entities
without a usable preview still appear with their filenames.

Aim at a surface before holding X. The game continues running, and movement remains
available while browsing; mouse look and object interaction are withheld until the
menu closes and its mouse buttons have been released. Escape dismisses the overlay.
The menu also closes on focus loss, map changes, and other game menus.

Placement follows Garry's Mod's sandbox spawning conventions: a 2,048 Source-unit
aim trace (about 52.02 metres in HPL), orientation relative to the camera, bounds
offset against the hit surface, and corrections for nearby obstacles. HPL also
checks a conservative bounding hull and refuses a spawn when there is insufficient
room. Static entities receive normal game ownership so undo removes their meshes,
bodies, lights, and other owned components as well.

Spawned entities and undo history are local to the current map session; they are
not stored in campaign saves. Entries destroyed or collected by gameplay are
skipped by undo. All entity files are listed, including enemies and objects normally
configured by level scripts; their existing game behavior still applies.
