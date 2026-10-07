# Embedded inference tests

This standalone harness compiles the same `LlamaInference.cpp` service used by
HPL2. It does not build or initialize the game, graphics, audio, or editors.

Run the commands below from the repository root. With Visual Studio 2026 on
Windows, add `-G "Visual Studio 18 2026" -A x64` to either configure command.

## Disabled backend

The default build needs no llama.cpp checkout or model files. It checks request
validation, unavailable-backend errors, uninitialized service behavior, and
repeatable shutdown. Request validation also bounds optional NUL-free GBNF
grammars to 64 KiB; their syntax is checked on the inference worker after a
model is loaded. Persistent-session settings validate a NUL-free 8 KiB engine
summary, 1–16 retained pairs, and a finite 25–95% refresh threshold. Unloaded
session stats and unknown reply acknowledgements are also checked.
Model configuration validates a nonnegative image-token minimum, a positive
maximum no larger than the context, and a minimum no larger than that maximum.
Zero minimum preserves the projector default; enabled lifecycle checks also
accept an explicit minimum equal to the maximum.

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
bld/llama-tests-on/Release/hpl2_llama_tests.exe --model D:/models/model.gguf --projector D:/models/mmproj.gguf --image D:/images/scene.ppm --prompt "Describe this scene." --context 8192 --image-min-tokens 1024 --image-tokens 2048
```

Use the CUDA executable and add `--gpu-layers 99` to offload the language model
and matching vision projector. `--image-min-tokens` sets an explicit minimum for
dynamic-resolution projectors; zero preserves their default. The pinned Qwen-VL
implementation recommends at least 1024 image tokens for spatial grounding.
`--image-tokens` is the maximum, not a fixed image size. Actual embedding counts
depend on the projector's patch grid, source aspect ratio, and rounding; verify
the resulting context statistics. Prefer a capture with enough source detail
instead of relying on preprocessing to upscale a tiny image.
The actual HPL2.lib consumer was verified on
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
The loaded-model smoke also requests exact JSON with a grammar under both
greedy and probabilistic sampling, and confirms malformed grammar fails only
that request before a subsequent constrained request succeeds.
The same live smoke then checks persistent text/image history, exact native KV
prefix reuse, fact recall after rejected/cancelled/incomplete turns, independent
request isolation, a forced context refresh with a factual engine summary,
owner/session resets, and token/observation accounting. Its small secret-code
fixture establishes session mechanics; it does not evaluate navigation quality.
Cancellation occurs between service operations and decode batches; it does
not interrupt a running CUDA kernel. Native image encoding may also need to
finish before cancellation is observed.
Additional value-taking options are `--system`, `--template`, `--max-tokens`,
`--image-min-tokens`, `--image-tokens`, `--threads`, and `--gpu-layers`. Use `--template chatml` when a
test fixture has no supported embedded chat template. Model loading has a
five-minute timeout; each inference request
has a ten-minute timeout.

See [the dependency bridge documentation](../../dependencies/llama/README.md)
for the upstream version and build choices.

## Focused action-format comparison

`--action-probe` bypasses the contract and lifecycle smoke queries and makes
exactly three independent image requests through the same inference service:
plain prose, requested JSON, and the identical JSON request with a GBNF grammar.
Use a previously exported RGB8 PPM to keep the camera image identical:

```powershell
bld/llama-tests-engine/Release/hpl2_llama_tests.exe --action-probe --model D:/models/Qwen3-VL-2B-Q4_K_M.gguf --projector D:/models/mmproj-Qwen3-VL-2B-F16.gguf --image bld/enemy-llama-explanation-30b/scratch/decision-frame.ppm --output-dir bld/llama-action-probe-2b --gpu-layers 99 --context 16384 --threads 8 --image-min-tokens 1024 --image-tokens 2048 --max-tokens 512
```

Replace model and projector paths with a matching local pair. The common prompt
asks the model to locate the magenta target and choose one approach action from
`turn_left`, `turn_right`, `move_forward`, or `wait`. It contains no example
answer, target location, mask-pixel counts, engine history, system prompt, or
player/combat terminology. The JSON stages add only output-format instructions
for `target_visible`, `target_side`, and `action`; the grammar permits every
listed value, including incorrect combinations, rather than forcing pursuit.
All requests use session zero, greedy sampling, seed zero, the same immutable
top-down RGB, and the same model/image/output budgets. The second and third
prompts are byte-identical; only their grammar setting differs. The native
service clears its KV cache for each independent request.

Add the valueless `--align-target` flag to require alignment before advancing:
left of image center calls for `turn_left`, right calls for `turn_right`, and
only a target near image center calls for `move_forward`. This rule is added
to all three prompts; the grammar still allows every action. Use a separate
output directory to retain the original comparison.

The valueless `--describe-target` flag adds `observation` as the first JSON field,
before visibility, side and action. It asks for one brief sentence describing
the magenta target's position relative to the whole image center. The grammar
accepts a JSON string of up to 240 characters, including valid JSON escapes;
it still permits every visibility/side/action combination. This compares
describing the visible image before choosing an action against the terse
three-field form. The free-text stage remains unchanged. Combine this option
with `--align-target` and use separate output directories for each frozen frame.
Summary and per-stage metadata report both flags. These descriptions are
observable image reports, not evidence of the model's internal reasoning.

The output directory receives each exact prompt, grammar, raw reply, timings,
context accounting and cancellation/truncation/error flags, plus `summary.txt`.
Every result is saved before the final technical-success check. A native error,
truncated response, missing image tokens or reused/session context fails the
probe. Exit success only confirms three technically complete requests; it does
not assert competent behavior or valid unconstrained JSON. Inspect recognition,
JSON formatting, and approach direction separately. For the referenced frozen
room fixture, the magenta target is right of center, making `turn_right` a
grounded immediate approach action. These diagnostic replies never execute
game actions or change the production enemy controller.

The frozen-room comparison was run with Qwen3-VL 2B Instruct Q4_K_M and its
matching F16 projector on the CUDA engine consumer. All three requests
recognized the magenta target and selected `move_forward`; requested and
grammar-enforced JSON both reported `target_visible=true` and
`target_side="right"`, with byte-identical replies. Each used 1080 image tokens,
no retained context, and completed without native errors or truncation. This
establishes detection and an approach request in this simplified task. It does
not establish steering accuracy: the target was right of center, but none chose
to turn right before advancing. Full prompts, replies and timings are in
`bld/llama-action-probe-2b/summary.txt`; the production enemy policy was not
changed by this diagnostic.

With `--align-target` on the same frame and model, all three replies selected
`turn_right`. The requested-JSON response included a Markdown code fence despite
the format instruction; grammar-enforced JSON returned the valid bare object.
The results are in `bld/llama-turn-probe-2b/summary.txt`. These single-frame
comparisons establish neither sustained tracking nor collision avoidance.
