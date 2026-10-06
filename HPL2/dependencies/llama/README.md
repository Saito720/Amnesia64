# Embedded llama.cpp backend

This bridge embeds `llama` and `mtmd` libraries in HPL2, with CPU support and optional NVIDIA CUDA acceleration. Upstream is pinned to [50569eb87df530daff11afda229ceb9ab8e6cae8](https://github.com/ggml-org/llama.cpp/commit/50569eb87df530daff11afda229ceb9ab8e6cae8). The selected upstream sources under `../sources/llama.cpp` are unchanged; `UPSTREAM.json` records the selection. Upstream's MIT license and bundled third-party licenses are included.

The integration is disabled by default. Enable it for the Windows solution with:

```powershell
msbuild Amnesia.sln /m /p:Configuration=Release /p:Platform=x64 /p:HPL2WithLlama=true
```

The bridge uses the same Visual Studio 2026 / v145 toolchain as the existing solution. CPU builds merge six static libraries into `bld/llama.cpp/v145/x64/CPU/lib/Release/HPL2Llama.lib`; HPL2's librarian incorporates this archive, so the game and editor project references carry the dependency. CUDA builds use a separate `CUDA` directory and also merge `ggml-cuda` and NVIDIA import libraries. Debug builds use the corresponding Debug archive and runtime. Win32 builds retain the disabled service; enabling the backend on Win32 fails with a clear error.

## CUDA

Install CUDA Toolkit 13.2 or newer with its Visual Studio integration for the v145 toolchain. Enable CUDA with:

```powershell
msbuild Amnesia.sln /m /p:Configuration=Release /p:Platform=x64 /p:HPL2WithLlama=true /p:HPL2LlamaBackend=CUDA
```

For a toolkit outside the standard installation, pass `/p:HPL2CudaToolkitRoot=D:/SDKs/CUDA`. That root must contain `bin/nvcc.exe`, headers, `lib/x64`, and `extras/visual_studio_integration/MSBuildExtensions`. The bridge passes the directory to both CMake's CUDA toolset and toolkit discovery. `CUDA_PATH` is used when the explicit property is absent.

To target specific GPUs and shorten compilation, set `/p:HPL2CudaArchitectures=89` for an RTX 4080. Without an override, upstream chooses its portable architecture set. The bridge limits parallel compilation to four jobs by default; `/p:HPL2LlamaBuildJobs=8` overrides this. CPU and CUDA CMake caches are isolated; changing toolkit or architecture within one backend may require a fresh build directory.

The embedded llama, mtmd and ggml libraries remain static. NVIDIA's Windows cuBLAS libraries are dynamic: the bridge stages `cudart64_*.dll`, `cublas64_*.dll`, and `cublasLt64_*.dll` beside its archive, then HPL2 copies them into the solution's shared output directory. Ship these runtime DLLs with a CUDA-enabled executable, subject to NVIDIA's redistribution terms. `nvcuda.dll` is supplied by the installed NVIDIA driver and is not copied. A CUDA executable requires these libraries even when a model is configured for CPU execution; use a CPU build for machines without the CUDA runtime. No automatic switch between independently built binaries is provided.

For the legacy HPL2 CMake build, enable `-DHPL2_WITH_LLAMA=ON`, and optionally `-DHPL2_LLAMA_CUDA=ON`. A standalone CMake consumer can `add_subdirectory` this directory and link `hpl2_llama_backend`, which exports `HPL2_WITH_LLAMA=1` and the upstream public headers and link dependencies. Call `hpl2_llama_copy_runtime(your_executable)` to deploy the Windows CUDA DLLs. A 64-bit compiler and CMake 3.18 or newer are required; the Visual Studio 2026 generator requires a newer CMake that supports that generator.

No server, CLI, common library, external OpenMP runtime, HTTPS support, subprocess/video support, dynamic backend loading, or model downloads are built. CPU instructions beyond the x64 baseline are disabled explicitly. Model loading and inference are opt-in at runtime through the HPL2 service. In a CUDA build, set `mlGpuLayers` to a positive count (for example 99 to cover all layers of a small model); zero explicitly uses CPU execution, including the vision projector. Qwen3-VL's model implementations are included in both the language and multimodal libraries. A compatible language GGUF and its matching mmproj GGUF must be supplied separately.

To update the dependency, check out the desired immutable commit from the official repository into a temporary directory. Replace only the paths listed in `UPSTREAM.json`, retaining their upstream bytes and licenses, then update the commit in `UPSTREAM.json` and this CMake wrapper. Do not copy model weights, tests, media, example programs, or the upstream Git directory. Review the upstream CMake target names and public llama/mtmd APIs, then run the standalone service tests with the backend enabled and disabled and build the x64 solution. The six libraries in the MSVC merge must be updated if upstream's static dependencies change.

CPU builds import no llama, mtmd, ggml, CUDA, or OpenMP DLLs. CUDA builds additionally require NVIDIA's runtime DLLs and a compatible driver. Model weights are not included. See the [standalone tests](../../tests/llama/README.md) for contract tests, an actual HPL2.lib consumer check, and optional GPU generation/image smoke tests.

Validation: the x64 Release CUDA bridge, game and editors build with Visual Studio 2026/v145 and CUDA Toolkit 13.2.2. An actual HPL2.lib consumer ran Qwen3-VL 2B Q4_K_M plus its F16 projector on an RTX 4080, with all 29 language layers and the vision encoder on CUDA. A synthetic 256x256 image was described correctly; lifecycle and subsequent CPU-generation checks passed. CPU and disabled service regressions pass, including Win32 disabled builds. The CUDA Toolkit and test models remain outside tracked source.
