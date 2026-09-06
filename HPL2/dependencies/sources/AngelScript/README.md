# AngelScript 2.24.1

The engine source and public header in this directory come from
`HPL2/dependencies.zip` in the Frictional Games
[Amnesia: A Machine for Pigs source release](https://github.com/FrictionalGames/AmnesiaAMachineForPigs).
This is the AngelScript version used by AMFP's standard string, array, and script
helper integrations in `HPL2/core`.

The existing Visual Studio project, static library output, and Debug/Release
configurations for Win32 and x64 are retained. The project compiles the 2.24.1
source files and assembles that release's native x64 call bridge for x64 only.
The former built-in array implementation was removed upstream; AMFP registers
the array add-on from `HPL2/core/sources/impl/scriptarray.cpp` instead.

The public header has one local compatibility fix: its 32-bit MSVC virtual
inheritance check copies the method-pointer representation as bytes instead of
using an invalid `static_cast` between unrelated pointer types. The source files
retain their upstream license notices.
