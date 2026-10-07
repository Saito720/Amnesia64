/* Asynchronous Enemy_Llama controller. GPL-3.0-or-later. */
#ifndef LUX_LLAMA_CONTROLLER_H
#define LUX_LLAMA_CONTROLLER_H

#include "LuxBase.h"
#include "ai/LlamaInference.h"

class cLuxEnemy_Llama;
class cLuxMap;

struct cLuxLlamaDecisionRecord
{
	cLuxLlamaDecisionRecord() : mpOwner(NULL), mpMap(NULL), mlRequestId(0), mlFrameId(0), mfTime(0) {}
	cLuxEnemy_Llama* mpOwner;
	cLuxMap* mpMap;
	uint64_t mlRequestId;
	unsigned mlFrameId;
	float mfTime;
	hpl::cLlamaRequest mRequest;
	tString msReply, msEngineStatus, msActionFeedback;
};

struct cLuxLlamaPerceptionFrame
{
	cLuxLlamaPerceptionFrame() : mlRequestId(0), mlFrameId(0), mfTime(0), mbDecisionExplanation(false), mlPlayerPixels(0),
		mvPlayerMin(-1), mvPlayerMax(-1) {}
	uint64_t mlRequestId;
	unsigned mlFrameId;
	float mfTime;
	bool mbDecisionExplanation;
	hpl::cLlamaImage mImage;
	hpl::cLlamaContextStats mContextStats;
	size_t mlPlayerPixels;
	cVector2l mvPlayerMin, mvPlayerMax;
	tString msReply;
	tString msSystemPrompt, msPrompt;
	tWString msExportPath;
};

// One game-thread consumer owns the engine inference service. The worker never
// receives entity pointers, and a model stays loaded across map transitions.
class cLuxLlamaController
{
	friend class cLuxLlamaControllerTestAdapter;
public:
	cLuxLlamaController();
	~cLuxLlamaController();
	void Update(float afTimeStep);
	void CaptureObservation(); // Before Scene::Render, on the graphics thread.
	void Invalidate();
	void OnOwnerUnavailable(cLuxEnemy_Llama* apEnemy);
	void OnManualOverride(cLuxEnemy_Llama* apEnemy);
	bool IsEnabled() const { return mbEnabled; }
	void SetEnabled(bool abEnabled);
	void Retry();
	bool IsPerceptionOnly() const { return mbPerceptionOnly; }
	void SetPerceptionOnly(bool abEnabled);
	bool IsTargetSteeringOnly() const { return mbTargetSteeringOnly; }
	void SetTargetSteeringOnly(bool abEnabled);
	void RequestPerceptionDiagnostic();
	bool RequestDecisionExplanation();
	const cLuxLlamaPerceptionFrame& GetPerceptionFrame() const { return mPerceptionFrame; }
	bool IsBusy() const { return mlPendingRequest != 0; }
	float GetLastLatency() const { return mfLastLatency; }
	float GetMaxResultAge() const { return mfMaxResultAge; }
	hpl::cLlamaContextStats GetContextStats() const;
	const tString& GetStatus() const { return msStatus; }

private:
	void InvalidateControl(bool abPreserveDecision);
	void CaptureDecisionFeedback();
	void RecordDecisionStatus(uint64_t alRequestId, const tString& asStatus);
	void PrepareDiagnosticImage(const hpl::cLlamaImage& aImage, unsigned alFrameId, float afTime);
	bool EnsureLoaded();
	void HandleResult(const hpl::cLlamaResult& aResult);
	void SubmitObservation(cLuxEnemy_Llama* apEnemy);
	void SubmitPerception(cLuxEnemy_Llama* apEnemy);
	void SavePerceptionReport(bool abSaveImage);
	hpl::cLlamaInference* mpInference;
	cLuxMap* mpOwnerMap;
	cLuxEnemy_Llama* mpOwner;
	unsigned long long mlGeneration;
	unsigned long long mlPendingGeneration;
	uint64_t mlPendingRequest;
	unsigned int mlLastSubmittedFrame;
	float mfClock;
	float mfSubmittedAt;
	float mfNextDecisionAt;
	float mfLastLatency;
	float mfMaxResultAge;
	int mlMaxTokens;
	int mlContextKeepTurns;
	float mfContextRefreshFraction;
	bool mbEnabled;
	bool mbPerceptionOnly;
	bool mbTargetSteeringOnly;
	bool mbPerceptionRequested;
	hpl::cLlamaContextStats mSteeringContextStats;
	cLuxLlamaPerceptionFrame mPerceptionFrame;
	cLuxLlamaDecisionRecord mPendingDecision, mLastDecision;
	tString msStatus;
};

#endif
