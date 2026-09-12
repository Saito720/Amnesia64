#ifndef MULTIPLAYER_INCIDENTAL_EFFECTS_REGRESSION_H
#define MULTIPLAYER_INCIDENTAL_EFFECTS_REGRESSION_H
#include "LuxHelpFuncs.h"
#include "sound/SoundEntityData.h"
#include "sound/SoundHandler.h"

// Exercise production creation hooks and transport through ordinary native
// objects. Names identify fixtures, without gameplay-specific effect opcodes.
class cIncidentalEffectsRegression {
    unsigned phase=0;
    Uint32 entered=0;
    cVector3f oldPosition,position;
    bool oldGravity=true,sent=false;
    cSoundEntity *worldSound=NULL,*playerSound=NULL,*localSound=NULL,*physicsSound=NULL;
    cParticleSystem *worldParticle=NULL,*playerParticle=NULL,*localParticle=NULL,*physicsParticle=NULL;
    static bool both(const char* suffix) {return exists(tString("host-")+suffix) && exists(tString("client-")+suffix);}
    void next() {++phase;entered=SDL_GetTicks();sent=false;}
    int fail(tString& error,const tString& message) {error="generic effects phase "+cString::ToString((int)phase)+": "+message;return -1;}
    static cLuxMultiplayerEffects::Remote* remote(cLuxMultiplayerEffects* effects,const tString& name) {
        for(auto& entry:effects->mRemote) if(entry.second.effect.name==name) return &entry.second;return NULL;
    }
    static unsigned remoteCount(cLuxMultiplayerEffects* effects,const tString& name) {
        unsigned count=0;for(auto& entry:effects->mRemote) if(entry.second.effect.name==name) ++count;return count;
    }
    static bool captured(cLuxMultiplayerEffects* effects,const tString& name) {
        for(auto& entry:effects->mLocal) if(entry.second.effect.name==name) return true;return remote(effects,name)!=NULL;
    }
    static tString readMarker(const tString& name) {
        FILE* file=NULL;fopen_s(&file,(outputDir+"/"+name).c_str(),"rb");if(!file) return "";
        char text[1024];size_t count=fread(text,1,sizeof(text)-1,file);fclose(file);text[count]=0;return text;
    }
    static bool near(float a,float b) {return std::fabs(a-b)<0.02f;}
    static void pair(cWorld* world,const tString& name,const cVector3f& position,cSoundEntity*& sound,cParticleSystem*& particle) {
        sound=world->CreateSoundEntity(name+"_sound","00_loop.snt",false);
        particle=world->CreateParticleSystem(name+"_particle","ps_light_dust.ps",cVector3f(1),false);
        if(sound) {sound->SetPosition(position);sound->SetVolume(0.35f);sound->SetMinDistance(2);sound->SetMaxDistance(11);}
        if(particle) {particle->SetPosition(position);particle->SetColor(cColor(0.2f,0.4f,0.6f,0.8f));}
    }
    static bool received(cLuxMultiplayerEffects* effects,const tString& name,const cVector3f& position) {
        auto* sound=remote(effects,name+"_sound");auto* particle=remote(effects,name+"_particle");
        return sound && particle && sound->sound && particle->particle &&
            cMath::Vector3Dist(sound->sound->GetWorldPosition(),position)<0.02f &&
            cMath::Vector3Dist(particle->particle->GetWorldPosition(),position)<0.02f &&
            near(sound->sound->GetVolume(),0.35f) && near(sound->sound->GetMinDistance(),2) && near(sound->sound->GetMaxDistance(),11) &&
            near(particle->particle->GetColor().g,0.4f) && !sound->sound->GetPresentationSuppressed() && !particle->particle->GetPresentationSuppressed() &&
            remoteCount(effects,name+"_sound")==1 && remoteCount(effects,name+"_particle")==1;
    }
public:
    int Update(tString& error,float dt) {
        auto* session=gpBase->mpMultiplayer;auto* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!session->IsReady() || !map) return fail(error,"session stopped");
        auto* effects=session->GetEffects();auto* world=map->GetWorld();auto* body=gpBase->mpPlayer->GetCharacterBody();
        const bool host=session->IsHost();const Uint32 age=entered?SDL_GetTicks()-entered:0;
        if(age>10000) return fail(error,"phase timed out");
        if(phase==0) {
            oldPosition=body->GetPosition();oldGravity=body->GravityIsActive();
            auto* door=map->GetEntityByName("Door_1",eLuxEntityType_Prop,eLuxPropType_SwingDoor);
            if(!door) return fail(error,"retail position fixture missing");
            position=door->GetBody(0)->GetWorldPosition()+cVector3f(0,0,2);
            body->SetGravityActive(false);body->SetForceVelocity(0);body->SetPosition(position);
            mark(role+"-fx-armed.txt","generic fixture positioned");next();return 0;
        }
        if(phase==1) {
            if(!both("fx-armed.txt") || age<300) return 0;
            pair(world,"codex_fx_world",position,worldSound,worldParticle);
            {cWorldEffectSourceScope source(world,body->GetCurrentBody());pair(world,"codex_fx_"+role,position,playerSound,playerParticle);}
            {cWorldEffectLocalScope local(world);pair(world,"codex_fx_local_"+role,position,localSound,localParticle);}
            {auto* door=map->GetEntityByName("Door_1",eLuxEntityType_Prop,eLuxPropType_SwingDoor);
                cWorldEffectSourceScope physics(world,door->GetBody(0));pair(world,"codex_fx_physics_"+role,position,physicsSound,physicsParticle);}
            if(!worldSound || !worldParticle || !playerSound || !playerParticle || !localSound || !localParticle || !physicsSound || !physicsParticle)
                return fail(error,"native fixture asset failed to create");
            next();return 0;
        }
        if(phase==2) {
            if(!received(effects,"codex_fx_"+tString(host?"client":"host"),position) || (!host && !received(effects,"codex_fx_world",position))) return 0;
            if(remote(effects,"codex_fx_"+role+"_sound") || remote(effects,"codex_fx_"+role+"_particle")) return fail(error,"player effect echoed to its creator");
            if(worldSound->GetPresentationSuppressed()!=!host || worldParticle->GetPresentationSuppressed()!=!host)
                return fail(error,"world authority did not suppress only the client native presentation");
            auto* entry=worldSound->GetSoundEntry(eSoundEntityType_Main,true);if(!entry) return 0;
            if(entry->GetPresentationSuppressed()!=!host || (!host && entry->GetChannel()->GetVolume()!=0) || worldSound->IsStopped() || !worldParticle->IsVisible())
                return fail(error,"suppression altered native lifetime or failed to mute the live channel");
            if(localSound->GetPresentationSuppressed() || physicsSound->GetPresentationSuppressed() || localParticle->GetPresentationSuppressed() || physicsParticle->GetPresentationSuppressed() ||
               captured(effects,"codex_fx_local_"+role+"_sound") || captured(effects,"codex_fx_physics_"+role+"_particle"))
                return fail(error,"local/physics presentation entered the network stream");
            if(host) {
                auto* observed=remote(effects,"codex_fx_client_particle");auto invalidMotion=observed->effect;
                invalidMotion.operation=luxfx::State;invalidMotion.matrix[3]+=1000;
                if(!effects->HandleMessage(invalidMotion.peer,luxfx::Packet(invalidMotion),error) ||
                   cMath::Vector3Dist(observed->particle->GetWorldPosition(),position)>0.02f)
                    return fail(error,"player effect state accepted motion far from its player and original emission");
            }
            mark(role+"-fx-created.txt","native objects finalized once; player relay and suppression verified");next();return 0;
        }
        if(phase==3) {
            if(!both("fx-created.txt")) return 0;
            if(host) {
                worldSound->SetVolume(0.65f);if(auto* entry=worldSound->GetSoundEntry(eSoundEntityType_Main,true)) entry->SetDefaultVolume(0.65f);
                worldSound->SetPosition(position+cVector3f(0.2f,0,0));worldParticle->SetColor(cColor(0.6f,0.7f,0.8f,0.9f));worldParticle->SetVisible(false);
            }
            next();return 0;
        }
        if(phase==4) {
            if(!host) {
                auto* sound=remote(effects,"codex_fx_world_sound");auto* particle=remote(effects,"codex_fx_world_particle");
                if(!sound || !particle || !near(sound->sound->GetVolume(),0.65f) ||
                   cMath::Vector3Dist(sound->sound->GetWorldPosition(),position+cVector3f(0.2f,0,0))>0.02f ||
                   !near(particle->particle->GetColor().g,0.7f) || particle->particle->IsVisible()) return 0;
            }
            mark(role+"-fx-state.txt","final state delivered after snapshot interval");next();return 0;
        }
        if(phase==5) {
            if(!both("fx-state.txt")) return 0;
            if(host) {worldSound->Stop(false);worldParticle->Kill();}
            next();return 0;
        }
        if(phase==6) {
            if(!host) {
                auto* sound=remote(effects,"codex_fx_world_sound");auto* particle=remote(effects,"codex_fx_world_particle");
                if(!sound || !(sound->effect.flags&luxfx::Stopped) || !sound->sound->IsStopped() || !particle || !(particle->effect.flags&luxfx::Dying)) return 0;
            }
            mark(role+"-fx-stopped.txt","native loop stop and particle kill replicated");next();return 0;
        }
        if(phase==7) {
            if(!both("fx-stopped.txt")) return 0;
            if(!host) {
                size_t before=effects->mPlayerSounds.size();cLuxSoundExtraData output;
                if(!gpBase->mpHelpFuncs->PlayGuiSoundData("step_walk_wood",eSoundEntryType_World,1,eSoundEntityType_Main,true,&output) || !output.mpSoundEntry)
                    return fail(error,"native world footstep helper failed");
                if(effects->mPlayerSounds.size()!=before+1) return fail(error,"world helper did not enqueue exactly one player sound");
                const auto& queued=effects->mPlayerSounds.back();tString clip=output.mpSoundEntry->GetName();
                if(queued.asset!=clip || !near(queued.volume,output.mfVolume) || !near(queued.minimum,output.mfMinDistance) || !near(queued.maximum,output.mfMaxDistance))
                    return fail(error,"player helper lost chosen sample or authored attenuation");
                before=effects->mPlayerSounds.size();gpBase->mpHelpFuncs->PlayGuiSoundData("step_walk_water",eSoundEntryType_Gui);
                if(effects->mPlayerSounds.size()!=before) return fail(error,"GUI sound was broadcast as a world sound");
                mark("client-fx-raw-clip.txt",clip);
            }
            next();return 0;
        }
        if(phase==8) {
            if(!exists("client-fx-raw-clip.txt")) return 0;
            if(host) {
                const tString clip=readMarker("client-fx-raw-clip.txt");bool heard=false;
                for(auto* entry:*world->GetSound()->GetSoundHandler()->GetEntryList()) if(entry->GetName()==clip && !entry->GetChannel()->GetPositionIsRelative()) heard=true;
                if(!heard) return 0;mark("host-fx-raw-heard.txt","native chosen footstep received positionally");
            }
            if(!exists("host-fx-raw-heard.txt")) return 0;
            if(host) map->RunScript("PlaySoundAtEntity(\"codex_fx_script_sound\",\"00_loop.snt\",\"Door_1\",0,false); CreateParticleSystemAtEntity(\"codex_fx_script_particle\",\"ps_light_dust.ps\",\"Door_1\",false);");
            next();return 0;
        }
        if(phase==9) {
            if(!world->GetSoundEntity("codex_fx_script_sound") || !world->GetParticleSystem("codex_fx_script_particle")) return 0;
            if(captured(effects,"codex_fx_script_sound") || captured(effects,"codex_fx_script_particle")) return fail(error,"typed script presentation entered generic stream twice");
            mark(role+"-fx-script.txt","script effects execute without generic duplication");next();return 0;
        }
        if(phase==10) {
            if(!both("fx-script.txt")) return 0;
            for(auto* sound:{worldSound,playerSound,localSound,physicsSound,world->GetSoundEntity("codex_fx_script_sound")}) if(sound) world->DestroySoundEntity(sound);
            for(auto* particle:{worldParticle,playerParticle,localParticle,physicsParticle,world->GetParticleSystem("codex_fx_script_particle")}) if(particle) world->DestroyParticleSystem(particle);
            worldSound=playerSound=localSound=physicsSound=NULL;worldParticle=playerParticle=localParticle=physicsParticle=NULL;
            next();return 0;
        }
        if(phase==11) {
            for(auto& entry:effects->mRemote) if(entry.second.effect.name.find("codex_fx_")==0) return 0;
            if(host) for(auto& packet:session->mvScriptHistory) if(!packet.empty() && packet[0]==luxnet::WorldEffect) return fail(error,"world effect entered script history");
            body->SetPosition(oldPosition);body->SetForceVelocity(0);body->SetGravityActive(oldGravity);
            mark(role+"-incidental-finished.txt","generic sound/particle creation, state, termination, player bridge, and echo guards passed");next();return 0;
        }
        return both("incidental-finished.txt")?1:0;
    }
};
#endif
