#include "LuxMultiplayerEntities.h"
#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerEntityProtocol.h"
#include "LuxMultiplayer.h"
#include "LuxMultiplayerWorld.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxJournal.h"
#include "LuxProp.h"
#include "LuxProp_Item.h"
#include "LuxProp_Lamp.h"
#include "LuxProp_SwingDoor.h"
#include "physics/PhysicsJointHinge.h"
#include "physics/PhysicsJointSlider.h"

using namespace luxnet;

cLuxMultiplayerEntities::cLuxMultiplayerEntities(cLuxMultiplayer* session):mpSession(session) { Reset(); }
void cLuxMultiplayerEntities::Reset() {
    mClaims.clear();mLastStates.clear();mRemovedItems.clear();mInitial.clear();mJointBreakRequests.clear();
    mPendingDiaries.clear();mpDiaryDecision=NULL;
    msPending.clear();msGranted.clear();mlToken=mlGrantedToken=0;
    mlPendingRuntimeID=0;
    mfSnapshotTime=mfPendingTime=mfGroundTruthTime=0;mbCallback=false;mlDiaryIndex=-1;
}
bool cLuxMultiplayerEntities::SeedCurrentMapItems(const std::vector<uint8_t>& mapBytes,std::string& error) {
    std::vector<tString> items;
    if(!LuxCollectMultiplayerMapItems(mapBytes,items,error)) return false;
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
    if(!map) {error="The current map is no longer loaded.";return false;}
    for(const tString& name:items) {
        iLuxEntity* entity=map->GetEntityByName(name,eLuxEntityType_LastEnum);
        if(!entity || entity->GetDestroyMe()) mRemovedItems[name]=entity?entity->GetRuntimeID():0;
    }
    return true;
}
bool cLuxMultiplayerEntities::AllowPhysicsJointBreak(iLuxProp* prop,iPhysicsJoint* joint) {
    if(!mpSession->IsActive()) return true;
    // Explicit host script/native Break() remains authoritative even while a
    // client owns the physical simulation of the connected assembly.
    if(mpSession->IsHost() && joint->IsBroken()) return true;
    iPhysicsBody* body=joint->GetChildBody();
    if(!body || body->GetMass()<=0 || body->GetUserData()!=prop) body=joint->GetParentBody();
    if(!body || body->GetMass()<=0 || body->GetUserData()!=prop) return mpSession->IsHost();
    uint32_t owner=0,token=0;
    mpSession->GetWorld()->GetSimulationLease(body,owner,token);
    if(mpSession->IsHost()) return owner==mpSession->GetLocalPeerId();
    if(!mpSession->IsReady() || !token || owner!=mpSession->GetLocalPeerId()) return false;
    for(uint32_t slot=0;slot<prop->mvJoints.size() && slot<128;++slot) if(prop->mvJoints[slot]==joint) {
        const auto key=std::make_pair(prop->GetRuntimeID(),slot);
        const auto previous=mJointBreakRequests.find(key);
        if(previous!=mJointBreakRequests.end() && previous->second==token) return false;
        if(previous==mJointBreakRequests.end() && mJointBreakRequests.size()>=8192) return false;
        JointBreakState request;request.epoch=mpSession->GetMapEpoch();request.name=prop->GetName();
        request.index=slot;request.body=LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID());request.token=token;
        if(mpSession->Send(0,WriteJointBreak(request),true)) mJointBreakRequests[key]=token;
        break;
    }
    // Keep the constraint alive until the host's reliable deletion arrives.
    return false;
}
bool cLuxMultiplayerEntities::IsNative(iLuxEntity* entity) const {
    if(!entity || entity->GetEntityType()!=eLuxEntityType_Prop) return false;
    eLuxPropType type=static_cast<iLuxProp*>(entity)->GetPropType();
    return type==eLuxPropType_Item || type==eLuxPropType_Lamp;
}
bool cLuxMultiplayerEntities::Eligible(iLuxEntity* entity) {
    if(!IsNative(entity) || !entity->IsActive() || entity->GetDestroyMe() || entity->GetInteractionDisabled() ||
       entity->GetBodyNum()==0 || !entity->CanInteract(entity->GetBody(0))) return false;
    auto removed=mRemovedItems.find(entity->GetName());
    if(removed!=mRemovedItems.end()) {
        if(removed->second==entity->GetRuntimeID()) return false;
        mRemovedItems.erase(removed); // A scripted replacement is a new world item.
    }
    return true;
}
bool cLuxMultiplayerEntities::DeferCallback(iLuxEntity* entity) const {
    return mpSession->IsActive() && IsNative(entity) && !mbCallback;
}
bool cLuxMultiplayerEntities::BeginInteraction(iLuxEntity* entity) {
    if(!mpSession->IsActive()) return true;
    if(!mpSession->IsReady() || !Eligible(entity)) return false;
    const tString& name=entity->GetName();
    if(mpSession->IsHost()) {
        if(mClaims.count(name)) return false;
        if(static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Lamp) {
            auto* lamp=static_cast<cLuxProp_Lamp*>(entity);
            if(lamp->GetLit() || !lamp->CanBeIgnitByPlayer()) {Commit(entity,false,true);return false;}
        }
        return true;
    }
    if(msGranted==name) return true;
    if(!msPending.empty() || mPendingDiaries.size()>=8) return false;
    Writer w(NativeRequest);w.U32(mpSession->GetMapEpoch());w.String(name);
    if(mpSession->Send(0,w.data,true)) {msPending=name;mlPendingRuntimeID=entity->GetRuntimeID();mfPendingTime=0;}
    return false;
}
void cLuxMultiplayerEntities::CompleteInteraction(iLuxEntity* entity,bool succeeded) {
    if(!mpSession->IsActive() || !IsNative(entity)) return;
    if(mpSession->IsHost()) {if(succeeded) Commit(entity,false);return;}
    if(msGranted!=entity->GetName()) return;
    Writer w(NativeResult);w.U32(mpSession->GetMapEpoch());w.String(msGranted);w.U32(mlGrantedToken);w.U8(succeeded);w.U32(mlDiaryIndex+1);
    if(!mpSession->Send(0,w.data,true)) mpSession->RejectPeer(0,"Could not confirm the item or lamp interaction with the host.");
}
void cLuxMultiplayerEntities::RecordDiaryIndex(const std::string& name,int index) {
    if(mpSession->IsClient() && msGranted==name && index>=0 && index<4096) mlDiaryIndex=index;
}
bool cLuxMultiplayerEntities::DeferDiaryPresentation(const std::string& name,cLuxDiary* diary) {
    if(!mpSession->IsClient() || msGranted!=name || !mlGrantedToken || mlDiaryIndex<0 || !diary) return false;
    mPendingDiaries[mlGrantedToken]={name,diary,0};
    return true;
}
void cLuxMultiplayerEntities::RecordDiaryDecision(bool open) {
    if(mpDiaryDecision) *mpDiaryDecision=open;
}
bool cLuxMultiplayerEntities::Commit(iLuxEntity* entity,bool remote,bool callbackOnly,int diaryIndex) {
    const bool item=static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Item;
    bool openDiary=true;
    // A callback's return decision belongs to this pickup, not to whichever
    // player's ReturnOpenJournal effect happened to arrive last on the client.
    struct DecisionScope {
        bool*& slot;bool* previous;
        DecisionScope(bool*& target,bool* value):slot(target),previous(target) {slot=value;}
        ~DecisionScope() {slot=previous;}
    } decision(mpDiaryDecision,remote && item && diaryIndex>=0?&openDiary:NULL);
    if(item) {
        mRemovedItems[entity->GetName()]=entity->GetRuntimeID();
        entity->GetMap()->DestroyEntity(entity);
        Writer w(ItemRemoved);w.U32(mpSession->GetMapEpoch());w.String(entity->GetName());
        mpSession->Broadcast(w.data,true);
    } else if(!callbackOnly) static_cast<cLuxProp_Lamp*>(entity)->SetLit(true,true);
    // Run native callbacks exactly once, in the host VM, attributed to the collector.
    // The collector alone receives the item / pays the tinderbox cost.
    if(!remote || mpSession->GetSettings().allPlayersTriggerScripts) {
        cLuxMultiplayerRemoteTriggerScope trigger(remote);
        mbCallback=true;
        if(remote && item && diaryIndex>=0) {
            auto* pickup=static_cast<cLuxProp_Item*>(entity);
            if(pickup->GetItemType()==eLuxItemType_Diary && !pickup->GetExtraStringVal().empty())
                entity->GetMap()->RunScript(pickup->GetExtraStringVal()+"(\""+entity->GetName()+"\","+cString::ToString(diaryIndex)+")");
        }
        if(!callbackOnly) entity->RunCallbackFunc(item?"OnPickup":"OnIgnite");
        entity->RunInteractCallbackFunc();
        mbCallback=false;
    }
    if(!item) BroadcastState(entity);
    return openDiary;
}
void cLuxMultiplayerEntities::OnPeerDisconnected(uint32_t peer) {
    mInitial.erase(peer);
    for(auto it=mClaims.begin();it!=mClaims.end();) {
        if(it->second.peer==peer) it=mClaims.erase(it);else ++it;
    }
}
std::vector<uint8_t> cLuxMultiplayerEntities::Capture(iLuxProp* prop) {
    Writer w(EntityState);w.U32(mpSession->GetMapEpoch());w.String(prop->GetName());
    const uint8_t kind=prop->GetPropType()==eLuxPropType_Lamp?LampState:
        prop->GetPropType()==eLuxPropType_SwingDoor?DoorState:PropState;
    w.U8(kind);
    w.U8((prop->IsActive()?EntityActive:0) | (prop->GetInteractionDisabled()?InteractionDisabled:0) |
         (prop->mbEffectsActive?EffectsActive:0) | (prop->mbStaticPhysics?StaticPhysics:0));
    uint8_t detail=0;
    if(kind==LampState) detail=static_cast<cLuxProp_Lamp*>(prop)->GetLit()?1:0;
    if(kind==DoorState) {
        cLuxProp_SwingDoor* door=static_cast<cLuxProp_SwingDoor*>(prop);
        detail=(door->GetClosed()?1:0)|(door->GetLocked()?2:0)|(door->GetDisableAutoClose()?4:0);
    }
    w.U8(detail);
    std::vector<JointState> joints;
    for(size_t i=0;i<prop->mvJoints.size() && i<128;++i) {
        iPhysicsJoint* joint=prop->mvJoints[i];
        JointState j;j.index=static_cast<uint32_t>(i);
        if(!joint || joint->IsBroken()) {
            j.kind=0;j.flags=0;j.min=0;j.max=0;joints.push_back(j);continue;
        }
        j.flags=(joint->GetStickyMinLimit()?1:0)|(joint->GetStickyMaxLimit()?2:0)|(joint->GetCollideBodies()?4:0);
        if(joint->GetType()==ePhysicsJointType_Hinge) {
            auto* hinge=static_cast<iPhysicsJointHinge*>(joint);j.kind=1;j.min=hinge->GetMinAngle();j.max=hinge->GetMaxAngle();
        } else if(joint->GetType()==ePhysicsJointType_Slider) {
            auto* slider=static_cast<iPhysicsJointSlider*>(joint);j.kind=2;j.min=slider->GetMinDistance();j.max=slider->GetMaxDistance();
        } else continue;
        joints.push_back(j);
    }
    w.U32(static_cast<uint32_t>(joints.size()));
    for(const auto& j:joints) {w.U32(j.index);w.U8(j.kind);w.U8(j.flags);w.Float(j.min);w.Float(j.max);}
    return w.data;
}
bool cLuxMultiplayerEntities::Apply(const std::vector<uint8_t>& data) {
    Reader r(data);NativeState state;
    if(!ReadNativeState(r,state)) return false;
    if(state.epoch!=mpSession->GetMapEpoch()) return true;
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
    iLuxEntity* entity=map?map->GetEntityByName(state.name):NULL;
    if(!entity || entity->GetDestroyMe()) return true;
    auto removed=mRemovedItems.find(state.name);
    if(removed!=mRemovedItems.end()) {
        if(removed->second==entity->GetRuntimeID()) return true;
        mRemovedItems.erase(removed);
    }
    if(entity->GetEntityType()!=eLuxEntityType_Prop) return false;
    iLuxProp* prop=static_cast<iLuxProp*>(entity);
    if((state.kind==LampState && prop->GetPropType()!=eLuxPropType_Lamp) ||
       (state.kind==DoorState && prop->GetPropType()!=eLuxPropType_SwingDoor)) return false;
    // Validate all referenced constraints before changing any entity state.
    for(const auto& j:state.joints) {
        if(j.index>=prop->mvJoints.size()) return false;
        auto* joint=prop->mvJoints[j.index];
        // A local break can precede the host's matching deletion. Never revive
        // that slot, or reject a preceding live snapshot for the missing joint.
        if(joint && ((j.kind==1 && joint->GetType()!=ePhysicsJointType_Hinge) ||
           (j.kind==2 && joint->GetType()!=ePhysicsJointType_Slider))) return false;
    }
    for(const auto& j:state.joints) {
        if(j.kind==0) mJointBreakRequests.erase(std::make_pair(prop->GetRuntimeID(),j.index));
        if(j.kind==0 && prop->mvJoints[j.index]) map->GetPhysicsWorld()->DestroyJoint(prop->mvJoints[j.index]);
    }
    prop->SetActive((state.flags&EntityActive)!=0);
    prop->SetInteractionDisabled((state.flags&InteractionDisabled)!=0);
    if(prop->mbStaticPhysics!=((state.flags&StaticPhysics)!=0)) prop->SetStaticPhysics((state.flags&StaticPhysics)!=0);
    if(state.kind==LampState) static_cast<cLuxProp_Lamp*>(prop)->SetLit(state.detail!=0,true);
    // Lit and effects can differ after an authored fade or saved-state restore.
    // SetLit also does nothing when the lit bit already matches the snapshot.
    if(prop->mbEffectsActive!=((state.flags&EffectsActive)!=0)) prop->SetEffectsActive((state.flags&EffectsActive)!=0,true);
    if(state.kind==DoorState) {
        auto* door=static_cast<cLuxProp_SwingDoor*>(prop);
        door->SetLocked((state.detail&2)!=0,false);door->SetClosed((state.detail&1)!=0,false);
        door->SetDisableAutoClose((state.detail&4)!=0);
    }
    for(const auto& j:state.joints) {
        auto* joint=prop->mvJoints[j.index];
        if(!joint || j.kind==0) continue;
        if(j.kind==1) {
            auto* hinge=static_cast<iPhysicsJointHinge*>(joint);hinge->SetMinAngle(j.min);hinge->SetMaxAngle(j.max);
        } else {
            auto* slider=static_cast<iPhysicsJointSlider*>(joint);slider->SetMinDistance(j.min);slider->SetMaxDistance(j.max);
        }
        joint->SetStickyMinLimit((j.flags&1)!=0);joint->SetStickyMaxLimit((j.flags&2)!=0);joint->SetCollideBodies((j.flags&4)!=0);
    }
    return true;
}
void cLuxMultiplayerEntities::BroadcastState(iLuxEntity* entity) {
    if(entity->GetDestroyMe() || entity->GetEntityType()!=eLuxEntityType_Prop) return;
    auto bytes=Capture(static_cast<iLuxProp*>(entity));
    mLastStates[entity->GetName()]=bytes;mpSession->Broadcast(bytes,true);
}
bool cLuxMultiplayerEntities::SendInitialState(uint32_t peer) {
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return false;
    auto& queue=mInitial[peer];queue.clear();
    for(const auto& removed:mRemovedItems) queue.push_back(removed.first);
    auto it=map->GetEntityIterator();
    while(it.HasNext()) {
        auto* entity=it.Next();
        if(entity->GetEntityType()==eLuxEntityType_Prop && !entity->GetDestroyMe() && !mRemovedItems.count(entity->GetName()))
            queue.push_back(entity->GetName());
        if(queue.size()>8192) {mInitial.erase(peer);return false;}
    }
    return true;
}
void cLuxMultiplayerEntities::Update(float dt) {
    if(mpSession->IsClient()) {
        if(!msPending.empty() && (mfPendingTime+=dt)>20) {mpSession->RejectPeer(0,"The host did not respond to an item or lamp interaction.");return;}
        for(auto& pending:mPendingDiaries) if((pending.second.age+=dt)>20) {
            mpSession->RejectPeer(0,"The host did not confirm the diary pickup.");return;
        }
        return;
    }
    std::set<uint32_t> expired;
    for(auto& claim:mClaims) if((claim.second.age+=dt)>20) expired.insert(claim.second.peer);
    // Never reassign an unanswered grant while its recipient remains connected.
    for(uint32_t peer:expired) mpSession->RejectPeer(peer,"Timed out confirming an item or lamp interaction.");
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return;
    for(auto it=mRemovedItems.begin();it!=mRemovedItems.end();) {
        auto* entity=map->GetEntityByName(it->first);
        if(entity && !entity->GetDestroyMe() && entity->GetRuntimeID()!=it->second) it=mRemovedItems.erase(it);else ++it;
    }
    for(auto it=mInitial.begin();it!=mInitial.end();) {
        auto& queue=it->second;
        for(unsigned sent=0;sent<32 && !queue.empty();++sent) {
            const auto& name=queue.front();std::vector<uint8_t> bytes;
            if(mRemovedItems.count(name)) {Writer w(ItemRemoved);w.U32(mpSession->GetMapEpoch());w.String(name);bytes=w.data;}
            else {
                auto* entity=map->GetEntityByName(name);
                if(entity && !entity->GetDestroyMe() && entity->GetEntityType()==eLuxEntityType_Prop) bytes=Capture(static_cast<iLuxProp*>(entity));
            }
            if(!bytes.empty() && !mpSession->Send(it->first,bytes,true)) break;
            queue.pop_front();
        }
        if(queue.empty()) it=mInitial.erase(it);else ++it;
    }
    mfGroundTruthTime+=dt;
    mfSnapshotTime+=dt;if(mfSnapshotTime<0.1f) return;mfSnapshotTime=0;
    const bool groundTruth=mfGroundTruthTime>=2;if(groundTruth) mfGroundTruthTime=0;
    auto it=map->GetEntityIterator();
    while(it.HasNext()) {
        auto* entity=it.Next();
        if(entity->GetEntityType()!=eLuxEntityType_Prop || entity->GetDestroyMe()) continue;
        auto bytes=Capture(static_cast<iLuxProp*>(entity));auto& previous=mLastStates[entity->GetName()];
        if(groundTruth || bytes!=previous) {previous=bytes;mpSession->Broadcast(bytes,true);}
    }
}
bool cLuxMultiplayerEntities::HandleMessage(uint32_t peer,const std::vector<uint8_t>& data) {
    if(data.empty()) return false;
    Reader r(data);const uint8_t type=data[0];uint32_t epoch=r.U32();
    if(!r.valid) return false;
    if(epoch!=mpSession->GetMapEpoch()) return true;
    if(type==JointBreakRequest) {
        Reader requestReader(data);JointBreakState request;
        if(!mpSession->IsHost() || !ReadJointBreak(requestReader,request)) return false;
        cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
        auto* prop=map?static_cast<iLuxProp*>(map->GetEntityByName(request.name,eLuxEntityType_Prop)):NULL;
        if(!prop || prop->GetName()!=request.name || prop->GetDestroyMe() || request.index>=prop->mvJoints.size()) return true;
        iPhysicsJoint* joint=prop->mvJoints[request.index];
        if(!joint || !joint->IsBreakable()) return true;
        iPhysicsBody* body=NULL;
        iPhysicsBody* endpoints[]={joint->GetChildBody(),joint->GetParentBody()};
        for(auto* endpoint:endpoints)
            if(endpoint && endpoint->GetUserData()==prop && endpoint->GetMass()>0 &&
               LuxWorldWire::BodyId(endpoint->GetName(),endpoint->GetUniqueID())==request.body) body=endpoint;
        uint32_t owner=0,token=0;
        if(!body || !mpSession->GetWorld()->GetSimulationLease(body,owner,token) || owner!=peer || token!=request.token) return true;
        // Packet handling occurs outside Newton's joint traversal. Preserve the
        // host's native break presentation, then publish the permanent slot deletion.
        joint->Break();
        if(!joint->CheckBreakage()) return true;
        map->GetPhysicsWorld()->DestroyJoint(joint);
        BroadcastState(prop);return true;
    }
    if(mpSession->IsClient() && type==EntityState) return Apply(data);
    tString name=r.String(256);if(!r.valid || name.empty()) return false;
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
    iLuxEntity* entity=map?map->GetEntityByName(name):NULL;
    if(mpSession->IsHost() && type==NativeRequest) {
        if(!r.Done()) return false;
        const auto& players=mpSession->GetWorld()->GetRemotePlayers();auto player=players.find(peer);
        bool allow=Eligible(entity) && entity->GetName()==name && !mClaims.count(name) && player!=players.end() && player->second.age<=2;
        if(allow) {
            allow=false;
            for(int i=0;i<entity->GetBodyNum();++i)
                if(cMath::Vector3Dist(entity->GetBody(i)->GetWorldPosition(),player->second.position)<=4) {allow=true;break;}
        }
        // Each client may hold only one unresolved native interaction.
        for(const auto& claim:mClaims) if(claim.second.peer==peer) allow=false;
        uint32_t token=0;bool callbackOnly=false;
        if(allow) {
            if(static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Lamp) {
                auto* lamp=static_cast<cLuxProp_Lamp*>(entity);callbackOnly=lamp->GetLit() || !lamp->CanBeIgnitByPlayer();
            }
            const bool diary=static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Item &&
                static_cast<cLuxProp_Item*>(entity)->GetItemType()==eLuxItemType_Diary;
            if(!++mlToken) ++mlToken;token=mlToken;mClaims[name]={peer,token,0,entity->GetRuntimeID(),callbackOnly,diary};
        }
        Writer w(NativeGrant);w.U32(epoch);w.String(name);w.U32(token);w.U8(callbackOnly);
        if(!mpSession->Send(peer,w.data,true)) mpSession->RejectPeer(peer,"Could not deliver the item or lamp interaction grant.");
        return true;
    }
    if(mpSession->IsHost() && type==NativeResult) {
        uint32_t token=r.U32();uint8_t success=r.U8();uint32_t diary=r.U32();
        if(!r.Done() || !token || success>1 || diary>4096) return false;
        auto claim=mClaims.find(name);
        if(claim==mClaims.end() || claim->second.peer!=peer || claim->second.token!=token) return false;
        if(diary && !claim->second.diary) return false;
        Claim accepted=claim->second;mClaims.erase(claim);
        const bool committed=success && IsNative(entity) && !entity->GetDestroyMe() && entity->GetRuntimeID()==accepted.runtimeId;
        bool openDiary=true;
        if(committed) openDiary=Commit(entity,true,accepted.callbackOnly,static_cast<int>(diary)-1);
        if(diary && mpSession->IsHost() && epoch==mpSession->GetMapEpoch()) {
            // Reliable ordering places the decision after all synchronous
            // callback effects; only its collector has this pending token.
            Writer response(NativeDiaryResult);response.U32(epoch);response.String(name);response.U32(token);
            response.U8(committed);response.U8(committed && openDiary);
            return mpSession->Send(peer,response.data,true);
        }
        return true;
    }
    if(mpSession->IsClient() && type==NativeGrant) {
        uint32_t token=r.U32();uint8_t callbackOnly=r.U8();if(!r.Done() || callbackOnly>1 || msPending!=name) return false;
        const bool sameInstance=entity && entity->GetRuntimeID()==mlPendingRuntimeID;
        msPending.clear();mlPendingRuntimeID=0;mfPendingTime=0;
        if(!token) return true;
        if(!sameInstance || !Eligible(entity) || gpBase->mpPlayer->GetHealth()<=0 || callbackOnly) {
            Writer w(NativeResult);w.U32(epoch);w.String(name);w.U32(token);
            w.U8(sameInstance && callbackOnly && entity && gpBase->mpPlayer->GetHealth()>0);w.U32(0);
            return mpSession->Send(0,w.data,true);
        }
        msGranted=name;mlGrantedToken=token;mlDiaryIndex=-1;
        entity->OnInteract(entity->GetBody(0),entity->GetBody(0)->GetWorldPosition());
        msGranted.clear();mlGrantedToken=0;
        return true;
    }
    if(mpSession->IsClient() && type==NativeDiaryResult) {
        const uint32_t token=r.U32();const uint8_t accepted=r.U8(),open=r.U8();
        if(!r.Done() || !token || accepted>1 || open>1 || (!accepted && open)) return false;
        auto pending=mPendingDiaries.find(token);
        if(pending==mPendingDiaries.end() || pending->second.name!=name) return false;
        cLuxDiary* diary=pending->second.diary;mPendingDiaries.erase(pending);
        if(accepted) {
            if(open && mpSession->IsReady() && !gpBase->mpPlayer->IsDead() && gpBase->mpPlayer->GetHealth()>0 &&
               gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!="MultiplayerLoading") {
                gpBase->mpEngine->GetUpdater()->SetContainer("Journal");
                gpBase->mpJournal->SetForceInstantExit(true);
                gpBase->mpJournal->OpenDiary(diary,true);
            } else gpBase->mpJournal->SetDiaryAsLastRead(diary);
        }
        return true;
    }
    if(mpSession->IsClient() && type==ItemRemoved) {
        if(!r.Done()) return false;
        mRemovedItems[name]=entity?entity->GetRuntimeID():0;
        if(entity && entity->GetEntityType()==eLuxEntityType_Prop && static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Item)
            map->DestroyEntity(entity);
        return true;
    }
    return false;
}
