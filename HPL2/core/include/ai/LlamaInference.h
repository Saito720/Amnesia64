#ifndef HPL_LLAMA_INFERENCE_H
#define HPL_LLAMA_INFERENCE_H

#include <stdint.h>
#include <string>
#include <vector>

namespace hpl {

enum eLlamaState
{
    eLlamaState_Unloaded,
    eLlamaState_Loading,
    eLlamaState_Ready,
    eLlamaState_Failed,
    eLlamaState_Stopping
};

struct cLlamaModelConfig
{
    cLlamaModelConfig();
    std::string msModelPath;     // Local GGUF, UTF-8 path. No downloads occur.
    std::string msProjectorPath; // Matching mmproj GGUF; empty for text only.
    std::string msChatTemplate;  // Optional built-in template name/override.
    int mlContextSize;
    int mlBatchSize;
    int mlThreads;
    int mlGpuLayers;            // CPU by default; requires a compiled GPU backend.
    int mlMaxImageTokens;       // Dynamic-resolution projectors only.
    unsigned mlMaxOutstandingRequests;
};

struct cLlamaImage
{
    cLlamaImage() : mlWidth(0), mlHeight(0) {}
    unsigned mlWidth;
    unsigned mlHeight;
    std::vector<unsigned char> mvRGB; // Packed RGB8, top row first; copied at Submit.
};

struct cLlamaRequest
{
    cLlamaRequest();
    std::string msSystemPrompt;
    std::string msPrompt;
    cLlamaImage mImage; // Optional single image; the service inserts its media marker.
    int mlMaxTokens;
    float mfTemperature; // Zero selects greedy sampling.
    uint32_t mlSeed;
};

struct cLlamaResult
{
    cLlamaResult() : mlRequestId(0), mbCancelled(false), mlGeneratedTokens(0) {}
    uint64_t mlRequestId;
    std::string msText; // UTF-8; may be partial on error/cancellation.
    std::string msError;
    bool mbCancelled;
    int mlGeneratedTokens;
};

// LoadAsync, Submit and Unload belong to the owning/game thread. State/error
// queries, Cancel and PollResult are synchronized. No engine callbacks run on
// the worker. Each request is an independent chat with an empty KV cache.
class cLlamaInference
{
public:
    cLlamaInference();
    ~cLlamaInference();

    static bool IsSupported();
    static bool ValidateRequest(const cLlamaRequest& aRequest, std::string& asError);

    // Starts loading without blocking the game loop. Requires Unloaded state;
    // after a failure, Unload before retrying. Poll GetState/GetLastError.
    bool LoadAsync(const cLlamaModelConfig& aConfig, std::string& asError);
    eLlamaState GetState() const;
    std::string GetLastError() const;

    // Zero means rejected, with asError explaining why. The outstanding limit
    // includes queued, running and completed requests until PollResult consumes them.
    uint64_t Submit(const cLlamaRequest& aRequest, std::string& asError);
    bool Cancel(uint64_t alRequestId);
    bool PollResult(cLlamaResult& aResult);

    // Cancels work, joins the worker, releases models and discards all requests
    // and results. Can block during native model/context loading or image encode;
    // use at loading transitions/shutdown. Repeated calls are safe.
    void Unload();

private:
    cLlamaInference(const cLlamaInference&);
    cLlamaInference& operator=(const cLlamaInference&);
    struct cImpl;
    cImpl* mpImpl;
};

}
#endif
