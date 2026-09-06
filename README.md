# Amnesia: A Machine for Pigs — Windows source port

This branch brings the AMFP game and HPL2.5 engine sources into the existing
Amnesia 64 Visual Studio solution, with both 32-bit and 64-bit builds.

## Key changes:
- Can be compiled in both 32-bit and 64-bit modes using Visual Studio 2026 with the v145 build tools.
- Single solution file for all projects (main game, HPL2, dependencies and editors). No need to compile the engine separately.
- Produces self-contained .exe files without dependency on 3rd party dlls (this prevents cluttering user's game folder with 64-bit dlls).
- Some libraries were changed, most notably:
	- SDL2 was upgraded from 2.0.4 to 2.0.12
	- alut was replaced with freealut
	- OpenAL Soft 1.25.2 is built from its bundled source and linked statically.
	- AngelScript uses AMFP's bundled 2.24.1 release for its string, array, and scripting APIs.
	- Newton Dynamics was upgraded from 2.08 to 2.32 (I simply couldn't find the source code for 2.08)
	- FBX import uses bundled [ufbx 0.23.0](https://github.com/ufbx/ufbx/tree/v0.23.0), compiled into the engine. It does not require the original Autodesk SDK.

## Building on Windows

Install Visual Studio 2026 with Desktop development with C++, the v145 toolset,
a Windows 10 or 11 SDK, and C++ CMake tools for Windows. A CMake version supporting
Visual Studio 18 2026 on `PATH` can also be used.

Open `Amnesia.sln` and build Debug or Release for x64 or x86. OpenAL Soft builds
automatically with the engine and needs no separate download or prebuilt library.
Executables and libraries are written to `x64/Debug` or `x64/Release` for x64,
and `Debug` or `Release` for x86.

Set the `AMFP_DIR` environment variable to your installed AMFP game directory
(the directory containing the game's `config`, `maps`, and other asset folders),
then restart Visual Studio so it sees the variable. The game and editor debugger
working directories use `$(AMFP_DIR)`. The game executable remains `Lux.exe`.
Game assets are supplied by your AMFP installation.

The AMFP sources come from the `AmnesiaAMachineForPigs-master` source archive.
The existing SDL2, OpenAL Soft, Newton, and other dependency projects are retained;
AngelScript is updated for the AMFP scripting APIs, and ufbx replaces the disabled
FBX importer. The fork uses a separate map-cache version because the retained
Newton library needs compatible physics data. Retail caches are rebuilt when
loaded; saving the new cache follows the game's `ForceCacheLoadingAndSkipSaving`
setting.

See [OpenAL Soft build and update details](HPL2/dependencies/sources/OPENAL_SOFT.md).
See [FBX dependency details](HPL2/dependencies/sources/ufbx/README.md).
