/* Asynchronous Enemy_Llama controller. GPL-3.0-or-later. */
#include "LuxLlamaController.h"
#include "LuxEnemy_Llama.h"
#include "LuxMapHandler.h"
#include "LuxMap.h"
#include "LuxLlamaDecision.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <sstream>
#include "graphics/Bitmap.h"
#include "resources/BitmapLoaderHandler.h"

cLuxLlamaController::cLuxLlamaController()
	: mpInference(gpBase->mpEngine->GetLlamaInference()), mpOwnerMap(NULL), mpOwner(NULL),
	  mlGeneration(0), mlPendingGeneration(0), mlPendingRequest(0), mlLastSubmittedFrame(0),
	  mfClock(0), mfSubmittedAt(0), mfNextDecisionAt(0), mfLastLatency(0),
	  mfMaxResultAge(6), mlMaxTokens(192), mlContextKeepTurns(4), mfContextRefreshFraction(0.8f),
	  mbEnabled(true), mbPerceptionOnly(false), mbTargetSteeringOnly(false), mbPerceptionRequested(false), msStatus("Waiting for Enemy_Llama")
{
	if(gpBase->mpGameCfg)
	{
		mbEnabled = gpBase->mpGameCfg->GetBool("Llama", "Enabled", true);
		mbTargetSteeringOnly = gpBase->mpGameCfg->GetBool("Llama", "TargetSteeringOnly", false);
	}
}

cLuxLlamaController::~cLuxLlamaController() { Invalidate(); }

hpl::cLlamaContextStats cLuxLlamaController::GetContextStats() const
{
	return mbTargetSteeringOnly ? mSteeringContextStats : mpInference->GetContextStats(mlGeneration + 1);
}

void cLuxLlamaController::Invalidate()
{
	InvalidateControl(false);
}

void cLuxLlamaController::CaptureDecisionFeedback()
{
	if(!mLastDecision.mpOwner || mLastDecision.mpMap != gpBase->mpMapHandler->GetCurrentMap() ||
		cLuxEnemy_Llama::GetControllerOwner(mLastDecision.mpMap) != mLastDecision.mpOwner) return;
	const tString& feedback = mLastDecision.mpOwner->GetLastActionFeedback();
	if(feedback != "No previous action") mLastDecision.msActionFeedback = feedback;
}

void cLuxLlamaController::InvalidateControl(bool abPreserveDecision)
{
	if(mlPendingRequest) mpInference->Cancel(mlPendingRequest);
	mpInference->ResetSession(mlGeneration + 1, "Owner, world or control changed");
	mlPendingRequest = 0;
	if(mpOwner)
	{
		if(abPreserveDecision) mpOwner->StopModelAction("control interrupted");
		CaptureDecisionFeedback();
		mpOwner->ResetModelAction();
	}
	mPendingDecision = cLuxLlamaDecisionRecord();
	if(!abPreserveDecision) mLastDecision = cLuxLlamaDecisionRecord();
	mpOwner = NULL;
	mpOwnerMap = NULL;
	++mlGeneration;
	mlLastSubmittedFrame = 0;
	mfNextDecisionAt = mfClock;
	mPerceptionFrame = cLuxLlamaPerceptionFrame();
	mSteeringContextStats = hpl::cLlamaContextStats();
	mbPerceptionRequested = mbPerceptionOnly;
}

void cLuxLlamaController::OnOwnerUnavailable(cLuxEnemy_Llama* apEnemy)
{
	if(mLastDecision.mpOwner == apEnemy) mLastDecision = cLuxLlamaDecisionRecord();
	if(mpOwner == apEnemy) Invalidate();
}

void cLuxLlamaController::OnManualOverride(cLuxEnemy_Llama* apEnemy)
{
	CaptureDecisionFeedback();
	if(mpOwner == apEnemy) InvalidateControl(true);
}

void cLuxLlamaController::SetEnabled(bool abEnabled)
{
	if(mbEnabled == abEnabled) return;
	InvalidateControl(true);
	mbEnabled = abEnabled;
	if(abEnabled && mpInference->GetState()==hpl::eLlamaState_Failed) mpInference->Unload();
	msStatus = abEnabled ? "Waiting for observation" : "VLM control disabled";
}

void cLuxLlamaController::Retry()
{
	InvalidateControl(true);
	// A failed load has already stopped its worker. Never join a live encoder
	// or model loader from a gameplay tick or this debug control.
	if(mpInference->GetState() == hpl::eLlamaState_Failed) mpInference->Unload();
	msStatus = "Waiting for observation";
}

void cLuxLlamaController::SetPerceptionOnly(bool abEnabled)
{
	if(mbPerceptionOnly == abEnabled) return;
	mbPerceptionOnly = abEnabled;
	InvalidateControl(true);
	msStatus = abEnabled ? "Waiting for a perception diagnostic frame" : "Waiting for autonomous observation";
}

void cLuxLlamaController::SetTargetSteeringOnly(bool abEnabled)
{
	if(mbTargetSteeringOnly == abEnabled) return;
	InvalidateControl(true);
	mbTargetSteeringOnly = abEnabled;
	msStatus = abEnabled ? "Waiting for target steering observation" : "Waiting for full behavior observation";
}

void cLuxLlamaController::RequestPerceptionDiagnostic()
{
	if(!mbPerceptionOnly) SetPerceptionOnly(true);
	if(mlPendingRequest) return;
	mPerceptionFrame = cLuxLlamaPerceptionFrame();
	mbPerceptionRequested = true;
	msStatus = "Waiting for a fresh perception diagnostic frame";
}

bool cLuxLlamaController::RequestDecisionExplanation()
{
	cLuxMap* map = gpBase->mpMapHandler->GetCurrentMap();
	cLuxEnemy_Llama* owner = cLuxEnemy_Llama::GetControllerOwner(map);
	if(!owner || mLastDecision.mpOwner != owner || mLastDecision.mpMap != map ||
		!mLastDecision.mlRequestId || mLastDecision.mRequest.mImage.mvRGB.empty())
	{
		msStatus = "No completed autonomous decision for this owner to explain";
		return false;
	}
	if(mbPerceptionOnly && mlPendingRequest)
	{
		msStatus = "Wait for the current diagnostic before requesting an explanation";
		return false;
	}
	CaptureDecisionFeedback();
	if(!mbPerceptionOnly) SetPerceptionOnly(true);
	mpOwner = owner;
	mpOwnerMap = map;
	mPerceptionFrame = cLuxLlamaPerceptionFrame();
	mPerceptionFrame.mbDecisionExplanation = true;
	mPerceptionFrame.msSystemPrompt = GetLuxLlamaDecisionExplanationSystemPrompt();
	mPerceptionFrame.msPrompt = BuildLuxLlamaDecisionExplanationPrompt(mLastDecision.mRequest.msSystemPrompt,
		mLastDecision.mRequest.msPrompt,mLastDecision.mRequest.msContextSummary,mLastDecision.msReply,
		mLastDecision.msEngineStatus + "\nLatest mechanical feedback: " + mLastDecision.msActionFeedback);
	PrepareDiagnosticImage(mLastDecision.mRequest.mImage,mLastDecision.mlFrameId,mLastDecision.mfTime);
	mbPerceptionRequested = true;
	msStatus = "Waiting to explain the last decision; autonomous commands paused";
	return true;
}

bool cLuxLlamaController::EnsureLoaded()
{
	if(!hpl::cLlamaInference::IsSupported())
	{
		msStatus = "This game build does not include llama.cpp";
		return false;
	}
	const hpl::eLlamaState state = mpInference->GetState();
	if(state == hpl::eLlamaState_Ready) return true;
	if(state == hpl::eLlamaState_Loading || state == hpl::eLlamaState_Stopping)
	{
		msStatus = "Loading local VLM";
		return false;
	}
	if(state == hpl::eLlamaState_Failed)
	{
		msStatus = "VLM load failed: " + mpInference->GetLastError();
		return false;
	}
	hpl::cLlamaModelConfig config;
	cConfigFile* pConfig = gpBase->mpGameCfg;
	config.msModelPath = pConfig->GetString("Llama", "ModelPath", "models/Qwen3VL-2B-Instruct-Q4_K_M.gguf");
	config.msProjectorPath = pConfig->GetString("Llama", "ProjectorPath", "models/mmproj-Qwen3VL-2B-Instruct-F16.gguf");
	config.msChatTemplate = pConfig->GetString("Llama", "ChatTemplate", "");
	config.mlContextSize = cMath::Clamp(pConfig->GetInt("Llama", "ContextSize", 16384), 1024, 32768);
	config.mlBatchSize = cMath::Clamp(pConfig->GetInt("Llama", "BatchSize", 256), 32, config.mlContextSize);
	config.mlThreads = cMath::Clamp(pConfig->GetInt("Llama", "Threads", 2), 1, 32);
	config.mlMaxImageTokens = cMath::Clamp(pConfig->GetInt("Llama", "ImageTokens", 2048), 64, config.mlContextSize/2);
	config.mlMinImageTokens = cMath::Clamp(pConfig->GetInt("Llama", "MinImageTokens", 1024), 0, config.mlContextSize/2);
	if(config.mlMinImageTokens > config.mlMaxImageTokens)
	{
		msStatus = "MinImageTokens must not exceed ImageTokens in game.cfg";
		return false;
	}
	config.mlMaxOutstandingRequests = 1;
	config.mlGpuLayers = pConfig->GetInt("Llama", "GpuLayers", -1);
	if(config.mlGpuLayers < 0) config.mlGpuLayers = hpl::cLlamaInference::IsGpuSupported() ? 99 : 0;
	config.mlGpuLayers = cMath::Clamp(config.mlGpuLayers, 0, 999);
	mlMaxTokens = cMath::Clamp(pConfig->GetInt("Llama", "MaxTokens", 192), 128, 512);
	mlContextKeepTurns = cMath::Clamp(pConfig->GetInt("Llama", "ContextKeepTurns", 4), 1, 8);
	const float fRefresh = pConfig->GetFloat("Llama", "ContextRefreshFraction", 0.8f);
	mfContextRefreshFraction = std::isfinite(fRefresh) ? cMath::Clamp(fRefresh,0.5f,0.95f) : 0.8f;
	float fAge = pConfig->GetFloat("Llama", "MaxResultAge", 6);
	mfMaxResultAge = std::isfinite(fAge) ? cMath::Clamp(fAge, 0.25f, 600.0f) : 6.0f;
	if(config.msModelPath.empty() || config.msProjectorPath.empty())
	{
		msStatus = "Set [Llama] ModelPath and ProjectorPath in game.cfg";
		return false;
	}
	std::string error;
	if(!mpInference->LoadAsync(config, error)) msStatus = "VLM unavailable: " + error;
	else msStatus = "Loading local VLM";
	return false;
}

void cLuxLlamaController::CaptureObservation()
{
	cLuxMap* pMap = gpBase->mpMapHandler->GetCurrentMap();
	cLuxEnemy_Llama* pEnemy = cLuxEnemy_Llama::GetControllerOwner(pMap);
	if(pEnemy && (mbPerceptionOnly || pEnemy->IsObservationEnabled() ||
		(mbEnabled && pEnemy->IsAutonomousControlEnabled()))) pEnemy->CaptureObservation();
}

void cLuxLlamaController::Update(float afTimeStep)
{
	mfClock += afTimeStep;
	cLuxMap* pMap = gpBase->mpMapHandler->GetCurrentMap();
	cLuxEnemy_Llama* pEnemy = cLuxEnemy_Llama::GetControllerOwner(pMap);
	if(mLastDecision.mpOwner && (mLastDecision.mpOwner != pEnemy || mLastDecision.mpMap != pMap))
		mLastDecision = cLuxLlamaDecisionRecord();
	if(!mbPerceptionOnly && (!mbEnabled || !pEnemy || !pEnemy->IsAutonomousControlEnabled() || pEnemy->IsDebugOverride())) pEnemy = NULL;
	if(mpOwner != pEnemy || (pEnemy && mpOwnerMap != pMap))
	{
		InvalidateControl(true);
		mpOwner = pEnemy;
		mpOwnerMap = pEnemy ? pMap : NULL;
	}
	hpl::cLlamaResult result;
	while(mpInference->PollResult(result)) HandleResult(result);
	if(!pEnemy)
	{
		msStatus = !mbEnabled ? "VLM control disabled" : "Waiting for eligible autonomous Enemy_Llama";
		return;
	}
	if(mlPendingRequest && mfClock - mfSubmittedAt > mfMaxResultAge)
	{
		mpInference->Cancel(mlPendingRequest);
		msStatus = "Cancelling expired decision";
		pEnemy->msModelStatus = msStatus;
		// Keep the ID until the worker acknowledges cancellation, maintaining
		// one outstanding request even while a GPU batch finishes.
		return;
	}
	if(!EnsureLoaded()) { pEnemy->msModelStatus = msStatus; return; }
	if(mbPerceptionOnly)
	{
		if(!mlPendingRequest && mbPerceptionRequested) SubmitPerception(pEnemy);
		return;
	}
	// Plan from an actual action outcome, not from a repeatedly interrupted move.
	if(mlPendingRequest || mfClock < mfNextDecisionAt || pEnemy->IsModelActionActive()) return;
	SubmitObservation(pEnemy);
}

void cLuxLlamaController::PrepareDiagnosticImage(const hpl::cLlamaImage& aImage, unsigned alFrameId, float afTime)
{
	mPerceptionFrame.mlFrameId = alFrameId;
	mPerceptionFrame.mfTime = afTime;
	mPerceptionFrame.mImage = aImage;
	mPerceptionFrame.mlPlayerPixels = 0;
	mPerceptionFrame.mvPlayerMin = mPerceptionFrame.mvPlayerMax = cVector2l(-1);
	for(unsigned y=0; y<aImage.mlHeight; ++y)
		for(unsigned x=0; x<aImage.mlWidth; ++x)
		{
			const size_t pixel = (static_cast<size_t>(y)*aImage.mlWidth+x)*3;
			if(aImage.mvRGB[pixel]!=255 || aImage.mvRGB[pixel+1]!=0 || aImage.mvRGB[pixel+2]!=255) continue;
			if(mPerceptionFrame.mlPlayerPixels++ == 0) mPerceptionFrame.mvPlayerMin = mPerceptionFrame.mvPlayerMax = cVector2l(x,y);
			else
			{
				mPerceptionFrame.mvPlayerMin.x = cMath::Min(mPerceptionFrame.mvPlayerMin.x,static_cast<int>(x));
				mPerceptionFrame.mvPlayerMin.y = cMath::Min(mPerceptionFrame.mvPlayerMin.y,static_cast<int>(y));
				mPerceptionFrame.mvPlayerMax.x = cMath::Max(mPerceptionFrame.mvPlayerMax.x,static_cast<int>(x));
				mPerceptionFrame.mvPlayerMax.y = cMath::Max(mPerceptionFrame.mvPlayerMax.y,static_cast<int>(y));
			}
		}
}

void cLuxLlamaController::SubmitPerception(cLuxEnemy_Llama* apEnemy)
{
	if(!mPerceptionFrame.mbDecisionExplanation)
	{
		if(apEnemy->GetObservationFrameId() == mlLastSubmittedFrame || !apEnemy->SnapshotModelObservation())
		{
			msStatus = "Waiting for a fresh perception diagnostic frame";
			return;
		}
		const cLuxLlamaObservation& image = apEnemy->mModelObservation;
		mPerceptionFrame = cLuxLlamaPerceptionFrame();
		mPerceptionFrame.msSystemPrompt = GetLuxLlamaPerceptionSystemPrompt();
		mPerceptionFrame.msPrompt = GetLuxLlamaPerceptionPrompt();
		hpl::cLlamaImage rgb;
		rgb.mlWidth = image.mvSize.x; rgb.mlHeight = image.mvSize.y; rgb.mvRGB = image.mvRGB;
		PrepareDiagnosticImage(rgb,image.mlFrameId,image.mfTime);
	}
	hpl::cLlamaRequest request;
	request.msSystemPrompt = mPerceptionFrame.msSystemPrompt;
	request.msPrompt = mPerceptionFrame.msPrompt;
	request.mImage = mPerceptionFrame.mImage;
	request.mlMaxTokens = 512;
	request.mfTemperature = 0;
	// Both diagnostics are stateless prose; neither uses the action grammar.
	std::string error;
	const uint64_t id = mpInference->Submit(request,error);
	if(!id)
	{
		msStatus = "Diagnostic submission failed: " + error;
		return;
	}
	mlPendingRequest = id;
	mlPendingGeneration = mlGeneration;
	mlLastSubmittedFrame = mPerceptionFrame.mlFrameId;
	mfSubmittedAt = mfClock;
	mbPerceptionRequested = false;
	mPerceptionFrame.mlRequestId = id;
	msStatus = mPerceptionFrame.mbDecisionExplanation ? "Explaining the last autonomous decision" :
		"Inspecting frozen frame " + cString::ToString(static_cast<int>(mPerceptionFrame.mlFrameId));
	SavePerceptionReport(true);
}

void cLuxLlamaController::SavePerceptionReport(bool abSaveImage)
{
	if(mPerceptionFrame.mImage.mvRGB.empty()) return;
	const tWString directory = gpBase->msBaseSavePath + _W("llama_diagnostics/");
	if(!cPlatform::FolderExists(directory) && !cPlatform::CreateFolder(directory)) return;
	if(mPerceptionFrame.msExportPath.empty())
	{
		const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		mPerceptionFrame.msExportPath = directory + cString::To16Char("frame_" + std::to_string(stamp) + "_" +
			std::to_string(mPerceptionFrame.mlRequestId));
	}
	if(abSaveImage)
	{
		cBitmap bitmap;
		bitmap.CreateData(cVector3l(mPerceptionFrame.mImage.mlWidth,mPerceptionFrame.mImage.mlHeight,1),ePixelFormat_RGB,0,0);
		// DevIL's raw-image exporter assumes a lower-left origin. Supply only
		// this export bitmap bottom-up; inference and the frozen preview stay top-down.
		const size_t rowBytes = static_cast<size_t>(mPerceptionFrame.mImage.mlWidth)*3;
		for(unsigned y=0; y<mPerceptionFrame.mImage.mlHeight; ++y)
			std::copy_n(mPerceptionFrame.mImage.mvRGB.data()+static_cast<size_t>(y)*rowBytes,rowBytes,
				bitmap.GetData(0,0)->mpData+static_cast<size_t>(mPerceptionFrame.mImage.mlHeight-1-y)*rowBytes);
		if(!gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(&bitmap,mPerceptionFrame.msExportPath+_W(".png"),0))
			Warning("Could not save Enemy_Llama perception image.\n");
	}
	FILE* file = cPlatform::OpenFile(mPerceptionFrame.msExportPath+_W(".txt"),_W("wb"));
	if(!file) return;
	std::ostringstream report;
	report.imbue(std::locale::classic());
	report << (mPerceptionFrame.mbDecisionExplanation ? "Retrospective decision explanation\n" : "Perception diagnostic\n")
		<< "Frozen frame " << mPerceptionFrame.mlFrameId << " at " << mPerceptionFrame.mfTime << "s\n"
		<< "Size: " << mPerceptionFrame.mImage.mlWidth << "x" << mPerceptionFrame.mImage.mlHeight << "\n"
		<< "Visual tokens: " << mPerceptionFrame.mContextStats.mlImageTokens << "\n"
		<< "Engine mask pixels: " << mPerceptionFrame.mlPlayerPixels << "; bounds: "
		<< mPerceptionFrame.mvPlayerMin.x << "," << mPerceptionFrame.mvPlayerMin.y << " to "
		<< mPerceptionFrame.mvPlayerMax.x << "," << mPerceptionFrame.mvPlayerMax.y << "\n"
		<< "Status: " << msStatus << "\nLatency: " << mfLastLatency << "s\n\nSystem prompt:\n"
		<< (mPerceptionFrame.msSystemPrompt.empty() ? GetLuxLlamaPerceptionSystemPrompt() : mPerceptionFrame.msSystemPrompt)
		<< "\n\nQuestion:\n" << (mPerceptionFrame.msPrompt.empty() ? GetLuxLlamaPerceptionPrompt() : mPerceptionFrame.msPrompt)
		<< "\n\nReply:\n" << mPerceptionFrame.msReply << "\n";
	const std::string text = report.str();
	std::fwrite(text.data(),1,text.size(),file);
	std::fclose(file);
}

void cLuxLlamaController::SubmitObservation(cLuxEnemy_Llama* apEnemy)
{
	if(apEnemy->IsModelActionActive()) return;
	apEnemy->SnapshotModelActionFeedback();
	if(apEnemy->GetObservationFrameId() == mlLastSubmittedFrame ||
		apEnemy->GetObservationFrameId() == apEnemy->mlModelActionEndFrame || !apEnemy->SnapshotModelObservation())
	{
		msStatus = "Waiting for fresh observation";
		apEnemy->msModelStatus = msStatus;
		return;
	}
	const cLuxLlamaObservation& image = apEnemy->mModelObservation;
	hpl::cLlamaRequest request;
	request.mlMaxTokens = mlMaxTokens;
	request.mfTemperature = 0;
	request.mImage.mlWidth = image.mvSize.x;
	request.mImage.mlHeight = image.mvSize.y;
	request.mImage.mvRGB = image.mvRGB;
	if(mbTargetSteeringOnly)
	{
		// Match the isolated visual probe: one user question and one image,
		// with no prior decisions, engine detections or world coordinates.
		request.msPrompt = GetLuxLlamaSteeringSystemPrompt();
		request.msGrammar = GetLuxLlamaSteeringGrammar();
	}
	else
	{
		request.msSystemPrompt = GetLuxLlamaDecisionSystemPrompt();
		request.msGrammar = GetLuxLlamaDecisionGrammar();
		request.mlSessionId = mlGeneration + 1;
		request.msContextSummary = apEnemy->GetModelContextSummary();
		request.mlSessionKeepTurns = mlContextKeepTurns;
		request.mfSessionRefreshFraction = mfContextRefreshFraction;
		std::ostringstream prompt;
		prompt.imbue(std::locale::classic());
		prompt << std::fixed << std::setprecision(2);
		prompt << "Observation " << image.mlFrameId << " at t=" << image.mfTime << "s. Horizontal FOV "
			<< cMath::ToDeg(apEnemy->GetObservationFOV()) << " degrees. "
			<< "Previous behavior=" << apEnemy->GetBehaviorName() << "; previous action=" << apEnemy->GetCurrentActionName() << ".\n"
			<< "Last action feedback: " << apEnemy->GetLastActionFeedback() << "\n"
			<< "Your remembered evidence: " << apEnemy->msModelMemory << "\n"
			<< "Perceived sounds (positive bearing is right, coarse distance in game units):\n";
		for(size_t i=0; i<image.mvSounds.size(); ++i)
		{
			const cLuxLlamaSound& sound = image.mvSounds[i];
			const float fBearing = cMath::GetAngleDistanceRad(0, sound.mfBearing-sound.mfListenerYaw+image.mfYaw);
			prompt << "age=" << sound.mfAge << "s, bearing=" << cMath::ToDeg(fBearing)
				<< "deg, distance=" << sound.mfDistance << ", loudness=" << sound.mfVolume << "\n";
		}
		if(image.mvSounds.empty()) prompt << "none\n";
		prompt << "Choose one action now. Maximum action duration " << apEnemy->mfActionMaxSeconds
			<< " seconds. Base decisions on this image and the evidence above. Return only the requested JSON.";
		request.msPrompt = prompt.str();
	}
	std::string error;
	const uint64_t id = mpInference->Submit(request, error);
	if(!id)
	{
		msStatus = "Decision submission failed: " + error;
		apEnemy->msModelStatus = msStatus;
		mfNextDecisionAt = mfClock + 0.25f;
		return;
	}
	mlPendingRequest = id;
	mPendingDecision = cLuxLlamaDecisionRecord();
	mPendingDecision.mpOwner = apEnemy;
	mPendingDecision.mpMap = gpBase->mpMapHandler->GetCurrentMap();
	mPendingDecision.mlRequestId = id;
	mPendingDecision.mlFrameId = image.mlFrameId;
	mPendingDecision.mfTime = image.mfTime;
	mPendingDecision.mRequest = request;
	mlPendingGeneration = mlGeneration;
	mlLastSubmittedFrame = image.mlFrameId;
	mfSubmittedAt = mfClock;
	mfNextDecisionAt = mfClock + apEnemy->mfDecisionInterval;
	msStatus = (mbTargetSteeringOnly ? "Target steering: deciding from frame " : "VLM deciding from frame ") +
		cString::ToString((int)image.mlFrameId);
	apEnemy->msModelStatus = msStatus;
}

void cLuxLlamaController::RecordDecisionStatus(uint64_t alRequestId, const tString& asStatus)
{
	if(mLastDecision.mlRequestId != alRequestId) return;
	mLastDecision.msEngineStatus = asStatus;
	CaptureDecisionFeedback();
}

void cLuxLlamaController::HandleResult(const hpl::cLlamaResult& aResult)
{
	if(aResult.mlRequestId != mlPendingRequest || !mlPendingRequest) return;
	mlPendingRequest = 0;
	mfLastLatency = mfClock-mfSubmittedAt;
	cLuxEnemy_Llama* pEnemy = mpOwner;
	if(mbPerceptionOnly)
	{
		if(!pEnemy || mlPendingGeneration != mlGeneration || !pEnemy->IsControllerOwner()) return;
		mPerceptionFrame.msReply = aResult.msText.substr(0,8192);
		mPerceptionFrame.mContextStats = aResult.mContextStats;
		msStatus = aResult.mbCancelled ? "Diagnostic cancelled" :
			(!aResult.msError.empty() ? "Diagnostic failed: " + aResult.msError :
			(aResult.mbTruncated ? "Diagnostic reply reached the output limit" :
			(mPerceptionFrame.mbDecisionExplanation ? "Explanation complete; autonomous commands remain paused" :
			"Perception complete; inspect again for a new frame")));
		SavePerceptionReport(false);
		return;
	}
	if(!pEnemy || mlPendingGeneration != mlGeneration || !pEnemy->IsControllerOwner() ||
		pEnemy->IsDebugOverride() || !pEnemy->IsAutonomousControlEnabled())
	{
		mpInference->ResolveSessionTurn(aResult.mlRequestId,false);
		return;
	}
	if(mbTargetSteeringOnly) mSteeringContextStats = aResult.mContextStats;
	if(mPendingDecision.mlRequestId == aResult.mlRequestId && !aResult.mbCancelled)
	{
		mLastDecision = mPendingDecision;
		mLastDecision.msReply = aResult.msText.substr(0,8192);
	}
	mPendingDecision = cLuxLlamaDecisionRecord();
	pEnemy->msLastModelReply = aResult.msText.substr(0,2048);
	if(aResult.mbCancelled || aResult.mbTruncated || !aResult.msError.empty())
	{
		mpInference->ResolveSessionTurn(aResult.mlRequestId,false);
		msStatus = aResult.mbCancelled ? "Decision cancelled; action not executed" :
			(aResult.mbTruncated ? "Decision exceeded output limit; action not executed" : "Decision failed: " + aResult.msError);
		pEnemy->msModelStatus = msStatus;
		pEnemy->StopModelAction(msStatus);
		pEnemy->RecordRejectedModelDecision(msStatus);
		RecordDecisionStatus(aResult.mlRequestId,msStatus);
		return;
	}
	const cLuxLlamaObservation& image = pEnemy->mModelObservation;
	if(mfLastLatency > mfMaxResultAge ||
		pEnemy->mfElapsedTime-image.mfTime > mfMaxResultAge ||
		cMath::Vector3Dist(image.mvPosition,pEnemy->mpObservationCamera->GetPosition()) > 2.0f ||
		std::fabs(cMath::GetAngleDistanceRad(image.mfYaw,pEnemy->mpCharBody->GetYaw())) > cMath::ToRad(45.0f))
	{
		mpInference->ResolveSessionTurn(aResult.mlRequestId,false);
		msStatus = "Discarded stale decision; requesting a fresh view";
		pEnemy->msModelStatus = msStatus;
		pEnemy->StopModelAction(msStatus);
		pEnemy->RecordRejectedModelDecision(msStatus);
		RecordDecisionStatus(aResult.mlRequestId,msStatus);
		return;
	}
	cLuxLlamaDecision decision;
	std::string error;
	bool parsed;
	if(mbTargetSteeringOnly)
	{
		cLuxLlamaSteeringDecision steering;
		parsed = ParseLuxLlamaSteeringDecision(aResult.msText,steering,error);
		if(parsed && (steering.mbTargetVisible == (steering.mSide == eLuxLlamaSteeringSide_None) ||
			(!steering.mbTargetVisible && steering.mAction != eLuxLlamaSteeringAction_Wait)))
		{
			parsed = false;
			error = "Target steering visibility, side and action contradict each other.";
		}
		if(parsed)
		{
			decision.mBehavior = steering.mbTargetVisible ? eLuxLlamaBehavior_Chase : eLuxLlamaBehavior_Patrol;
			decision.mTarget = steering.mbTargetVisible ? eLuxLlamaTarget_Player : eLuxLlamaTarget_None;
			decision.mfDuration = 0.75f;
			decision.mbRun = false;
			// The model selects the direction. A small FOV-scaled step supplies
			// actuator magnitude without aiming at the player's world position.
			const float turnStep = cMath::Clamp(cMath::ToDeg(pEnemy->GetObservationFOV())*0.05f,0.1f,10.0f);
			switch(steering.mAction)
			{
			case eLuxLlamaSteeringAction_TurnLeft:
				decision.mAction = eLuxLlamaAction_Turn;
				decision.mfTurnDegrees = -turnStep;
				break;
			case eLuxLlamaSteeringAction_TurnRight:
				decision.mAction = eLuxLlamaAction_Turn;
				decision.mfTurnDegrees = turnStep;
				break;
			case eLuxLlamaSteeringAction_MoveForward:
				decision.mAction = eLuxLlamaAction_Move;
				decision.mfForward = 1;
				break;
			default:
				decision.mAction = eLuxLlamaAction_Wait;
				break;
			}
		}
	}
	else parsed = ParseLuxLlamaDecision(aResult.msText,decision,error);
	if(!parsed)
	{
		mpInference->ResolveSessionTurn(aResult.mlRequestId,false);
		msStatus = "Rejected model action: " + error;
		pEnemy->msModelStatus = msStatus;
		pEnemy->StopModelAction(msStatus);
		pEnemy->RecordRejectedModelDecision(msStatus);
		RecordDecisionStatus(aResult.mlRequestId,msStatus);
		return;
	}
	pEnemy->ApplyModelDecision(decision);
	// Retain the proposed command; its mechanical success is reported next turn.
	if(!mbTargetSteeringOnly) mpInference->ResolveSessionTurn(aResult.mlRequestId,true);
	msStatus = mbTargetSteeringOnly ? "Target steering active" : "VLM control active";
	RecordDecisionStatus(aResult.mlRequestId,"Decision accepted for mechanical execution");
	pEnemy->msModelStatus = msStatus;
}
