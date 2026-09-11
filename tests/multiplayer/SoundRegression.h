#ifndef CODEX_SOUND_REGRESSION_H
#define CODEX_SOUND_REGRESSION_H
#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerScript.h"
#include "LuxProp_Object.h"

// Use the real host script entry point and reliable effect stream. The existing
// isolated drawer prop supplies a health barrier which native snapshots do not
// replicate, so the client can pass only after executing every preceding effect.
class cSoundRegression {
    unsigned phase=0;
    Uint32 entered=0;
    float originalHealth=0;
    cLuxMap* originalMap=NULL;
    uint32_t originalEpoch=0;
    static bool both(const char* suffix) {
        return exists(tString("host-")+suffix) && exists(tString("client-")+suffix);
    }
    int fail(tString& error,const tString& message) const {
        error="sound preload regression: "+message;return -1;
    }
    bool decoderChecks(tString& error) {
        // These go directly through the production decoder so expected failures
        // do not intentionally disconnect the real session under test.
        luxnet::Writer truncated(luxnet::ScriptEffect);truncated.U32(84);
        truncated.U32(5);truncated.U8('x');
        luxnet::Reader truncatedReader(truncated.data);
        if(LuxApplyMultiplayerScriptEffect(truncatedReader,error)) {
            error="truncated PreloadSound payload was accepted";return false;
        }
        luxnet::Writer trailing(luxnet::ScriptEffect);trailing.U32(84);
        trailing.String("water_lurker_eat_rev.snt");trailing.U8(0);
        luxnet::Reader trailingReader(trailing.data);
        if(LuxApplyMultiplayerScriptEffect(trailingReader,error)) {
            error="PreloadSound payload with trailing data was accepted";return false;
        }
        const tString missing="codex_missing_sound_"+std::filesystem::path(outputDir).filename().string()+".snt";
        tString validationError;
        if(LuxValidateMultiplayerAsset(missing,"snt",validationError)) {
            error="missing sound fixture unexpectedly exists";return false;
        }
        luxnet::Writer optional(luxnet::ScriptEffect);optional.U32(84);optional.String(missing);
        luxnet::Reader optionalReader(optional.data);error="previous error";
        if(!LuxApplyMultiplayerScriptEffect(optionalReader,error) || !error.empty()) {
            error="unavailable optional preload was rejected or retained an error";return false;
        }
        const uint32_t playbackIds[]={29,88}; // PlayGuiSound, PlaySoundAtEntity.
        for(uint32_t id:playbackIds) {
            luxnet::Writer playback(luxnet::ScriptEffect);playback.U32(id);
            if(id==88) playback.String("codex_missing_playback");
            playback.String(missing);
            if(id==88) playback.String("Player");
            playback.Float(id==29?1.0f:0.0f);
            if(id==88) playback.U8(0);
            luxnet::Reader playbackReader(playback.data);error.clear();
            if(LuxApplyMultiplayerScriptEffect(playbackReader,error) || error.find(missing)==tString::npos) {
                error="missing playback sound was accepted or lacked a descriptive error";return false;
            }
        }
        error.clear();return true;
    }
public:
    // 0 pending, 1 complete, -1 failed.
    int Update(tString& error) {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!mp->IsActive() || !mp->IsReady() || !map) return fail(error,"session/map stopped or became unready");
        if(entered && SDL_GetTicks()-entered>10000) return fail(error,"reliable sound barrier timed out");
        auto* fixture=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_drawers_a",eLuxEntityType_Prop,eLuxPropType_Object));
        if(!fixture) return fail(error,"isolated drawer fixture is missing");
        if(phase==0) {
            originalMap=map;originalEpoch=mp->GetMapEpoch();originalHealth=fixture->GetHealth();
            if(originalHealth==123.25f) return fail(error,"health barrier already has its sentinel value");
            if(role=="client" && !decoderChecks(error)) return -1;
            mark(role+"-sound-armed.txt","decoder checks complete; waiting for real host preloads");
            entered=SDL_GetTicks();phase=1;return 0;
        }
        if(map!=originalMap || mp->GetMapEpoch()!=originalEpoch) return fail(error,"sound test changed the current map");
        if(phase==1) {
            if(!both("sound-armed.txt")) return 0;
            if(role=="host") {
                static const char* hints[]={
                    "water_lurker_eat_rev2", "8_done02.snt", "guardian_idle6",
                    "26_zimmerman_part1.ogg", "waterlurker_run_splash",
                    "insanity_monster_roar02", "insanity_monster_roar03",
                    "12_event_blood", "11_event_tree", "ater_lurker_hunt",
                    "28_done03.snt ", "water_lurker_eat_rev2.ogg ",
                    "water_lurker_eat_rev.snt"
                };
                const size_t count=sizeof(hints)/sizeof(hints[0]);
                tString validationError;
                if(!LuxValidateMultiplayerAsset(hints[count-1],"snt",validationError))
                    return fail(error,"valid sound preload fixture failed validation: "+validationError);
                const size_t historyStart=mp->mvScriptHistory.size();
                for(const char* hint:hints) map->RunScript(tString("PreloadSound(\"")+hint+"\");");
                map->RunScript("SetPropHealth(\"codex_drawers_a\",123.25f);");
                if(mp->mvScriptHistory.size()!=historyStart+count+1 || fixture->GetHealth()!=123.25f)
                    return fail(error,"host script did not broadcast every preload and the health barrier");
                for(size_t i=0;i<count;++i) {
                    luxnet::Reader sent(mp->mvScriptHistory[historyStart+i]);
                    if(sent.U32()!=originalEpoch || sent.U32()!=84 || sent.String(4096)!=hints[i] || !sent.Done())
                        return fail(error,"host preload was not serialized by the production script entry point");
                }
                mark("host-sound-broadcast.txt","ten reported hints, two whitespace variants, valid SNT, then reliable health barrier");
            }
            phase=2;return 0;
        }
        if(phase==2) {
            if(!exists("host-sound-broadcast.txt") || fixture->GetHealth()!=123.25f) return 0;
            mark(role+"-sound-barrier.txt","all preceding sound preloads processed; session remains active and ready");
            phase=3;return 0;
        }
        if(!both("sound-barrier.txt")) return 0;
        fixture->SetHealth(originalHealth);
        return 1;
    }
};
#endif
