# Embedded llama.cpp backend

This bridge embeds the CPU-only `llama` and `mtmd` libraries in HPL2. Upstream is pinned to [50569eb87df530daff11afda229ceb9ab8e6cae8](https://github.com/ggml-org/llama.cpp/commit/50569eb87df530daff11afda229ceb9ab8e6cae8). The selected upstream sources under `../sources/llama.cpp` are unchanged; `UPSTREAM.json` records the selection. Upstream's MIT license and bundled third-party licenses are included.

The integration is disabled by default. Enable it for the Windows solution with:

```powershell
msbuild Amnesia.sln /m /p:Configuration=Release /p:Platform=x64 /p:HPL2WithLlama=true
```

The bridge uses the same Visual Studio 2026 / v145 toolchain as the existing solution. It builds six static libraries and merges them into `bld/llama.cpp/v145/x64/lib/Release/HPL2Llama.lib`; HPL2's librarian incorporates this archive, so the game and editor project references carry the dependency. Debug builds use the corresponding Debug archive and runtime. Win32 builds retain the disabled service; enabling the backend on Win32 fails with a clear error.

For the legacy HPL2 CMake build, enable `-DHPL2_WITH_LLAMA=ON`. A standalone CMake consumer can `add_subdirectory` this directory and link `hpl2_llama_backend`, which exports `HPL2_WITH_LLAMA=1` and the upstream public headers. A 64-bit compiler and CMake 3.14 or newer are required.

The backend is self-contained: no server, CLI, common library, external OpenMP runtime, HTTPS support, subprocess/video support, dynamic backend loading, or model downloads are built. CPU instructions beyond the x64 baseline are disabled explicitly. This favors compatibility over throughput; profiling and an accelerated backend can be added separately. Model loading and inference are opt-in at runtime through the HPL2 service. Qwen3-VL's model implementations are included in both the language and multimodal libraries. A compatible language GGUF and its matching mmproj GGUF must be supplied separately.

To update the dependency, check out the desired immutable commit from the official repository into a temporary directory. Replace only the paths listed in `UPSTREAM.json`, retaining their upstream bytes and licenses, then update the commit in `UPSTREAM.json` and this CMake wrapper. Do not copy model weights, tests, media, example programs, or the upstream Git directory. Review the upstream CMake target names and public llama/mtmd APIs, then run the standalone service tests with the backend enabled and disabled and build the x64 solution. The six libraries in the MSVC merge must be updated if upstream's static dependencies change.

Validation: x64 Release and Debug backend bridges build successfully, the disabled bridge performs no compilation, and enabling Win32 produces the intended platform error. All 1,761 vendored files match their upstream originals. The linked Release game and LevelEditor import no llama, mtmd, ggml, or OpenMP DLLs. Model weights are not included, so actual Qwen3-VL inference requires a separately supplied model pair.
