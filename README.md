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
