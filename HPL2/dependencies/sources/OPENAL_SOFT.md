# OpenAL Soft

The upstream files in `openal-soft` are taken unchanged from the official
OpenAL Soft 1.25.2 release (`b2c48f7718ef3fcf67921a8b6534c4914e328970`).

`openal-soft/OpenALSoft.vcxproj` is the Amnesia solution bridge. It invokes the
upstream CMake build for a static OpenAL library with WASAPI and embedded HRTF
data, then places `OpenAL32.lib` in the active Visual Studio output directory.
The utilities, examples, tests, install targets, and legacy Windows backends are
disabled because they are not part of the game runtime.

To update the dependency, replace the upstream tree while retaining
`OpenALSoft.vcxproj`, then update the release and commit recorded above.
