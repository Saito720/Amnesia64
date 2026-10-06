# Embedded inference tests

This standalone harness compiles the same `LlamaInference.cpp` service used by
HPL2. It does not build or initialize the game, graphics, audio, or editors.

Run the commands below from the repository root. With Visual Studio 2026 on
Windows, add `-G "Visual Studio 18 2026" -A x64` to either configure command.

## Disabled backend

The default build needs no llama.cpp checkout or model files. It checks request
validation, unavailable-backend errors, uninitialized service behavior, and
repeatable shutdown.

```powershell
cmake -S HPL2/tests/llama -B bld/llama-tests-off -DHPL2_WITH_LLAMA=OFF
cmake --build bld/llama-tests-off --config Release --target hpl2_llama_tests
ctest --test-dir bld/llama-tests-off -C Release --output-on-failure
```

## Enabled backend

The pinned llama.cpp source is bundled in this repository. Build the static
CPU backend with:

```powershell
cmake -S HPL2/tests/llama -B bld/llama-tests-on -DHPL2_WITH_LLAMA=ON
cmake --build bld/llama-tests-on --config Release --target hpl2_llama_tests
ctest --test-dir bld/llama-tests-on -C Release --output-on-failure
```

These tests link llama.cpp and its multimodal library, verify asynchronous
missing-file failures, retry after unloading, and unload while loading. They do
not download model weights and do not prove model-specific generation works.

For CUDA, add `-DHPL2_LLAMA_CUDA=ON` to a separate build directory. With Visual
Studio 2026/v145, use CUDA Toolkit 13.2 or newer. For example, a locally installed
toolkit can be selected with:

```powershell
cmake -S HPL2/tests/llama -B bld/llama-tests-cuda -G "Visual Studio 18 2026" -A x64 -T "v145,cuda=D:/CUDA/v13.2" -DHPL2_WITH_LLAMA=ON -DHPL2_LLAMA_CUDA=ON -DCUDAToolkit_ROOT=D:/CUDA/v13.2
cmake --build bld/llama-tests-cuda --config Release --target hpl2_llama_tests
ctest --test-dir bld/llama-tests-cuda -C Release --output-on-failure
```

Replace the toolkit path with your installation. For the default installed
toolkit, the explicit toolset path and `CUDAToolkit_ROOT` may be omitted. Optional
`-DCMAKE_CUDA_ARCHITECTURES=89` selects an architecture explicitly. The harness
copies cudart, cuBLAS and cuBLASLt runtime DLLs beside its executable. These
DLLs and a compatible NVIDIA driver are required even though llama/ggml are
statically linked. The contract tests handle both available and unavailable
GPU devices; a generation smoke test with positive GPU layers requires a
usable GPU.

To verify that an existing engine archive carries the native dependencies,
configure a separate consumer build. It compiles only the tests and links the
actual HPL2 library, without rebuilding the service or llama.cpp:

```powershell
cmake -S HPL2/tests/llama -B bld/llama-tests-engine -DHPL2_WITH_LLAMA=ON -DHPL2_PREBUILT_LIBRARY=D:/Amnesia64/x64/Release/HPL2.lib
cmake --build bld/llama-tests-engine --config Release --target hpl2_llama_tests
ctest --test-dir bld/llama-tests-engine -C Release --output-on-failure
```

Use the path to your built library and match its architecture, configuration,
and enabled/disabled backend setting. The same smoke-test options work with
this consumer executable.

For a CUDA engine archive, add `-DHPL2_LLAMA_CUDA=ON` to the consumer configure
command. The merged `HPL2.lib` contains the native libraries and CUDA import
libraries, so the consumer does not need a CUDA Toolkit or separate CUDA link
libraries. The harness copies cudart, cuBLAS and cuBLASLt DLLs from the archive's
directory into its executable directory. Explicit CUDA consumer configuration
fails if any required runtime family is missing.

Build that archive through MSBuild with
`/p:HPL2WithLlama=true /p:Platform=x64 /p:HPL2LlamaBackend=CUDA`. Optional
`/p:HPL2CudaToolkitRoot=D:/CUDA/v13.2` selects a local toolkit, and
`/p:HPL2CudaArchitectures=89` selects the target architecture.

## Optional generation smoke test

Pass a local GGUF model to the enabled executable. For Visual Studio builds it
is under `bld/llama-tests-on/Release`; single-configuration generators place it
directly in `bld/llama-tests-on`.

```powershell
bld/llama-tests-on/Release/hpl2_llama_tests.exe --model D:/models/model.gguf --prompt "Reply with a short greeting."
```

For a CUDA executable, request positive GPU layers:

```powershell
bld/llama-tests-cuda/Release/hpl2_llama_tests.exe --model D:/models/model.gguf --gpu-layers 99 --prompt "Reply with a short greeting."
```

Positive layer counts beyond the model's size saturate at its available layers.
Negative counts are invalid. `--gpu-layers 0` explicitly selects CPU execution,
including in a CUDA build. The smoke test checks actual GPU-device availability
before accepting a GPU run and prints model-load and first-request durations.
Confirm actual offloading in native logs such as `offloaded N/N layers to GPU`
and CUDA compute-buffer reports; the smoke-test label alone is not proof.

For an image request, provide the model's matching multimodal projector and a
binary PPM image with RGB8 channels (`P6`, maximum channel value `255`):

```powershell
bld/llama-tests-on/Release/hpl2_llama_tests.exe --model D:/models/model.gguf --projector D:/models/mmproj.gguf --image D:/images/scene.ppm --prompt "Describe this scene." --context 8192
```

Use the CUDA executable and add `--gpu-layers 99` to offload the language model
and matching vision projector. The actual HPL2.lib consumer was verified on
an RTX 4080 with CUDA Toolkit 13.2.2, Qwen3-VL 2B Q4_K_M and its F16 projector:
a 256x256 red-square image produced the correct description. Native logs
confirmed 29/29 language layers and the vision encoder were on CUDA, and the
full lifecycle smoke checks passed. This synthetic fixture does not establish
in-game image quality or frame times.

The smoke test verifies generation, the outstanding request limit, capacity
recovery after polling, context-budget errors and recovery, identical output
from independent requests with the same seed, cancellation, shutdown while work
is pending, reloading the model afterward, a missing projector after language
context creation, and one service remaining usable while another is unloaded.
GPU smoke mode additionally checks short CPU generation after GPU shutdown.
Cancellation occurs between service operations and decode batches; it does
not interrupt a running CUDA kernel. Native image encoding may also need to
finish before cancellation is observed.
Additional value-taking options are `--system`, `--template`, `--max-tokens`,
`--image-tokens`, `--threads`, and `--gpu-layers`. Use `--template chatml` when a
test fixture has no supported embedded chat template. Model loading has a
five-minute timeout; each inference request
has a ten-minute timeout.

See [the dependency bridge documentation](../../dependencies/llama/README.md)
for the upstream version and build choices.
