# Embedded inference in HPL2

HPL2 exposes an optional, in-process llama.cpp service through
`cEngine::GetLlamaInference()`. The service can load local GGUF models, run
independent text requests, and run image requests with a matching multimodal
projector. Model loading and inference run on a worker thread. Game code polls
results and applies them on the game thread.

llama.cpp and its multimodal library are statically linked, with CPU support
and an optional CUDA backend. The service does not start a server, download
models, or load a model at engine startup. It is disabled by default and
requires x64 when enabled. CPU settings favor compatibility. Inference latency
and its effect on rendering must be measured with the intended model and game.

## Build

Use Visual Studio 2026/v145 and the CMake tools described in the repository
README. From a Visual Studio developer terminal:

```powershell
msbuild Amnesia.sln /m /p:Configuration=Release /p:Platform=x64 /p:HPL2WithLlama=true
```

The dependency bridge builds and incorporates llama, mtmd and their static
dependencies into HPL2. The game and editors retain their existing project
references. Use the same property for Debug. Omit the property or set it to
`false` to build the lightweight disabled service, including on Win32.

To include CUDA alongside CPU support, select the CUDA backend:

```powershell
msbuild Amnesia.sln /m /p:Configuration=Release /p:Platform=x64 /p:HPL2WithLlama=true /p:HPL2LlamaBackend=CUDA
```

Visual Studio 2026/v145 requires CUDA Toolkit 13.2 or newer. The build uses
`CUDA_PATH` by default. For a toolkit installed in another directory, supply
`/p:HPL2CudaToolkitRoot=D:/CUDA/v13.2`. Optional
`/p:HPL2CudaArchitectures=89` selects a CUDA architecture explicitly; omit it
to use upstream's architecture selection. CPU and CUDA dependency builds use
separate directories.

Windows CUDA builds deploy NVIDIA's cudart, cuBLAS and cuBLASLt runtime DLLs
beside the engine library and executable consumers. The llama/ggml libraries
remain static, but a CUDA distribution is not a single self-contained
executable. A compatible NVIDIA driver is also required.

The legacy engine CMake path uses `-DHPL2_WITH_LLAMA=ON`; add
`-DHPL2_LLAMA_CUDA=ON` to enable CUDA. A local toolkit can be selected with
`-DCUDAToolkit_ROOT=D:/CUDA/v13.2`. For the Visual Studio generator, also use
`-T "v145,cuda=D:/CUDA/v13.2"` when selecting that toolkit. See the
[dependency bridge](../../dependencies/llama/README.md) for the pinned upstream
revision, source selection, and update instructions.

## Model loading

Supply model files separately. For Qwen3-VL 2B, use a language GGUF from the
[official Qwen repository](https://huggingface.co/Qwen/Qwen3-VL-2B-Instruct-GGUF)
and its matching `mmproj-Qwen3VL-2B-Instruct` GGUF. llama.cpp's
[multimodal documentation](https://github.com/ggml-org/llama.cpp/blob/master/tools/mtmd/README.md)
explains the separate projector. Keep the paths relative to the game's working
directory or use absolute UTF-8 paths. No weights are committed to this repository.

```cpp
#include "ai/LlamaInference.h"

hpl::cLlamaInference* inference = engine->GetLlamaInference();
hpl::cLlamaModelConfig config;
config.msModelPath = "models/Qwen3VL-2B-Instruct-Q4_K_M.gguf";
config.msProjectorPath = "models/mmproj-Qwen3VL-2B-Instruct-Q8_0.gguf";
config.mlThreads = 2;
// Optional in a CUDA build; keep zero for explicit CPU execution.
if(hpl::cLlamaInference::IsGpuSupported()) config.mlGpuLayers = 99;
std::string error;
if(!inference->LoadAsync(config, error))
{
    // Display/log error here on the game thread.
}
```

`LoadAsync` returns after starting the worker. Poll `GetState()` until `Ready`
or `Failed`; read `GetLastError()` on failure. Call `Unload()` before retrying
or replacing a model. A missing/corrupt model, incompatible projector or failed
context allocation is recoverable and does not end the engine.

The default context is 4096 tokens, batch size 256, two CPU threads, and four
outstanding requests. Dynamic-resolution projectors are limited to 1024 image
tokens by default through `mlMaxImageTokens`. Prompt plus image tokens plus
reserved output tokens must fit the context; oversized requests produce an
error result. The service clears the KV cache for every request, so there is no
implicit conversation history.

The service applies the model's chat template using llama.cpp's built-in
template formatter. If the embedded template is missing or unsupported, set
`msChatTemplate` to a supported explicit override such as `"chatml"` for a
compatible Qwen model. Arbitrary Jinja templates, tools and structured output
are outside this API.

`mlGpuLayers` defaults to zero, which forces CPU execution even when CUDA is
compiled in. A positive value selects GPU offloading; values beyond the
model's layer count saturate at the available layers, so `99` is a convenient
full-offload request for Qwen3-VL 2B. Negative values are invalid in this API.
The matching vision projector also uses the GPU when `mlGpuLayers` is positive.
`IsGpuSupported()` checks whether a GPU device is available to the compiled
backend, rather than only whether CUDA was included in the build. It may
initialize native device discovery, but it does not load a model. An explicit
GPU request is rejected when no supported device is available; choose zero
if CPU execution is desired.

## Requests and results

```cpp
if(inference->GetState() == hpl::eLlamaState_Ready)
{
    hpl::cLlamaRequest request;
    request.msSystemPrompt = "Describe only what is visible in the image.";
    request.msPrompt = "What obstacles are ahead of the player?";
    request.mlMaxTokens = 96;
    // Optional: a CPU image captured and converted on the render thread.
    request.mImage.mlWidth = width;
    request.mImage.mlHeight = height;
    request.mImage.mvRGB = packedTopDownRGB;
    uint64_t requestId = inference->Submit(request, error);
    // Zero means rejected. Store a nonzero ID with your game/map generation.
}

// In Update(), on the game thread:
hpl::cLlamaResult result;
while(inference->PollResult(result))
{
    if(result.mbCancelled || !result.msError.empty())
        continue;
    // Check that result.mlRequestId still belongs to the current game/map.
    // Consume result.msText here; it is model output, not an engine command.
}
```

For text-only requests, leave the image at its defaults and omit the projector
when loading a text model. Images must contain exactly `width * height * 3`
RGB8 bytes, top row first. `Submit` copies the inputs, so the caller may reuse
its buffers after it returns. Copying large images still costs game-thread
time; capture a small image at a low cadence for future gameplay integration.
The service inserts the media marker automatically; do not insert one yourself.

HPL2 already has `iLowLevelGraphics::CopyFrameBufferToBitmap()` for a future
capture adapter. Call it on the render thread after rendering, convert to RGB8,
and flip OpenGL's bottom-up rows. No graphics calls run on the inference worker.
Automatic capture, script bindings, enemy decisions and UI are future consumers
of this service.

`Cancel(id)` marks queued or active work for cancellation; poll its result as
usual. The CPU decoder uses an abort callback. CUDA cancellation is checked
between service operations and decode batches; it does not interrupt a running
GPU kernel. Native image encoding may also need to finish before cancellation
is observed. Model loaders use progress callbacks to react to shutdown.
Finished results still count toward the outstanding limit until polled,
preventing unbounded queues if game code stops consuming results.

Call `LoadAsync`, `Submit` and `Unload` on the service's owning/game thread.
State/error queries, result polling and cancellation are synchronized. Worker
code does not call gameplay callbacks, HPL logging or HPL's debug memory manager.
`Unload()` cancels work, waits for the worker, releases native resources, and
discards requests/results. Use it at loading transitions or shutdown because
it can block during native loading or image encoding. The engine destroys the
service before other engine modules. With CUDA, shutdown may also wait for
the active decode batch to finish.

## Verification

The [standalone harness](../../tests/llama/README.md) compiles this same service
with the backend disabled and enabled, tests validation and asynchronous
load failures, and offers a model smoke mode for generation, queue bounds and
cancellation. CUDA smoke mode requires an available GPU device and also checks
CPU generation after GPU shutdown. Actual offloading can be confirmed in the
native load logs and CUDA compute-buffer reports. The CUDA Release build was
verified on an RTX 4080 with CUDA Toolkit 13.2.2, using the actual HPL2 library,
Qwen3-VL 2B Q4_K_M and its matching F16 projector. A 256x256 RGB image of a red
square produced the correct description; native logs confirmed all 29 language
layers and the vision encoder used CUDA. Queue bounds, independent requests,
cancellation, reload, concurrent services and subsequent CPU generation passed.
This is an inference smoke test; game-frame capture and rendering performance
remain application-level verification work.
