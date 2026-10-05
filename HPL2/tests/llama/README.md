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

The pinned llama.cpp source is bundled in this repository. Build the real static
CPU backend with:

```powershell
cmake -S HPL2/tests/llama -B bld/llama-tests-on -DHPL2_WITH_LLAMA=ON
cmake --build bld/llama-tests-on --config Release --target hpl2_llama_tests
ctest --test-dir bld/llama-tests-on -C Release --output-on-failure
```

These tests link llama.cpp and its multimodal library, verify asynchronous
missing-file failures, retry after unloading, and unload while loading. They do
not download model weights and do not prove model-specific generation works.

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

## Optional generation smoke test

Pass a local GGUF model to the enabled executable. For Visual Studio builds it
is under `bld/llama-tests-on/Release`; single-configuration generators place it
directly in `bld/llama-tests-on`.

```powershell
bld/llama-tests-on/Release/hpl2_llama_tests.exe --model D:/models/model.gguf --prompt "Reply with a short greeting."
```

For an image request, provide the model's matching multimodal projector and a
binary PPM image with RGB8 channels (`P6`, maximum channel value `255`):

```powershell
bld/llama-tests-on/Release/hpl2_llama_tests.exe --model D:/models/model.gguf --projector D:/models/mmproj.gguf --image D:/images/scene.ppm --prompt "Describe this scene." --context 8192
```

The smoke test verifies generation, the outstanding request limit, capacity
recovery after polling, context-budget errors and recovery, identical output
from independent requests with the same seed, cancellation, shutdown while work
is pending, reloading the model afterward, a missing projector after language
context creation, and one service remaining usable while another is unloaded.
Additional value-taking options are `--system`, `--template`, `--max-tokens`,
`--image-tokens`, `--threads`, and `--gpu-layers`. Use `--template chatml` when a
test fixture has no supported embedded chat template. GPU layers
are reserved for a future accelerated backend; the bundled bridge builds CPU
support only. Model loading has a five-minute timeout; each inference request
has a ten-minute timeout.

See [the dependency bridge documentation](../../dependencies/llama/README.md)
for the upstream version and build choices.
