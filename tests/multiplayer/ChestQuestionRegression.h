#ifndef CODEX_CHEST_QUESTION_REGRESSION_H
#define CODEX_CHEST_QUESTION_REGRESSION_H
#include "LuxProp_Chest.h"
#include "LuxMessageHandler.h"

// The retail chest meshes use Lever, so supply an empty prop world while
// exercising the production Chest question and destruction implementations.
class cQuestionChestFixture : public cLuxProp_Chest {
public:
    explicit cQuestionChestFixture(cLuxMap* map) : cLuxProp_Chest("codex_question_chest",-1,map) {
        mpWorld=map->GetWorld();mpMeshEntity=NULL;
        cResourceVarsObject vars;vars.AddVarString("CoinsNeeded","0");
        cLuxPropLoader_Chest loader("Question regression");loader.LoadInstanceVariables(this,&vars);
        SetLocked(true,false);
    }
};

class cQuestionObserver : public iLuxMessageCallback {
public:
    unsigned calls=0;
    bool answeredYes=false;
    iLuxMessageCallback* next=NULL;
    void OnPress(bool yes) {
        ++calls;answeredYes=yes;
        if(next) {
            gpBase->mpMessageHandler->StartPauseMessage(_W("Replacement question"),true,next);
            // Releasing the old owner during dispatch must preserve its successor.
            gpBase->mpMessageHandler->CancelPauseMessage(this);
        }
    }
};

static bool RunChestQuestionRegression(tString& error) {
    auto* messages=gpBase->mpMessageHandler;
    auto* map=gpBase->mpMapHandler->GetCurrentMap();
    const auto input=gpBase->mpInputHandler->GetState();
    const auto state=gpBase->mpPlayer->GetCurrentState();
    const int coins=gpBase->mpPlayer->GetCoins();
    auto fail=[&error](const char* text){error=text;return false;};
    auto* chest=hplNew(cQuestionChestFixture,(map));
    chest->OnInteract(NULL,cVector3f(0));
    const bool opened=messages->IsPauseMessageActive();
    hplDelete(chest);
    if(!opened || messages->IsPauseMessageActive())
        return fail("destroyed chest left its purchase question active");
    messages->DoAction(eLuxPlayerAction_Interact,true);
    if(messages->IsPauseMessageActive() || gpBase->mpPlayer->GetCoins()!=coins)
        return fail("a cancelled chest purchase still accepted input");

    cQuestionObserver replacement;
    chest=hplNew(cQuestionChestFixture,(map));chest->OnInteract(NULL,cVector3f(0));
    messages->StartPauseMessage(_W("Unrelated question"),true,&replacement);
    hplDelete(chest);
    messages->CancelPauseMessage(NULL);
    if(!messages->IsPauseMessageActive()) return fail("chest destruction cancelled an unrelated question");
    messages->DoAction(eLuxPlayerAction_Attack,true);
    messages->DoAction(eLuxPlayerAction_Attack,true);
    if(replacement.calls!=1 || replacement.answeredYes || messages->IsPauseMessageActive())
        return fail("an answered question retained or repeated its callback");

    cQuestionObserver first,second;first.next=&second;
    messages->StartPauseMessage(_W("First question"),true,&first);
    messages->DoAction(eLuxPlayerAction_Interact,true);
    if(first.calls!=1 || !first.answeredYes || !messages->IsPauseMessageActive())
        return fail("question dispatch discarded its replacement question");
    messages->DoAction(eLuxPlayerAction_Attack,true);
    if(second.calls!=1 || second.answeredYes || messages->IsPauseMessageActive())
        return fail("replacement question failed to dispatch once");
    if(gpBase->mpInputHandler->GetState()!=input || gpBase->mpPlayer->GetCurrentState()!=state ||
       !map->GetWorld()->IsActive()) return fail("question cancellation changed player input or paused the multiplayer world");
    mark(role+"-chest-question-passed.txt","owner destruction cancels chest question; unrelated/reentrant dialogs and input survive");
    return true;
}
#endif
