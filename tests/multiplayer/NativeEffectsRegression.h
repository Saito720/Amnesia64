#ifndef MULTIPLAYER_NATIVE_EFFECTS_REGRESSION_H
#define MULTIPLAYER_NATIVE_EFFECTS_REGRESSION_H
#include "LuxHelpFuncs.h"
#include "LuxMultiplayerEntityProtocol.h"
#include "LuxProp_SwingDoor.h"
#include "sound/SoundHandler.h"

// Native producers use the same stream as arbitrary world effects. Baseline
// replay replaces only the fixture handles, leaving the session running.
class cNativeEffectsRegression {
    unsigned phase=0;
    Uint32 entered=0;
    bool sent=false,oldGravity=true,oldClosed=false,oldAutoClose=false;
    cVector3f oldPosition,position;
    cSoundEntity* loop=NULL;
    cSoundEntity* reusable=NULL;
    cParticleSystem* particle=NULL;
    static bool both(const char* suffix) {return exists(tString("host-")+suffix) && exists(tString("client-")+suffix);}
    void next() {++phase;entered=SDL_GetTicks();sent=false;}
    int fail(tString& error,const tString& message) {error="native effects phase "+cString::ToString((int)phase)+": "+message;return -1;}
    static bool near(float a,float b) {return std::fabs(a-b)<0.02f;}
    static unsigned sounds(cWorld* world,const tString& name) {
        unsigned count=0;auto it=world->GetSoundEntityIterator();while(it.HasNext()) if(it.Next()->GetName()==name) ++count;return count;
    }
    static cLuxMultiplayerEffects::Remote* remote(cLuxMultiplayerEffects* effects,const tString& name) {
        for(auto& entry:effects->mRemote) if(entry.second.effect.name==name) return &entry.second;return NULL;
    }
    static unsigned remoteCount(cLuxMultiplayerEffects* effects,const tString& name) {
        unsigned count=0;for(auto& entry:effects->mRemote) if(entry.second.effect.name==name) ++count;return count;
    }
    static tString readClip() {
        FILE* file=NULL;fopen_s(&file,(outputDir+"/client-native-water-clip.txt").c_str(),"rb");if(!file) return "";
        char text[512];size_t count=fread(text,1,sizeof(text)-1,file);fclose(file);text[count]=0;return text;
    }
    static bool received(cLuxMultiplayerEffects* effects,const cVector3f& position) {
        auto* sound=remote(effects,"codex_native_live_sound");auto* ps=remote(effects,"codex_native_live_particle");
        return sound && ps && sound->sound && ps->particle &&
            remoteCount(effects,"codex_native_live_sound")==1 && remoteCount(effects,"codex_native_live_particle")==1 &&
            cMath::Vector3Dist(sound->sound->GetWorldPosition(),position)<0.02f &&
            cMath::Vector3Dist(ps->particle->GetWorldPosition(),position)<0.02f &&
            near(sound->sound->GetVolume(),0.45f) && near(ps->particle->GetColor().g,0.3f) &&
            !sound->sound->IsStopped() && !sound->sound->GetPresentationSuppressed() && !ps->particle->GetPresentationSuppressed();
    }
    bool replaySounds(cLuxMultiplayerEffects* effects,cLuxMultiplayer* session) {
        auto locals=effects->mLocal;auto remotes=effects->mRemote;
        for(auto it=effects->mLocal.begin();it!=effects->mLocal.end();) {
            if(it->second.sound!=loop && it->second.sound!=reusable) it=effects->mLocal.erase(it);else ++it;
        }
        effects->mRemote.clear();bool ok=true;
        for(const auto& peer:session->mPeers) if(peer.second.ready) ok=effects->SendInitialState(peer.first) && ok;
        effects->mLocal=locals;effects->mRemote=remotes;return ok;
    }
public:
    int Update(tString& error,float dt) {
        auto* session=gpBase->mpMultiplayer;auto* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!session->IsReady() || !map) return fail(error,"session stopped");
        auto* effects=session->GetEffects();auto* world=map->GetWorld();auto* body=gpBase->mpPlayer->GetCharacterBody();
        auto* door=static_cast<cLuxProp_SwingDoor*>(map->GetEntityByName("Door_1",eLuxEntityType_Prop,eLuxPropType_SwingDoor));
        if(!door) return fail(error,"retail Door_1 is missing");
        const bool host=session->IsHost();const Uint32 age=entered?SDL_GetTicks()-entered:0;
        if(age>10000) return fail(error,"phase timed out");
        if(phase==0) {
            oldPosition=body->GetPosition();oldGravity=body->GravityIsActive();oldClosed=door->GetClosed();oldAutoClose=door->GetDisableAutoClose();
            position=door->GetBody(0)->GetWorldPosition()+cVector3f(0,0,2);
            body->SetGravityActive(false);body->SetForceVelocity(0);body->SetPosition(position);
            door->SetDisableAutoClose(true);door->SetClosed(false,false);
            while(auto* sound=world->GetSoundEntity("Door_1_CloseOn")) world->DestroySoundEntity(sound);
            if(!host) {
                door->SetClosed(true,false);
                luxnet::Writer state(luxnet::EntityState);state.U32(session->GetMapEpoch());state.String("Door_1");
                state.U8(luxnet::DoorState);state.U8(luxnet::EntityActive|luxnet::EffectsActive);
                state.U8(4|(door->GetLocked()?2:0));state.U32(0);
                if(!session->GetEntities()->HandleMessage(0,state.data) || door->GetClosed() || sounds(world,"Door_1_CloseOn"))
                    return fail(error,"native state correction emitted a latch sound");
            }
            mark(role+"-native-fx-armed.txt","silent door correction and stationary players ready");next();return 0;
        }
        if(phase==1) {
            if(!both("native-fx-armed.txt") || age<300) return 0;
            if(host) {
                door->SetClosed(true,true);door->SetClosed(true,true);
                if(sounds(world,"Door_1_CloseOn")!=1) return fail(error,"native latch did not play once for one state edge");
            }
            next();return 0;
        }
        if(phase==2) {
            if(!host) {
                auto* latch=remote(effects,"Door_1_CloseOn");if(!latch || !latch->sound) return 0;
                if(remoteCount(effects,"Door_1_CloseOn")!=1 || sounds(world,"Door_1_CloseOn") || latch->sound->GetPresentationSuppressed())
                    return fail(error,"native latch duplicated or remained silent on client");
            } else if(remote(effects,"Door_1_CloseOn")) return fail(error,"host latch echoed to its origin");
            mark(role+"-native-latch-heard.txt","native latch shared once");next();return 0;
        }
        if(phase==3) {
            if(!both("native-latch-heard.txt")) return 0;
            if(!host) {
                const size_t count=effects->mPlayerSounds.size();cLuxSoundExtraData output;
                if(!gpBase->mpHelpFuncs->PlayGuiSoundData("step_walk_water",eSoundEntryType_World,0.6f,eSoundEntityType_Main,true,&output) || !output.mpSoundEntry)
                    return fail(error,"native water step helper failed");
                if(effects->mPlayerSounds.size()!=count+1) return fail(error,"water step did not enqueue exactly one effect");
                const auto& queued=effects->mPlayerSounds.back();
                if(queued.asset!=output.mpSoundEntry->GetName() || !near(queued.volume,output.mfVolume*0.6f) ||
                   !near(queued.minimum,output.mfMinDistance) || !near(queued.maximum,output.mfMaxDistance))
                    return fail(error,"water step lost selected clip, gain or attenuation");
                mark("client-native-water-clip.txt",queued.asset);
            }
            next();return 0;
        }
        if(phase==4) {
            if(!exists("client-native-water-clip.txt")) return 0;
            const tString clip=readClip();unsigned positional=0;
            for(auto* entry:*world->GetSound()->GetSoundHandler()->GetEntryList()) if(entry->GetName()==clip && !entry->GetChannel()->GetPositionIsRelative()) {
                ++positional;
                if(host && (!near(entry->GetDefaultVolume(),0.24f) || !near(entry->GetChannel()->GetMinDistance(),4) || !near(entry->GetChannel()->GetMaxDistance(),16)))
                    return fail(error,"remote water step has incorrect native gain or distance");
            }
            if(!host && positional) return fail(error,"water step echoed positionally to its first-person actor");
            if(host) {if(!positional) return 0;if(positional!=1) return fail(error,"water step played more than once");mark("host-native-water-heard.txt","selected water step received once");}
            if(!exists("host-native-water-heard.txt")) return 0;
            mark(role+"-native-water-done.txt","water helper preserved native playback without echo");next();return 0;
        }
        if(phase==5) {
            if(!both("native-water-done.txt")) return 0;
            if(host) {
                loop=world->CreateSoundEntity("codex_native_live_sound","00_loop.snt",false);
                particle=world->CreateParticleSystem("codex_native_live_particle","ps_light_dust.ps",cVector3f(1),false);
                if(!loop || !particle) return fail(error,"live baseline assets failed to load");
                loop->SetPosition(position);loop->SetVolume(0.45f);particle->SetPosition(position);particle->SetColor(cColor(0.2f,0.3f,0.4f,1));
            }
            next();return 0;
        }
        if(phase==6) {
            if(!host && !sent) {
                if(!received(effects,position)) return 0;
                for(const auto* name:{"codex_native_live_sound","codex_native_live_particle"}) {
                    auto effect=remote(effects,name)->effect;effect.operation=luxfx::Remove;
                    if(!effects->HandleMessage(0,luxfx::Packet(effect),error)) return fail(error,"could not clear stale baseline fixture");
                }
                mark("client-native-baseline-stale.txt","live fixture handles removed through public decoder");sent=true;
            }
            if(host && exists("client-native-baseline-stale.txt")) {
                // Restrict this replay to the removed fixture handles so live
                // unrelated effects cannot receive duplicate Create packets.
                auto locals=effects->mLocal;auto remotes=effects->mRemote;
                for(auto it=effects->mLocal.begin();it!=effects->mLocal.end();) {
                    if(it->second.sound!=loop && it->second.particle!=particle) it=effects->mLocal.erase(it);else ++it;
                }
                effects->mRemote.clear();bool ok=true;
                for(const auto& peer:session->mPeers) if(peer.second.ready) ok=effects->SendInitialState(peer.first) && ok;
                effects->mLocal=locals;effects->mRemote=remotes;
                if(!ok) return fail(error,"live baseline send failed");
                mark("host-native-baseline-sent.txt","production initial effect state resent");
            }
            if(!exists("host-native-baseline-sent.txt")) return 0;
            next();return 0;
        }
        if(phase==7) {
            if(!host && !received(effects,position)) return 0;
            mark(role+"-native-baseline-restored.txt","live loop and particle restored once with current presentation state");next();return 0;
        }
        if(phase==8) {
            if(!both("native-baseline-restored.txt")) return 0;
            if(host) {world->DestroySoundEntity(loop);world->DestroyParticleSystem(particle);loop=NULL;particle=NULL;}
            next();return 0;
        }
        if(phase==9) {
            if(!host && (remote(effects,"codex_native_live_sound") || remote(effects,"codex_native_live_particle"))) return 0;
            if(host) {
                loop=world->CreateSoundEntity("codex_native_restart_loop","00_loop.snt",false);
                reusable=world->CreateSoundEntity("codex_native_restart_once","scare_male_terrified5.snt",false);
                if(!loop || !reusable || reusable->GetData()->GetLoop()) return fail(error,"reusable sound fixtures failed");
                loop->SetPosition(position);reusable->SetPosition(position);
            }
            next();return 0;
        }
        if(phase==10) {
            if(!host) {
                if(!remote(effects,"codex_native_restart_loop") || !remote(effects,"codex_native_restart_once")) return 0;
                mark("client-native-reusable-created.txt","reusable handles initially received");
            }
            if(!exists("client-native-reusable-created.txt")) return 0;
            if(host) {loop->Stop(false);reusable->Stop(false);}
            next();return 0;
        }
        if(phase==11) {
            if(!host && !sent) {
                for(const auto* name:{"codex_native_restart_loop","codex_native_restart_once"}) {
                    auto* value=remote(effects,name);
                    if(!value || !(value->effect.flags&luxfx::Stopped) || !value->sound || !value->sound->IsStopped()) return 0;
                }
                for(const auto* name:{"codex_native_restart_loop","codex_native_restart_once"}) {
                    auto effect=remote(effects,name)->effect;effect.operation=luxfx::Remove;
                    if(!effects->HandleMessage(0,luxfx::Packet(effect),error)) return fail(error,"failed to remove reusable baseline fixtures");
                }
                mark("client-native-reusable-stale.txt","stopped handles removed before late-join baseline");sent=true;
            }
            if(host && exists("client-native-reusable-stale.txt") && !sent) {
                if(!replaySounds(effects,session)) return fail(error,"dormant baseline send failed");
                mark("host-native-reusable-baseline.txt","stopped loop and reusable one-shot declared");sent=true;
            }
            if(!exists("host-native-reusable-baseline.txt")) return 0;
            next();return 0;
        }
        if(phase==12) {
            if(!host) {
                for(const auto* name:{"codex_native_restart_loop","codex_native_restart_once"}) {
                    auto* value=remote(effects,name);if(!value || !value->sound) return 0;
                    if(!value->sound->IsStopped() || value->sound->GetSoundEntry(eSoundEntityType_Main,true) || remoteCount(effects,name)!=1)
                        return fail(error,"late-join declaration replayed a stopped sound");
                }
                if(age<300) return 0;
                mark("client-native-reusable-silent.txt","dormant loop and one-shot stay silent after native world updates");
            }
            if(!exists("client-native-reusable-silent.txt")) return 0;
            if(host) {loop->Play(false);reusable->Play(false);}
            next();return 0;
        }
        if(phase==13) {
            if(!host) {
                for(const auto* name:{"codex_native_restart_loop","codex_native_restart_once"}) {
                    auto* value=remote(effects,name);
                    if(!value || !value->sound || !value->sound->GetSoundEntry(eSoundEntityType_Main,true)) return 0;
                    if(remoteCount(effects,name)!=1 || value->sound->GetPresentationSuppressed()) return fail(error,"restarted sound duplicated or stayed suppressed");
                }
                mark("client-native-reusable-restarted.txt","both dormant handles played their next native occurrence");
            } else if(remote(effects,"codex_native_restart_loop") || remote(effects,"codex_native_restart_once"))
                return fail(error,"restart echoed to the host");
            if(!exists("client-native-reusable-restarted.txt")) return 0;
            // An ongoing reusable one-shot's declaration skips that occurrence,
            // including subsequent transform/volume updates, until its next run.
            if(!host) {
                luxfx::Effect declaration=remote(effects,"codex_native_restart_once")->effect;
                declaration.id=0x70001001;declaration.name="codex_native_dormant_active";declaration.operation=luxfx::DeclareSound;
                declaration.flags=luxfx::Active;
                if(!effects->HandleMessage(0,luxfx::Packet(declaration),error)) return fail(error,"active reusable declaration rejected");
                auto* value=remote(effects,declaration.name);
                if(!value || !value->sound->IsStopped()) return fail(error,"declaration replayed an ongoing historical one-shot");
                declaration.operation=luxfx::State;declaration.volume=0.4f;
                if(!effects->HandleMessage(0,luxfx::Packet(declaration),error) || !value->sound->IsStopped()) return fail(error,"state update replayed skipped historical one-shot");
                declaration.flags|=luxfx::Stopped;
                if(!effects->HandleMessage(0,luxfx::Packet(declaration),error)) return fail(error,"dormant stop rejected");
                declaration.flags=luxfx::Active;
                if(!effects->HandleMessage(0,luxfx::Packet(declaration),error) || !value->sound->GetSoundEntry(eSoundEntityType_Main,true))
                    return fail(error,"dormant active one-shot did not play its next occurrence");
                declaration.operation=luxfx::Remove;
                if(!effects->HandleMessage(0,luxfx::Packet(declaration),error) || remote(effects,declaration.name)) return fail(error,"dormant handle removal failed");
            }
            mark(role+"-native-reusable-passed.txt","silent declarations, next playback, no echo and skipped in-progress one-shot passed");next();return 0;
        }
        if(phase==14) {
            if(!both("native-reusable-passed.txt")) return 0;
            if(host) {world->DestroySoundEntity(loop);world->DestroySoundEntity(reusable);loop=NULL;reusable=NULL;}
            next();return 0;
        }
        if(phase==15) {
            if(!host && (remote(effects,"codex_native_restart_loop") || remote(effects,"codex_native_restart_once"))) return 0;
            door->SetClosed(oldClosed,false);door->SetDisableAutoClose(oldAutoClose);
            body->SetPosition(oldPosition);body->SetForceVelocity(0);body->SetGravityActive(oldGravity);
            mark(role+"-native-effects-finished.txt","native latch, water step, live and dormant sound baselines passed");next();return 0;
        }
        return both("native-effects-finished.txt")?1:0;
    }
};
#endif
