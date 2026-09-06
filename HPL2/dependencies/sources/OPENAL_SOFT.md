# OpenAL Soft

The upstream files in `openal-soft` are taken unchanged from the official
OpenAL Soft 1.25.2 release (`b2c48f7718ef3fcf67921a8b6534c4914e328970`).

`openal-soft/OpenALSoft.vcxproj` is the Amnesia solution bridge. It invokes the
upstream CMake build for a static OpenAL library with WASAPI, EAX, and embedded
HRTF data. WASAPI is required at configuration time; other audio backends,
utilities, examples, tests, install targets, and C++ modules are disabled.
The null and loopback backends supplied by upstream remain available.

## Building

Use Visual Studio 2026 with the Desktop development with C++ workload, the v145
toolset, a Windows 10 or 11 SDK, and C++ CMake tools for Windows. OpenAL Soft
requires C++20. The bridge uses Visual Studio's bundled CMake when available,
otherwise `cmake.exe` from `PATH`. An explicit MSBuild `CMakeExecutable` property
overrides that selection. CMake must support the Visual Studio 18 2026 generator.

Build `Amnesia.sln` in any of its four configurations: Debug or Release for x64
or x86 (called Win32 inside the projects). The HPL2 project dependency builds
OpenAL Soft automatically using the same Visual Studio installation and toolset.
No network access or prebuilt OpenAL library is needed.

CMake intermediates are stored under `bld/openal-soft/v145/<platform>`.
The resulting `OpenAL32.lib` is copied to `x64/Debug` or `x64/Release` for x64,
and `Debug` or `Release` for Win32. These generated outputs are ignored by Git.
Cleaning and rebuilding the solution also clean and rebuild OpenAL Soft for
the selected configuration.

HPL2 and freealut use the vendored OpenAL headers and define
`AL_LIBTYPE_STATIC`. The executables link the generated library and the Windows
WASAPI support library, `avrt.lib`, without requiring `OpenAL32.dll`.

## Updating

To update the dependency, replace the upstream tree while retaining
`OpenALSoft.vcxproj`, then update the release and commit recorded above.
Include upstream's bundled `fmt` and `gsl` sources, HRTF data, and license files.
Verify all four OpenAL configurations and the game/editor solution builds.
