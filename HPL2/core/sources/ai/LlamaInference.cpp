#include "ai/LlamaInference.h"

#if defined(_MSC_VER) && defined(_DLL) && defined(HPL2_LLAMA_CUDA) && HPL2_LLAMA_CUDA
// NVIDIA's Windows driver-loader library requests LIBCMT despite using only
// Windows loader APIs and _fltused. Keep this service's DLL CRT (MD/MDd);
// carry the correction inside HPL2.lib so its consumers inherit it too.
#pragma comment(linker, "/NODEFAULTLIB:LIBCMT")
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#if defined(HPL2_WITH_LLAMA) && HPL2_WITH_LLAMA
#include "llama.h"
#include "mtmd.h"
#include "mtmd-helper.h"
#endif

namespace hpl {

cLlamaModelConfig::cLlamaModelConfig()
    : mlContextSize(4096), mlBatchSize(256), mlThreads(2), mlGpuLayers(0),
      mlMinImageTokens(0), mlMaxImageTokens(1024), mlMaxOutstandingRequests(4) {}

cLlamaRequest::cLlamaRequest()
    : mlMaxTokens(128), mfTemperature(0.2f), mlSeed(0), mlSessionId(0),
      mlSessionKeepTurns(4), mfSessionRefreshFraction(0.8f) {}

cLlamaContextStats::cLlamaContextStats()
    : mlSessionId(0), mlUsedTokens(0), mlCapacityTokens(0), mlTextTokens(0),
      mlImageTokens(0), mlOutputTokens(0), mlReservedTokens(0), mlTurns(0),
      mlObservations(0), mlRefreshCount(0), mlReusedTokens(0), mbAwaitingResolution(false) {}

namespace {
bool Reject(std::string& asError, const char* asMessage)
{
    asError = asMessage;
    return false;
}

bool ValidateConfig(const cLlamaModelConfig& aConfig, std::string& asError)
{
    if(aConfig.msModelPath.empty() || aConfig.msModelPath.find('\0') != std::string::npos ||
       aConfig.msProjectorPath.find('\0') != std::string::npos ||
       aConfig.msChatTemplate.find('\0') != std::string::npos)
        return Reject(asError, "Supply a local GGUF model path and NUL-free strings.");
    if(aConfig.mlContextSize < 256 || aConfig.mlContextSize > 131072 ||
       aConfig.mlBatchSize < 1 || aConfig.mlBatchSize > aConfig.mlContextSize ||
       aConfig.mlThreads < 1 || aConfig.mlThreads > 128 || aConfig.mlGpuLayers < 0 ||
       aConfig.mlMinImageTokens < 0 || aConfig.mlMinImageTokens > aConfig.mlMaxImageTokens ||
       aConfig.mlMaxImageTokens < 1 || aConfig.mlMaxImageTokens > aConfig.mlContextSize ||
       aConfig.mlMaxOutstandingRequests < 1 || aConfig.mlMaxOutstandingRequests > 64)
        return Reject(asError, "Invalid context, batch, thread, GPU, image-token or queue limits.");
    return true;
}

#if defined(HPL2_WITH_LLAMA) && HPL2_WITH_LLAMA
// llama backend initialization is process-wide; multiple engine instances can
// coexist without one freeing the backend while another is using it.
std::mutex gBackendMutex;
unsigned gBackendUsers = 0;
struct cBackendLease
{
    cBackendLease()
    {
        std::lock_guard<std::mutex> lock(gBackendMutex);
        if(gBackendUsers == 0) llama_backend_init();
        ++gBackendUsers;
    }
    ~cBackendLease()
    {
        std::lock_guard<std::mutex> lock(gBackendMutex);
        if(--gBackendUsers == 0) llama_backend_free();
    }
};

typedef std::unique_ptr<llama_model, decltype(&llama_model_free)> tModel;
typedef std::unique_ptr<llama_context, decltype(&llama_free)> tContext;
typedef std::unique_ptr<mtmd_context, decltype(&mtmd_free)> tProjector;
typedef std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> tSampler;
typedef std::unique_ptr<mtmd_bitmap, decltype(&mtmd_bitmap_free)> tBitmap;
typedef std::unique_ptr<mtmd_input_chunks, decltype(&mtmd_input_chunks_free)> tChunks;

// Token pieces are bytes, and stopping after a token can split a code point.
// Keep completed results usable by game UI: trim an unfinished suffix and
// replace invalid byte sequences without changing well-formed model output.
void NormalizeUTF8(std::string& asText)
{
    std::string clean;
    clean.reserve(asText.size());
    for(size_t i = 0; i < asText.size();)
    {
        const unsigned char first = static_cast<unsigned char>(asText[i]);
        const size_t count = first < 0x80 ? 1 :
            (first >= 0xC2 && first <= 0xDF ? 2 :
             (first >= 0xE0 && first <= 0xEF ? 3 :
              (first >= 0xF0 && first <= 0xF4 ? 4 : 0)));
        bool valid = count != 0;
        for(size_t j = 1; valid && j < count && i + j < asText.size(); ++j)
        {
            const unsigned char byte = static_cast<unsigned char>(asText[i + j]);
            valid = byte >= 0x80 && byte <= 0xBF;
            if(j == 1)
                valid = valid && !(first == 0xE0 && byte < 0xA0) &&
                    !(first == 0xED && byte >= 0xA0) && !(first == 0xF0 && byte < 0x90) &&
                    !(first == 0xF4 && byte >= 0x90);
        }
        if(valid && count > asText.size() - i) break;
        if(valid) { clean.append(asText, i, count); i += count; }
        else { clean.append("\xEF\xBF\xBD", 3); ++i; }
    }
    asText.swap(clean);
}

struct cBatch
{
    explicit cBatch(int alSize) : mBatch(llama_batch_init(alSize, 0, 1)) {}
    ~cBatch() { llama_batch_free(mBatch); }
    llama_batch mBatch;
};

struct cChatTurn
{
    cChatTurn() : mlOutputTokens(0) {}
    std::string msPrompt, msReply;
    cLlamaImage mImage;
    unsigned mlOutputTokens;
};

std::string FormatChat(llama_model* apModel, const cLlamaModelConfig& aConfig,
                       const cLlamaRequest& aRequest, const std::deque<cChatTurn>& avTurns,
                       const std::string& asSummary)
{
    std::string sSystem = aRequest.msSystemPrompt;
    if(!asSummary.empty()) sSystem += "\nFactual engine history summary (older observations were refreshed):\n" + asSummary;
    std::vector<std::string> vUsers;
    vUsers.reserve(avTurns.size() + 1);
    for(const cChatTurn& turn : avTurns)
        vUsers.push_back((turn.mImage.mvRGB.empty() ? "" : std::string(mtmd_default_marker()) + "\n") + turn.msPrompt);
    vUsers.push_back((aRequest.mImage.mvRGB.empty() ? "" : std::string(mtmd_default_marker()) + "\n") + aRequest.msPrompt);
    std::vector<llama_chat_message> vMessages;
    if(!sSystem.empty()) vMessages.push_back({"system", sSystem.c_str()});
    size_t i = 0;
    for(const cChatTurn& turn : avTurns)
    {
        vMessages.push_back({"user", vUsers[i++].c_str()});
        vMessages.push_back({"assistant", turn.msReply.c_str()});
    }
    vMessages.push_back({"user", vUsers.back().c_str()});
    const char* pTemplate = aConfig.msChatTemplate.empty() ?
        llama_model_chat_template(apModel, nullptr) : aConfig.msChatTemplate.c_str();
    if(!pTemplate) throw std::runtime_error("The model has no chat template; set msChatTemplate explicitly.");
    int n = llama_chat_apply_template(pTemplate, vMessages.data(), vMessages.size(), true, nullptr, 0);
    if(n <= 0) throw std::runtime_error("Unsupported model chat template; supply a supported msChatTemplate override.");
    std::vector<char> vBuffer(static_cast<size_t>(n) + 1);
    int written = llama_chat_apply_template(pTemplate, vMessages.data(), vMessages.size(), true,
                                           vBuffer.data(), static_cast<int>(vBuffer.size()));
    if(written != n) throw std::runtime_error("Could not format the model chat template.");
    return std::string(vBuffer.data(), static_cast<size_t>(n));
}

std::string TokenPiece(const llama_vocab* apVocab, llama_token alToken, bool abSpecial)
{
    char piece[256];
    int length = llama_token_to_piece(apVocab, alToken, piece, sizeof(piece), 0, abSpecial);
    if(length >= 0) return std::string(piece, static_cast<size_t>(length));
    std::vector<char> buffer(static_cast<size_t>(-length));
    length = llama_token_to_piece(apVocab, alToken, buffer.data(), static_cast<int>(buffer.size()), 0, abSpecial);
    if(length < 0) throw std::runtime_error("Could not decode an output token.");
    return std::string(buffer.data(), static_cast<size_t>(length));
}

struct cPreparedPrompt
{
    cPreparedPrompt() : mChunks(nullptr, mtmd_input_chunks_free), mlTextTokens(0), mlImageTokens(0) {}
    tChunks mChunks;
    std::vector<llama_token> mvTokens;
    size_t mlTextTokens, mlImageTokens;
    size_t Count() const { return mlTextTokens + mlImageTokens; }
};

void PreparePrompt(const std::string& asChat, const std::vector<const cLlamaImage*>& avImages,
                   const llama_vocab* apVocab, mtmd_context* apProjector, bool abFirst,
                   cPreparedPrompt& aPrepared)
{
    if(avImages.empty())
    {
        int count = llama_tokenize(apVocab, asChat.data(), static_cast<int>(asChat.size()), nullptr, 0, abFirst, true);
        if(count >= 0 || count == std::numeric_limits<int>::min())
            throw std::runtime_error("Could not tokenize the prompt.");
        aPrepared.mvTokens.resize(static_cast<size_t>(-count));
        if(llama_tokenize(apVocab, asChat.data(), static_cast<int>(asChat.size()),
                          aPrepared.mvTokens.data(), -count, abFirst, true) != -count)
            throw std::runtime_error("Could not tokenize the prompt.");
        aPrepared.mlTextTokens = aPrepared.mvTokens.size();
        return;
    }
    std::vector<tBitmap> vBitmaps;
    std::vector<const mtmd_bitmap*> vInputs;
    for(const cLlamaImage* image : avImages)
    {
        vBitmaps.emplace_back(mtmd_bitmap_init(image->mlWidth, image->mlHeight, image->mvRGB.data()), mtmd_bitmap_free);
        if(!vBitmaps.back()) throw std::runtime_error("Could not allocate image inputs.");
        vInputs.push_back(vBitmaps.back().get());
    }
    aPrepared.mChunks.reset(mtmd_input_chunks_init());
    mtmd_input_text text = {asChat.data(), asChat.size(), abFirst, true};
    if(!aPrepared.mChunks || mtmd_tokenize(apProjector, aPrepared.mChunks.get(), &text,
                                          vInputs.data(), vInputs.size()) != 0)
        throw std::runtime_error("Multimodal preprocessing failed; do not put media markers in prompts.");
    for(size_t i = 0; i < mtmd_input_chunks_size(aPrepared.mChunks.get()); ++i)
    {
        const mtmd_input_chunk* chunk = mtmd_input_chunks_get(aPrepared.mChunks.get(), i);
        size_t count = mtmd_input_chunk_get_n_tokens(chunk);
        if(mtmd_input_chunk_get_type(chunk) == MTMD_INPUT_CHUNK_TYPE_TEXT) aPrepared.mlTextTokens += count;
        else aPrepared.mlImageTokens += count;
    }
}
#endif
}

struct cLlamaInference::cImpl
{
    struct cControl
    {
        cControl() : mbCancelled(false), mbFinished(false), mlSessionId(0) {}
        std::atomic<bool> mbCancelled;
        bool mbFinished; // Protected by mMutex.
        uint64_t mlSessionId;
    };
    struct cJob
    {
        uint64_t mlId;
        cLlamaRequest mRequest;
        std::shared_ptr<cControl> mpControl;
    };

    struct cSessionCommand
    {
        uint64_t mlId;
        bool mbReset, mbAccepted;
        std::string msReason;
    };
    cImpl() : mState(eLlamaState_Unloaded), mbStop(false), mlNextId(1),
              mlPendingSessionRequest(0), mlPendingSessionId(0) {}
    mutable std::mutex mMutex;
    std::condition_variable mWake;
    std::thread mWorker;
    eLlamaState mState;
    std::string msLastError;
    cLlamaModelConfig mConfig;
    std::atomic<bool> mbStop;
    uint64_t mlNextId;
    std::deque<cJob> mJobs;
    std::deque<cLlamaResult> mResults;
    std::map<uint64_t, std::shared_ptr<cControl> > mOutstanding;
    std::deque<cSessionCommand> mSessionCommands;
    uint64_t mlPendingSessionRequest, mlPendingSessionId; // Protected by mMutex, survives PollResult.
    cLlamaContextStats mContextStats;
    // Only the worker reads/writes this pointer. Other threads set atomics on
    // the same control object through mOutstanding.
    std::shared_ptr<cControl> mpActive;

    bool ShouldAbort() const
    {
        return mbStop.load() || (mpActive && mpActive->mbCancelled.load());
    }

#if defined(HPL2_WITH_LLAMA) && HPL2_WITH_LLAMA
    struct cSession
    {
        cSession() : mlId(0), mlPendingId(0), mbCacheValid(false), mlPast(0) {}
        uint64_t mlId, mlPendingId;
        std::string msSystem, msSummary, msCacheChat;
        std::deque<cChatTurn> mvTurns;
        cChatTurn mPending;
        bool mbCacheValid;
        llama_pos mlPast; // M-RoPE position; deliberately separate from KV token occupancy.
        cLlamaContextStats mStats;
    } mSession; // All history, prompt formatting and native KV state belong to the worker.

    void PublishStats()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mContextStats = mSession.mStats;
    }

    void DiscardProvisional(llama_context* apContext, const char* asReason)
    {
        mSession.mbCacheValid = false;
        mSession.mlPendingId = 0;
        mSession.mPending = cChatTurn();
        mSession.mStats.mlUsedTokens = mSession.mStats.mlTextTokens = mSession.mStats.mlImageTokens =
            mSession.mStats.mlOutputTokens = mSession.mStats.mlReservedTokens = mSession.mStats.mlReusedTokens = 0;
        mSession.mStats.mlTurns = static_cast<unsigned>(mSession.mvTurns.size());
        mSession.mStats.mlObservations = 0;
        for(const cChatTurn& turn : mSession.mvTurns) if(!turn.mImage.mvRGB.empty()) ++mSession.mStats.mlObservations;
        mSession.mStats.mbAwaitingResolution = false;
        mSession.mStats.msRefreshReason = asReason;
        llama_memory_clear(llama_get_memory(apContext), false);
    }

    void ApplySessionCommand(const cSessionCommand& aCommand, llama_context* apContext)
    {
        if(aCommand.mbReset)
        {
            if(aCommand.mlId != mSession.mlId) return;
            mSession = cSession();
            mSession.mStats.msRefreshReason = aCommand.msReason;
            llama_memory_clear(llama_get_memory(apContext), true);
        }
        else
        {
            if(aCommand.mlId != mSession.mlPendingId) return;
            if(aCommand.mbAccepted)
            {
                mSession.mvTurns.push_back(std::move(mSession.mPending));
                mSession.mStats.mbAwaitingResolution = false;
            }
            else
            {
                DiscardProvisional(apContext, "Rejected decision; rebuilding accepted history");
            }
            mSession.mlPendingId = 0;
            mSession.mPending = cChatTurn();
        }
        PublishStats();
    }

    void EvaluatePrompt(const cPreparedPrompt& aPrepared, llama_context* apContext,
                        mtmd_context* apProjector, llama_pos& alPast)
    {
        if(aPrepared.mChunks)
        {
            if(mtmd_helper_eval_chunks(apProjector, apContext, aPrepared.mChunks.get(), alPast, 0,
                                      mConfig.mlBatchSize, true, &alPast) != 0)
                throw std::runtime_error("Image/prompt evaluation failed.");
            return;
        }
        cBatch batch(mConfig.mlBatchSize);
        for(size_t offset = 0; offset < aPrepared.mvTokens.size();)
        {
            if(ShouldAbort()) throw std::runtime_error("Cancelled prompt evaluation.");
            batch.mBatch.n_tokens = static_cast<int>(std::min(static_cast<size_t>(mConfig.mlBatchSize), aPrepared.mvTokens.size() - offset));
            for(int i = 0; i < batch.mBatch.n_tokens; ++i)
            {
                batch.mBatch.token[i] = aPrepared.mvTokens[offset + i];
                batch.mBatch.pos[i] = alPast++;
                batch.mBatch.n_seq_id[i] = 1;
                batch.mBatch.seq_id[i][0] = 0;
                batch.mBatch.logits[i] = (offset + i + 1 == aPrepared.mvTokens.size());
            }
            if(llama_decode(apContext, batch.mBatch) != 0) throw std::runtime_error("Prompt evaluation failed.");
            offset += batch.mBatch.n_tokens;
        }
    }
    static bool AbortCallback(void* apData)
    {
        return static_cast<cImpl*>(apData)->ShouldAbort();
    }
    static bool LoadProgress(float, void* apData)
    {
        return !static_cast<cImpl*>(apData)->mbStop.load();
    }

    cLlamaResult Generate(const cJob& aJob, llama_model* apModel, llama_context* apContext,
                          mtmd_context* apProjector)
    {
        cLlamaResult result;
        result.mlRequestId = aJob.mlId;
        const cLlamaRequest& request = aJob.mRequest;
        try
        {
            if(ShouldAbort()) { result.mbCancelled = true; return result; }
            const bool bSession = request.mlSessionId != 0;
            const llama_vocab* pVocab = llama_model_get_vocab(apModel);
            const size_t lContext = llama_n_ctx(apContext);
            // Persistent turns must also decode their terminating token into KV.
            const size_t lReserve = static_cast<size_t>(request.mlMaxTokens) + (bSession ? 1 : 0);
            llama_pos lPast = 0;
            cLlamaContextStats stats;
            stats.mlSessionId = request.mlSessionId;
            stats.mlCapacityTokens = static_cast<unsigned>(lContext);
            stats.mlReservedTokens = static_cast<unsigned>(lReserve);
            std::deque<cChatTurn> emptyTurns;
            if(bSession)
            {
                if(mSession.mlId != request.mlSessionId || mSession.msSystem != request.msSystemPrompt)
                {
                    mSession = cSession();
                    mSession.mlId = request.mlSessionId;
                    mSession.msSystem = request.msSystemPrompt;
                    mSession.mStats.mlSessionId = request.mlSessionId;
                    mSession.mStats.mlCapacityTokens = static_cast<unsigned>(lContext);
                    mSession.mStats.msRefreshReason = "New session";
                }
                stats = mSession.mStats;
                stats.mlCapacityTokens = static_cast<unsigned>(lContext);
                stats.mlReservedTokens = static_cast<unsigned>(lReserve);
                stats.mlReusedTokens = 0;
                stats.mbAwaitingResolution = false;
            }
            else if(mSession.mlId != 0)
            {
                DiscardProvisional(apContext, "Independent request; rebuilding retained session on next turn");
                PublishStats();
            }

            std::deque<cChatTurn>& turns = bSession ? mSession.mvTurns : emptyTurns;
            std::string sChat = FormatChat(apModel, mConfig, request, turns, bSession ? mSession.msSummary : "");
            bool bReuse = bSession && mSession.mbCacheValid &&
                sChat.compare(0, mSession.msCacheChat.size(), mSession.msCacheChat) == 0;
            std::unique_ptr<cPreparedPrompt> prepared(new cPreparedPrompt());
            std::vector<const cLlamaImage*> images;
            if(bReuse)
            {
                if(!request.mImage.mvRGB.empty()) images.push_back(&request.mImage);
                PreparePrompt(sChat.substr(mSession.msCacheChat.size()), images, pVocab, apProjector, false, *prepared);
            }
            else
            {
                for(const cChatTurn& turn : turns) if(!turn.mImage.mvRGB.empty()) images.push_back(&turn.mImage);
                if(!request.mImage.mvRGB.empty()) images.push_back(&request.mImage);
                PreparePrompt(sChat, images, pVocab, apProjector, true, *prepared);
            }
            size_t nTokens = prepared->Count() + (bReuse ? mSession.mStats.mlUsedTokens : 0);
            size_t nBytes = request.mImage.mvRGB.size() + request.msPrompt.size();
            for(const cChatTurn& turn : turns) nBytes += turn.mImage.mvRGB.size() + turn.msPrompt.size() + turn.msReply.size();
            const bool bStorageLimit = turns.size() >= 64 || nBytes > 256 * 1024 * 1024;
            const bool bRefresh = bSession && !turns.empty() &&
                (nTokens + lReserve > static_cast<size_t>(lContext * request.mfSessionRefreshFraction) || bStorageLimit);
            if(bRefresh)
            {
                while(turns.size() > request.mlSessionKeepTurns) turns.pop_front();
                mSession.msSummary = request.msContextSummary;
                stats.mlRefreshCount++;
                stats.msRefreshReason = bStorageLimit ? "History storage limit" : "Context budget";
                bReuse = false;
                // Rebuild rather than shifting M-RoPE positions or assuming their
                // numerical extent equals the number of occupied embedding cells.
                for(;;)
                {
                    sChat = FormatChat(apModel, mConfig, request, turns, mSession.msSummary);
                    images.clear();
                    for(const cChatTurn& turn : turns) if(!turn.mImage.mvRGB.empty()) images.push_back(&turn.mImage);
                    if(!request.mImage.mvRGB.empty()) images.push_back(&request.mImage);
                    prepared.reset(new cPreparedPrompt());
                    PreparePrompt(sChat, images, pVocab, apProjector, true, *prepared);
                    nTokens = prepared->Count();
                    nBytes = request.mImage.mvRGB.size() + request.msPrompt.size();
                    for(const cChatTurn& turn : turns) nBytes += turn.mImage.mvRGB.size() + turn.msPrompt.size() + turn.msReply.size();
                    if((nTokens + lReserve <= static_cast<size_t>(lContext * request.mfSessionRefreshFraction) &&
                        nBytes <= 256 * 1024 * 1024) || turns.empty()) break;
                    turns.pop_front();
                }
                mSession.mbCacheValid = false;
            }
            if(nTokens == 0 || nTokens > lContext || lReserve > lContext - nTokens)
                throw std::runtime_error("Image and prompt exceed the context budget reserved for generation.");
            if(ShouldAbort()) { result.mbCancelled = true; return result; }
            if(bReuse)
            {
                lPast = mSession.mlPast;
                stats.mlReusedTokens = mSession.mStats.mlUsedTokens;
                stats.mlTextTokens += static_cast<unsigned>(prepared->mlTextTokens);
                stats.mlImageTokens += static_cast<unsigned>(prepared->mlImageTokens);
            }
            else
            {
                llama_memory_clear(llama_get_memory(apContext), true);
                stats.mlOutputTokens = 0;
                for(const cChatTurn& turn : turns) stats.mlOutputTokens += turn.mlOutputTokens;
                stats.mlTextTokens = static_cast<unsigned>(prepared->mlTextTokens);
                // Rebuilt transcripts tokenize assistant bodies as text; classify
                // their retained generated tokens separately in debug accounting.
                stats.mlOutputTokens = std::min(stats.mlOutputTokens, stats.mlTextTokens);
                stats.mlTextTokens -= stats.mlOutputTokens;
                stats.mlImageTokens = static_cast<unsigned>(prepared->mlImageTokens);
            }
            stats.mlUsedTokens = static_cast<unsigned>(nTokens);
            stats.mlTurns = static_cast<unsigned>(turns.size());
            stats.mlObservations = request.mImage.mvRGB.empty() ? 0 : 1;
            for(const cChatTurn& turn : turns) if(!turn.mImage.mvRGB.empty()) ++stats.mlObservations;
            EvaluatePrompt(*prepared, apContext, apProjector, lPast);
            if(bSession)
            {
                mSession.mStats = stats;
                PublishStats();
            }
            tSampler sampler(llama_sampler_chain_init(llama_sampler_chain_default_params()), llama_sampler_free);
            if(!sampler) throw std::runtime_error("Could not allocate a sampler.");
            if(!request.msGrammar.empty())
            {
                // Filter logits before either greedy or probabilistic selection.
                // llama_sampler_sample accepts chosen tokens into the whole chain.
                tSampler grammar(llama_sampler_init_grammar(pVocab, request.msGrammar.c_str(), "root"), llama_sampler_free);
                if(!grammar) throw std::runtime_error("Could not initialize the request's GBNF grammar.");
                llama_sampler_chain_add(sampler.get(), grammar.get());
                grammar.release();
            }
            if(request.mfTemperature == 0)
                llama_sampler_chain_add(sampler.get(), llama_sampler_init_greedy());
            else
            {
                llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_k(40));
                llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_p(0.95f, 1));
                llama_sampler_chain_add(sampler.get(), llama_sampler_init_temp(request.mfTemperature));
                llama_sampler_chain_add(sampler.get(), llama_sampler_init_dist(request.mlSeed));
            }
            cBatch batch(1);
            bool bComplete = false;
            std::string sDecodedReply;
            for(int i = 0; i < request.mlMaxTokens; ++i)
            {
                if(ShouldAbort()) { result.mbCancelled = true; break; }
                llama_token token = llama_sampler_sample(sampler.get(), apContext, -1);
                bComplete = llama_vocab_is_eog(pVocab, token);
                if(!bComplete)
                {
                    result.msText += TokenPiece(pVocab, token, false);
                    sDecodedReply += TokenPiece(pVocab, token, true);
                    ++result.mlGeneratedTokens;
                }
                else if(bSession) sDecodedReply += TokenPiece(pVocab, token, true);
                // Stateless consumers historically permit partial output. A
                // session evaluates every output token and its closure so the
                // next suffix continues after a complete assistant turn.
                if((bComplete && !bSession) || (!bSession && i + 1 == request.mlMaxTokens)) break;
                batch.mBatch.n_tokens = 1;
                batch.mBatch.token[0] = token;
                batch.mBatch.pos[0] = lPast++;
                batch.mBatch.n_seq_id[0] = 1;
                batch.mBatch.seq_id[0][0] = 0;
                batch.mBatch.logits[0] = true;
                if(llama_decode(apContext, batch.mBatch) != 0)
                    throw std::runtime_error("Generation evaluation failed.");
                ++stats.mlUsedTokens;
                if(stats.mlReservedTokens > 0) --stats.mlReservedTokens;
                if(bComplete) stats.mlReservedTokens = 0;
                if(bComplete) ++stats.mlTextTokens;
                else ++stats.mlOutputTokens;
                if(bSession && (bComplete || (i + 1) % 8 == 0))
                {
                    mSession.mStats = stats;
                    PublishStats();
                }
                if(bComplete) break;
            }
            result.mbTruncated = !bComplete && !result.mbCancelled;
            stats.mlReservedTokens = 0;
            result.mContextStats = stats;
            if(bSession && !result.mbCancelled && !result.mbTruncated && !ShouldAbort())
            {
                NormalizeUTF8(result.msText);
                mSession.mPending.msPrompt = request.msPrompt;
                mSession.mPending.mImage = request.mImage;
                mSession.mPending.msReply = result.msText;
                mSession.mPending.mlOutputTokens = static_cast<unsigned>(result.mlGeneratedTokens);
                mSession.mlPendingId = aJob.mlId;
                mSession.msCacheChat = sChat + sDecodedReply;
                mSession.mlPast = lPast;
                mSession.mbCacheValid = true;
                stats.mbAwaitingResolution = true;
                ++stats.mlTurns;
                mSession.mStats = stats;
                result.mContextStats = stats;
                PublishStats();
            }
        }
        catch(const std::exception& e)
        {
            if(ShouldAbort()) result.mbCancelled = true;
            else result.msError = e.what();
        }
        NormalizeUTF8(result.msText);
        return result;
    }

    void Run()
    {
        try
        {
            cBackendLease lease;
            llama_model_params modelParams = llama_model_default_params();
            modelParams.n_gpu_layers = mConfig.mlGpuLayers;
            // n_gpu_layers=0 alone still leaves GPU devices available to llama's
            // scheduler. An explicit empty list preserves a true CPU-only mode
            // in CUDA-enabled builds, including context and host operations.
            ggml_backend_dev_t cpuDevices[] = {nullptr};
            if(mConfig.mlGpuLayers == 0) modelParams.devices = cpuDevices;
            modelParams.progress_callback = LoadProgress;
            modelParams.progress_callback_user_data = this;
            tModel model(llama_model_load_from_file(mConfig.msModelPath.c_str(), modelParams), llama_model_free);
            if(mbStop.load()) return;
            if(!model) throw std::runtime_error("Could not load the local GGUF language model.");
            if(llama_model_has_encoder(model.get()))
                throw std::runtime_error("This service requires a decoder-only language model.");
            llama_context_params contextParams = llama_context_default_params();
            contextParams.n_ctx = mConfig.mlContextSize;
            contextParams.n_batch = mConfig.mlBatchSize;
            contextParams.n_ubatch = mConfig.mlBatchSize;
            contextParams.n_threads = mConfig.mlThreads;
            contextParams.n_threads_batch = mConfig.mlThreads;
            contextParams.offload_kqv = mConfig.mlGpuLayers > 0;
            contextParams.op_offload = mConfig.mlGpuLayers > 0;
            contextParams.abort_callback = AbortCallback;
            contextParams.abort_callback_data = this;
            tContext context(llama_init_from_model(model.get(), contextParams), llama_free);
            if(mbStop.load()) return;
            if(!context) throw std::runtime_error("Could not allocate the language model context.");
            if(!llama_get_memory(context.get()))
                throw std::runtime_error("This service requires a decoder-only language model with a KV cache.");
            tProjector projector(nullptr, mtmd_free);
            if(!mConfig.msProjectorPath.empty())
            {
                mtmd_context_params params = mtmd_context_params_default();
                params.use_gpu = mConfig.mlGpuLayers > 0;
                params.n_threads = mConfig.mlThreads;
                params.print_timings = false;
                params.warmup = false;
                if(mConfig.mlMinImageTokens > 0) params.image_min_tokens = mConfig.mlMinImageTokens;
                params.image_max_tokens = mConfig.mlMaxImageTokens;
                params.progress_callback = LoadProgress;
                params.progress_callback_user_data = this;
                projector.reset(mtmd_init_from_file(mConfig.msProjectorPath.c_str(), model.get(), params));
                if(mbStop.load()) return;
                if(!projector || !mtmd_support_vision(projector.get()))
                    throw std::runtime_error("Could not load a matching vision projector GGUF.");
            }
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if(mbStop.load()) return;
                mState = eLlamaState_Ready;
            }
            while(!mbStop.load())
            {
                cJob job;
                cSessionCommand command;
                bool bCommand = false;
                {
                    std::unique_lock<std::mutex> lock(mMutex);
                    mWake.wait(lock, [this] { return mbStop.load() || !mJobs.empty() || !mSessionCommands.empty(); });
                    if(mbStop.load()) break;
                    if(!mSessionCommands.empty())
                    {
                        command = std::move(mSessionCommands.front());
                        mSessionCommands.pop_front();
                        bCommand = true;
                    }
                    else
                    {
                        job = std::move(mJobs.front());
                        mJobs.pop_front();
                        mpActive = job.mpControl;
                    }
                }
                if(bCommand) { ApplySessionCommand(command, context.get()); continue; }
                cLlamaResult result = Generate(job, model.get(), context.get(), projector.get());
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    // Make a cancellation received just after the final token visible.
                    result.mbCancelled = result.mbCancelled || job.mpControl->mbCancelled.load();
                    if(job.mRequest.mlSessionId != 0 && (result.mbCancelled || result.mbTruncated || !result.msError.empty()))
                    {
                        if(mSession.mlId == job.mRequest.mlSessionId)
                        {
                            DiscardProvisional(context.get(), result.mbCancelled ? "Cancelled decision; rebuilding accepted history" :
                                (result.mbTruncated ? "Incomplete reply; rebuilding accepted history" : "Failed request; rebuilding accepted history"));
                            mContextStats = mSession.mStats;
                            result.mContextStats = mSession.mStats;
                        }
                        if(mlPendingSessionRequest == job.mlId) mlPendingSessionRequest = mlPendingSessionId = 0;
                    }
                    job.mpControl->mbFinished = true;
                    if(!mbStop.load()) mResults.push_back(std::move(result));
                    mpActive.reset();
                }
            }
        }
        catch(const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if(!mbStop.load()) { msLastError = e.what(); mState = eLlamaState_Failed; }
        }
        catch(...)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if(!mbStop.load()) { msLastError = "Unexpected native inference failure."; mState = eLlamaState_Failed; }
        }
    }
#endif
};

cLlamaInference::cLlamaInference() : mpImpl(new cImpl) {}
cLlamaInference::~cLlamaInference() { Unload(); delete mpImpl; }

bool cLlamaInference::IsSupported()
{
#if defined(HPL2_WITH_LLAMA) && HPL2_WITH_LLAMA
    return true;
#else
    return false;
#endif
}

bool cLlamaInference::IsGpuSupported()
{
#if defined(HPL2_WITH_LLAMA) && HPL2_WITH_LLAMA
    // Serialize device discovery with process-wide backend initialization.
    std::lock_guard<std::mutex> lock(gBackendMutex);
    if(!ggml_backend_reg_count()) ggml_backend_load_all();
    return ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU) != nullptr ||
           ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU) != nullptr;
#else
    return false;
#endif
}

bool cLlamaInference::ValidateRequest(const cLlamaRequest& aRequest, std::string& asError)
{
    asError.clear();
    const size_t lMaxPromptBytes = 1024 * 1024;
    if(aRequest.msGrammar.find('\0') != std::string::npos || aRequest.msGrammar.size() > 64 * 1024)
        return Reject(asError, "Grammar must be NUL-free and at most 64 KiB.");
    if(aRequest.msContextSummary.size() > 8 * 1024 || aRequest.msContextSummary.find('\0') != std::string::npos ||
       aRequest.mlSessionKeepTurns < 1 || aRequest.mlSessionKeepTurns > 16 ||
       !std::isfinite(aRequest.mfSessionRefreshFraction) || aRequest.mfSessionRefreshFraction < 0.25f ||
       aRequest.mfSessionRefreshFraction > 0.95f)
        return Reject(asError, "Invalid session summary, retained turn limit or refresh fraction.");
    if(aRequest.msPrompt.empty() || aRequest.msPrompt.find('\0') != std::string::npos ||
       aRequest.msSystemPrompt.find('\0') != std::string::npos ||
       aRequest.msPrompt.size() > lMaxPromptBytes || aRequest.msSystemPrompt.size() > lMaxPromptBytes - aRequest.msPrompt.size())
        return Reject(asError, "Prompts must be nonempty, NUL-free and at most 1 MiB combined.");
    if(aRequest.mlMaxTokens < 1 || aRequest.mlMaxTokens > 131072 ||
       !std::isfinite(aRequest.mfTemperature) || aRequest.mfTemperature < 0 || aRequest.mfTemperature > 5)
        return Reject(asError, "Invalid generation token limit or temperature.");
    const cLlamaImage& image = aRequest.mImage;
    if(image.mlWidth == 0 && image.mlHeight == 0 && image.mvRGB.empty()) return true;
    const size_t lMaxImageBytes = 64 * 1024 * 1024;
    if(image.mlWidth == 0 || image.mlHeight == 0 || image.mlWidth > 8192 || image.mlHeight > 8192 ||
       static_cast<size_t>(image.mlWidth) > lMaxImageBytes / 3 / image.mlHeight ||
       image.mvRGB.size() != static_cast<size_t>(image.mlWidth) * image.mlHeight * 3)
        return Reject(asError, "Image must be packed RGB8 with valid dimensions and at most 64 MiB.");
    return true;
}

bool cLlamaInference::LoadAsync(const cLlamaModelConfig& aConfig, std::string& asError)
{
    asError.clear();
    if(!ValidateConfig(aConfig, asError)) return false;
    if(!IsSupported()) return Reject(asError, "HPL2 was built without llama.cpp; enable HPL2WithLlama/HPL2_WITH_LLAMA.");
#if defined(HPL2_WITH_LLAMA) && HPL2_WITH_LLAMA
    if(aConfig.mlGpuLayers > 0 && !IsGpuSupported())
        return Reject(asError, "No GPU device is available to the embedded backend; build with CUDA and a compatible driver, or set mlGpuLayers to zero.");
#endif
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    if(mpImpl->mState != eLlamaState_Unloaded)
        return Reject(asError, "Unload the current model before loading another.");
#if defined(HPL2_WITH_LLAMA) && HPL2_WITH_LLAMA
    mpImpl->mConfig = aConfig;
    mpImpl->msLastError.clear();
    mpImpl->mbStop.store(false);
    mpImpl->mState = eLlamaState_Loading;
    try { mpImpl->mWorker = std::thread(&cImpl::Run, mpImpl); }
    catch(const std::exception& e)
    {
        mpImpl->msLastError = asError = e.what();
        mpImpl->mState = eLlamaState_Failed;
        return false;
    }
    return true;
#else
    return false;
#endif
}

eLlamaState cLlamaInference::GetState() const
{
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    return mpImpl->mState;
}

std::string cLlamaInference::GetLastError() const
{
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    return mpImpl->msLastError;
}

uint64_t cLlamaInference::Submit(const cLlamaRequest& aRequest, std::string& asError)
{
    if(!ValidateRequest(aRequest, asError)) return 0;
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    if(mpImpl->mState != eLlamaState_Ready)
    { Reject(asError, "The language model is not ready."); return 0; }
    if(aRequest.mlMaxTokens >= mpImpl->mConfig.mlContextSize)
    { Reject(asError, "Generation token limit must leave context space for the prompt."); return 0; }
    if(!aRequest.mImage.mvRGB.empty() && mpImpl->mConfig.msProjectorPath.empty())
    { Reject(asError, "Load a matching vision projector before submitting images."); return 0; }
    if(mpImpl->mlPendingSessionRequest != 0)
    { Reject(asError, "Resolve the previous persistent reply before submitting another session turn."); return 0; }
    if(mpImpl->mOutstanding.size() >= mpImpl->mConfig.mlMaxOutstandingRequests || mpImpl->mlNextId == 0)
    { Reject(asError, "Inference request limit reached; consume results before submitting more."); return 0; }
    cImpl::cJob job;
    job.mlId = mpImpl->mlNextId;
    job.mRequest = aRequest;
    job.mpControl = std::make_shared<cImpl::cControl>();
    job.mpControl->mlSessionId = aRequest.mlSessionId;
    mpImpl->mOutstanding.insert(std::make_pair(job.mlId, job.mpControl));
    try { mpImpl->mJobs.push_back(std::move(job)); }
    catch(...) { mpImpl->mOutstanding.erase(mpImpl->mlNextId); throw; }
    uint64_t id = mpImpl->mlNextId++;
    if(aRequest.mlSessionId != 0)
    {
        mpImpl->mlPendingSessionRequest = id;
        mpImpl->mlPendingSessionId = aRequest.mlSessionId;
    }
    mpImpl->mWake.notify_one();
    return id;
}

bool cLlamaInference::Cancel(uint64_t alRequestId)
{
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    auto it = mpImpl->mOutstanding.find(alRequestId);
    if(it == mpImpl->mOutstanding.end() || it->second->mbFinished) return false;
    it->second->mbCancelled.store(true);
    return true;
}

bool cLlamaInference::PollResult(cLlamaResult& aResult)
{
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    if(mpImpl->mResults.empty()) return false;
    aResult = std::move(mpImpl->mResults.front());
    mpImpl->mResults.pop_front();
    mpImpl->mOutstanding.erase(aResult.mlRequestId);
    return true;
}

bool cLlamaInference::ResolveSessionTurn(uint64_t alRequestId, bool abAccepted)
{
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    if(alRequestId == 0 || mpImpl->mlPendingSessionRequest != alRequestId ||
       !mpImpl->mContextStats.mbAwaitingResolution || mpImpl->mOutstanding.find(alRequestId) != mpImpl->mOutstanding.end()) return false;
    cImpl::cSessionCommand command;
    command.mlId = alRequestId;
    command.mbReset = false;
    command.mbAccepted = abAccepted;
    mpImpl->mSessionCommands.push_back(std::move(command));
    mpImpl->mlPendingSessionRequest = mpImpl->mlPendingSessionId = 0;
    mpImpl->mWake.notify_one();
    return true;
}

void cLlamaInference::ResetSession(uint64_t alSessionId, const std::string& asReason)
{
    if(alSessionId == 0) return;
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    if(mpImpl->mState != eLlamaState_Ready && mpImpl->mState != eLlamaState_Loading) return;
    for(auto& entry : mpImpl->mOutstanding)
        if(entry.second->mlSessionId == alSessionId) entry.second->mbCancelled.store(true);
    if(mpImpl->mlPendingSessionId == alSessionId) mpImpl->mlPendingSessionRequest = mpImpl->mlPendingSessionId = 0;
    cImpl::cSessionCommand command;
    command.mlId = alSessionId;
    command.mbReset = true;
    command.mbAccepted = false;
    command.msReason = asReason.substr(0, 256);
    mpImpl->mSessionCommands.push_back(std::move(command));
    mpImpl->mWake.notify_one();
}

cLlamaContextStats cLlamaInference::GetContextStats(uint64_t alSessionId) const
{
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    return alSessionId != 0 && mpImpl->mContextStats.mlSessionId == alSessionId ? mpImpl->mContextStats : cLlamaContextStats();
}

void cLlamaInference::Unload()
{
    {
        std::lock_guard<std::mutex> lock(mpImpl->mMutex);
        mpImpl->mbStop.store(true);
        if(mpImpl->mWorker.joinable()) mpImpl->mState = eLlamaState_Stopping;
        mpImpl->mWake.notify_all();
    }
    if(mpImpl->mWorker.joinable()) mpImpl->mWorker.join();
    std::lock_guard<std::mutex> lock(mpImpl->mMutex);
    mpImpl->mJobs.clear();
    mpImpl->mResults.clear();
    mpImpl->mOutstanding.clear();
    mpImpl->mSessionCommands.clear();
    mpImpl->mlPendingSessionRequest = mpImpl->mlPendingSessionId = 0;
    mpImpl->mContextStats = cLlamaContextStats();
#if defined(HPL2_WITH_LLAMA) && HPL2_WITH_LLAMA
    mpImpl->mSession = cImpl::cSession();
#endif
    mpImpl->mpActive.reset();
    mpImpl->msLastError.clear();
    mpImpl->mState = eLlamaState_Unloaded;
}

}
