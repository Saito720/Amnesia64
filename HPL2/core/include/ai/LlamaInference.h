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
    int mlGpuLayers;            // Zero forces CPU; positive values require an available GPU backend.
    int mlMinImageTokens;       // Dynamic-resolution projectors only; zero keeps the projector default.
    int mlMaxImageTokens;       // Dynamic-resolution projectors only; must be at least the explicit minimum.
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
    std::string msGrammar; // Optional GBNF grammar with entry rule "root"; empty is unconstrained.
    cLlamaImage mImage; // Optional single image; the service inserts its media marker.
    int mlMaxTokens;
    float mfTemperature; // Zero selects greedy sampling.
    uint32_t mlSeed;
    uint64_t mlSessionId; // Zero preserves independent requests; a nonzero ID retains one conversation.
    std::string msContextSummary; // Factual engine summary inserted only when rebuilding older history.
    unsigned mlSessionKeepTurns; // Recent accepted user/assistant pairs retained on refresh.
    float mfSessionRefreshFraction;
};

struct cLlamaContextStats
{
    cLlamaContextStats();
    uint64_t mlSessionId;
    unsigned mlUsedTokens, mlCapacityTokens, mlTextTokens, mlImageTokens, mlOutputTokens;
    unsigned mlReservedTokens, mlTurns, mlObservations, mlRefreshCount, mlReusedTokens;
    std::string msRefreshReason;
    bool mbAwaitingResolution;
};

struct cLlamaResult
{
    cLlamaResult() : mlRequestId(0), mbCancelled(false), mbTruncated(false), mlGeneratedTokens(0) {}
    uint64_t mlRequestId;
    std::string msText; // UTF-8; may be partial on error/cancellation.
    std::string msError;
    bool mbCancelled;
    bool mbTruncated; // The generation limit was reached before a complete assistant end token.
    int mlGeneratedTokens;
    cLlamaContextStats mContextStats;
};

// LoadAsync, Submit and Unload belong to the owning/game thread. State/error
// queries, Cancel and PollResult are synchronized. No engine callbacks run on
// the worker. Session zero is independent; one nonzero session retains history
// and reuses its KV cache. Switching nonzero IDs discards the previous history.
class cLlamaInference
{
public:
    cLlamaInference();
    ~cLlamaInference();

    static bool IsSupported();
    // Checks for an available GPU device, not only whether GPU support was compiled.
    // May initialize native device discovery; no model is loaded.
    static bool IsGpuSupported();
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
    // Persistent replies remain provisional until the consumer validates/applies
    // them. Rejection discards the proposed turn and rebuilds accepted history.
    // These calls queue work and never wait for native inference.
    bool ResolveSessionTurn(uint64_t alRequestId, bool abAccepted);
    void ResetSession(uint64_t alSessionId, const std::string& asReason);
    cLlamaContextStats GetContextStats(uint64_t alSessionId) const;

    // Cancels work, joins the worker, releases models and discards all requests
    // and results. Can block during native loading, image encode or a GPU decode batch;
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
