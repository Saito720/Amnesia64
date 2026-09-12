#include "LuxMultiplayerEffects.h"
#include "LuxMultiplayer.h"
#include "LuxMultiplayerWorld.h"
#include "LuxMultiplayerContent.h"
#include "LuxMap.h"
#include "LuxPlayer.h"
#include "system/Script.h"
#include "sound/SoundEntityData.h"
#include "sound/SoundHandler.h"
#include <algorithm>

namespace {
cVector3f Position(const luxfx::Effect& e) {return cVector3f(e.matrix[3],e.matrix[7],e.matrix[11]);}
cMatrixf Matrix(const luxfx::Effect& e) {
    cMatrixf matrix=cMatrixf::Identity;
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<4;++c) matrix.m[r][c]=e.matrix[r*4+c];
    return matrix;
}
void SetMatrix(luxfx::Effect& e,const cMatrixf& matrix) {
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<4;++c) e.matrix[r*4+c]=matrix.m[r][c];
}
bool SoundExists(cWorld* world,cSoundEntity* sound,int creation) {
    if(!world || !sound) return false;
    auto it=world->GetSoundEntityIterator();
    while(it.HasNext()) {auto* live=it.Next();if(live==sound) return live->GetCreationID()==creation;}
    return false;
}
bool ParticleExists(cWorld* world,cParticleSystem* particle,uint64_t creation) {
    if(!world || !particle) return false;
    auto it=world->GetParticleSystemIterator();while(it.HasNext()) {auto* live=it.Next();if(live==particle) return live->GetCreationID()==creation;}
    return false;
}
}

cLuxMultiplayerEffects::cLuxMultiplayerEffects(cLuxMultiplayer* session):mpSession(session) {}
cLuxMultiplayerEffects::~cLuxMultiplayerEffects() {Reset();}
uint32_t cLuxMultiplayerEffects::NextID() {if(!++mlNextID) ++mlNextID;return mlNextID;}
bool cLuxMultiplayerEffects::IgnoreCapture() const {
    return !mpWorld || !mpSession->IsActive() || mbApplying || hpl::IsScriptExecuting() ||
        mpSession->IsApplyingScriptEffect() || mpWorld->GetEffectLocalPresentation();
}
void cLuxMultiplayerEffects::OnMapLoaded(cLuxMap* map) {
    Reset();mpWorld=map?map->GetWorld():NULL;
    // Attach after authored map creation. Its ambient objects already exist on
    // every peer and must not be instantiated a second time by this stream.
    if(mpWorld) mpWorld->SetEffectCallback(this);
}
void cLuxMultiplayerEffects::Reset() {
    if(mpWorld && gpBase->mpEngine->GetScene()->WorldExists(mpWorld)) {
        if(mpWorld->GetEffectCallback()==this) mpWorld->SetEffectCallback(NULL);
        for(auto& entry:mRemote) RemoveRemote(entry.second);
        std::set<cSoundEntity*> sounds;auto si=mpWorld->GetSoundEntityIterator();while(si.HasNext()) sounds.insert(si.Next());
        std::set<cParticleSystem*> particles;auto pi=mpWorld->GetParticleSystemIterator();while(pi.HasNext()) particles.insert(pi.Next());
        for(auto& entry:mLocal) {
            if(entry.second.sound && sounds.count(entry.second.sound) && uint64_t(entry.second.sound->GetCreationID())==entry.second.creation) entry.second.sound->SetPresentationSuppressed(false);
            if(entry.second.particle && particles.count(entry.second.particle) && entry.second.particle->GetCreationID()==entry.second.creation) entry.second.particle->SetPresentationSuppressed(false);
        }
    }
    mLocal.clear();mRemote.clear();mBudgets.clear();mLastCreated.clear();mPlayerSounds.clear();mRemoved.clear();mpWorld=NULL;mlNextID=0;mbSendFailed=false;
}
void cLuxMultiplayerEffects::SetOrigin(luxfx::Effect& effect,iPhysicsBody* body) {
    effect.epoch=mpSession->GetMapEpoch();effect.peer=mpSession->GetLocalPeerId();
    if(body && body->IsCharacter() && gpBase->mpPlayer && body->GetCharacterBody()==gpBase->mpPlayer->GetCharacterBody())
        effect.origin=luxfx::Player;
    else effect.origin=luxfx::World;
}
bool cLuxMultiplayerEffects::Owns(const luxfx::Effect& effect) const {
    return effect.origin==luxfx::Player || mpSession->IsHost();
}
void cLuxMultiplayerEffects::OnSoundCreated(cWorld* world,cSoundEntity* sound,iPhysicsBody* body) {
    if(world!=mpWorld || IgnoreCapture() || !sound) return;
    if(body && (!body->IsCharacter() || body->GetCharacterBody()!=gpBase->mpPlayer->GetCharacterBody())) return;
    if(mLocal.size()>=4096) return;
    auto old=mLocal.find(sound);if(old!=mLocal.end()) {if(old->second.published) {auto removed=old->second.effect;removed.operation=luxfx::Remove;mRemoved.push_back(removed);}mLocal.erase(old);}
    Local local;local.sound=sound;local.creation=uint64_t(sound->GetCreationID());local.effect.kind=luxfx::Sound;SetOrigin(local.effect,body);
    local.effect.asset=sound->GetData()->GetName();local.effect.name=sound->GetName();
    // Network transmission and asset validation wait until the native caller
    // has finalized the object; callbacks never send from a physics worker.
    const bool own=Owns(local.effect);
    sound->SetPresentationSuppressed(!own);mLocal[sound]=local;
}
void cLuxMultiplayerEffects::OnParticleCreated(cWorld* world,cParticleSystem* particle,const tString& asset,
    const cVector3f& size,iPhysicsBody* body) {
    if(world!=mpWorld || IgnoreCapture() || !particle) return;
    if(body && (!body->IsCharacter() || body->GetCharacterBody()!=gpBase->mpPlayer->GetCharacterBody())) return;
    if(mLocal.size()>=4096) return;
    auto old=mLocal.find(particle);if(old!=mLocal.end()) {if(old->second.published) {auto removed=old->second.effect;removed.operation=luxfx::Remove;mRemoved.push_back(removed);}mLocal.erase(old);}
    Local local;local.particle=particle;local.creation=particle->GetCreationID();local.effect.kind=luxfx::Particle;SetOrigin(local.effect,body);
    local.effect.asset=asset;local.effect.name=particle->GetName();
    local.effect.size[0]=size.x;local.effect.size[1]=size.y;local.effect.size[2]=size.z;
    const bool own=Owns(local.effect);
    particle->SetPresentationSuppressed(!own);mLocal[particle]=local;
}
bool cLuxMultiplayerEffects::Capture(Local& local) {
    auto& e=local.effect;e.flags=0;
    if(local.sound) {
        auto* sound=local.sound;
        if(sound->GetForcePlayAsGUISound()) {sound->SetPresentationSuppressed(false);return false;}
        SetMatrix(e,sound->GetWorldMatrix());
        if(sound->IsActive()) e.flags|=luxfx::Active;
        auto* start=sound->GetSoundEntry(eSoundEntityType_Start,true);
        auto* end=sound->GetSoundEntry(eSoundEntityType_Stop,true);
        if(sound->IsStopped() || end) e.flags|=luxfx::Stopped;
        if(start && !end && !sound->IsStopped()) e.flags|=luxfx::SoundStart;
        if(end) e.flags|=luxfx::SoundEnd;
        if(sound->IsFadingOut()) e.flags|=luxfx::Dying;
        if(sound->GetData()->GetLoop()) e.flags|=luxfx::Loop;
        if(sound->GetRemoveWhenOver()) e.flags|=luxfx::AutoRemove;
        auto* entry=sound->GetSoundEntry(eSoundEntityType_Main,true);
        if(!entry) entry=end?end:start;
        e.volume=entry?entry->GetDefaultVolume()*entry->GetVolumeMul():sound->GetVolume();
        e.minimum=sound->GetMinDistance();e.maximum=sound->GetMaxDistance();
    } else {
        auto* particle=local.particle;SetMatrix(e,particle->GetWorldMatrix());
        if(particle->IsActive()) e.flags|=luxfx::Active;
        if(particle->IsVisible()) e.flags|=luxfx::Visible;
        if(particle->IsDying()) e.flags|=luxfx::Dying;
        if(particle->GetRemoveWhenDead()) e.flags|=luxfx::AutoRemove;
        const auto color=particle->GetColor();e.color[0]=color.r;e.color[1]=color.g;e.color[2]=color.b;e.color[3]=color.a;
    }
    return true;
}
void cLuxMultiplayerEffects::Send(const luxfx::Effect& effect,uint32_t except) {
    auto packet=luxfx::Packet(effect);
    if(mpSession->IsHost()) {
        for(auto& peer:mpSession->mPeers) if(peer.second.ready && peer.first!=except && !mpSession->Send(peer.first,packet,true))
            peer.second.reliableSendFailed=true;
    } else if(!mpSession->Send(0,packet,true)) mbSendFailed=true;
}
void cLuxMultiplayerEffects::RemoveLocal(Local& local) {
    if(local.published) {auto removed=local.effect;removed.operation=luxfx::Remove;Send(removed);local.published=false;}
}
void cLuxMultiplayerEffects::EmitPlayerSound(const tString& asset,float volume,float minimum,float maximum) {
    if(IgnoreCapture() || !mpSession->IsReady() || !gpBase->mpPlayer || !gpBase->mpPlayer->GetCharacterBody() || mPlayerSounds.size()>=32) return;
    luxfx::Effect e;e.epoch=mpSession->GetMapEpoch();e.peer=mpSession->GetLocalPeerId();
    e.kind=luxfx::PlayerSound;e.origin=luxfx::Player;e.asset=asset;e.name="Player";
    e.volume=volume;e.minimum=minimum;e.maximum=maximum;
    SetMatrix(e,cMath::MatrixTranslate(gpBase->mpPlayer->GetCharacterBody()->GetFeetPosition()+cVector3f(0,0.1f,0)));
    mPlayerSounds.push_back(e);
}
void cLuxMultiplayerEffects::PostUpdate(float dt) {
    if(!mpWorld || !mpSession->IsReady()) return;
    for(const auto& removed:mRemoved) Send(removed);mRemoved.clear();
    for(auto& budget:mBudgets) if((budget.second.elapsed+=dt)>=1) budget.second=Budget();
    std::set<cSoundEntity*> sounds;auto si=mpWorld->GetSoundEntityIterator();while(si.HasNext()) sounds.insert(si.Next());
    std::set<cParticleSystem*> particles;auto pi=mpWorld->GetParticleSystemIterator();while(pi.HasNext()) particles.insert(pi.Next());
    for(auto it=mLocal.begin();it!=mLocal.end();) {
        Local& local=it->second;
        if((local.sound && (!sounds.count(local.sound) || uint64_t(local.sound->GetCreationID())!=local.creation)) ||
           (local.particle && (!particles.count(local.particle) || local.particle->GetCreationID()!=local.creation)) || !Capture(local)) {
            RemoveLocal(local);it=mLocal.erase(it);continue;
        }
        const bool own=Owns(local.effect);
        if(local.sound) local.sound->SetPresentationSuppressed(!own);
        else local.particle->SetPresentationSuppressed(!own);
        if(!own) {RemoveLocal(local);++it;continue;}
        if(!local.published) {
            // Creation may succeed even when an optional sample/material in
            // its definition is unavailable locally. Apply the same resource
            // rules at the origin before claiming peers must possess it.
            if(!local.resourcesChecked) {
                tString error;
                local.resourcesValid=LuxValidateMultiplayerAsset(local.effect.asset,
                    local.effect.kind==luxfx::Sound?"snt":"ps",error);
                local.resourcesChecked=true;
            }
            if(!local.resourcesValid) {++it;continue;}
            // An effect destroyed before its first finalized sample has no
            // presentation to replay. Late join likewise receives live objects.
            if((local.effect.flags&luxfx::Stopped) || (local.particle && local.particle->IsDead())) {++it;continue;}
            local.effect.id=NextID();local.effect.operation=luxfx::Create;
            auto bytes=luxfx::Packet(local.effect);luxnet::Reader reader(bytes);luxfx::Effect valid;
            if(!luxfx::Read(reader,valid)) {++it;continue;}
            Send(local.effect);local.published=true;local.elapsed=0;
            local.effect.operation=luxfx::State;local.last=luxfx::Packet(local.effect);
        } else local.elapsed+=dt;
        local.effect.operation=luxfx::State;
        auto bytes=luxfx::Packet(local.effect);
        if(local.elapsed>=0.1f && bytes!=local.last) {Send(local.effect);local.elapsed=0;local.last=bytes;}
        ++it;
    }
    for(auto& effect:mPlayerSounds) {
        effect.id=NextID();
        auto bytes=luxfx::Packet(effect);luxnet::Reader reader(bytes);luxfx::Effect valid;
        if(luxfx::Read(reader,valid)) Send(effect);
    }
    mPlayerSounds.clear();
    for(auto it=mRemote.begin();it!=mRemote.end();) {
        Remote& remote=it->second;
        if(remote.sound && (!sounds.count(remote.sound) || remote.sound->GetCreationID()!=remote.soundCreation)) remote.sound=NULL;
        if(remote.particle && (!particles.count(remote.particle) || remote.particle->GetCreationID()!=remote.particleCreation)) remote.particle=NULL;
        ++it; // Keep the bounded handle until its owner's Remove arrives.
    }
    // Stop clears these containers, so defer it until all iteration is done.
    if(mbSendFailed) mpSession->Stop("Could not send a multiplayer world effect.");
}
bool cLuxMultiplayerEffects::ValidateOrigin(uint32_t peer,const luxfx::Effect& effect) const {
    const auto& players=mpSession->GetWorld()->GetRemotePlayers();auto player=players.find(peer);
    if(player==players.end() || player->second.age>2) return false;
    return effect.origin==luxfx::Player && cMath::Vector3Dist(Position(effect),player->second.position)<=4;
}
bool cLuxMultiplayerEffects::Apply(Remote& remote,bool create,tString& error) {
    auto& e=remote.effect;
    struct Applying {bool& flag;Applying(bool& f):flag(f) {flag=true;}~Applying(){flag=false;}} applying(mbApplying);
    if(create) {
        const tString type=e.kind==luxfx::Sound?"snt":e.kind==luxfx::Particle?"ps":"audio";
        if(!LuxValidateMultiplayerAsset(e.asset,type,error)) return false;
        if(e.kind==luxfx::PlayerSound) {
            mpWorld->GetSound()->GetSoundHandler()->Play3D(e.asset,false,e.volume,Position(e),e.minimum,e.maximum,eSoundEntryType_World,false,0,false);
            return true;
        }
        const tString name="MultiplayerEffect_"+cString::ToString((int)e.peer)+"_"+cString::ToString((int)e.id);
        if(e.kind==luxfx::Sound) {
            // The origin owns this handle's lifetime. Keeping the receiver's
            // stopped object lets a dormant baseline or later Play reuse it.
            remote.sound=mpWorld->CreateSoundEntity(name,e.asset,false);
            if(!remote.sound) {error="Could not create multiplayer sound: "+e.asset;return false;}
            remote.soundCreation=remote.sound->GetCreationID();
            if(e.operation==luxfx::DeclareSound) {
                remote.sound->Stop(false);
                remote.skipCurrentPlayback=(e.flags&luxfx::Stopped)==0;
            }
        } else {
            remote.particle=mpWorld->CreateParticleSystem(name,e.asset,cVector3f(e.size[0],e.size[1],e.size[2]),(e.flags&luxfx::AutoRemove)!=0);
            if(!remote.particle) {error="Could not create multiplayer particle system: "+e.asset;return false;}
            remote.particleCreation=remote.particle->GetCreationID();
        }
    }
    if(remote.sound && SoundExists(mpWorld,remote.sound,remote.soundCreation)) {
        auto* sound=remote.sound;sound->SetMatrix(Matrix(e));sound->SetActive((e.flags&luxfx::Active)!=0);
        sound->SetVolume(e.volume);sound->SetMinDistance(e.minimum);sound->SetMaxDistance(e.maximum);
        if(e.flags&luxfx::Stopped) remote.skipCurrentPlayback=false;
        if(e.flags&luxfx::Stopped) {
            if(!remote.applied || !(remote.appliedFlags&luxfx::Stopped)) sound->Stop(e.operation!=luxfx::DeclareSound && (e.flags&luxfx::SoundEnd)!=0);
        } else if(!remote.skipCurrentPlayback && ((create && (e.flags&luxfx::SoundStart)) || (remote.applied && (remote.appliedFlags&luxfx::Stopped))))
            sound->Play((e.flags&luxfx::SoundStart)!=0);
        if(auto* entry=sound->GetSoundEntry(eSoundEntityType_Main,true)) {entry->SetDefaultVolume(e.volume);entry->SetVolumeMul(1);}
    }
    if(remote.particle && ParticleExists(mpWorld,remote.particle,remote.particleCreation)) {
        auto* particle=remote.particle;particle->SetMatrix(Matrix(e));particle->SetActive((e.flags&luxfx::Active)!=0);
        particle->SetVisible((e.flags&luxfx::Visible)!=0);particle->SetColor(cColor(e.color[0],e.color[1],e.color[2],e.color[3]));
        if((e.flags&luxfx::Dying) && !particle->IsDying()) particle->Kill();
    }
    remote.applied=true;remote.appliedFlags=e.flags;
    return true;
}
void cLuxMultiplayerEffects::RemoveRemote(Remote& remote) {
    if(SoundExists(mpWorld,remote.sound,remote.soundCreation)) mpWorld->DestroySoundEntity(remote.sound);
    if(ParticleExists(mpWorld,remote.particle,remote.particleCreation)) mpWorld->DestroyParticleSystem(remote.particle);
    remote.sound=NULL;remote.particle=NULL;
}
bool cLuxMultiplayerEffects::HandleMessage(uint32_t peer,const std::vector<uint8_t>& bytes,tString& error) {
    if(bytes.size()>2048 || !mpWorld) return false;
    luxnet::Reader reader(bytes);luxfx::Effect effect;if(!luxfx::Read(reader,effect)) return false;
    if(effect.epoch!=mpSession->GetMapEpoch()) return true;
    if(mpSession->IsHost()) {
        if(effect.peer!=peer || !peer || effect.operation==luxfx::DeclareSound) return false;
        Budget& budget=mBudgets[peer];budget.bytes+=unsigned(bytes.size());++budget.packets;
        if(budget.bytes>256*1024 || budget.packets>512 || (effect.operation==luxfx::Create && ++budget.creates>64)) return false;
    } else if(effect.peer==mpSession->GetLocalPeerId()) return true;
    const Key key(effect.peer,effect.id);auto found=mRemote.find(key);
    if(effect.operation==luxfx::Create || effect.operation==luxfx::DeclareSound) {
        if(found!=mRemote.end()) return false;
        if(mpSession->IsHost()) {
            if(effect.id<=mLastCreated[peer]) return false;
            mLastCreated[peer]=effect.id;
            if(!ValidateOrigin(peer,effect)) return true; // The player may have changed location during transit.
        }
        unsigned count=0;for(const auto& entry:mRemote) if(entry.first.first==effect.peer) ++count;
        // The host's world stream has the same bound as its local capture;
        // player-authored streams have a tighter per-player allowance.
        if(count>=(effect.peer==0?4096u:256u) || mRemote.size()>=8192) return false;
        Remote remote;remote.effect=effect;remote.initialPosition=Position(effect);if(!Apply(remote,true,error)) return false;
        if(effect.kind!=luxfx::PlayerSound) mRemote[key]=remote;
    } else {
        // Gracefully ignore a late update/removal for a rejected old position.
        if(found==mRemote.end()) return true;
        if(found->second.effect.kind!=effect.kind || found->second.effect.origin!=effect.origin) return false;
        if(effect.operation==luxfx::Remove) {RemoveRemote(found->second);mRemote.erase(found);}
        else {
            // Fixed particles can outlive their player's presence; moving
            // effects must remain near that player or their original emission.
            if(mpSession->IsHost() && cMath::Vector3Dist(Position(effect),found->second.initialPosition)>0.5f &&
               !ValidateOrigin(peer,effect)) return true;
            effect.asset=found->second.effect.asset;effect.name=found->second.effect.name;
            found->second.effect=effect;if(!Apply(found->second,false,error)) return false;
        }
    }
    if(mpSession->IsHost()) Send(effect,peer);
    return true;
}
void cLuxMultiplayerEffects::OnPeerDisconnected(uint32_t peer) {
    for(auto it=mRemote.begin();it!=mRemote.end();) {
        if(it->first.first!=peer) {++it;continue;}
        if(mpSession->IsHost()) {auto effect=it->second.effect;effect.operation=luxfx::Remove;Send(effect,peer);}
        RemoveRemote(it->second);it=mRemote.erase(it);
    }
    mBudgets.erase(peer);mLastCreated.erase(peer);
}
bool cLuxMultiplayerEffects::SendInitialState(uint32_t peer) {
    auto prepare=[](luxfx::Effect& effect) {
        effect.operation=luxfx::Create;
        if(effect.kind!=luxfx::Sound) return true;
        // Active loops are current world state. Finished/ongoing reusable
        // one-shots need their handle, without replaying the old occurrence.
        if(!(effect.flags&luxfx::Loop) && (effect.flags&luxfx::AutoRemove)) return false;
        if((effect.flags&luxfx::Stopped) || !(effect.flags&luxfx::Loop)) effect.operation=luxfx::DeclareSound;
        return true;
    };
    for(const auto& entry:mLocal) if(entry.second.published) {
        auto effect=entry.second.effect;
        if(!prepare(effect)) continue;
        if(!mpSession->Send(peer,luxfx::Packet(effect),true)) return false;
    }
    for(const auto& entry:mRemote) {
        auto effect=entry.second.effect;if(effect.peer==peer) continue;
        if((effect.kind==luxfx::Sound && !entry.second.sound) || (effect.kind==luxfx::Particle && !entry.second.particle) || !prepare(effect)) continue;
        if(!mpSession->Send(peer,luxfx::Packet(effect),true)) return false;
    }
    return true;
}
