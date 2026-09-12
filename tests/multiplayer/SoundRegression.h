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
    cSoundEntity* invalidRuntimeSound=NULL;
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
        // Missing host audio may still leave a native queued voice/subtitle
        // effect. Waive only that resource, retaining strictness for the other
        // audio argument in the same command.
        const tString effect="00_loop.ogg", absentEffect=missing+"_effect";
        if(!LuxValidateMultiplayerAsset(effect,"audio",validationError)) {
            error="valid voice effect fixture failed: "+validationError;return false;
        }
        auto voiceCommand=[&](uint32_t mask,const tString& second) {
            luxnet::Writer w(luxnet::ScriptEffect);w.U32(20|(mask?LuxScriptOptionalResources:0));
            if(mask) w.U32(mask);
            w.String(missing);w.String(second);w.String("");w.String("");
            w.U8(0);w.String("");w.Float(1);w.Float(10);return w;
        };
        auto* voices=gpBase->mpEffectHandler->GetPlayVoice();
        const size_t voiceCount=voices->mlstVoices.size();
        const bool voiceActive=voices->IsActive();
        auto preflight=voiceCommand(0,effect);luxnet::Reader check(preflight.data);
        uint32_t unavailable=0;
        if(!LuxValidateMultiplayerScriptEffect(check,error,&unavailable) || unavailable!=1 || voices->mlstVoices.size()!=voiceCount) {
            error="host preflight did not isolate the unavailable voice or mutated native state";return false;
        }
        auto strict=voiceCommand(1,absentEffect);luxnet::Reader strictReader(strict.data);
        if(LuxApplyMultiplayerScriptEffect(strictReader,error) || error.find(absentEffect)==tString::npos || voices->mlstVoices.size()!=voiceCount) {
            error="optional first resource waived the second resource or changed native state";return false;
        }
        auto partial=voiceCommand(1,effect);luxnet::Reader partialReader(partial.data);
        if(!LuxApplyMultiplayerScriptEffect(partialReader,error) || voices->mlstVoices.size()!=voiceCount+1 ||
           voices->mlstVoices.back().msVoiceFile!=missing || voices->mlstVoices.back().msEffectFile!=effect) {
            error="native voice queue side effect was discarded with optional audio";return false;
        }
        voices->mlstVoices.pop_back();voices->SetActive(voiceActive);
        for(uint32_t mask:{0u,2u}) {
            luxnet::Writer invalid(luxnet::ScriptEffect);invalid.U32(29|LuxScriptOptionalResources);invalid.U32(mask);
            invalid.String(missing);invalid.Float(1);luxnet::Reader bad(invalid.data);
            if(LuxApplyMultiplayerScriptEffect(bad,error)) {error="invalid resource mask was accepted";return false;}
        }
        luxnet::Writer shortMask(luxnet::ScriptEffect);shortMask.U32(29|LuxScriptOptionalResources);shortMask.U8(1);
        luxnet::Reader shortReader(shortMask.data);
        if(LuxApplyMultiplayerScriptEffect(shortReader,error)) {error="truncated resource mask was accepted";return false;}
        auto* fixture=static_cast<cLuxProp_Object*>(gpBase->mpMapHandler->GetCurrentMap()->GetEntityByName("codex_drawers_a",eLuxEntityType_Prop,eLuxPropType_Object));
        const float health=fixture->GetHealth();
        luxnet::Writer noResource(luxnet::ScriptEffect);noResource.U32(152|LuxScriptOptionalResources);noResource.U32(1);
        noResource.String(fixture->GetName());noResource.Float(health+10);luxnet::Reader noResourceReader(noResource.data);
        if(LuxApplyMultiplayerScriptEffect(noResourceReader,error) || fixture->GetHealth()!=health) {
            error="unused resource mask was accepted or mutated gameplay before rejection";return false;
        }
        error.clear();return true;
    }
    bool hostAssetChecks(cLuxMap* map,cLuxMultiplayer* mp,cLuxProp_Object* fixture,tString& error) {
        // Validation shares the execution registry but must never apply a
        // gameplay effect while checking the host's resource requirements.
        const float health=fixture->GetHealth();
        luxnet::Writer validate(luxnet::ScriptEffect);validate.U32(152);validate.String(fixture->GetName());validate.Float(712.5f);
        luxnet::Reader validation(validate.data);
        if(!LuxValidateMultiplayerScriptEffect(validation,error) || fixture->GetHealth()!=health)
            return false;
        const size_t count=mp->mvScriptHistory.size();
        // These are actual delayed retail mistakes, followed by a unique
        // missing particle to prove the rule is not a sound-name exception.
        map->RunScript("PlayGuiSound(\"react_creath\",0.7f);PlaySoundAtEntity(\"enemy\",\"enemy\",\"Door_1\",0,false);");
        const tString missing="codex_missing_runtime_"+std::filesystem::path(outputDir).filename().string();
        map->RunScript("CreateParticleSystemAtEntity(\"codex_missing_ps\",\""+missing+"\",\"Door_1\",false);");
        if(mp->mvScriptHistory.size()!=count+3) {error="host-native fallback commands were discarded";return false;}
        const uint32_t ids[]={29,88,85};
        for(size_t i=0;i<3;++i) {
            luxnet::Reader sent(mp->mvScriptHistory[count+i]);
            if(sent.U32()!=mp->GetMapEpoch() || sent.U32()!=(ids[i]|LuxScriptOptionalResources) || sent.U32()!=1) {
                error="unavailable host resource did not receive its precise optional mask";return false;
            }
        }
        // A real asset and a nested native particle helper still broadcast once.
        map->RunScript("PlaySoundAtEntity(\"codex_valid_sound\",\"00_loop.snt\",\"Door_1\",0,false);CreateParticleSystemAtEntity(\"codex_valid_ps\",\"ps_light_dust.ps\",\"Door_1\",false);");
        if(mp->mvScriptHistory.size()!=count+5 || !map->GetWorld()->GetSoundEntity("codex_valid_sound") || !map->GetWorld()->GetParticleSystem("codex_valid_ps")) {
            error="valid host playback or nested helper did not publish once";return false;
        }
        return true;
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
                    const uint32_t epoch=sent.U32(), encoded=sent.U32();
                    const uint32_t mask=(encoded&LuxScriptOptionalResources)?sent.U32():0;
                    if(epoch!=originalEpoch || (encoded&~LuxScriptOptionalResources)!=84 || mask>1 ||
                       ((encoded&LuxScriptOptionalResources) && !mask) || sent.String(4096)!=hints[i] || !sent.Done())
                        return fail(error,"host preload was not serialized by the production script entry point");
                }
                if(!hostAssetChecks(map,mp,fixture,error)) return fail(error,error);
                map->RunScript("SetPropHealth(\"codex_drawers_a\",123.5f);");
                mark("host-sound-broadcast.txt","ten reported hints, two whitespace variants, valid SNT, then reliable health barrier");
            }
            phase=2;return 0;
        }
        if(phase==2) {
            if(!exists("host-sound-broadcast.txt") || fixture->GetHealth()!=123.5f ||
               !map->GetWorld()->GetSoundEntity("codex_valid_sound") || !map->GetWorld()->GetParticleSystem("codex_valid_ps")) return 0;
            mark(role+"-sound-barrier.txt","all preceding sound preloads processed; session remains active and ready");
            phase=3;return 0;
        }
        if(phase==3) {
            if(!both("sound-barrier.txt")) return 0;
            if(role=="host") {
                // A native sound entity can exist even though its sample is
                // absent. Its origin must not impose that broken definition
                // on peers as a mandatory asset.
                const auto folder=std::filesystem::path(outputDir)/"native-sound-fixture";
                std::filesystem::create_directory(folder);
                const tString asset="codex_invalid_native_"+std::filesystem::path(outputDir).filename().string()+".snt";
                FILE* file=cPlatform::OpenFile((folder/asset).wstring(),_W("wb"));
                if(!file) return fail(error,"could not write native asset fixture");
                const tString xml="<SOUNDENTITY><SOUNDS><Main><Sound File=\"codex_missing_native_sample\" /></Main></SOUNDS><PROPERTIES Loop=\"False\" Use3D=\"True\" /></SOUNDENTITY>";
                std::fwrite(xml.data(),1,xml.size(),file);std::fclose(file);
                gpBase->mpEngine->GetResources()->AddResourceDir(folder.wstring(),false);
                invalidRuntimeSound=map->GetWorld()->CreateSoundEntity("codex_invalid_native",asset,false);
                if(!invalidRuntimeSound) return fail(error,"native missing-sample fixture did not create its controller");
            }
            phase=4;entered=SDL_GetTicks();return 0;
        }
        if(phase==4) {
            if(SDL_GetTicks()-entered<200) return 0;
            if(role=="host") {
                auto found=mp->GetEffects()->mLocal.find(invalidRuntimeSound);
                if(found==mp->GetEffects()->mLocal.end() || !found->second.resourcesChecked || found->second.resourcesValid || found->second.published)
                    return fail(error,"invalid native source definition was not rejected before publication");
                map->GetWorld()->DestroySoundEntity(invalidRuntimeSound);invalidRuntimeSound=NULL;
            }
            for(auto& entry:mp->GetEffects()->mRemote) if(entry.second.effect.name=="codex_invalid_native")
                return fail(error,"invalid native source reached another player");
            if(auto* sound=map->GetWorld()->GetSoundEntity("codex_valid_sound")) map->GetWorld()->DestroySoundEntity(sound);
            if(auto* ps=map->GetWorld()->GetParticleSystem("codex_valid_ps")) map->GetWorld()->DestroyParticleSystem(ps);
            fixture->SetHealth(originalHealth);mark(role+"-sound-source-passed.txt","host-native script fallbacks preserved; invalid native presentation omitted; valid source resources remain strict on clients");
            phase=5;return 0;
        }
        return both("sound-source-passed.txt")?1:0;
    }
};
#endif
