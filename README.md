# Amnesia 64
64-bit Windows port of Amnesia: The Dark Descent

## Key changes:
- Experimental Steam multiplayer using Steamworks 1.65 lobbies and Steam Datagram Relay, with campaign host/join and a global tilde-key ImGui window. Direct IP remains available. See [multiplayer status, controls and tests](MULTIPLAYER.md).
- Windowed mode supports resizing and maximizing, with the game rendering at the current window resolution. Resizing never changes the configured launch resolution; `0,0` uses the monitor's desktop resolution and respects the fullscreen setting.
- Can be compiled in both 32-bit and 64-bit modes using Visual Studio 2026 with the v145 build tools.
- Single solution file for all projects (main game, HPL2, dependencies and editors). No need to compile the engine separately.
- Engine dependencies are linked statically. Steam-enabled builds additionally ship Valve's `steam_api64.dll` (`steam_api.dll` for 32-bit builds) beside the executable.
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

Steamworks SDK 1.65 headers and redistributables are included under
`HPL2/dependencies/steamworks/sdk`; the original ZIP is not needed to build.
The default build uses the AppID in `steam_appid.txt`
(currently **1362050**) and deploys its Steam runtime beside the executable.
See [Steamworks build and development setup](HPL2/dependencies/steamworks/README.md).

Dear ImGui and the standalone GameNetworkingSockets/protobuf sources are also
included. Build with `/p:HplUseSteamworks=false` for the account-free direct-IP
backend. A build selects one networking backend; the two libraries are not linked
together. No build-time download is required. See [networking dependency details](HPL2/dependencies/networking/README.md).

See [window resizing verification](HPL2/tests/WindowResize/README.md) for the native rendering tests and game verification steps.
