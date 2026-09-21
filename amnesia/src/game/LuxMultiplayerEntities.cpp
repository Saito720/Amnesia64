#include "LuxMultiplayerEntities.h"
#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerEntityProtocol.h"
#include "LuxMultiplayerEntityDefinition.h"
#include "LuxMultiplayer.h"
#include "LuxMultiplayerWorld.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxJournal.h"
#include "LuxCompletionCountHandler.h"
#include "LuxProp.h"
#include "LuxProp_Item.h"
#include "LuxProp_Lamp.h"
#include "LuxProp_SwingDoor.h"
#include "LuxProp_Button.h"
#include "LuxProp_Chest.h"
#include "physics/PhysicsJointHinge.h"
#include "physics/PhysicsJointSlider.h"
#include "physics/PhysicsRope.h"
#include "scene/RopeEntity.h"

using namespace luxnet;

cLuxMultiplayerEntities::cLuxMultiplayerEntities(cLuxMultiplayer* session):mpSession(session) { Reset(); }
void cLuxMultiplayerEntities::Reset() {
    mvMapBaseline.clear();mbMapBaselineValid=false;
    msLastError.clear();msMapBaselineError.clear();
    mDefinitionBindings.clear();
    mAnnouncedProps.clear();
    msPendingSyncError.clear();
    mLastRopes.clear();
    mClaims.clear();mLastStates.clear();mRemovedItems.clear();mInitial.clear();mJointBreakRequests.clear();
    mPendingDiaries.clear();mpDiaryDecision=NULL;
    msPending.clear();msGranted.clear();mlToken=mlGrantedToken=0;
    mlPendingEntityID=mlGrantedEntityID=UINT32_MAX;
    mlPendingRuntimeID=0;
    mfSnapshotTime=mfPendingTime=mfGroundTruthTime=0;mbCallback=false;mlDiaryIndex=-1;
}
bool cLuxMultiplayerEntities::Fail(const std::string& reason) {
    auto* map=gpBase->mpMapHandler->GetCurrentMap();
    msLastError="Map '"+(map?map->GetName():tString("<none>"))+"': "+reason;
    Warning("Multiplayer entity synchronization failed: %s\n",msLastError.c_str());
    return false;
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
        JointBreakState request;request.epoch=mpSession->GetMapEpoch();request.name=prop->GetName();request.id=prop->GetID();
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
    return type==eLuxPropType_Item || type==eLuxPropType_Lamp || type==eLuxPropType_Button || type==eLuxPropType_Chest;
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
    // A chest's authored interact callback belongs to opening its question,
    // including declining/cannot-afford cases. Its later purchase only changes
    // the shared lock; preserve the existing generic callback path.
    if(entity && entity->GetEntityType()==eLuxEntityType_Prop &&
       static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Chest) return false;
    return mpSession->IsActive() && IsNative(entity) && !mbCallback;
}
bool cLuxMultiplayerEntities::BeginInteraction(iLuxEntity* entity) {
    if(!mpSession->IsActive()) return true;
    if(!mpSession->IsReady() || !Eligible(entity)) return false;
    const tString& name=entity->GetName();
    if(mpSession->IsHost()) {
        if(mClaims.count(entity->GetID())) return false;
        if(static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Lamp) {
            auto* lamp=static_cast<cLuxProp_Lamp*>(entity);
            if(lamp->GetLit() || !lamp->CanBeIgnitByPlayer()) {Commit(entity,false,true);return false;}
        }
        return true;
    }
    if(msGranted==name && mlGrantedEntityID==static_cast<uint32_t>(entity->GetID())) return true;
    if(!msPending.empty() || mPendingDiaries.size()>=8) return false;
    Writer w(NativeRequest);w.U32(mpSession->GetMapEpoch());w.String(name);w.U32(entity->GetID());
    if(mpSession->Send(0,w.data,true)) {msPending=name;mlPendingEntityID=entity->GetID();mlPendingRuntimeID=entity->GetRuntimeID();mfPendingTime=0;}
    return false;
}
void cLuxMultiplayerEntities::CompleteInteraction(iLuxEntity* entity,bool succeeded) {
    if(!mpSession->IsActive() || !IsNative(entity)) return;
    if(mpSession->IsHost()) {if(succeeded) Commit(entity,false);return;}
    if(msGranted!=entity->GetName() || mlGrantedEntityID!=static_cast<uint32_t>(entity->GetID())) return;
    Writer w(NativeResult);w.U32(mpSession->GetMapEpoch());w.String(msGranted);w.U32(mlGrantedEntityID);w.U32(mlGrantedToken);w.U8(succeeded);w.U32(mlDiaryIndex+1);
    if(!mpSession->Send(0,w.data,true)) mpSession->RejectPeer(0,"Could not confirm the native interaction with the host.");
}
void cLuxMultiplayerEntities::RecordDiaryIndex(const std::string& name,int index) {
    if(mpSession->IsClient() && msGranted==name && index>=0 && index<4096) mlDiaryIndex=index;
}
bool cLuxMultiplayerEntities::DeferDiaryPresentation(const std::string& name,cLuxDiary* diary) {
    if(!mpSession->IsClient() || msGranted!=name || !mlGrantedToken || mlDiaryIndex<0 || !diary) return false;
    mPendingDiaries[mlGrantedToken]={name,diary,0,mlGrantedEntityID};
    return true;
}
void cLuxMultiplayerEntities::RecordDiaryDecision(bool open) {
    if(mpDiaryDecision) *mpDiaryDecision=open;
}
bool cLuxMultiplayerEntities::Commit(iLuxEntity* entity,bool remote,bool callbackOnly,int diaryIndex,uint32_t peer) {
    cLuxMultiplayerRemoteTriggerScope actionTrigger(remote,peer);
    const eLuxPropType kind=static_cast<iLuxProp*>(entity)->GetPropType();
    const bool item=kind==eLuxPropType_Item;
    bool openDiary=true;
    // A callback's return decision belongs to this pickup, not to whichever
    // player's ReturnOpenJournal effect happened to arrive last on the client.
    struct DecisionScope {
        bool*& slot;bool* previous;
        DecisionScope(bool*& target,bool* value):slot(target),previous(target) {slot=value;}
        ~DecisionScope() {slot=previous;}
    } decision(mpDiaryDecision,remote && item && diaryIndex>=0?&openDiary:NULL);
    if(item) {
        if(remote) mpSession->RecordRemoteItem(peer,static_cast<cLuxProp_Item*>(entity));
        mRemovedItems[entity->GetName()]=entity->GetRuntimeID();
        entity->GetMap()->DestroyEntity(entity);
        Writer w(ItemRemoved);w.U32(mpSession->GetMapEpoch());w.String(entity->GetName());
        mpSession->Broadcast(w.data,true);
    } else if(!callbackOnly) {
        if(kind==eLuxPropType_Lamp) static_cast<cLuxProp_Lamp*>(entity)->SetLit(true,true);
        if(remote && kind==eLuxPropType_Button) {
            auto* button=static_cast<cLuxProp_Button*>(entity);button->SetSwitchedOn(!button->GetSwitchedOn(),true);
        }
        if(remote && kind==eLuxPropType_Chest) {
            static_cast<cLuxProp_Chest*>(entity)->SetLocked(false,true);
            entity->GetMap()->AddCompletionAmount(gpBase->mpCompletionCountHandler->mlChestCompletionValue);
        }
    }
    // Run native callbacks exactly once, in the host VM, attributed to the collector.
    // The collector alone receives the item / pays the tinderbox cost.
    // Accepted native actions must run their completion scripts even when
    // passive Player-area triggers are restricted to the host.
    {
        cLuxMultiplayerRemoteTriggerScope trigger(remote,peer);
        mbCallback=true;
        if(remote && item && diaryIndex>=0) {
            auto* pickup=static_cast<cLuxProp_Item*>(entity);
            if(pickup->GetItemType()==eLuxItemType_Diary && !pickup->GetExtraStringVal().empty())
                entity->GetMap()->RunScript(pickup->GetExtraStringVal()+"(\""+entity->GetName()+"\","+cString::ToString(diaryIndex)+")");
        }
        if(!callbackOnly && (item || kind==eLuxPropType_Lamp)) entity->RunCallbackFunc(item?"OnPickup":"OnIgnite");
        if(kind!=eLuxPropType_Chest) entity->RunInteractCallbackFunc();
        mbCallback=false;
    }
    if(!item) BroadcastState(entity);
    else mpSession->AutoCombineInventory(remote?peer:mpSession->GetLocalPeerId(),entity->GetName());
    return openDiary;
}
void cLuxMultiplayerEntities::OnPeerDisconnected(uint32_t peer) {
    mpSession->RecoverRemoteItems(peer);
    mInitial.erase(peer);
    for(auto it=mClaims.begin();it!=mClaims.end();) {
        if(it->second.peer==peer) it=mClaims.erase(it);else ++it;
    }
}
std::vector<uint8_t> cLuxMultiplayerEntities::Capture(iLuxProp* prop) {
    NativeState state;state.epoch=mpSession->GetMapEpoch();state.name=prop->GetName();state.id=prop->GetID();
    const uint8_t kind=prop->GetPropType()==eLuxPropType_Lamp?LampState:
        prop->GetPropType()==eLuxPropType_SwingDoor?DoorState:
        prop->GetPropType()==eLuxPropType_Button?ButtonState:
        prop->GetPropType()==eLuxPropType_Chest?ChestState:PropState;
    state.kind=kind;state.health=prop->GetHealth();
    state.flags=(prop->IsActive()?EntityActive:0) | (prop->GetInteractionDisabled()?InteractionDisabled:0) |
         (prop->mbEffectsActive?EffectsActive:0) | (prop->mbStaticPhysics?StaticPhysics:0);
    uint8_t detail=0;
    if(kind==LampState) detail=static_cast<cLuxProp_Lamp*>(prop)->GetLit()?1:0;
    if(kind==ButtonState) detail=static_cast<cLuxProp_Button*>(prop)->GetSwitchedOn()?1:0;
    if(kind==ChestState) detail=static_cast<cLuxProp_Chest*>(prop)->GetLocked()?1:0;
    if(kind==DoorState) {
        cLuxProp_SwingDoor* door=static_cast<cLuxProp_SwingDoor*>(prop);
        detail=(door->GetClosed()?DoorClosed:0)|(door->GetLocked()?DoorLocked:0)|
            (door->GetDisableAutoClose()?DoorAutoCloseDisabled:0)|(door->IsBroken()?DoorBroken:0)|
            (door->GetCurrentDamageLevel()<<DoorDamageShift)|(door->GetDisableBreakable()?DoorDisableBreakable:0);
        state.brokenEntityId=door->GetBrokenEntityID()<0?UINT32_MAX:static_cast<uint32_t>(door->GetBrokenEntityID());
    }
    state.detail=detail;
    auto& joints=state.joints;
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
    return WriteNativeState(state);
}
bool cLuxMultiplayerEntities::Apply(const std::vector<uint8_t>& data) {
    Reader r(data);NativeState state;
    if(!ReadNativeState(r,state)) return false;
    if(state.epoch!=mpSession->GetMapEpoch()) return true;
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
    iLuxEntity* entity=map?map->GetEntityByID(static_cast<int>(state.id)):NULL;
    if(!entity || entity->GetName()!=state.name || entity->GetDestroyMe()) return true;
    auto removed=mRemovedItems.find(state.name);
    if(removed!=mRemovedItems.end()) {
        if(removed->second==entity->GetRuntimeID()) return true;
        mRemovedItems.erase(removed);
    }
    if(entity->GetEntityType()!=eLuxEntityType_Prop) return false;
    iLuxProp* prop=static_cast<iLuxProp*>(entity);
    if((state.kind==LampState && prop->GetPropType()!=eLuxPropType_Lamp) ||
       (state.kind==DoorState && prop->GetPropType()!=eLuxPropType_SwingDoor) ||
       (state.kind==ButtonState && prop->GetPropType()!=eLuxPropType_Button) ||
       (state.kind==ChestState && prop->GetPropType()!=eLuxPropType_Chest)) return false;
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
    // Native health side effects run on the host. Object-break events already
    // have their own path; a snapshot must not replay them on the client.
    prop->mfHealth=state.health;
    if(prop->mbStaticPhysics!=((state.flags&StaticPhysics)!=0)) prop->SetStaticPhysics((state.flags&StaticPhysics)!=0);
    if(state.kind==LampState) static_cast<cLuxProp_Lamp*>(prop)->SetLit(state.detail!=0,true);
    if(state.kind==ButtonState) static_cast<cLuxProp_Button*>(prop)->SetSwitchedOn(state.detail!=0,true);
    if(state.kind==ChestState) static_cast<cLuxProp_Chest*>(prop)->SetLocked(state.detail!=0,true);
    // Lit and effects can differ after an authored fade or saved-state restore.
    // SetLit also does nothing when the lit bit already matches the snapshot.
    if(prop->mbEffectsActive!=((state.flags&EffectsActive)!=0)) prop->SetEffectsActive((state.flags&EffectsActive)!=0,true);
    if(state.kind==DoorState) {
        auto* door=static_cast<cLuxProp_SwingDoor*>(prop);
        door->SetLocked((state.detail&DoorLocked)!=0,false);door->SetClosed((state.detail&DoorClosed)!=0,false);
        door->SetDisableAutoClose((state.detail&DoorAutoCloseDisabled)!=0);
        door->ApplyNetworkState(state.health,(state.detail&DoorDamageMask)>>DoorDamageShift,
            (state.detail&DoorBroken)!=0,(state.detail&DoorDisableBreakable)!=0,
            state.brokenEntityId==UINT32_MAX?-1:static_cast<int>(state.brokenEntityId));
    }
    for(const auto& j:state.joints) {
        auto* joint=prop->mvJoints[j.index];
        if(!joint || j.kind==0) continue;
        // A wheel's current owner computes its changing angular constraints.
        if(prop->GetPropType()==eLuxPropType_Wheel &&
           mpSession->GetWorld()->IsEntityLeased(prop) &&
           mpSession->GetWorld()->OwnsSimulation(prop->GetMainBody())) continue;
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
    if(!SyncCreatedProps()) return;
    auto bytes=Capture(static_cast<iLuxProp*>(entity));
    mLastStates[entity->GetID()]=bytes;mpSession->Broadcast(bytes,true);
}
PropDefinition cLuxMultiplayerEntities::CaptureDefinition(iLuxProp* prop) {
    PropDefinition definition;definition.epoch=mpSession->GetMapEpoch();
    definition.incarnation=prop->GetRuntimeID();
    definition.id=static_cast<uint32_t>(prop->GetID());definition.name=prop->GetName();
    definition.file=cString::ReplaceCharTo(prop->msFileName,"\\","/");
    if(!SafeRelativePath(definition.file)) definition.file=cString::GetFileName(definition.file);
    for(int row=0;row<3;++row) for(int col=0;col<4;++col)
        definition.matrix[row*4+col]=prop->m_mtxOnLoadTransform.m[row][col];
    definition.scale[0]=prop->mvOnLoadScale.x;definition.scale[1]=prop->mvOnLoadScale.y;definition.scale[2]=prop->mvOnLoadScale.z;
    if(auto* parent=prop->GetAttachmentParent()) {
        definition.parent=parent->GetName();
        definition.parentId=static_cast<uint32_t>(parent->GetID());
        for(const auto* attached:parent->mlstAttachedProps) if(attached->mpProp==prop) {
            for(int row=0;row<3;++row) for(int col=0;col<4;++col)
                definition.attachment[row*4+col]=attached->m_mtxOffset.m[row][col];
            break;
        }
    }
    for(int i=0;i<prop->GetBodyNum();++i) {
        auto* body=prop->GetBody(i);if(!body) continue;
        const uint32_t generation=mpSession->GetWorld()->GetBodyGeneration(body);
        if(generation) definition.bodies.push_back({LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID()),generation});
        else if(!prop->GetAttachmentParent() && body->GetMass()<=0) {
            PropDefinition::StaticBodyPose pose;
            pose.id=LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID());
            pose.flags=(body->GetEnabled()?LuxWorldWire::Awake:0)|(body->IsActive()?LuxWorldWire::Active:0)|
                (body->GetGravity()?LuxWorldWire::Gravity:0)|(body->GetCollide()?LuxWorldWire::Collide:0)|
                (body->GetCollideCharacter()?LuxWorldWire::CollideCharacter:0);
            for(int row=0;row<3;++row) for(int col=0;col<4;++col) pose.matrix[row*4+col]=body->GetLocalMatrix().m[row][col];
            definition.staticBodies.push_back(pose);
        }
    }
    return definition;
}
bool cLuxMultiplayerEntities::CaptureDefinitions(std::vector<std::vector<uint8_t> >& output) {
    output.clear();msLastError.clear();
    auto* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return Fail("Cannot capture prop reconstruction without a loaded map.");
    std::vector<PropDefinition> definitions;
    auto it=map->GetEntityIterator();
    while(it.HasNext()) {
        auto* entity=it.Next();
        if(entity->GetEntityType()!=eLuxEntityType_Prop) continue;
        auto* prop=static_cast<iLuxProp*>(entity);
        bool pendingDestruction=false;
        std::set<iLuxProp*> ancestors;
        for(auto* ancestor=prop;ancestor;ancestor=ancestor->GetAttachmentParent()) {
            if(!ancestors.insert(ancestor).second)
                return Fail("Attachment cycle involving "+DescribePropDefinition(CaptureDefinition(prop))+".");
            if(ancestor->GetDestroyMe()) {pendingDestruction=true;break;}
        }
        if(!pendingDestruction) definitions.push_back(CaptureDefinition(prop));
    }
    std::string error;
    if(!OrderPropDefinitions(definitions,&error)) return Fail(error);
    for(const auto& definition:definitions) {
        auto bytes=WritePropDefinition(definition);Reader reader(bytes);PropDefinition verified;
        if(!ReadPropDefinition(reader,verified)) return Fail("Cannot encode valid reconstruction for "+DescribePropDefinition(definition)+".");
        output.push_back(std::move(bytes));
    }
    return true;
}
void cLuxMultiplayerEntities::CaptureMapBaseline() {
    if(!mpSession->IsHost()) return;
    mbMapBaselineValid=CaptureDefinitions(mvMapBaseline);
    msMapBaselineError=msLastError;
    if(mbMapBaselineValid) for(const auto& bytes:mvMapBaseline) {
        Reader reader(bytes);PropDefinition definition;ReadPropDefinition(reader,definition);
        mAnnouncedProps[definition.id]={definition.epoch,definition.id,definition.incarnation,definition.name};
    }
}
bool cLuxMultiplayerEntities::SyncCreatedProps() {
    if(!msPendingSyncError.empty()) return false;
    auto* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return false;
    bool changed=false;
    std::map<uint32_t,uint64_t> present;
    auto it=map->GetEntityIterator();
    while(it.HasNext()) {
        auto* entity=it.Next();
        if(entity->GetEntityType()!=eLuxEntityType_Prop) continue;
        present[entity->GetID()]=entity->GetRuntimeID();
        // Destruction callbacks still see this ID occupied on the host. Keep
        // it reserved on clients until destruction has actually completed.
        if(entity->GetDestroyMe()) continue;
        auto known=mAnnouncedProps.find(entity->GetID());
        if(known==mAnnouncedProps.end() || known->second.incarnation!=entity->GetRuntimeID()) changed=true;
    }
    for(auto known=mAnnouncedProps.begin();known!=mAnnouncedProps.end();) {
        auto current=present.find(known->first);
        if(current==present.end() || current->second!=known->second.incarnation) {
            mpSession->BroadcastScriptEffect(WritePropRemoval(known->second));
            known=mAnnouncedProps.erase(known);
        } else ++known;
    }
    if(!changed) return true;

    // Native gameplay can create props without a script command (door debris,
    // for example). Publish their recipes before any native/physics snapshots.
    // Reuse full-roster validation so attachments are emitted parent first.
    std::vector<std::vector<uint8_t> > definitions;
    if(!CaptureDefinitions(definitions)) {msPendingSyncError=msLastError;return false;}
    std::set<uint32_t> emitted;
    for(const auto& bytes:definitions) {
        Reader reader(bytes);PropDefinition definition;ReadPropDefinition(reader,definition);
        auto known=mAnnouncedProps.find(definition.id);
        if(known!=mAnnouncedProps.end() && known->second.incarnation==definition.incarnation &&
            (definition.parent.empty() || !emitted.count(definition.parentId))) continue;
        // Preserve creation order for both connected clients and history
        // replay, including script callbacks following a native creation.
        mpSession->BroadcastScriptEffect(bytes);
        mAnnouncedProps[definition.id]={definition.epoch,definition.id,definition.incarnation,definition.name};
        emitted.insert(definition.id);
    }
    return true;
}
bool cLuxMultiplayerEntities::ApplyRemoval(const std::vector<uint8_t>& bytes) {
    Reader reader(bytes);PropRemoval removal;
    if(!ReadPropRemoval(reader,removal)) return false;
    if(removal.epoch!=mpSession->GetMapEpoch()) return true;
    auto* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return false;
    map->FlushNetworkEntityDestruction();
    auto bound=mDefinitionBindings.find(removal.id);
    auto* entity=map->GetEntityByID(static_cast<int>(removal.id));
    if(bound==mDefinitionBindings.end() || bound->second.host!=removal.incarnation) return true;
    if(entity && entity->GetName()==removal.name && entity->GetRuntimeID()==bound->second.local) {
        map->DestroyEntity(entity);
        map->FlushNetworkEntityDestruction();
    }
    mDefinitionBindings.erase(bound);
    return true;
}
bool cLuxMultiplayerEntities::SendMapBaseline(uint32_t peer) {
    msLastError.clear();
    if(!mbMapBaselineValid) {msLastError=msMapBaselineError;return false;}
    if(mvMapBaseline.size()>8192) return Fail("Prop reconstruction baseline exceeds 8192 entities.");
    for(const auto& bytes:mvMapBaseline) if(!mpSession->Send(peer,bytes,true)) {
        Reader reader(bytes);PropDefinition definition;ReadPropDefinition(reader,definition);
        return Fail("Could not queue restored baseline "+DescribePropDefinition(definition)+" for peer "+std::to_string(peer)+".");
    }
    return true;
}
bool cLuxMultiplayerEntities::ApplyDefinition(const std::vector<uint8_t>& bytes) {
    msLastError.clear();
    Reader reader(bytes);PropDefinition definition;
    if(!ReadPropDefinition(reader,definition)) return Fail("Invalid reconstruction packet for "+DescribePropDefinition(definition)+".");
    if(definition.epoch!=mpSession->GetMapEpoch()) return true;
    auto* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return Fail("Cannot apply "+DescribePropDefinition(definition)+" without a loaded map.");
    const auto fail=[&](const std::string& reason) {return Fail(reason+" for "+DescribePropDefinition(definition)+".");};
    const auto bind=[&](iLuxProp* prop) {
        if(!definition.parent.empty()) {
            auto* entity=map->GetEntityByID(static_cast<int>(definition.parentId),eLuxEntityType_Prop);
            if(!entity || entity->GetName()!=definition.parent || entity->GetDestroyMe()) return fail("Missing attachment parent '"+definition.parent+"' (ID "+std::to_string(definition.parentId)+")");
            cMatrixf offset=cMatrixf::Identity;
            for(int row=0;row<3;++row) for(int col=0;col<4;++col) offset.m[row][col]=definition.attachment[row*4+col];
            if(!static_cast<iLuxProp*>(entity)->AttachExistingProp(prop,offset)) return fail("Cannot attach to parent '"+definition.parent+"' (ID "+std::to_string(definition.parentId)+")");
        } else if(prop->GetAttachmentParent()) {
            prop->GetAttachmentParent()->RemoveAttachedProp(prop);
        }
        for(const auto& expected:definition.bodies) {
            iPhysicsBody* matched=NULL;
            for(int i=0;i<prop->GetBodyNum();++i) {
                auto* body=prop->GetBody(i);
                if(body && LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID())==expected.id) {matched=body;break;}
            }
            if(!matched) return fail("Missing replicated body "+std::to_string(expected.id));
            mpSession->GetWorld()->BindBodyGeneration(matched,expected.generation);
        }
        for(const auto& pose:definition.staticBodies) {
            iPhysicsBody* matched=NULL;
            for(int i=0;i<prop->GetBodyNum();++i) {
                auto* body=prop->GetBody(i);
                if(body && LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID())==pose.id) {matched=body;break;}
            }
            if(!matched) return fail("Missing static body "+std::to_string(pose.id));
            // A current dynamic pose may overtake a historical static recipe.
            // Keep the bound simulation stream authoritative in that case.
            if(mpSession->GetWorld()->GetBodyGeneration(matched)) continue;
            cMatrixf matrix=cMatrixf::Identity;
            for(int row=0;row<3;++row) for(int col=0;col<4;++col) matrix.m[row][col]=pose.matrix[row*4+col];
            matched->SetMass(0);matched->SetMatrix(matrix);matched->ResetRenderInterpolation();
            matched->SetActive((pose.flags&LuxWorldWire::Active)!=0);
            matched->SetGravity((pose.flags&LuxWorldWire::Gravity)!=0);
            matched->SetCollide((pose.flags&LuxWorldWire::Collide)!=0);
            matched->SetCollideCharacter((pose.flags&LuxWorldWire::CollideCharacter)!=0);
            if(pose.flags&LuxWorldWire::Awake) matched->Enable();
        }
        mDefinitionBindings[definition.id]={definition.incarnation,prop->GetRuntimeID()};
        return true;
    };
    // Authored names are not unique: the campaign contains same-name siblings.
    // Their ID, rather than the first name-map entry, owns reconstruction.
    // ObjectBreak marks an object for deferred destruction; its destruction
    // can create the very debris this definition describes. Finish that work
    // before deciding whether reconstruction is necessary.
    map->FlushNetworkEntityDestruction();
    auto* existing=map->GetEntityByID(static_cast<int>(definition.id));
    if(existing && !existing->GetDestroyMe() && existing->GetEntityType()==eLuxEntityType_Prop) {
        auto* prop=static_cast<iLuxProp*>(existing);
        auto previous=mDefinitionBindings.find(definition.id);
        const bool differentIncarnation=previous!=mDefinitionBindings.end() &&
            DefinitionChangesBoundIncarnation(previous->second,definition.incarnation,prop->GetRuntimeID());
        tString file=cString::ReplaceCharTo(prop->msFileName,"\\","/");
        if(!SafeRelativePath(file)) file=cString::GetFileName(file);
        const bool sameScale=prop->mvOnLoadScale==cVector3f(definition.scale[0],definition.scale[1],definition.scale[2]);
        if(!differentIncarnation && sameScale && existing->GetName()==definition.name &&
           cString::ToLowerCase(file)==cString::ToLowerCase(definition.file)) return bind(prop);
    }
    tString error;
    if(!LuxValidateMultiplayerAsset(definition.file,"ent",error)) return fail("Invalid entity resource: "+error);
    // Definitions are applied only from packet handling, between engine updates.
    // Remove stale authored incarnations before loading the host's restored one.
    if(existing) map->DestroyEntity(existing);
    map->FlushNetworkEntityDestruction();
    cMatrixf transform=cMatrixf::Identity;
    for(int row=0;row<3;++row) for(int col=0;col<4;++col) transform.m[row][col]=definition.matrix[row*4+col];
    cLuxMap* previousLoading=gpBase->mpCurrentMapLoading;gpBase->mpCurrentMapLoading=map;
    map->ResetLatestEntity();
    map->GetWorld()->CreateEntity(definition.name,transform,definition.file,static_cast<int>(definition.id),true,
        cVector3f(definition.scale[0],definition.scale[1],definition.scale[2]));
    gpBase->mpCurrentMapLoading=previousLoading;
    auto* created=map->GetLatestEntity();
    if(!created || created->GetID()!=static_cast<int>(definition.id) || created->GetName()!=definition.name || created->GetEntityType()!=eLuxEntityType_Prop)
        return fail("Entity resource did not create the expected prop");
    return bind(static_cast<iLuxProp*>(created));
}
bool cLuxMultiplayerEntities::SendInitialState(uint32_t peer) {
    msLastError.clear();
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return Fail("Cannot synchronize entities without a loaded map.");
    // Saved-map restoration runs after OnMapLoaded. Reconcile at the ready
    // barrier against the restored host world, including every return visit.
    std::string error;
    if(!SeedCurrentMapItems(mpSession->mvMapBytes,error)) return Fail("Cannot identify collected map items: "+error);
    std::vector<std::vector<uint8_t> > definitions;
    if(!CaptureDefinitions(definitions)) return false;
    for(const auto& bytes:definitions) if(!mpSession->Send(peer,bytes,true)) {
        Reader reader(bytes);PropDefinition definition;ReadPropDefinition(reader,definition);
        return Fail("Could not queue current "+DescribePropDefinition(definition)+" for peer "+std::to_string(peer)+".");
    }
    SyncRopes(peer);
    auto& queue=mInitial[peer];queue.clear();
    for(const auto& removed:mRemovedItems) queue.push_back({-1,removed.first});
    auto it=map->GetEntityIterator();
    while(it.HasNext()) {
        auto* entity=it.Next();
        // History may have created or replaced props since the restored baseline.
        // Reconcile the current roster before any native/body snapshots arrive.
        if(entity->GetEntityType()==eLuxEntityType_Prop && !entity->GetDestroyMe() && !mRemovedItems.count(entity->GetName()))
            queue.push_back({entity->GetID(),entity->GetName()});
        if(queue.size()>8192) {mInitial.erase(peer);return Fail("Initial native entity state exceeds 8192 entries.");}
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
    for(uint32_t peer:expired) mpSession->RejectPeer(peer,"Timed out confirming a native interaction.");
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return;
    if(!SyncCreatedProps()) {
        mpSession->Stop("Could not synchronize new map entities: "+msLastError);return;
    }
    for(auto it=mRemovedItems.begin();it!=mRemovedItems.end();) {
        auto* entity=map->GetEntityByName(it->first);
        if(entity && !entity->GetDestroyMe() && entity->GetRuntimeID()!=it->second) it=mRemovedItems.erase(it);else ++it;
    }
    for(auto it=mInitial.begin();it!=mInitial.end();) {
        auto& queue=it->second;
        for(unsigned sent=0;sent<32 && !queue.empty();++sent) {
            const auto& pending=queue.front();const auto& name=pending.second;std::vector<uint8_t> bytes;
            if(pending.first<0 && mRemovedItems.count(name)) {Writer w(ItemRemoved);w.U32(mpSession->GetMapEpoch());w.String(name);bytes=w.data;}
            else {
                auto* entity=map->GetEntityByID(pending.first);
                if(entity && entity->GetName()==name && !entity->GetDestroyMe() && entity->GetEntityType()==eLuxEntityType_Prop) bytes=Capture(static_cast<iLuxProp*>(entity));
            }
            if(!bytes.empty() && !mpSession->Send(it->first,bytes,true)) break;
            queue.pop_front();
        }
        if(queue.empty()) it=mInitial.erase(it);else ++it;
    }
    mfGroundTruthTime+=dt;
    mfSnapshotTime+=dt;if(mfSnapshotTime<0.1f) return;mfSnapshotTime=0;
    SyncRopes();
    const bool groundTruth=mfGroundTruthTime>=2;if(groundTruth) mfGroundTruthTime=0;
    if(groundTruth) mpSession->RecoverPendingItems();
    auto it=map->GetEntityIterator();
    while(it.HasNext()) {
        auto* entity=it.Next();
        if(entity->GetEntityType()!=eLuxEntityType_Prop || entity->GetDestroyMe()) continue;
        auto bytes=Capture(static_cast<iLuxProp*>(entity));auto& previous=mLastStates[entity->GetID()];
        if(groundTruth || bytes!=previous) {previous=bytes;mpSession->Broadcast(bytes,true);}
    }
}
bool cLuxMultiplayerEntities::HandleMessage(uint32_t peer,const std::vector<uint8_t>& data) {
    if(data.empty()) return false;
    Reader r(data);const uint8_t type=data[0];uint32_t epoch=r.U32();
    if(!r.valid) return false;
    if(epoch!=mpSession->GetMapEpoch()) return true;
    if(type==RopeState && mpSession->IsClient()) {
        Reader reader(data);RopeSnapshot s;if(!ReadRope(reader,s)) return false;
        auto* map=gpBase->mpMapHandler->GetCurrentMap();
        auto* rope=map?map->GetPhysicsWorld()->GetRope(s.name):NULL;
        if(!rope) return true;
        rope->SetMinTotalLength(s.min);rope->SetMaxTotalLength(s.max);rope->SetTotalLength(s.length);
        rope->SetMotorWantedLength(s.wanted);rope->SetMotorSpeedMul(s.mul);
        rope->SetMotorMinSpeed(s.minSpeed);rope->SetMotorMaxSpeed(s.maxSpeed);rope->SetMotorActive(s.motor!=0);
        rope->SetAutoMoveAcc(s.acc);rope->SetAutoMoveMaxSpeed(s.maxAuto);rope->SetAutoMoveSpeed(s.speed);rope->SetAutoMoveActive(s.autoMove!=0);
        return true;
    }
    if(type==JointBreakRequest) {
        Reader requestReader(data);JointBreakState request;
        if(!mpSession->IsHost() || !ReadJointBreak(requestReader,request)) return false;
        cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
        auto* prop=map?static_cast<iLuxProp*>(map->GetEntityByID(static_cast<int>(request.id),eLuxEntityType_Prop)):NULL;
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
    const uint32_t id=type==ItemRemoved?UINT32_MAX:r.U32();
    if(!r.valid || (type!=ItemRemoved && id>0x7fffffffu)) return false;
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
    iLuxEntity* entity=map?(type==ItemRemoved?map->GetEntityByName(name):map->GetEntityByID(static_cast<int>(id))):NULL;
    if(entity && entity->GetName()!=name) entity=NULL;
    if(mpSession->IsHost() && type==NativeRequest) {
        if(!r.Done()) return false;
        const auto& players=mpSession->GetWorld()->GetRemotePlayers();auto player=players.find(peer);
        bool allow=Eligible(entity) && !mClaims.count(id) && player!=players.end() &&
            player->second.age<=2 && (player->second.gameplay.flags&LuxWorldWire::PlayerAlive);
        if(allow) {
            allow=false;
            for(int i=0;i<entity->GetBodyNum();++i)
                if(cMath::Vector3Dist(entity->GetBody(i)->GetWorldPosition(),player->second.position)<=4) {allow=true;break;}
        }
        // Each client may hold only one unresolved native interaction.
        for(const auto& claim:mClaims) if(claim.second.peer==peer) allow=false;
        // Buttons have no local inventory cost or presentation decision. Apply
        // their toggle directly on the authority so a later result cannot
        // invert a newer script/other-player toggle.
        if(allow && static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Button) {
            Commit(entity,true,false,-1,peer);
            Writer response(NativeGrant);response.U32(epoch);response.String(name);response.U32(id);response.U32(0);response.U8(0);
            return mpSession->Send(peer,response.data,true);
        }
        uint32_t token=0;bool callbackOnly=false;
        if(allow) {
            if(static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Lamp) {
                auto* lamp=static_cast<cLuxProp_Lamp*>(entity);callbackOnly=lamp->GetLit() || !lamp->CanBeIgnitByPlayer();
            }
            const bool diary=static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Item &&
                static_cast<cLuxProp_Item*>(entity)->GetItemType()==eLuxItemType_Diary;
            if(!++mlToken) ++mlToken;token=mlToken;mClaims[id]={peer,token,0,entity->GetRuntimeID(),callbackOnly,diary};
        }
        Writer w(NativeGrant);w.U32(epoch);w.String(name);w.U32(id);w.U32(token);w.U8(callbackOnly);
        if(!mpSession->Send(peer,w.data,true)) mpSession->RejectPeer(peer,"Could not deliver the native interaction grant.");
        return true;
    }
    if(mpSession->IsHost() && type==NativeResult) {
        uint32_t token=r.U32();uint8_t success=r.U8();uint32_t diary=r.U32();
        if(!r.Done() || !token || success>1 || diary>4096) return false;
        auto claim=mClaims.find(id);
        if(claim==mClaims.end() || claim->second.peer!=peer || claim->second.token!=token) return false;
        if(diary && !claim->second.diary) return false;
        Claim accepted=claim->second;mClaims.erase(claim);
        const bool committed=success && IsNative(entity) && !entity->GetDestroyMe() && entity->GetRuntimeID()==accepted.runtimeId;
        bool openDiary=true;
        if(committed) openDiary=Commit(entity,true,accepted.callbackOnly,static_cast<int>(diary)-1,peer);
        if(diary && mpSession->IsHost() && epoch==mpSession->GetMapEpoch()) {
            // Reliable ordering places the decision after all synchronous
            // callback effects; only its collector has this pending token.
            Writer response(NativeDiaryResult);response.U32(epoch);response.String(name);response.U32(id);response.U32(token);
            response.U8(committed);response.U8(committed && openDiary);
            return mpSession->Send(peer,response.data,true);
        }
        return true;
    }
    if(mpSession->IsClient() && type==NativeGrant) {
        uint32_t token=r.U32();uint8_t callbackOnly=r.U8();if(!r.Done() || callbackOnly>1 || msPending!=name || mlPendingEntityID!=id) return false;
        const bool sameInstance=entity && entity->GetRuntimeID()==mlPendingRuntimeID;
        msPending.clear();mlPendingEntityID=UINT32_MAX;mlPendingRuntimeID=0;mfPendingTime=0;
        if(!token) return true;
        if(!sameInstance || !Eligible(entity) || gpBase->mpPlayer->GetHealth()<=0 || callbackOnly) {
            Writer w(NativeResult);w.U32(epoch);w.String(name);w.U32(id);w.U32(token);
            w.U8(sameInstance && callbackOnly && entity && gpBase->mpPlayer->GetHealth()>0);w.U32(0);
            return mpSession->Send(0,w.data,true);
        }
        msGranted=name;mlGrantedEntityID=id;mlGrantedToken=token;mlDiaryIndex=-1;
        if(static_cast<iLuxProp*>(entity)->GetPropType()==eLuxPropType_Chest)
            static_cast<cLuxProp_Chest*>(entity)->Purchase();
        else entity->OnInteract(entity->GetBody(0),entity->GetBody(0)->GetWorldPosition());
        msGranted.clear();mlGrantedEntityID=UINT32_MAX;mlGrantedToken=0;
        return true;
    }
    if(mpSession->IsClient() && type==NativeDiaryResult) {
        const uint32_t token=r.U32();const uint8_t accepted=r.U8(),open=r.U8();
        if(!r.Done() || !token || accepted>1 || open>1 || (!accepted && open)) return false;
        auto pending=mPendingDiaries.find(token);
        if(pending==mPendingDiaries.end() || pending->second.name!=name || pending->second.id!=id) return false;
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
void cLuxMultiplayerEntities::SyncRopes(uint32_t peer) {
    auto* map=gpBase->mpMapHandler->GetCurrentMap();if(!map || !mpSession->IsHost()) return;
    auto it=map->GetWorld()->GetRopeEntityIterator();
    while(it.HasNext()) {
        auto* rope=it.Next()->GetPhysicsRope();
        RopeSnapshot s;s.epoch=mpSession->GetMapEpoch();s.name=rope->GetName();
        s.length=rope->GetTotalLength();s.min=rope->GetMinTotalLength();s.max=rope->GetMaxTotalLength();
        s.motor=rope->GetMotorActive();s.autoMove=rope->GetAutoMoveActive();
        s.wanted=rope->GetMotorWantedLength();s.mul=rope->GetMotorSpeedMul();
        s.minSpeed=rope->GetMotorMinSpeed();s.maxSpeed=rope->GetMotorMaxSpeed();
        s.speed=rope->GetAutoMoveSpeed();s.acc=rope->GetAutoMoveAcc();s.maxAuto=rope->GetAutoMoveMaxSpeed();
        const auto bytes=WriteRope(s);
        if(peer!=UINT32_MAX) mpSession->Send(peer,bytes,true);
        else if(mLastRopes[rope->GetName()]!=bytes) {
            mLastRopes[rope->GetName()]=bytes;mpSession->Broadcast(bytes,true);
        }
    }
}
