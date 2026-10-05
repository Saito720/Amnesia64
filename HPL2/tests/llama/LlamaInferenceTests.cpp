#include "ai/LlamaInference.h"

#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
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
    std::string error;
    Require(inference.Submit(MakeRequest(), error) == 0 && !error.empty(), "Unloaded service accepted work");

    hpl::cLlamaModelConfig config;
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
    Require(!inference.LoadAsync(gpuConfig, error) && !error.empty(),
            "CPU-only bridge accepted requested GPU layers");
    for(int attempt = 0; attempt < 2; ++attempt)
    {
        if(attempt == 1)
        {
            config.mlContextSize = 256;
            config.mlBatchSize = 1;
            config.mlMaxImageTokens = 1;
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
    Require(!inference.LoadAsync(config, error) && !error.empty(), "Disabled backend accepted model loading");
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
        else throw std::runtime_error("Unknown option: " + flag);
    }
    Require(hpl::cLlamaInference::IsSupported(), "Smoke test requires a build with HPL2_WITH_LLAMA=ON");
    Require(!config.msModelPath.empty(), "Smoke test requires --model PATH");
    if(!imagePath.empty())
    {
        Require(!config.msProjectorPath.empty(), "Image smoke test requires --projector PATH");
        request.mImage = ReadPPM(imagePath);
    }

    hpl::cLlamaInference inference;
    std::string error;
    Require(inference.LoadAsync(config, error), "Could not start model loading: " + error);
    WaitForLoad(inference, std::chrono::minutes(5));
    Require(inference.GetState() == hpl::eLlamaState_Ready, "Could not load model: " + inference.GetLastError());

    hpl::cLlamaRequest oversizedOutput = request;
    oversizedOutput.mlMaxTokens = config.mlContextSize;
    Require(inference.Submit(oversizedOutput, error) == 0 && !error.empty(),
            "Output limit that fills the context was accepted");

    const uint64_t id = inference.Submit(request, error);
    Require(id != 0, "Could not submit smoke request: " + error);
    Require(inference.Submit(request, error) == 0 && !error.empty(), "Outstanding request limit was not enforced");
    const hpl::cLlamaResult result = WaitForResult(inference, id);
    Require(result.msError.empty() && !result.mbCancelled, "Generation failed: " + result.msError);
    Require(result.mlGeneratedTokens > 0, "Model did not generate any tokens");
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

    const uint64_t cancelId = inference.Submit(request, error);
    Require(cancelId != 0, "Polling did not release outstanding request capacity: " + error);
    const bool cancelled = inference.Cancel(cancelId);
    const hpl::cLlamaResult cancelledResult = WaitForResult(inference, cancelId);
    if(cancelled) Require(cancelledResult.mbCancelled, "Accepted cancellation did not produce a cancelled result");
    else Require(cancelledResult.msError.empty(), "Raced cancellation ended in a failure: " + cancelledResult.msError);
    std::cout << "Cancellation check: " << (cancelledResult.mbCancelled ? "cancelled" : "completed before cancellation") << '\n';
    std::cout << "Context budget and independent repeat checks passed.\n";

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
}

} // namespace

int main(int alArgc, char** apArgv)
{
    try
    {
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
