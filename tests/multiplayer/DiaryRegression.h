#ifndef CODEX_DIARY_REGRESSION_H
#define CODEX_DIARY_REGRESSION_H
static std::map<std::string,unsigned> diaryCallbacks;
static void __stdcall CodexSuppressDiary(std::string& name,int index) {
    ++diaryCallbacks[name];
    gpBase->mpMapHandler->GetCurrentMap()->RunScript("ReturnOpenJournal(false);");
}
static void __stdcall CodexShowDiary(std::string& name,int index) {
    ++diaryCallbacks[name];
    gpBase->mpMapHandler->GetCurrentMap()->RunScript("ReturnOpenJournal(true);");
}

// Retail diary assets use the same callback signature and ReturnOpenJournal
// decision as the Archives/Choir visions. No installed scripts are modified.
class cNativeDiaryRegression {
    unsigned trial=0,phase=0;
    Uint32 entered=0;
    bool requested=false,checkedResponses=false;
    bool originalTriggerPolicy=true;
    tString name;
    tString marker(const char* suffix) const {
        return "native-diary-"+cString::ToString(static_cast<int>(trial))+"-"+suffix;
    }
    static bool both(const tString& suffix) {return exists("host-"+suffix) && exists("client-"+suffix);}
    void next(unsigned value) {phase=value;entered=SDL_GetTicks();}
    int fail(tString& error,const tString& message) const {
        error="diary trial "+cString::ToString(static_cast<int>(trial))+": "+message;return -1;
    }
public:
    int Update(const cMatrixf& transform,tString& error) {
        auto* mp=gpBase->mpMultiplayer;auto* map=gpBase->mpMapHandler->GetCurrentMap();
        const bool host=role=="host",actor=host==(trial==4);
        const bool opens=trial!=0 && trial!=4;
        if(!mp->IsActive() || !mp->IsReady() || !map) return fail(error,"session/map is no longer ready");
        if(entered && SDL_GetTicks()-entered>10000) return fail(error,"pickup presentation timed out");
        if(phase==0) {
            if(trial==0) {
                originalTriggerPolicy=mp->mSettings.allPlayersTriggerScripts;
                auto* system=gpBase->mpEngine->GetSystem()->GetLowLevel();
                if(!system->AddScriptFunc("void CodexSuppressDiary(string &in name, int index)",(void*)CodexSuppressDiary) ||
                   !system->AddScriptFunc("void CodexShowDiary(string &in name, int index)",(void*)CodexShowDiary))
                    return fail(error,"diary callback registration failed");
            }
            if(host) mp->mSettings.allPlayersTriggerScripts=trial==3?false:originalTriggerPolicy;
            gpBase->mpEngine->GetUpdater()->SetContainer("Default");
            gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
            name="codex_diary_"+cString::ToString(static_cast<int>(trial));
            map->CreateEntity(name,"entities/item/diary_paper01/diary_paper01.ent",transform,1);
            auto* item=static_cast<cLuxProp_Item*>(map->GetEntityByName(name,eLuxEntityType_Prop,eLuxPropType_Item));
            if(!item || item->GetItemType()!=eLuxItemType_Diary) return fail(error,"retail diary fixture did not load");
            cResourceVarsObject vars;
            vars.AddVarString("DiaryText",trial<3?"CH01L03_Daniel":(trial==3?"CH01L10_Daniel03":"CH02L24_Daniel07_03"));
            vars.AddVarString("DiaryCallback",trial==1?"":(trial==2?"CodexShowDiary":"CodexSuppressDiary"));
            cLuxPropLoader_Item loader("Diary regression");loader.LoadInstanceVariables(item,&vars);
            if(actor) gpBase->mpPlayer->GetCharacterBody()->SetPosition(item->GetBody(0)->GetWorldPosition()+cVector3f(0,0,1));
            requested=false;
            mark(role+"-"+marker("armed.txt"),"retail diary and callback ready");next(1);return 0;
        }
        if(phase==1) {
            if(!both(marker("armed.txt")) || SDL_GetTicks()-entered<700) return 0;
            if(actor && !requested) {
                auto* item=map->GetEntityByName(name);
                if(!item) return fail(error,"diary disappeared before pickup");
                item->OnInteract(item->GetBody(0),item->GetBody(0)->GetWorldPosition());requested=true;
            }
            next(2);return 0;
        }
        if(phase==2) {
            const bool journal=gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()=="Journal";
            if((!actor || !opens) && journal) return fail(error,"suppressed or remote diary opened this player's journal");
            if(!host && journal && !mp->mpEntities->mPendingDiaries.empty())
                return fail(error,"client opened a diary before the host's callback decision");
            if(!host && !checkedResponses && !mp->mpEntities->mPendingDiaries.empty()) {
                const auto& pending=*mp->mpEntities->mPendingDiaries.begin();
                const size_t before=mp->mpEntities->mPendingDiaries.size();
                luxnet::Writer wrong(luxnet::NativeDiaryResult);wrong.U32(mp->GetMapEpoch());wrong.String(pending.second.name);
                wrong.U32(pending.first+1);wrong.U8(1);wrong.U8(1);
                if(mp->mpEntities->HandleMessage(0,wrong.data) || mp->mpEntities->mPendingDiaries.size()!=before)
                    return fail(error,"wrong diary response token consumed a pending presentation");
                luxnet::Writer mismatch(luxnet::NativeDiaryResult);mismatch.U32(mp->GetMapEpoch());mismatch.String("wrong_diary");
                mismatch.U32(pending.first);mismatch.U8(1);mismatch.U8(1);
                if(mp->mpEntities->HandleMessage(0,mismatch.data) || mp->mpEntities->mPendingDiaries.size()!=before)
                    return fail(error,"wrong diary response name consumed a pending presentation");
                // The normal packet router suspends effects while loading; the
                // presentation handler also refuses to open when not ready.
                cLuxMultiplayerEntities isolated(mp);isolated.mPendingDiaries.insert(pending);
                luxnet::Writer waiting(luxnet::NativeDiaryResult);waiting.U32(mp->GetMapEpoch());waiting.String(pending.second.name);
                waiting.U32(pending.first);waiting.U8(1);waiting.U8(1);
                mp->mbReady=false;const bool handled=isolated.HandleMessage(0,waiting.data);mp->mbReady=true;
                if(!handled || !isolated.mPendingDiaries.empty() || gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()=="Journal")
                    return fail(error,"late diary response reopened a waiting client");
                isolated.mPendingDiaries.insert(pending);isolated.Reset();
                if(!isolated.mPendingDiaries.empty()) return fail(error,"reset retained pending diary presentation");
                checkedResponses=true;
            }
            auto* item=map->GetEntityByName(name);
            if((item && !item->GetDestroyMe()) || !mp->mpEntities->msPending.empty() ||
               !mp->mpEntities->mClaims.empty() || !mp->mpEntities->mPendingDiaries.empty()) return 0;
            if(actor && opens && !journal) return fail(error,"accepted true/default diary decision did not open the collector's journal");
            const unsigned expected=host && trial!=1 && trial!=3?1:0;
            if(diaryCallbacks[name]!=expected) return fail(error,"diary callback ignored host-only policy or ran more than once");
            mark(role+"-"+marker("passed.txt"),opens?"collector journal opened after approved decision":"journal suppressed for scripted vision");
            next(3);return 0;
        }
        if(!both(marker("passed.txt"))) return 0;
        gpBase->mpEngine->GetUpdater()->SetContainer("Default");
        gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
        if(trial==4) {
            if(host) mp->mSettings.allPlayersTriggerScripts=originalTriggerPolicy;
            if(!host && !checkedResponses) return fail(error,"pending diary response validation was not exercised");
            return 1;
        }
        ++trial;next(0);return 0;
    }
};
#endif
