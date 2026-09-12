#ifndef MULTIPLAYER_JOINT_LIFECYCLE_REGRESSION_H
#define MULTIPLAYER_JOINT_LIFECYCLE_REGRESSION_H
#include "LuxProp_Object.h"
#include "LuxMultiplayerEntities.h"
#include "LuxMultiplayerEntityProtocol.h"
#include "LuxPlayerState.h"
#include "input/Action.h"

class cJointHeldInput : public iSubAction {
public:
    bool held=false;
    bool IsTriggerd() {return held;}
    float GetValue() {return held?1.0f:0.0f;}
    tString GetInputName() {return "Joint lifecycle regression input";}
    tString GetInputType() {return "Test";}
};

// Reproduce the reported painting failure with its unchanged retail constraint,
// then keep saving, replicating and leasing the released Newton body.
class cJointLifecycleRegression : public iPhysicsJointDestroyCallback {
    unsigned phase=0,destructions=0;
    Uint32 entered=0;
    bool originalGravity=true;
    cVector3f originalPosition;
    cJointHeldInput* input=NULL; // Owned by the engine action.
    void next() {++phase;entered=SDL_GetTicks();}
    static bool both(const char* suffix) {return exists(tString("host-")+suffix) && exists(tString("client-")+suffix);}
    int fail(tString& error,const char* message) {
        error="joint lifecycle phase "+cString::ToString((int)phase)+": "+message;return -1;
    }
    static bool savedBroken(cLuxProp_Object* prop) {
        auto* data=prop->CreateSaveData();prop->SaveToSaveData(data);
        auto* saved=static_cast<cLuxProp_Object_SaveData*>(data);
        const bool broken=saved->mvJoints.Size()==1 && saved->mvJoints[0].mbBroken;
        hplDelete(data);return broken;
    }
public:
    void OnPhysicsJointDestroyed(iPhysicsJoint*) {++destructions;}
    int Update(tString& error,float dt) {
        auto* session=gpBase->mpMultiplayer;auto* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!session->IsReady() || !map) return fail(error,"session stopped");
        auto* physics=map->GetPhysicsWorld();auto* world=session->GetWorld();
        auto* player=gpBase->mpPlayer->GetCharacterBody();const bool host=session->IsHost();
        const Uint32 age=entered?SDL_GetTicks()-entered:0;
        if(age>10000) return fail(error,"phase timed out");
        auto* prop=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_break_joint",eLuxEntityType_Prop,eLuxPropType_Object));
        if(phase==0) {
            originalPosition=player->GetPosition();originalGravity=player->GravityIsActive();
            player->SetGravityActive(false);player->SetForceVelocity(0);player->SetPosition(cVector3f(host?-7:-5,40,2));
            cVector3f minimum=physics->GetWorldSizeMin(),maximum=physics->GetWorldSizeMax();
            minimum.x=std::min(minimum.x,-10.0f);minimum.z=std::min(minimum.z,-5.0f);
            maximum.y=std::max(maximum.y,45.0f);maximum.z=std::max(maximum.z,5.0f);
            physics->SetWorldSize(minimum,maximum);
            cMatrixf transform=cMatrixf::Identity;transform.SetTranslation(cVector3f(-7,40,0));
            map->CreateEntity("codex_break_joint","entities/ornament/paintings/painting03/painting03_dynamic.ent",transform,1);
            prop=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_break_joint",eLuxEntityType_Prop,eLuxPropType_Object));
            auto* body=prop?prop->GetBodyFromID(3):NULL;
            if(!body || body->GetJointNum()!=1 || body->GetJoint(0)->GetType()!=ePhysicsJointType_Ball ||
               !body->GetJoint(0)->IsBreakable() || body->GetJoint(0)->GetBreakForce()!=80)
                return fail(error,"retail painting's authored breakable ball joint changed");
            body->SetGravity(false);body->GetJoint(0)->AddDestroyCallback(this);
            // A follower cannot break the authored joint even when its local
            // Newton force threshold is reached. Only host deletion may remove it.
            if(!host) {
                body->GetJoint(0)->SetBreakForce(0);
                if(body->GetJoint(0)->CheckBreakage()) return fail(error,"unowned follower could break a joint");
                body->GetJoint(0)->SetBreakForce(80);
            }
            mark(role+"-joint-ready.txt","retail painting ready");next();return 0;
        }
        if(!prop) return fail(error,"painting disappeared");
        auto* body=prop->GetBodyFromID(3);
        if(phase==1) {
            if(!both("joint-ready.txt") || age<700) return 0;
            if(host && body->GetJointNum()) {body->AddForce(cVector3f(0,0,1500));return 0;}
            if(body->GetJointNum()) return 0;
            if(destructions!=1 || !savedBroken(prop)) return fail(error,"deleted constraint remained in native prop save data");
            body->SetLinearVelocity(0);body->SetAngularVelocity(0);
            prop->SetStuckState(1);prop->SetStuckState(0);
            // Force fresh native allocations while the authored slot stays
            // deleted: an allocator-reused address must never resurrect it.
            auto* replacement=physics->CreateJointBall("codex_replacement_joint",body->GetWorldPosition(),cVector3f(0,1,0),NULL,body);
            replacement->AddDestroyCallback(this);replacement->AddDestroyCallback(this);
            if(!savedBroken(prop)) return fail(error,"a replacement allocation revived the deleted authored slot");
            physics->DestroyJoint(replacement);
            if(destructions!=2 || body->GetJointNum()) return fail(error,"explicit deletion or duplicate observer handling failed");
            replacement=physics->CreateJointBall("codex_unobserved_joint",body->GetWorldPosition(),cVector3f(0,1,0),NULL,body);
            replacement->AddDestroyCallback(this);replacement->RemoveDestroyCallback(this);physics->DestroyJoint(replacement);
            if(destructions!=2) return fail(error,"removed observer was called");
            mark(role+"-joint-deleted.txt","native deletion, stable save slot and observer lifecycle passed");next();return 0;
        }
        if(phase==2) {
            if(!both("joint-deleted.txt")) return 0;
            if(host) {
                const cVector3f center=(body->GetBoundingVolume()->GetMin()+body->GetBoundingVolume()->GetMax())*0.5f;
                player->SetPosition(center+cVector3f(0,0,0.4f));
                if(!world->AllowPlayerContact(body) || !world->OwnsSimulation(body)) return fail(error,"released painting cannot acquire a contact lease");
                player->SetPosition(originalPosition);
                for(const auto& peer:world->GetRemotePlayers())
                    if(!world->SendInitialState(peer.first) || !session->GetEntities()->SendInitialState(peer.first))
                        return fail(error,"post-break baseline failed");
                mark("host-joint-baseline.txt","body and entity baselines queued after joint deletion");
            }
            next();return 0;
        }
        if(phase==3) {
            if(!exists("host-joint-baseline.txt") || age<2300 ||
               world->mBodyLeases.count(LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID()))) return 0;
            if(body->GetJointNum()!=0 || !savedBroken(prop) || destructions!=2) return fail(error,"periodic state or baseline revived a deleted joint");
            // Repeated reliable deletion is harmless on a client that has
            // already processed it (including joints other than hinge/slider).
            if(!host) {
                luxnet::Writer packet(luxnet::EntityState);packet.U32(session->GetMapEpoch());packet.String(prop->GetName());
                packet.U8(luxnet::PropState);packet.U8(luxnet::EntityActive|luxnet::EffectsActive);packet.U8(0);packet.U32(1);
                packet.U32(0);packet.U8(0);packet.U8(0);packet.Float(0);packet.Float(0);
                if(!session->GetEntities()->HandleMessage(0,packet.data) || !session->GetEntities()->HandleMessage(0,packet.data))
                    return fail(error,"repeated deleted-slot state was rejected");
            }
            player->SetPosition(originalPosition);player->SetGravityActive(originalGravity);player->SetForceVelocity(0);
            mark(role+"-joint-painting-passed.txt","retail joint break survives native save, replacement allocation, snapshots, baseline and lease release");next();return 0;
        }
        if(phase==4) {
            if(!both("joint-painting-passed.txt")) return 0;
            player->SetGravityActive(false);
            cMatrixf transform=cMatrixf::Identity;transform.SetTranslation(cVector3f(-7,40,-3));
            map->CreateEntity("codex_pending_joint","entities/furniture/chest_of_drawers_nice/chest_of_drawers_nice.ent",transform,1);
            auto* cabinet=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_pending_joint",eLuxEntityType_Prop,eLuxPropType_Object));
            if(!cabinet || cabinet->GetBodyNum()!=4) return fail(error,"pending drawer fixture failed");
            for(int i=0;i<cabinet->GetBodyNum();++i) cabinet->GetBody(i)->SetGravity(false);
            player->SetPosition(cabinet->GetBodyFromID(32)->GetWorldPosition()+cVector3f(host?3:0,0,1.5f));
            if(!host) {input=hplNew(cJointHeldInput,());gpBase->mpEngine->GetInput()->GetAction(eLuxAction_Interact)->AddSubAction(input);}
            mark(role+"-joint-pending-ready.txt","pending drawer fixture ready");next();return 0;
        }
        if(phase==5) {
            if(!both("joint-pending-ready.txt") || age<700) return 0;
            auto* cabinet=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_pending_joint",eLuxEntityType_Prop,eLuxPropType_Object));
            auto* drawer=cabinet->GetBodyFromID(32);
            if(!host) {
                if(drawer->GetJointNum()!=1) return fail(error,"pending drawer lost its intact joint");
                auto* joint=static_cast<iPhysicsJointSlider*>(drawer->GetJoint(0));
                auto* data=cabinet->CreateSaveData();cabinet->SaveToSaveData(data);
                auto* saved=static_cast<cLuxProp_Object_SaveData*>(data);uint32_t slot=128;
                for(size_t i=0;i<saved->mvJoints.Size();++i) if(saved->mvJoints[i].msName==joint->GetName()) slot=static_cast<uint32_t>(i);
                hplDelete(data);if(slot==128) return fail(error,"authored slider slot not found");
                luxnet::Writer oldState(luxnet::EntityState);oldState.U32(session->GetMapEpoch());oldState.String(cabinet->GetName());
                oldState.U8(luxnet::PropState);oldState.U8(luxnet::EntityActive|luxnet::EffectsActive);oldState.U8(0);oldState.U32(1);
                oldState.U32(slot);oldState.U8(2);oldState.U8(0);oldState.Float(joint->GetMinDistance());oldState.Float(joint->GetMaxDistance());
                input->held=true;gpBase->mpEngine->GetInput()->GetAction(eLuxAction_Interact)->ResetToCurrentState();
                if(world->RequestInteraction(drawer,eLuxPlayerState_InteractSlide,drawer->GetWorldPosition()) || !world->mlPendingRequest)
                    return fail(error,"client did not wait for an interaction grant");
                const uint32_t request=world->mlPendingRequest;
                physics->DestroyJoint(joint);
                if(!session->GetEntities()->HandleMessage(0,oldState.data)) return fail(error,"stale live slider state rejected after local deletion");
                // Deliver a delayed grant through the public decoder before
                // the real round trip. It must release authority, never enter
                // a slide controller whose constraint no longer exists.
                LuxWorldWire::Writer grant(LuxWorldWire::LeaseGrant,session->GetMapEpoch());
                grant.U32(session->GetLocalPeerId());grant.U32(0x70000001);grant.U32(request);grant.U8(0);grant.U8(1);
                grant.U64(LuxWorldWire::BodyId(drawer->GetName(),drawer->GetUniqueID()));
                if(!world->HandleMessage(0,grant.bytes) || gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_Normal ||
                   world->mlPendingRequest || world->mlLocalLease || cabinet->CanInteract(drawer))
                    return fail(error,"delayed grant entered a controller with a deleted joint");
                input->held=false;gpBase->mpEngine->GetInput()->GetAction(eLuxAction_Interact)->ResetToCurrentState();
                mark("client-joint-pending-cancelled.txt","joint deletion safely cancels a delayed slider grant and tolerates stale live state");
            }
            next();return 0;
        }
        if(phase==6) {
            if(!exists("client-joint-pending-cancelled.txt") || age<1500) return 0;
            auto* cabinet=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_pending_joint",eLuxEntityType_Prop,eLuxPropType_Object));
            if(world->IsEntityLeased(cabinet)) return 0;
            player->SetPosition(originalPosition);player->SetGravityActive(originalGravity);player->SetForceVelocity(0);
            mark(role+"-joint-pending-finished.txt","joint lifecycle and deletion during pending interaction passed");next();return 0;
        }
        if(phase==7) {
            if(!both("joint-pending-finished.txt")) return 0;
            player->SetGravityActive(false);player->SetForceVelocity(0);
            cMatrixf transform=cMatrixf::Identity;transform.SetTranslation(cVector3f(-7,40,3));
            map->CreateEntity("codex_owner_break_joint","entities/ornament/paintings/painting03/painting03_dynamic.ent",transform,1);
            auto* owned=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_owner_break_joint",eLuxEntityType_Prop,eLuxPropType_Object));
            auto* ownedBody=owned?owned->GetBodyFromID(3):NULL;
            if(!ownedBody || ownedBody->GetJointNum()!=1 || !ownedBody->GetJoint(0)->IsBreakable())
                return fail(error,"client-owned retail painting fixture failed");
            ownedBody->SetGravity(false);
            // The host must accept the owner's break announcement, rather than
            // accidentally breaking its own follower from the same forces.
            if(host) ownedBody->GetJoint(0)->SetBreakForce(1000000000.0f);
            const cVector3f center=(ownedBody->GetBoundingVolume()->GetMin()+ownedBody->GetBoundingVolume()->GetMax())*0.5f;
            player->SetPosition(host?center+cVector3f(3,0,0):center+cVector3f(0,0,0.4f));
            mark(role+"-joint-owner-fixture.txt","client-owned painting fixture ready");next();return 0;
        }
        if(phase>=8 && phase<=10) {
            auto* owned=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_owner_break_joint",eLuxEntityType_Prop,eLuxPropType_Object));
            auto* ownedBody=owned?owned->GetBodyFromID(3):NULL;
            if(!ownedBody) return fail(error,"client-owned painting disappeared");
            if(phase==8) {
                if(!both("joint-owner-fixture.txt") || age<700) return 0;
                if(!ownedBody->GetJointNum()) return fail(error,"painting broke before its owner applied force");
                uint32_t owner=0,token=0;
                if(!host) {
                    world->AllowPlayerContact(ownedBody);
                    if(!world->OwnsSimulation(ownedBody)) return 0;
                    mark("client-joint-owner-ready.txt","client acquired painting simulation");
                } else {
                    if(!world->GetSimulationLease(ownedBody,owner,token) || owner==0) return 0;
                    auto* joint=ownedBody->GetJoint(0);
                    joint->SetBreakForce(0);
                    if(joint->CheckBreakage()) return fail(error,"host follower broke a client-owned joint");
                    joint->SetBreakForce(1000000000.0f);
                    luxnet::JointBreakState request;request.epoch=session->GetMapEpoch();request.name=owned->GetName();
                    request.index=0;request.body=LuxWorldWire::BodyId(ownedBody->GetName(),ownedBody->GetUniqueID());request.token=token+1;
                    if(!session->GetEntities()->HandleMessage(owner,luxnet::WriteJointBreak(request)) || !ownedBody->GetJointNum())
                        return fail(error,"stale lease token could break the painting");
                    request.token=token;request.body^=1;
                    if(!session->GetEntities()->HandleMessage(owner,luxnet::WriteJointBreak(request)) || !ownedBody->GetJointNum())
                        return fail(error,"different body identity could break the painting");
                    request.body^=1;
                    if(!session->GetEntities()->HandleMessage(0,luxnet::WriteJointBreak(request)) || !ownedBody->GetJointNum())
                        return fail(error,"a nonowner could announce the painting break");
                    joint->SetBreakable(false);
                    if(!session->GetEntities()->HandleMessage(owner,luxnet::WriteJointBreak(request)) || !ownedBody->GetJointNum())
                        return fail(error,"owner could break a nonbreakable host joint");
                    joint->SetBreakable(true);
                    mark("host-joint-owner-ready.txt","host follower and owner/token/body/breakable validation passed");
                }
                if(!both("joint-owner-ready.txt")) return 0;
                next();return 0;
            }
            if(phase==9) {
                if(ownedBody->GetJointNum()) {
                    if(!host) {
                        world->AllowPlayerContact(ownedBody);
                        if(!world->OwnsSimulation(ownedBody)) return fail(error,"client lost ownership before announcing its joint break");
                        ownedBody->AddForce(cVector3f(0,0,1500));
                    }
                    return 0;
                }
                if(!savedBroken(owned)) return fail(error,"owner break did not leave a stable deleted slot");
                ownedBody->SetLinearVelocity(0);ownedBody->SetAngularVelocity(0);
                player->SetPosition(originalPosition);player->SetGravityActive(originalGravity);player->SetForceVelocity(0);
                mark(role+"-joint-owner-deleted.txt","client-authoritative physical break reached host and observer");next();return 0;
            }
            if(!both("joint-owner-deleted.txt") || age<1500 ||
               world->mBodyLeases.count(LuxWorldWire::BodyId(ownedBody->GetName(),ownedBody->GetUniqueID()))) return 0;
            if(ownedBody->GetJointNum() || !savedBroken(owned)) return fail(error,"owner break was revived after lease release");
            mark(role+"-joint-passed.txt","joint lifecycle, pending grants, owner-authoritative breaks and follower suppression passed");next();return 0;
        }
        return both("joint-passed.txt")?1:0;
    }
};
#endif
