# Embedded inference in HPL2

HPL2 exposes an optional, in-process llama.cpp service through
`cEngine::GetLlamaInference()`. The service can load local GGUF models, run
independent text requests, and run image requests with a matching multimodal
projector. Model loading and inference run on a worker thread. Game code polls
results and applies them on the game thread.

The initial backend is a statically linked CPU build. It does not start a
server, download models, or load a model at engine startup. It is disabled by
default and requires x64 when enabled. CPU settings favor compatibility; this
is an integration foundation, not a frame-rate/latency guarantee for a 2B VLM.

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

The legacy engine CMake path uses `-DHPL2_WITH_LLAMA=ON`. See the
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
are outside this first API. The current CPU bridge has no GPU backend;
leave `mlGpuLayers` at zero.

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
usual. Cancellation cannot interrupt every native image operation, and GPU
abort behavior would require review when a GPU backend is introduced. The CPU
decoder uses an abort callback. Model loaders use progress callbacks to react
to shutdown. Finished results still count toward the outstanding limit until
polled, preventing unbounded queues if game code stops consuming results.

Call `LoadAsync`, `Submit` and `Unload` on the service's owning/game thread.
State/error queries, result polling and cancellation are synchronized. Worker
code does not call gameplay callbacks, HPL logging or HPL's debug memory manager.
`Unload()` cancels work, waits for the worker, releases native resources, and
discards requests/results. Use it at loading transitions or shutdown because
it can block during native loading or image encoding. The engine destroys the
service before other engine modules.

## Verification

The [standalone harness](../../tests/llama/README.md) compiles this same service
with the backend disabled and enabled, tests validation and asynchronous
load failures, and offers a model smoke mode for generation, queue bounds and
cancellation. Real Qwen3-VL output and image quality require its GGUF pair and
an application capture path; compiling the image API alone does not verify
those model-specific behaviors.
