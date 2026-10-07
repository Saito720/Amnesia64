/* Bounded Enemy_Llama decision protocol. GPL-3.0-or-later. */
#ifndef LUX_LLAMA_DECISION_H
#define LUX_LLAMA_DECISION_H

#include <string>

enum eLuxLlamaBehavior : int
{
    eLuxLlamaBehavior_Patrol,
    eLuxLlamaBehavior_Chase,
    eLuxLlamaBehavior_Investigate,
    eLuxLlamaBehavior_Attack
};

enum eLuxLlamaAction : int
{
    eLuxLlamaAction_Wait,
    eLuxLlamaAction_Move,
    eLuxLlamaAction_Turn,
    eLuxLlamaAction_Attack
};

enum eLuxLlamaTarget : int
{
    eLuxLlamaTarget_None,
    eLuxLlamaTarget_Player,
    eLuxLlamaTarget_Door,
    eLuxLlamaTarget_Obstacle
};

struct cLuxLlamaDecision
{
    cLuxLlamaDecision();
    eLuxLlamaBehavior mBehavior;
    eLuxLlamaAction mAction;
    eLuxLlamaTarget mTarget;
    float mfForward;     // [-1,1]: positive forward, negative backward.
    float mfTurnDegrees; // [-90,90]: positive turns right.
    float mfDuration;    // [0.1,2] seconds; mechanics still enforce their own limits.
    bool mbRun;
    int mlTargetX;       // [0,1000], left to right in the current image.
    int mlTargetY;       // [0,1000], top to bottom in the current image.
    std::string msMemory; // At most 240 Unicode characters, not hidden world facts.
};

enum eLuxLlamaSteeringSide : int
{
    eLuxLlamaSteeringSide_None,
    eLuxLlamaSteeringSide_Left,
    eLuxLlamaSteeringSide_Center,
    eLuxLlamaSteeringSide_Right
};

enum eLuxLlamaSteeringAction : int
{
    eLuxLlamaSteeringAction_Wait,
    eLuxLlamaSteeringAction_TurnLeft,
    eLuxLlamaSteeringAction_TurnRight,
    eLuxLlamaSteeringAction_MoveForward
};

// A small visual steering protocol for isolating target alignment from the
// full policy. Semantic contradictions remain observable rather than being
// rewritten into an engine-selected action by this parser.
struct cLuxLlamaSteeringDecision
{
    cLuxLlamaSteeringDecision();
    bool mbTargetVisible;
    eLuxLlamaSteeringSide mSide;
    eLuxLlamaSteeringAction mAction;
};

// Strict JSON only. All ten fields are required; duplicates, unknown fields,
// invalid types/ranges and trailing content fail without modifying aDecision.
bool ParseLuxLlamaDecision(const std::string& asText, cLuxLlamaDecision& aDecision,
                          std::string& asError);
const char* GetLuxLlamaDecisionSystemPrompt();
const char* GetLuxLlamaDecisionGrammar(); // GBNF with entry rule "root".
// Same transactional strict-JSON contract with exactly three required fields:
// target_visible, target_side and action. No semantic steering is forced.
bool ParseLuxLlamaSteeringDecision(const std::string& asText,
    cLuxLlamaSteeringDecision& aDecision, std::string& asError);
const char* GetLuxLlamaSteeringSystemPrompt();
const char* GetLuxLlamaSteeringGrammar();
const char* GetLuxLlamaPerceptionSystemPrompt();
const char* GetLuxLlamaPerceptionPrompt();
// Diagnostic prose only. Historical request text is quoted as data, and the
// result must never be parsed or applied as an enemy command.
const char* GetLuxLlamaDecisionExplanationSystemPrompt();
std::string BuildLuxLlamaDecisionExplanationPrompt(
    const std::string& asOriginalSystemPrompt, const std::string& asOriginalUserPrompt,
    const std::string& asContextSummary, const std::string& asRawDecision,
    const std::string& asEngineStatus);
const char* GetLuxLlamaBehaviorName(eLuxLlamaBehavior aBehavior);
const char* GetLuxLlamaActionName(eLuxLlamaAction aAction);
const char* GetLuxLlamaTargetName(eLuxLlamaTarget aTarget);

#endif
