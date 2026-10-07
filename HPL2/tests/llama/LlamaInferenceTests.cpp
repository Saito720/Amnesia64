#include "ai/LlamaInference.h"

#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void Require(bool abCondition, const std::string& asMessage)
{
    if(!abCondition) throw std::runtime_error(asMessage);
}

hpl::cLlamaRequest MakeRequest()
{
    hpl::cLlamaRequest request;
    request.msPrompt = "Reply with a short greeting.";
    return request;
}

void ExpectInvalidRequest(const hpl::cLlamaRequest& aRequest, const char* asDescription)
{
    std::string error;
    Require(!hpl::cLlamaInference::ValidateRequest(aRequest, error),
            std::string("Accepted invalid request: ") + asDescription);
    Require(!error.empty(), std::string("Missing validation error: ") + asDescription);
}

void TestRequestValidation()
{
    hpl::cLlamaRequest request = MakeRequest();
    std::string error;
    Require(hpl::cLlamaInference::ValidateRequest(request, error), "Valid text request rejected: " + error);

    request.mImage.mlWidth = 1;
    request.mImage.mlHeight = 1;
    request.mImage.mvRGB.assign(3, 127);
    Require(hpl::cLlamaInference::ValidateRequest(request, error), "Valid RGB request rejected: " + error);

    request.mImage.mvRGB.pop_back();
    ExpectInvalidRequest(request, "short RGB buffer");
    request.mImage.mvRGB.assign(4, 0);
    ExpectInvalidRequest(request, "oversized RGB buffer");
    request.mImage.mvRGB.clear();
    ExpectInvalidRequest(request, "image dimensions without pixels");
    request.mImage.mlHeight = 0;
    ExpectInvalidRequest(request, "partially specified image dimensions");
    request.mImage.mlWidth = 0;
    request.mImage.mvRGB.assign(3, 0);
    ExpectInvalidRequest(request, "pixels without dimensions");
    request.mImage.mlWidth = std::numeric_limits<unsigned int>::max();
    request.mImage.mlHeight = std::numeric_limits<unsigned int>::max();
    ExpectInvalidRequest(request, "image dimensions that could overflow byte count");

    request = MakeRequest();
    request.msContextSummary.assign(8 * 1024 + 1, 'x');
    ExpectInvalidRequest(request, "oversized context summary");
    request = MakeRequest();
    request.msContextSummary.assign("x\0y", 3);
    ExpectInvalidRequest(request, "NUL in context summary");
    request = MakeRequest();
    request.mlSessionKeepTurns = 0;
    ExpectInvalidRequest(request, "zero retained session turns");
    request.mlSessionKeepTurns = 17;
    ExpectInvalidRequest(request, "too many retained session turns");
    request = MakeRequest();
    request.mfSessionRefreshFraction = std::numeric_limits<float>::quiet_NaN();
    ExpectInvalidRequest(request, "nonfinite refresh fraction");
    request.mfSessionRefreshFraction = 0.2f;
    ExpectInvalidRequest(request, "refresh fraction below minimum");
    request.mfSessionRefreshFraction = 1.0f;
    ExpectInvalidRequest(request, "refresh fraction above maximum");
    request = MakeRequest();
    request.msPrompt.clear();
    ExpectInvalidRequest(request, "empty prompt");
    request.msPrompt.assign("hello\0world", 11);
    ExpectInvalidRequest(request, "embedded NUL in prompt");
    request = MakeRequest();
    request.msSystemPrompt.assign("hello\0world", 11);
    ExpectInvalidRequest(request, "embedded NUL in system prompt");
    request = MakeRequest();
    request.msPrompt.assign(1024 * 1024 + 1, 'x');
    ExpectInvalidRequest(request, "prompt beyond the byte limit");
    request.msPrompt.assign(512 * 1024, 'x');
    request.msSystemPrompt.assign(512 * 1024 + 1, 'x');
    ExpectInvalidRequest(request, "combined prompts beyond the byte limit");
    request = MakeRequest();
    request.msGrammar = "root ::= \"ok\"\n";
    Require(hpl::cLlamaInference::ValidateRequest(request, error), "Valid optional grammar rejected: " + error);
    request.msGrammar.assign("root\0bad", 8);
    ExpectInvalidRequest(request, "embedded NUL in grammar");
    request.msGrammar.assign(64 * 1024 + 1, 'x');
    ExpectInvalidRequest(request, "grammar beyond the byte limit");

    request = MakeRequest();
    request.mlMaxTokens = 0;
    ExpectInvalidRequest(request, "zero output token limit");
    request.mlMaxTokens = -1;
    ExpectInvalidRequest(request, "negative output token limit");
    request.mlMaxTokens = 131073;
    ExpectInvalidRequest(request, "excessive output token limit");
    request = MakeRequest();
    request.mfTemperature = -0.1f;
    ExpectInvalidRequest(request, "negative temperature");
    request.mfTemperature = 5.1f;
    ExpectInvalidRequest(request, "excessive temperature");
    request.mfTemperature = std::numeric_limits<float>::infinity();
    ExpectInvalidRequest(request, "infinite temperature");
    request.mfTemperature = std::numeric_limits<float>::quiet_NaN();
    ExpectInvalidRequest(request, "NaN temperature");
    request.mfTemperature = 0.0f;
    Require(hpl::cLlamaInference::ValidateRequest(request, error), "Greedy sampling temperature rejected: " + error);
}

void ExpectInvalidConfig(hpl::cLlamaInference& aInference, const hpl::cLlamaModelConfig& aConfig,
                         const char* asDescription)
{
    std::string error;
    Require(!aInference.LoadAsync(aConfig, error), std::string("Accepted invalid config: ") + asDescription);
    Require(!error.empty(), std::string("Missing config error: ") + asDescription);
    aInference.Unload();
    Require(aInference.GetState() == hpl::eLlamaState_Unloaded, "Unload did not restore Unloaded state");
}

void TestUnloadedAndConfigValidation()
{
    hpl::cLlamaInference inference;
    Require(inference.GetState() == hpl::eLlamaState_Unloaded, "New service is not Unloaded");
    hpl::cLlamaResult result;
    Require(!inference.PollResult(result), "New service returned a result");
    Require(!inference.Cancel(0) && !inference.Cancel(123456), "Unknown request was cancelled");
    Require(!inference.ResolveSessionTurn(0, true) && !inference.ResolveSessionTurn(123456, false),
            "Unknown session reply was resolved");
    Require(inference.GetContextStats(1).mlUsedTokens == 0 && inference.GetContextStats(1).mlCapacityTokens == 0,
            "Unloaded service reported context use");
    inference.ResetSession(1, "Unloaded reset");
    std::string error;
    Require(inference.Submit(MakeRequest(), error) == 0 && !error.empty(), "Unloaded service accepted work");

    hpl::cLlamaModelConfig config;
    Require(config.mlMinImageTokens == 0, "Default image minimum must preserve projector defaults");
    ExpectInvalidConfig(inference, config, "empty model path");
    config.msModelPath = "hpl2-llama-test-nonexistent.gguf";
    config.mlBatchSize = 1;
    config.mlMaxImageTokens = 1;
    config.mlContextSize = 0;
    ExpectInvalidConfig(inference, config, "zero context");
    config.mlContextSize = 255;
    ExpectInvalidConfig(inference, config, "context below the minimum");
    config.mlContextSize = 131073;
    ExpectInvalidConfig(inference, config, "context above the maximum");
    config.mlContextSize = 4096;
    config.mlBatchSize = 0;
    ExpectInvalidConfig(inference, config, "zero batch size");
    config.mlBatchSize = config.mlContextSize + 1;
    ExpectInvalidConfig(inference, config, "batch larger than context");
    config.mlBatchSize = 256;
    config.mlThreads = 0;
    ExpectInvalidConfig(inference, config, "zero worker threads");
    config.mlThreads = 129;
    ExpectInvalidConfig(inference, config, "excessive worker threads");
    config.mlThreads = 2;
    config.mlGpuLayers = -1;
    ExpectInvalidConfig(inference, config, "negative GPU layers");
    config.mlGpuLayers = 0;
    config.mlMaxImageTokens = 0;
    ExpectInvalidConfig(inference, config, "zero image token limit");
    config.mlMaxImageTokens = config.mlContextSize + 1;
    ExpectInvalidConfig(inference, config, "image token limit above context size");
    config.mlMaxImageTokens = 1024;
    config.mlMinImageTokens = -1;
    ExpectInvalidConfig(inference, config, "negative image token minimum");
    config.mlMinImageTokens = config.mlMaxImageTokens + 1;
    ExpectInvalidConfig(inference, config, "image token minimum above maximum");
    config.mlMinImageTokens = config.mlContextSize + 1;
    ExpectInvalidConfig(inference, config, "image token minimum above context size");
    config.mlMinImageTokens = 0;
    config.mlMaxOutstandingRequests = 0;
    ExpectInvalidConfig(inference, config, "zero outstanding request limit");
    config.mlMaxOutstandingRequests = 65;
    ExpectInvalidConfig(inference, config, "excessive outstanding request limit");

    inference.Unload();
    inference.Unload();
    Require(inference.GetState() == hpl::eLlamaState_Unloaded, "Repeated Unload changed the state");
}

void WaitForLoad(hpl::cLlamaInference& aInference, std::chrono::seconds aTimeout)
{
    const auto deadline = std::chrono::steady_clock::now() + aTimeout;
    while(aInference.GetState() == hpl::eLlamaState_Loading)
    {
        Require(std::chrono::steady_clock::now() < deadline, "Timed out while loading a model");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void TestBackendLifecycle()
{
    hpl::cLlamaInference inference;
    hpl::cLlamaModelConfig config;
    // Create a unique nonexistent filename in the test working directory.
    config.msModelPath = "hpl2-missing-model-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".gguf";
    std::string error;

#if HPL2_WITH_LLAMA
    Require(hpl::cLlamaInference::IsSupported(), "Enabled build reports unsupported backend");
    hpl::cLlamaModelConfig gpuConfig = config;
    gpuConfig.mlGpuLayers = 1;
    if(hpl::cLlamaInference::IsGpuSupported())
    {
        Require(inference.LoadAsync(gpuConfig, error),
                "Available GPU backend rejected requested GPU layers: " + error);
        WaitForLoad(inference, std::chrono::seconds(15));
        Require(inference.GetState() == hpl::eLlamaState_Failed && !inference.GetLastError().empty(),
                "GPU missing-model load did not fail asynchronously");
        inference.Unload();
    }
    else
    {
        Require(!inference.LoadAsync(gpuConfig, error) && !error.empty(),
                "Unavailable GPU backend accepted requested GPU layers");
    }
    for(int attempt = 0; attempt < 2; ++attempt)
    {
        if(attempt == 1)
        {
            config.mlContextSize = 256;
            config.mlBatchSize = 1;
            config.mlMaxImageTokens = 1;
            config.mlMinImageTokens = 1;
            config.mlThreads = 1;
            config.mlMaxOutstandingRequests = 1;
        }
        Require(inference.LoadAsync(config, error), "Missing model load did not start asynchronously: " + error);
        WaitForLoad(inference, std::chrono::seconds(15));
        Require(inference.GetState() == hpl::eLlamaState_Failed, "Missing model did not enter Failed state");
        Require(!inference.GetLastError().empty(), "Failed load has no diagnostic");
        Require(!inference.LoadAsync(config, error) && !error.empty(), "Failed state accepted reload without Unload");
        Require(inference.Submit(MakeRequest(), error) == 0 && !error.empty(), "Failed service accepted work");
        inference.Unload();
        Require(inference.GetState() == hpl::eLlamaState_Unloaded, "Unload after failure did not restore state");
    }
    Require(inference.LoadAsync(config, error), "Immediate-unload load did not start: " + error);
    inference.Unload();
    Require(inference.GetState() == hpl::eLlamaState_Unloaded, "Unload during loading did not restore state");
#else
    Require(!hpl::cLlamaInference::IsSupported(), "Disabled build reports supported backend");
    Require(!hpl::cLlamaInference::IsGpuSupported(), "Disabled build reports available GPU support");
    Require(!inference.LoadAsync(config, error) && !error.empty(), "Disabled backend accepted model loading");
    config.mlMinImageTokens = config.mlMaxImageTokens;
    Require(!inference.LoadAsync(config, error) && error.find("without llama.cpp") != std::string::npos,
            "Valid equal image-token minimum/maximum failed config validation");
#endif

    inference.Unload();
    inference.Unload();
    Require(inference.GetState() == hpl::eLlamaState_Unloaded, "Final Unload did not restore state");
}

std::string ReadPPMToken(std::istream& aInput)
{
    std::string token;
    char ch;
    while(aInput.get(ch))
    {
        if(ch == '#')
        {
            aInput.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        if(!std::isspace(static_cast<unsigned char>(ch)))
        {
            token += ch;
            break;
        }
    }
    while(aInput.get(ch))
    {
        if(std::isspace(static_cast<unsigned char>(ch)))
        {
            if(ch == '\r' && aInput.peek() == '\n') aInput.get();
            return token;
        }
        token += ch;
    }
    return token;
}

unsigned int ParseUnsigned(const std::string& asText, const char* asDescription)
{
    Require(!asText.empty() && asText.find_first_not_of("0123456789") == std::string::npos,
            std::string("Invalid ") + asDescription + ": " + asText);
    size_t consumed = 0;
    const unsigned long long value = std::stoull(asText, &consumed);
    Require(consumed == asText.size() && value <= std::numeric_limits<unsigned int>::max(),
            std::string("Invalid ") + asDescription + ": " + asText);
    return static_cast<unsigned int>(value);
}

int ParseIntegerOption(const std::string& asText, const char* asDescription)
{
    const unsigned int value = ParseUnsigned(asText, asDescription);
    Require(value <= static_cast<unsigned int>(std::numeric_limits<int>::max()),
            std::string("Invalid ") + asDescription + ": " + asText);
    return static_cast<int>(value);
}

hpl::cLlamaImage ReadPPM(const std::string& asPath)
{
    std::ifstream input(asPath.c_str(), std::ios::binary);
    Require(input.good(), "Could not open image: " + asPath);
    Require(ReadPPMToken(input) == "P6", "Image must be a binary RGB8 PPM (P6)");
    hpl::cLlamaImage image;
    image.mlWidth = ParseUnsigned(ReadPPMToken(input), "PPM width");
    image.mlHeight = ParseUnsigned(ReadPPMToken(input), "PPM height");
    Require(ReadPPMToken(input) == "255", "PPM must use 8-bit channels (max value 255)");
    Require(image.mlWidth > 0 && image.mlHeight > 0 && image.mlWidth <= 8192 && image.mlHeight <= 8192,
            "PPM dimensions must be in 1..8192");
    const size_t bytes = static_cast<size_t>(image.mlWidth) * image.mlHeight * 3;
    Require(bytes <= 64 * 1024 * 1024, "PPM RGB data exceeds 64 MiB");
    image.mvRGB.resize(bytes);
    input.read(reinterpret_cast<char*>(image.mvRGB.data()), static_cast<std::streamsize>(bytes));
    Require(input.gcount() == static_cast<std::streamsize>(bytes), "PPM pixel data is truncated");
    Require(input.peek() == std::char_traits<char>::eof(), "PPM contains unexpected trailing data");
    return image;
}

hpl::cLlamaResult WaitForResult(hpl::cLlamaInference& aInference, uint64_t alRequestId)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    hpl::cLlamaResult result;
    while(!aInference.PollResult(result))
    {
        Require(std::chrono::steady_clock::now() < deadline, "Timed out waiting for inference result");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(result.mlRequestId == alRequestId, "Result ID does not match submitted request");
    return result;
}

void TestPersistentSession(hpl::cLlamaInference& aInference, const hpl::cLlamaModelConfig& aConfig,
                           const hpl::cLlamaImage& aImage)
{
    std::string error;
    hpl::cLlamaRequest request;
    request.mlSessionId = 41;
    request.mlMaxTokens = 32;
    request.mfTemperature = 0;
    request.mfSessionRefreshFraction = 0.95f;
    request.msSystemPrompt = "Remember the current secret code from this conversation. When asked for it, reply with the code only. If none was supplied, reply UNKNOWN.";
    request.msPrompt = "The secret code is SAPPHIRE. Acknowledge with ack.";
    request.msGrammar = "root ::= \"ack\"\n";
    request.mImage = aImage;
    uint64_t id = aInference.Submit(request, error);
    Require(id != 0, "Session first turn rejected: " + error);
    hpl::cLlamaResult first = WaitForResult(aInference, id);
    Require(first.msError.empty() && !first.mbCancelled && !first.mbTruncated && first.msText == "ack",
            "Session first turn failed: " + first.msError);
    Require(first.mContextStats.mlSessionId == 41 && first.mContextStats.mbAwaitingResolution &&
            first.mContextStats.mlTurns == 1 && first.mContextStats.mlReusedTokens == 0,
            "First session stats or provisional-turn state is incorrect");
    Require(first.mContextStats.mlCapacityTokens >= static_cast<unsigned>(aConfig.mlContextSize) &&
            first.mContextStats.mlReservedTokens == 0 &&
            first.mContextStats.mlUsedTokens == first.mContextStats.mlTextTokens +
                first.mContextStats.mlImageTokens + first.mContextStats.mlOutputTokens &&
            first.mContextStats.mlUsedTokens + first.mContextStats.mlReservedTokens <= first.mContextStats.mlCapacityTokens,
            "Session context accounting does not sum to actual occupancy/capacity");
    if(!aImage.mvRGB.empty()) Require(first.mContextStats.mlImageTokens > 0 && first.mContextStats.mlObservations == 1,
                                    "Session image tokens/observation count missing");
    Require(aInference.Submit(request, error) == 0 && !error.empty(), "Unresolved persistent turn accepted more work");
    Require(aInference.ResolveSessionTurn(id, true), "Could not accept first session turn");

    request.msPrompt = "What is the current secret code?";
    request.msGrammar = "root ::= \"SAPPHIRE\" | \"OBSIDIAN\" | \"UNKNOWN\"\n";
    id = aInference.Submit(request, error);
    Require(id != 0, "Session second turn rejected: " + error);
    Require(!aInference.ResolveSessionTurn(id, true), "Unpolled next session request was prematurely acknowledged");
    hpl::cLlamaResult second = WaitForResult(aInference, id);
    Require(second.msError.empty() && !second.mbTruncated && second.msText == "SAPPHIRE",
            "Persistent conversation forgot its first turn: " + second.msText + " " + second.msError);
    Require(second.mContextStats.mlTurns == 2 && second.mContextStats.mlReusedTokens == first.mContextStats.mlUsedTokens,
            "Session did not reuse its committed native KV prefix");
    std::cout << "Session context: first=" << first.mContextStats.mlUsedTokens
              << ", second=" << second.mContextStats.mlUsedTokens
              << ", reused=" << second.mContextStats.mlReusedTokens
              << ", image=" << second.mContextStats.mlImageTokens
              << ", capacity=" << second.mContextStats.mlCapacityTokens << " tokens.\n";
    if(!aImage.mvRGB.empty()) Require(second.mContextStats.mlImageTokens > first.mContextStats.mlImageTokens &&
                                    second.mContextStats.mlObservations == 2, "Second image was not appended/accounted");
    Require(aInference.ResolveSessionTurn(id, true), "Could not accept second session turn");

    request.msPrompt = "Replace the current secret code with OBSIDIAN. Acknowledge with ack.";
    request.msGrammar = "root ::= \"ack\"\n";
    id = aInference.Submit(request, error);
    Require(id != 0, "Rejected-turn test could not submit: " + error);
    hpl::cLlamaResult proposed = WaitForResult(aInference, id);
    Require(proposed.msError.empty() && !proposed.mbTruncated, "Rejected-turn generation failed");
    Require(aInference.ResolveSessionTurn(id, false), "Could not reject proposed session turn");
    request.msPrompt = "What is the current secret code?";
    request.msGrammar = "root ::= \"SAPPHIRE\" | \"OBSIDIAN\" | \"UNKNOWN\"\n";
    id = aInference.Submit(request, error);
    Require(id != 0, "Session recovery could not submit: " + error);
    hpl::cLlamaResult recovered = WaitForResult(aInference, id);
    Require(recovered.msError.empty() && recovered.msText == "SAPPHIRE" && recovered.mContextStats.mlTurns == 3 &&
            recovered.mContextStats.mlReusedTokens == 0, "Rejected decision leaked into retained conversation");
    Require(aInference.ResolveSessionTurn(id, true), "Could not accept recovered turn");

    request.mlMaxTokens = 1;
    request.msPrompt = "Replace the code with OBSIDIAN and produce a long acknowledgement.";
    request.msGrammar = "root ::= \"this reply cannot fit in one token\"\n";
    id = aInference.Submit(request, error);
    Require(id != 0, "Truncation test could not submit: " + error);
    hpl::cLlamaResult truncated = WaitForResult(aInference, id);
    Require(truncated.mbTruncated && !truncated.mContextStats.mbAwaitingResolution &&
            !aInference.ResolveSessionTurn(id, true), "Incomplete assistant turn was allowed to commit");

    request.mlMaxTokens = 32;
    request.msPrompt = "Replace the code with OBSIDIAN. Acknowledge with ack.";
    request.msGrammar = "root ::= \"ack\"\n";
    id = aInference.Submit(request, error);
    Require(id != 0 && aInference.Cancel(id), "Could not cancel persistent request");
    hpl::cLlamaResult cancelled = WaitForResult(aInference, id);
    Require(cancelled.mbCancelled && !aInference.ResolveSessionTurn(id, true), "Cancelled session turn could commit");
    request.msGrammar = "root ::= (";
    id = aInference.Submit(request, error);
    Require(id != 0, "Persistent malformed grammar request rejected before worker validation");
    hpl::cLlamaResult failed = WaitForResult(aInference, id);
    Require(failed.msError.find("grammar") != std::string::npos && !aInference.ResolveSessionTurn(id, true),
            "Failed persistent request retained a provisional turn");

    hpl::cLlamaRequest independent = request;
    independent.mlSessionId = 0;
    independent.msPrompt = "What is the current secret code?";
    independent.msGrammar = "root ::= \"SAPPHIRE\" | \"OBSIDIAN\" | \"UNKNOWN\"\n";
    id = aInference.Submit(independent, error);
    Require(id != 0, "Independent isolation request rejected: " + error);
    hpl::cLlamaResult isolated = WaitForResult(aInference, id);
    Require(isolated.msError.empty() && isolated.msText == "UNKNOWN", "Persistent history leaked into independent request");

    request.msPrompt = "What is the current secret code?";
    request.msGrammar = independent.msGrammar;
    id = aInference.Submit(request, error);
    Require(id != 0, "Session resume after independent request rejected: " + error);
    hpl::cLlamaResult resumed = WaitForResult(aInference, id);
    Require(resumed.msError.empty() && resumed.msText == "SAPPHIRE" && resumed.mContextStats.mlReusedTokens == 0 &&
            resumed.mContextStats.mlTurns == 4, "Independent/cancel/truncated work corrupted accepted session history");
    Require(aInference.ResolveSessionTurn(id, true), "Could not accept resumed turn");

    request.mfSessionRefreshFraction = 0.25f;
    request.mlSessionKeepTurns = 1;
    request.msContextSummary = "Confirmed accepted observation: current secret code is SAPPHIRE. Rejected and cancelled OBSIDIAN proposals never happened.";
    request.msPrompt = "Padding unrelated to the secret:";
    for(int i = 0; i < aConfig.mlContextSize / 3; ++i) request.msPrompt += " x";
    request.msPrompt += "\nWhat is the current secret code?";
    id = aInference.Submit(request, error);
    Require(id != 0, "Context refresh request rejected: " + error);
    hpl::cLlamaResult refreshed = WaitForResult(aInference, id);
    Require(refreshed.msError.empty() && refreshed.msText == "SAPPHIRE" && refreshed.mContextStats.mlRefreshCount >= 1 &&
            refreshed.mContextStats.mlTurns <= 2 && refreshed.mContextStats.mlReusedTokens == 0 &&
            refreshed.mContextStats.msRefreshReason == "Context budget", "Bounded context refresh/summary failed");
    Require(refreshed.mContextStats.mlUsedTokens == refreshed.mContextStats.mlTextTokens +
            refreshed.mContextStats.mlImageTokens + refreshed.mContextStats.mlOutputTokens,
            "Refreshed multimodal context accounting is inconsistent");
    std::cout << "Compacted session: used=" << refreshed.mContextStats.mlUsedTokens
              << ", pairs=" << refreshed.mContextStats.mlTurns
              << ", images=" << refreshed.mContextStats.mlObservations
              << ", compactions=" << refreshed.mContextStats.mlRefreshCount << ".\n";
    Require(aInference.ResolveSessionTurn(id, true), "Could not accept refreshed turn");
    aInference.ResetSession(41, "Test owner changed");
    request = independent;
    request.mlSessionId = 42;
    id = aInference.Submit(request, error);
    Require(id != 0, "New owner session rejected: " + error);
    hpl::cLlamaResult newOwner = WaitForResult(aInference, id);
    Require(newOwner.msError.empty() && newOwner.msText == "UNKNOWN" && newOwner.mContextStats.mlTurns == 1 &&
            newOwner.mContextStats.mlReusedTokens == 0 && aInference.GetContextStats(41).mlCapacityTokens == 0,
            "New owner inherited old session history");
    Require(aInference.ResolveSessionTurn(id, false), "Could not dispose of new-owner test turn");
    aInference.ResetSession(42, "Session smoke complete");
    request.mlSessionId = 43;
    request.msPrompt = "The secret code is OBSIDIAN. Acknowledge with ack.";
    request.msGrammar = "root ::= \"ack\"\n";
    id = aInference.Submit(request, error);
    Require(id != 0, "Reset-during-request test could not submit");
    aInference.ResetSession(43, "Owner removed during request");
    hpl::cLlamaResult reset = WaitForResult(aInference, id);
    Require(reset.mbCancelled && !aInference.ResolveSessionTurn(id, true), "Reset active session could still commit");
    request = independent;
    request.mlSessionId = 44;
    id = aInference.Submit(request, error);
    Require(id != 0, "Post-reset fresh request rejected");
    hpl::cLlamaResult resetRecovered = WaitForResult(aInference, id);
    Require(resetRecovered.msError.empty() && resetRecovered.msText == "UNKNOWN" && resetRecovered.mContextStats.mlTurns == 1,
            "Active-session reset leaked cancelled facts into a fresh owner");
    Require(aInference.ResolveSessionTurn(id, false), "Could not dispose of reset-recovery turn");
    aInference.ResetSession(44, "Session smoke complete");
    std::cout << "Persistent text/image history, native KV reuse, rejection/cancel/truncation rollback, independent isolation, bounded refresh and accounting checks passed.\n";
}

void SmokeTest(int alArgc, char** apArgv)
{
    hpl::cLlamaModelConfig config;
    config.mlMaxOutstandingRequests = 1;
    hpl::cLlamaRequest request = MakeRequest();
    std::string imagePath;
    for(int i = 1; i < alArgc; ++i)
    {
        const std::string flag = apArgv[i];
        Require(i + 1 < alArgc, "Missing value for " + flag);
        const std::string value = apArgv[++i];
        if(flag == "--model") config.msModelPath = value;
        else if(flag == "--projector") config.msProjectorPath = value;
        else if(flag == "--template") config.msChatTemplate = value;
        else if(flag == "--image") imagePath = value;
        else if(flag == "--prompt") request.msPrompt = value;
        else if(flag == "--system") request.msSystemPrompt = value;
        else if(flag == "--max-tokens") request.mlMaxTokens = ParseIntegerOption(value, "output token limit");
        else if(flag == "--context") config.mlContextSize = ParseIntegerOption(value, "context size");
        else if(flag == "--threads") config.mlThreads = ParseIntegerOption(value, "thread count");
        else if(flag == "--gpu-layers") config.mlGpuLayers = ParseIntegerOption(value, "GPU layer count");
        else if(flag == "--image-tokens") config.mlMaxImageTokens = ParseIntegerOption(value, "image token limit");
        else if(flag == "--image-min-tokens") config.mlMinImageTokens = ParseIntegerOption(value, "image token minimum");
        else throw std::runtime_error("Unknown option: " + flag);
    }
    Require(hpl::cLlamaInference::IsSupported(), "Smoke test requires a build with HPL2_WITH_LLAMA=ON");
    Require(!config.msModelPath.empty(), "Smoke test requires --model PATH");
    if(config.mlGpuLayers > 0)
        Require(hpl::cLlamaInference::IsGpuSupported(),
                "GPU smoke test requires an available GPU device and an accelerated backend");
    if(!imagePath.empty())
    {
        Require(!config.msProjectorPath.empty(), "Image smoke test requires --projector PATH");
        request.mImage = ReadPPM(imagePath);
    }

    hpl::cLlamaInference inference;
    std::string error;
    std::cout << "Smoke execution: " << (config.mlGpuLayers > 0 ? "GPU" : "CPU")
              << " (requested GPU layers: " << config.mlGpuLayers << ").\n";
    std::cout << "Image token budget: minimum " << config.mlMinImageTokens
              << " (zero uses projector default), maximum " << config.mlMaxImageTokens << ".\n";
    const auto loadStart = std::chrono::steady_clock::now();
    Require(inference.LoadAsync(config, error), "Could not start model loading: " + error);
    WaitForLoad(inference, std::chrono::minutes(5));
    Require(inference.GetState() == hpl::eLlamaState_Ready, "Could not load model: " + inference.GetLastError());
    std::cout << "Model ready after " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - loadStart).count() << " ms.\n";

    hpl::cLlamaRequest oversizedOutput = request;
    oversizedOutput.mlMaxTokens = config.mlContextSize;
    Require(inference.Submit(oversizedOutput, error) == 0 && !error.empty(),
            "Output limit that fills the context was accepted");

    const auto generationStart = std::chrono::steady_clock::now();
    const uint64_t id = inference.Submit(request, error);
    Require(id != 0, "Could not submit smoke request: " + error);
    Require(inference.Submit(request, error) == 0 && !error.empty(), "Outstanding request limit was not enforced");
    const hpl::cLlamaResult result = WaitForResult(inference, id);
    Require(result.msError.empty() && !result.mbCancelled, "Generation failed: " + result.msError);
    Require(result.mlGeneratedTokens > 0, "Model did not generate any tokens");
    std::cout << "Request completed after " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - generationStart).count() << " ms.\n";
    std::cout << "Generated " << result.mlGeneratedTokens << " tokens:\n" << result.msText << '\n';
    Require(!inference.Cancel(id), "Completed request accepted cancellation");

    hpl::cLlamaRequest oversizedPrompt = request;
    oversizedPrompt.msPrompt.clear();
    for(int i = 0; i < config.mlContextSize + 100; ++i) oversizedPrompt.msPrompt += " x";
    const uint64_t oversizedId = inference.Submit(oversizedPrompt, error);
    Require(oversizedId != 0, "Could not submit context budget test: " + error);
    const hpl::cLlamaResult oversizedResult = WaitForResult(inference, oversizedId);
    Require(!oversizedResult.mbCancelled && oversizedResult.msError.find("context") != std::string::npos,
            "Prompt exceeding the context did not produce a context-budget error: " + oversizedResult.msError);

    const uint64_t repeatId = inference.Submit(request, error);
    Require(repeatId != 0 && repeatId != id, "Could not submit independent repeat request: " + error);
    const hpl::cLlamaResult repeatResult = WaitForResult(inference, repeatId);
    Require(repeatResult.msError.empty() && !repeatResult.mbCancelled && repeatResult.mlGeneratedTokens > 0,
            "Generation did not recover after context-budget error: " + repeatResult.msError);
    Require(repeatResult.msText == result.msText && repeatResult.mlGeneratedTokens == result.mlGeneratedTokens,
            "Independent requests with identical seed produced different output");

    hpl::cLlamaRequest invalidGrammar = request;
    invalidGrammar.msGrammar = "root ::= (";
    const uint64_t invalidGrammarId = inference.Submit(invalidGrammar, error);
    Require(invalidGrammarId != 0, "Could not submit invalid-grammar recovery test: " + error);
    const hpl::cLlamaResult invalidGrammarResult = WaitForResult(inference, invalidGrammarId);
    Require(!invalidGrammarResult.mbCancelled && invalidGrammarResult.msError.find("grammar") != std::string::npos &&
            inference.GetState() == hpl::eLlamaState_Ready,
            "Invalid grammar did not produce a recoverable request error: " + invalidGrammarResult.msError);

    hpl::cLlamaRequest constrained = request;
    constrained.msSystemPrompt = "Reply with a JSON object.";
    constrained.msPrompt = "Return exactly {\"ok\":true}.";
    constrained.msGrammar = R"GBNF(root ::= "{\"ok\":true}")GBNF";
    constrained.mlMaxTokens = 64;
    for(float temperature : {0.0f, 0.2f})
    {
        constrained.mfTemperature = temperature;
        const uint64_t constrainedId = inference.Submit(constrained, error);
        Require(constrainedId != 0, "Could not submit grammar-constrained request: " + error);
        const hpl::cLlamaResult constrainedResult = WaitForResult(inference, constrainedId);
        Require(constrainedResult.msError.empty() && !constrainedResult.mbCancelled &&
                constrainedResult.msText == "{\"ok\":true}",
                "Grammar did not constrain generation/recover after invalid grammar: " + constrainedResult.msError +
                " output=" + constrainedResult.msText);
    }
    std::cout << "Grammar-constrained greedy/probabilistic output and invalid-grammar recovery checks passed.\n";

    const uint64_t cancelId = inference.Submit(request, error);
    Require(cancelId != 0, "Polling did not release outstanding request capacity: " + error);
    const bool cancelled = inference.Cancel(cancelId);
    const hpl::cLlamaResult cancelledResult = WaitForResult(inference, cancelId);
    if(cancelled) Require(cancelledResult.mbCancelled, "Accepted cancellation did not produce a cancelled result");
    else Require(cancelledResult.msError.empty(), "Raced cancellation ended in a failure: " + cancelledResult.msError);
    std::cout << "Cancellation check: " << (cancelledResult.mbCancelled ? "cancelled" : "completed before cancellation") << '\n';
    std::cout << "Context budget and independent repeat checks passed.\n";
    TestPersistentSession(inference, config, request.mImage);

    const uint64_t shutdownId = inference.Submit(request, error);
    Require(shutdownId != 0, "Could not submit shutdown request: " + error);
    inference.Unload();
    Require(inference.GetState() == hpl::eLlamaState_Unloaded, "Smoke test shutdown failed");
    hpl::cLlamaResult discarded;
    Require(!inference.PollResult(discarded) && !inference.Cancel(shutdownId),
            "Unload retained discarded work or results");
    Require(inference.LoadAsync(config, error), "Could not reload model after active shutdown: " + error);
    WaitForLoad(inference, std::chrono::minutes(5));
    Require(inference.GetState() == hpl::eLlamaState_Ready,
            "Model did not reload after active shutdown: " + inference.GetLastError());

    hpl::cLlamaInference companion;
    Require(companion.LoadAsync(config, error), "Could not load a concurrent service: " + error);
    WaitForLoad(companion, std::chrono::minutes(5));
    Require(companion.GetState() == hpl::eLlamaState_Ready,
            "Concurrent service load failed: " + companion.GetLastError());
    inference.Unload();
    const uint64_t companionId = companion.Submit(request, error);
    Require(companionId != 0, "Concurrent service lost its backend after first service unload: " + error);
    const hpl::cLlamaResult companionResult = WaitForResult(companion, companionId);
    Require(companionResult.msError.empty() && !companionResult.mbCancelled &&
            companionResult.msText == result.msText && companionResult.mlGeneratedTokens == result.mlGeneratedTokens,
            "Concurrent service could not generate after first service unload: " + companionResult.msError);
    companion.Unload();

    hpl::cLlamaModelConfig missingProjector = config;
    missingProjector.msProjectorPath = "hpl2-missing-projector-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".gguf";
    Require(inference.LoadAsync(missingProjector, error), "Could not start missing-projector load: " + error);
    WaitForLoad(inference, std::chrono::minutes(5));
    Require(inference.GetState() == hpl::eLlamaState_Failed && !inference.GetLastError().empty(),
            "Missing projector did not fail after language context initialization");
    inference.Unload();

    for(int attempt = 0; attempt < 2; ++attempt)
    {
        Require(inference.LoadAsync(config, error), "Could not start repeated loading shutdown test: " + error);
        inference.Unload();
        Require(inference.GetState() == hpl::eLlamaState_Unloaded,
                "Immediate Unload of valid model did not restore Unloaded state");
    }
    std::cout << "Active shutdown, native reload, projector failure, and concurrent service checks passed.\n";

    if(config.mlGpuLayers > 0)
    {
        // A CUDA-enabled executable must still support explicitly selected CPU
        // inference after a GPU context has been used and unloaded. Do not
        // compare CPU/GPU output bytes: floating-point kernels may differ.
        hpl::cLlamaModelConfig cpuConfig = config;
        cpuConfig.mlGpuLayers = 0;
        cpuConfig.msProjectorPath.clear();
        Require(inference.LoadAsync(cpuConfig, error), "Could not start CPU load after GPU shutdown: " + error);
        WaitForLoad(inference, std::chrono::minutes(5));
        Require(inference.GetState() == hpl::eLlamaState_Ready,
                "CPU mode failed after GPU shutdown: " + inference.GetLastError());
        hpl::cLlamaRequest cpuRequest = MakeRequest();
        cpuRequest.mlMaxTokens = 16;
        const uint64_t cpuId = inference.Submit(cpuRequest, error);
        Require(cpuId != 0, "CPU mode rejected work after GPU shutdown: " + error);
        const hpl::cLlamaResult cpuResult = WaitForResult(inference, cpuId);
        Require(cpuResult.msError.empty() && !cpuResult.mbCancelled && cpuResult.mlGeneratedTokens > 0,
                "CPU generation failed after GPU shutdown: " + cpuResult.msError);
        inference.Unload();
        std::cout << "Explicit CPU generation after GPU shutdown passed.\n";
    }
}

void WriteProbeFile(const std::filesystem::path& aPath, const std::string& asText)
{
    std::ofstream output(aPath, std::ios::binary | std::ios::trunc);
    output.write(asText.data(), static_cast<std::streamsize>(asText.size()));
    Require(output.good(), "Could not save probe output: " + aPath.string());
}

void ActionProbe(int alArgc, char** apArgv)
{
    hpl::cLlamaModelConfig config;
    config.mlMaxOutstandingRequests = 1;
    hpl::cLlamaRequest request;
    request.mlMaxTokens = 512;
    request.mlSessionId = 0;
    request.mfTemperature = 0;
    request.mlSeed = 0;
    std::string imagePath, outputPath;
    bool alignTarget = false;
    bool describeTarget = false;
    for(int i = 2; i < alArgc; ++i)
    {
        const std::string flag = apArgv[i];
        if(flag == "--align-target") { alignTarget = true; continue; }
        if(flag == "--describe-target") { describeTarget = true; continue; }
        Require(i + 1 < alArgc, "Missing value for " + flag);
        const std::string value = apArgv[++i];
        if(flag == "--model") config.msModelPath = value;
        else if(flag == "--projector") config.msProjectorPath = value;
        else if(flag == "--image") imagePath = value;
        else if(flag == "--output-dir") outputPath = value;
        else if(flag == "--gpu-layers") config.mlGpuLayers = ParseIntegerOption(value, "GPU layer count");
        else if(flag == "--context") config.mlContextSize = ParseIntegerOption(value, "context size");
        else if(flag == "--threads") config.mlThreads = ParseIntegerOption(value, "thread count");
        else if(flag == "--image-min-tokens") config.mlMinImageTokens = ParseIntegerOption(value, "image token minimum");
        else if(flag == "--image-tokens") config.mlMaxImageTokens = ParseIntegerOption(value, "image token limit");
        else if(flag == "--max-tokens") request.mlMaxTokens = ParseIntegerOption(value, "output token limit");
        else throw std::runtime_error("Unknown action-probe option: " + flag);
    }
    Require(hpl::cLlamaInference::IsSupported(), "Action probe requires an enabled backend");
    Require(!config.msModelPath.empty() && !config.msProjectorPath.empty() && !imagePath.empty() && !outputPath.empty(),
            "Action probe requires --model, --projector, --image and --output-dir");
    if(config.mlGpuLayers > 0)
        Require(hpl::cLlamaInference::IsGpuSupported(), "Action probe requires an available GPU device");
    request.mImage = ReadPPM(imagePath);
    const std::filesystem::path outputDirectory = std::filesystem::absolute(outputPath).lexically_normal();
    std::filesystem::create_directories(outputDirectory);
    std::ofstream summary(outputDirectory / "summary.txt", std::ios::binary | std::ios::trunc);
    Require(summary.good(), "Could not create action-probe summary");
    summary << "Independent action-format comparison; no game actions execute.\n"
            << "model=" << config.msModelPath << "\nprojector=" << config.msProjectorPath
            << "\nimage=" << imagePath << "\nimage_width=" << request.mImage.mlWidth
            << "\nimage_height=" << request.mImage.mlHeight << "\nimage_rgb_bytes=" << request.mImage.mvRGB.size()
            << "\ngpu_layers=" << config.mlGpuLayers << "\ncontext=" << config.mlContextSize
            << "\nthreads=" << config.mlThreads << "\nimage_min_tokens=" << config.mlMinImageTokens
            << "\nimage_max_tokens=" << config.mlMaxImageTokens << "\nmax_output_tokens=" << request.mlMaxTokens
            << "\nalign_target=" << alignTarget
            << "\ndescribe_target=" << describeTarget
            << "\nsystem_prompt=empty\nsession_id=0\ntemperature=0\nseed=0\n";
    summary.flush();

    std::string objective =
        "Locate the magenta target in the image and choose one action that approaches it. "
        "Available actions: turn_left (rotate left), turn_right (rotate right), "
        "move_forward (advance straight ahead), wait (stay still). "
        "If no magenta target is visible, choose wait.";
    if(alignTarget)
        objective += " First align the camera with the visible target before advancing. "
            "If the target is left of image center, choose turn_left. "
            "If the target is right of image center, choose turn_right. "
            "Choose move_forward only when the target is near image center. "
            "Judge left and right from the whole image, not from another object.";
    std::string jsonFormat = "\nReturn only one JSON object with fields in this order: ";
    if(describeTarget)
        jsonFormat += "observation (a JSON string: one brief sentence describing whether magenta is visible and "
            "its position relative to the whole image center), ";
    jsonFormat +=
        "target_visible (boolean), target_side (\"left\", \"center\", \"right\", or \"none\" relative to the image center), "
        "action (\"turn_left\", \"turn_right\", \"move_forward\", or \"wait\"). "
        "No markdown or extra fields.";
    const std::string grammar = describeTarget ? R"GBNF(root ::= ws "{" ws "\"observation\"" ws ":" ws observation ws "," ws "\"target_visible\"" ws ":" ws boolean ws "," ws "\"target_side\"" ws ":" ws side ws "," ws "\"action\"" ws ":" ws action ws "}" ws
observation ::= "\"" observation-char{0,240} "\""
observation-char ::= [^"\\\x00-\x1F] | "\\" (["\\/bfnrt] | "u" [0-9a-fA-F]{4})
boolean ::= "true" | "false"
side ::= "\"left\"" | "\"center\"" | "\"right\"" | "\"none\""
action ::= "\"turn_left\"" | "\"turn_right\"" | "\"move_forward\"" | "\"wait\""
ws ::= [ \t\n\r]*
)GBNF" : R"GBNF(root ::= ws "{" ws "\"target_visible\"" ws ":" ws boolean ws "," ws "\"target_side\"" ws ":" ws side ws "," ws "\"action\"" ws ":" ws action ws "}" ws
boolean ::= "true" | "false"
side ::= "\"left\"" | "\"center\"" | "\"right\"" | "\"none\""
action ::= "\"turn_left\"" | "\"turn_right\"" | "\"move_forward\"" | "\"wait\""
ws ::= [ \t\n\r]*
)GBNF";

    hpl::cLlamaInference inference;
    std::string error;
    const auto loadStart = std::chrono::steady_clock::now();
    Require(inference.LoadAsync(config, error), "Could not start action-probe loading: " + error);
    WaitForLoad(inference, std::chrono::minutes(5));
    Require(inference.GetState() == hpl::eLlamaState_Ready, "Could not load probe model: " + inference.GetLastError());
    summary << "load_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - loadStart).count() << '\n';
    summary.flush();

    bool complete = true;
    const char* stages[] = {"01-free-text", "02-json", "03-json-grammar"};
    for(int stage = 0; stage < 3; ++stage)
    {
        request.msPrompt = objective + (stage == 0 ? "" : jsonFormat);
        request.msGrammar = stage == 2 ? grammar : "";
        WriteProbeFile(outputDirectory / (std::string(stages[stage]) + "-prompt.txt"), request.msPrompt);
        WriteProbeFile(outputDirectory / (std::string(stages[stage]) + "-grammar.gbnf"), request.msGrammar);
        const auto start = std::chrono::steady_clock::now();
        hpl::cLlamaResult result;
        error.clear();
        const uint64_t id = inference.Submit(request, error);
        if(id)
        {
            try { result = WaitForResult(inference, id); }
            catch(const std::exception& failure)
            {
                error = failure.what();
                inference.Cancel(id);
            }
        }
        else if(error.empty()) error = "Request rejected without a diagnostic";
        if(!result.msError.empty()) error = result.msError;
        WriteProbeFile(outputDirectory / (std::string(stages[stage]) + "-reply.txt"), result.msText);
        const hpl::cLlamaContextStats& stats = result.mContextStats;
        const bool success = id && error.empty() && !result.mbCancelled && !result.mbTruncated &&
            !result.msText.empty() && result.mlGeneratedTokens > 0 && stats.mlSessionId == 0 &&
            stats.mlReusedTokens == 0 && stats.mlObservations == 1 && stats.mlImageTokens > 0 &&
            stats.mlImageTokens >= static_cast<unsigned>(config.mlMinImageTokens) &&
            stats.mlImageTokens <= static_cast<unsigned>(config.mlMaxImageTokens);
        complete = complete && success;
        std::ostringstream metadata;
        metadata << "stage=" << stages[stage] << "\nrequest_id=" << id << "\nelapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
                 << "\nalign_target=" << alignTarget << "\ndescribe_target=" << describeTarget
                 << "\nerror=" << error << "\ncancelled=" << result.mbCancelled << "\ntruncated=" << result.mbTruncated
                 << "\ngenerated_tokens=" << result.mlGeneratedTokens << "\nsession_id=" << stats.mlSessionId
                 << "\nused_tokens=" << stats.mlUsedTokens << "\ncapacity_tokens=" << stats.mlCapacityTokens
                 << "\ntext_tokens=" << stats.mlTextTokens << "\nimage_tokens=" << stats.mlImageTokens
                 << "\noutput_tokens=" << stats.mlOutputTokens << "\nreused_tokens=" << stats.mlReusedTokens
                 << "\nturns=" << stats.mlTurns << "\nobservations=" << stats.mlObservations
                 << "\ntechnically_complete=" << success << '\n';
        WriteProbeFile(outputDirectory / (std::string(stages[stage]) + "-metadata.txt"), metadata.str());
        summary << "\n" << metadata.str() << "RAW_REPLY:\n" << result.msText << '\n';
        summary.flush();
        Require(summary.good(), "Could not update action-probe summary");
        std::cout << stages[stage] << ": " << (success ? "completed" : "failed") << ", image_tokens="
                  << stats.mlImageTokens << "\n" << result.msText << '\n';
    }
    inference.Unload();
    summary << "\nAll raw replies are retained. Judge target recognition and action direction separately from formatting.\n"
            << "technical_success=" << complete << '\n';
    summary.flush();
    Require(complete, "Action probe has a native error, cancellation, truncation or unexpected image/session accounting; inspect saved replies");
}

} // namespace

int main(int alArgc, char** apArgv)
{
    try
    {
        if(alArgc > 1 && std::string(apArgv[1]) == "--action-probe")
        {
            ActionProbe(alArgc, apArgv);
            return EXIT_SUCCESS;
        }
        TestRequestValidation();
        TestUnloadedAndConfigValidation();
        TestBackendLifecycle();
        std::cout << "Service contract tests passed (backend "
                  << (hpl::cLlamaInference::IsSupported() ? "enabled" : "disabled") << ").\n";
        if(alArgc > 1) SmokeTest(alArgc, apArgv);
        return EXIT_SUCCESS;
    }
    catch(const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
