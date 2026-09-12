// Load the unchanged retail asset and run its real slide controller on both
// peers. The only synthetic component is a held interaction input binding.
#ifndef MULTIPLAYER_DRAWER_REGRESSION_H
#define MULTIPLAYER_DRAWER_REGRESSION_H
#include "LuxProp_Object.h"
#include "LuxPlayerState.h"
#include "input/Action.h"
#define protected public
#include "LuxPlayerState_InteractSlide.h"
#undef protected

class cDrawerHeldInput : public iSubAction {
public:
    bool held=false;
    bool IsTriggerd() { return held; }
    float GetValue() { return held?1.0f:0.0f; }
    tString GetInputName() { return "Drawer regression input"; }
    tString GetInputType() { return "Test"; }
};

class cDrawerRegression {
    unsigned phase=0,trial=0;
    Uint32 entered=0,diagnosticAt=0;
    cDrawerHeldInput* input=NULL; // Owned by the engine's cAction.
    cVector3f originalPlayerPosition;
    bool originalGravity=true;
    cVector3f initialDrawerPosition;
    cVector3f contactStart,contactPrevious;
    bool attempted=false;
    void next() { ++phase;entered=SDL_GetTicks();attempted=false; }
    tString marker(const char* suffix) {
        return "drawer-"+cString::ToString(static_cast<int>(trial))+"-"+suffix;
    }
    bool both(const tString& suffix) { return exists("host-"+suffix) && exists("client-"+suffix); }
    int fail(tString& error,const char* message) {
        error="drawer trial "+cString::ToString(static_cast<int>(trial))+" phase "+
            cString::ToString(static_cast<int>(phase))+": "+message;return -1;
    }
    cLuxProp_Object* cabinet(cLuxMap* map,unsigned index) {
        return static_cast<cLuxProp_Object*>(map->GetEntityByName(index?"codex_drawers_b":"codex_drawers_a",
            eLuxEntityType_Prop,eLuxPropType_Object));
    }
    void hold(bool value) {
        input->held=value;
        gpBase->mpEngine->GetInput()->GetAction(eLuxAction_Interact)->ResetToCurrentState();
    }
    void stopMovement() {
        auto* player=gpBase->mpPlayer->GetCharacterBody();
        const float forward=player->GetMoveAcc(eCharDir_Forward),right=player->GetMoveAcc(eCharDir_Right);
        // HPL's StopMovement also clears the configured acceleration. Preserve
        // it so the following trial still uses the normal movement controller.
        player->StopMovement();
        player->SetMoveAcc(eCharDir_Forward,forward);player->SetMoveAcc(eCharDir_Right,right);
    }
    int UpdateDrops(tString& error,cLuxMap* map,bool actor,Uint32 age) {
        cLuxMultiplayerWorld* world=gpBase->mpMultiplayer->GetWorld();
        const tString name=trial?"codex_drop_client":"codex_drop_host";
        auto* prop=static_cast<cLuxProp_Object*>(map->GetEntityByName(name,eLuxEntityType_Prop,eLuxPropType_Object));
        if(phase==6) {
            if(!prop) {
                cMatrixf matrix=cMatrixf::Identity;matrix.SetTranslation(cVector3f(-4,40,float(trial)*3));
                map->CreateEntity(name,"entities/container/wood_box_small01/wood_box_small01.ent",matrix,1);
                prop=static_cast<cLuxProp_Object*>(map->GetEntityByName(name,eLuxEntityType_Prop,eLuxPropType_Object));
                if(!prop || prop->GetBodyNum()!=1) return fail(error,"retail drop fixture did not load");
                prop->GetBody(0)->SetGravity(false);
            }
            gpBase->mpPlayer->GetCharacterBody()->SetPosition(prop->GetBody(0)->GetWorldPosition()+cVector3f(actor?0:3,0,1.5f));
            mark(role+"-"+marker("drop-ready.txt"),"ready");next();return 0;
        }
        if(!prop) return fail(error,"drop fixture disappeared");
        iPhysicsBody* body=prop->GetBody(0);
        if(phase==7) {
            if(!both(marker("drop-ready.txt")) || age<700) return 0;
            if(actor) {hold(true);prop->OnInteract(body,body->GetWorldPosition());}
            next();return 0;
        }
        if(phase==8) {
            if(actor && !attempted) {
                if(gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_InteractGrab) return 0;
                if(!world->OwnsInteraction(body)) return fail(error,"native grab has no ownership lease");
                // Deliberately release while overlapping the real player cylinder.
                // Native OnLeaveState must retain its collision guard through the
                // lease release and a subsequent authoritative collision snapshot.
                body->SetPosition(gpBase->mpPlayer->GetCharacterBody()->GetPosition()+cVector3f(0.1f,0,0));
                body->SetLinearVelocity(0);body->SetAngularVelocity(0);
                hold(false);gpBase->mpPlayer->ChangeState(eLuxPlayerState_Normal);
                if(body->GetCollideCharacter() || !prop->IsPlayerCollisionTemporarilyDisabled(body))
                    return fail(error,"lease release removed native post-drop collision guard");
                if(gpBase->mpMultiplayer->IsClient()) {
                    const uint64_t id=LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID());
                    auto track=world->mBodies.find(id);
                    if(track==world->mBodies.end()) return fail(error,"dropped body not indexed");
                    LuxWorldWire::Body snapshot={};snapshot.id=id;
                    for(int i=0;i<12;++i) snapshot.matrix[i]=body->GetLocalMatrix().v[i];
                    snapshot.flags=LuxWorldWire::Awake|LuxWorldWire::Active|LuxWorldWire::Collide|LuxWorldWire::CollideCharacter;
                    if(body->GetGravity()) snapshot.flags|=LuxWorldWire::Gravity;
                    LuxWorldWire::Writer packet(LuxWorldWire::Bodies,gpBase->mpMultiplayer->GetMapEpoch());
                    packet.U32(track->second.received?track->second.receivedSequence+1:1);
                    packet.U32(0);packet.U8(1);packet.U8(1);LuxWorldWire::WriteBody(packet,snapshot);
                    if(!world->HandleMessage(0,packet.bytes)) return fail(error,"valid post-drop host snapshot was rejected");
                    if(body->GetCollideCharacter()) return fail(error,"host snapshot removed local post-drop collision guard");
                }
                attempted=true;
                mark(role+"-"+marker("drop-guard.txt"),"native grab release and host snapshot preserve local collision guard");
                gpBase->mpPlayer->GetCharacterBody()->SetPosition(body->GetWorldPosition()+cVector3f(3,0,0));
            }
            if(!exists((trial?"client-":"host-")+marker("drop-guard.txt"))) return 0;
            next();return 0;
        }
        if(phase==9) {
            if(actor && !attempted) {
                if(prop->IsPlayerCollisionTemporarilyDisabled(body) || !body->GetCollideCharacter()) return 0;
                attempted=true;
                mark(role+"-"+marker("drop-clear.txt"),"native collision restored after player clearance");
            }
            if(!exists((trial?"client-":"host-")+marker("drop-clear.txt")) || world->IsEntityLeased(prop)) return 0;
            mark(role+"-"+marker("drop-passed.txt"),"passed");
            if(!both(marker("drop-passed.txt"))) return 0;
            if(trial==0) {trial=1;phase=6;entered=SDL_GetTicks();return 0;}
            hold(false);phase=10;entered=SDL_GetTicks();return 0;
        }
        return 0;
    }
    int UpdateContact(tString& error,cLuxMap* map,bool host,Uint32 age) {
        auto* mp=gpBase->mpMultiplayer;auto* world=mp->GetWorld();
        auto* player=gpBase->mpPlayer->GetCharacterBody();
        auto* prop=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_contact_box",eLuxEntityType_Prop,eLuxPropType_Object));
        if(phase==10) {
            cMatrixf matrix=cMatrixf::Identity;matrix.SetTranslation(cVector3f(6,40,-3));
            map->CreateEntity("codex_contact_box","entities/container/wood_box01/wood_box01.ent",matrix,1);
            prop=static_cast<cLuxProp_Object*>(map->GetEntityByName("codex_contact_box",eLuxEntityType_Prop,eLuxPropType_Object));
            if(!prop || prop->GetBodyNum()!=1) return fail(error,"retail contact fixture did not load");
            auto* body=prop->GetBody(0);body->SetGravity(false);body->SetLinearDamping(0.8f);body->SetAngularDamping(0.8f);
            // Exercise a push supported by the unchanged retail asset mass
            // and the native character's configured limit.
            if(body->GetMass()<=0 || body->GetMass()>player->GetMaxPushMass())
                return fail(error,"retail contact fixture exceeds native walking push mass");
            contactStart=contactPrevious=body->GetWorldPosition();
            const cVector3f halfSize=(body->GetBoundingVolume()->GetMax()-body->GetBoundingVolume()->GetMin())*0.5f;
            const float clearance=player->GetSize().x*0.5f+0.15f;
            stopMovement();player->SetPosition(contactStart+cVector3f(host?halfSize.x+clearance:0,0,host?0:halfSize.z+clearance));
            player->SetYaw(0);gpBase->mpPlayer->GetCamera()->SetYaw(0);
            mark(role+"-contact-ready.txt","retail box and native character ready");next();return 0;
        }
        if(!prop) return fail(error,"retail contact fixture disappeared");
        auto* body=prop->GetBody(0);
        const uint64_t id=LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID());
        if(SDL_GetTicks()-diagnosticAt>1500) {
            diagnosticAt=SDL_GetTicks();const cVector3f position=player->GetPosition(),box=body->GetWorldPosition();
            std::printf("%s contact phase=%u player=(%.4g %.4g %.4g) box=(%.4g %.4g %.4g) acc=%.4g speed=%.4g contacts=%u owned=%d\n",
                role.c_str(),phase,position.x,position.y,position.z,box.x,box.y,box.z,
                player->GetMoveAcc(eCharDir_Forward),player->GetMoveSpeed(eCharDir_Forward),unsigned(world->mContactAges.count(id)),int(world->OwnsSimulation(body)));
            std::fflush(stdout);
        }
        if(phase==11) {
            if(!both("contact-ready.txt") || age<700) return 0;
            auto track=world->mBodies.find(id);
            if(track==world->mBodies.end() || (!host && !track->second.received)) return 0;
            // Guarantee a short waiting interval even on loopback. The host is
            // nearby but not touching; its passive lease expires naturally.
            if(host && !world->AllowPlayerContact(body)) return fail(error,"contact waiting fixture lease refused");
            if(host) mark("host-contact-wait.txt","short passive ownership guard");
            contactPrevious=body->GetWorldPosition();next();return 0;
        }
        if((body->GetWorldPosition()-contactPrevious).Length()>0.6f) return fail(error,"contact body jumped during ownership handoff");
        contactPrevious=body->GetWorldPosition();
        if(phase==12) {
            if(!exists("host-contact-wait.txt")) return 0;
            if(!host) {
                gpBase->mpPlayer->Move(eCharDir_Forward,1);
                if(world->mContactAges.count(id) && !world->OwnsSimulation(body)) {
                    if((body->GetWorldPosition()-contactStart).Length()>0.02f)
                        return fail(error,"unowned native character contact moved the client box");
                    mark("client-contact-blocked.txt","native collision remained solid while ownership was pending");
                }
                if(!world->OwnsSimulation(body)) return 0;
                if(!exists("client-contact-blocked.txt") || world->OwnsInteraction(body) ||
                   gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_Normal)
                    return fail(error,"native contact failed to acquire independent simulation ownership");
                mark("client-contact-owned.txt","native character callback acquired passive ownership");
            }
            if(!exists("client-contact-owned.txt")) return 0;
            next();return 0;
        }
        if(phase==13) {
            if(!host) {
                gpBase->mpPlayer->Move(eCharDir_Forward,1);
                if((body->GetWorldPosition()-contactStart).Length()<0.25f) return 0;
                if(!world->OwnsSimulation(body)) return fail(error,"contact ownership expired while native player was pushing");
                stopMovement();player->SetPosition(body->GetWorldPosition()+cVector3f(3,0,0));
                mark("client-contact-pushed.txt","retail box moved at least 25 cm through native character forces");
            }
            if(!exists("client-contact-pushed.txt")) return 0;
            next();return 0;
        }
        if(phase==14) {
            if(age<200) return 0;
            if(!host && !world->OwnsSimulation(body)) return fail(error,"contact ownership vanished immediately after separation");
            mark(role+"-contact-grace.txt","brief separation retains passive ownership");next();return 0;
        }
        if(phase==15) {
            if(age<1600 || world->mBodyLeases.count(id) || body->GetLinearVelocity().Length()>0.03f) return 0;
            if(host && !exists("host-contact-target.txt")) {
                const cVector3f position=body->GetWorldPosition();
                mark("host-contact-target.txt",cString::ToString(position.x)+" "+cString::ToString(position.y)+" "+cString::ToString(position.z));
            }
            FILE* file=NULL;fopen_s(&file,(outputDir+"/host-contact-target.txt").c_str(),"rb");if(!file) return 0;
            cVector3f target;const bool complete=std::fscanf(file,"%f %f %f",&target.x,&target.y,&target.z)==3;std::fclose(file);
            if(!complete || (body->GetWorldPosition()-target).Length()>0.08f) return 0;
            if((body->GetWorldPosition()-contactStart).Length()<0.25f) return fail(error,"contact-pushed box returned to its original position");
            mark(role+"-contact-passed.txt","native contact force gate, ownership grace, release and replicated displacement passed");
            if(!both("contact-passed.txt")) return 0;
            stopMovement();player->SetPosition(originalPlayerPosition);player->SetGravityActive(originalGravity);return 1;
        }
        return 0;
    }
public:
    int Update(tString& error) {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!mp->IsActive() || !map) return fail(error,"session/map disappeared");
        const bool host=role=="host",actor=host==(trial==0);
        const Uint32 age=SDL_GetTicks()-entered;
        if(phase && age>10000) return fail(error,"timed out waiting for drawer interaction/replication");
        if(phase>=10) return UpdateContact(error,map,host,age);
        if(phase>=6) return UpdateDrops(error,map,actor,age);
        if(phase==0) {
            originalPlayerPosition=gpBase->mpPlayer->GetCharacterBody()->GetPosition();
            originalGravity=gpBase->mpPlayer->GetCharacterBody()->GravityIsActive();
            gpBase->mpPlayer->GetCharacterBody()->SetGravityActive(false);
            input=hplNew(cDrawerHeldInput,());
            gpBase->mpEngine->GetInput()->GetAction(eLuxAction_Interact)->AddSubAction(input);
            // World::Compile bounds Newton to retail static geometry. Include
            // our airborne fixtures explicitly; outside that box Newton does
            // not integrate them even when their active/enabled flags are set.
            cVector3f minimum=map->GetPhysicsWorld()->GetWorldSizeMin();
            cVector3f maximum=map->GetPhysicsWorld()->GetWorldSizeMax();
            std::printf("%s retail physics bounds before drawer fixtures: min=(%.6g %.6g %.6g) max=(%.6g %.6g %.6g)\n",
                role.c_str(),minimum.x,minimum.y,minimum.z,maximum.x,maximum.y,maximum.z);
            minimum.x=std::min(minimum.x,-10.0f);minimum.y=std::min(minimum.y,35.0f);minimum.z=std::min(minimum.z,-5.0f);
            maximum.x=std::max(maximum.x,10.0f);maximum.y=std::max(maximum.y,45.0f);maximum.z=std::max(maximum.z,5.0f);
            map->GetPhysicsWorld()->SetWorldSize(minimum,maximum);
            // Put the intact props above the level so existing room geometry
            // cannot obstruct the drawers or affect the measured movement.
            for(unsigned i=0;i<2;++i) {
                cMatrixf transform=cMatrixf::Identity;transform.SetTranslation(cVector3f(float(i)*4,40,0));
                map->CreateEntity(i?"codex_drawers_b":"codex_drawers_a",
                    "entities/furniture/chest_of_drawers_nice/chest_of_drawers_nice.ent",transform,1);
                cLuxProp_Object* prop=cabinet(map,i);
                if(!prop || prop->GetBodyNum()!=4) return fail(error,"retail chest did not load its four bodies");
                for(int j=0;j<prop->GetBodyNum();++j) prop->GetBody(j)->SetGravity(false);
            }
            mark(role+"-drawers-created.txt","ready");next();return 0;
        }
        cLuxProp_Object* prop=cabinet(map,trial);
        if(!prop) return fail(error,"retail drawer fixture disappeared");
        iPhysicsBody* drawer=prop->GetBodyFromID(trial?34:32);
        if(!drawer || drawer->GetJointNum()!=1 || drawer->GetJoint(0)->GetType()!=ePhysicsJointType_Slider)
            return fail(error,"retail drawer/slider IDs changed");
        cLuxMultiplayerWorld* world=mp->GetWorld();
        if(phase>=3 && SDL_GetTicks()-diagnosticAt>2000) {
            diagnosticAt=SDL_GetTicks();
            std::printf("%s drawer trial=%u phase=%u state=%d local-lease=%u pending=%u slider=%.6g displacement=%.6g\n",
                role.c_str(),trial,phase,int(gpBase->mpPlayer->GetCurrentState()),world->mlLocalLease,
                world->mlPendingRequest,drawer->GetJoint(0)->GetDistance(),
                cMath::Vector3Dist(drawer->GetWorldPosition(),initialDrawerPosition));
            std::fflush(stdout);
            if(gpBase->mpPlayer->GetCurrentState()==eLuxPlayerState_InteractSlide) {
                auto* slide=static_cast<cLuxPlayerState_InteractSlide*>(gpBase->mpPlayer->GetCurrentStateData());
                std::printf("drawer PID speed=%.9g input=(%.6g %.6g) last-input=(%.6g %.6g) force=(%.6g %.6g %.6g) camera=(%.6g %.6g) mass=%.6g active=%d enabled=%d\n",
                    slide->mfSlideSpeed,slide->mvMouseAdd.x,slide->mvMouseAdd.y,slide->mvLastMouseAdd.x,slide->mvLastMouseAdd.y,
                    slide->mvLastForce.x,slide->mvLastForce.y,slide->mvLastForce.z,gpBase->mpPlayer->GetCamera()->GetYaw(),
                    gpBase->mpPlayer->GetCamera()->GetPitch(),drawer->GetMass(),drawer->IsActive(),drawer->GetEnabled());
                std::fflush(stdout);
            }
        }
        if(phase==1) {
            if(!both("drawers-created.txt") || age<700) return 0;
            std::set<uint64_t> ids;
            for(unsigned i=0;i<2;++i) {
                cLuxProp_Object* check=cabinet(map,i);
                for(int j=0;j<check->GetBodyNum();++j) {
                    iPhysicsBody* body=check->GetBody(j);
                    if(body->GetName()!=check->GetBody(0)->GetName()) return fail(error,"fixture no longer has duplicate authored body names");
                    uint64_t id=LuxWorldWire::BodyId(body->GetName(),body->GetUniqueID());
                    if(!ids.insert(id).second) return fail(error,"same-named retail bodies share a network ID");
                    if(body->GetMass()>0) {
                        auto found=world->mBodies.find(id);
                        if(found==world->mBodies.end() || found->second.body!=body) return fail(error,"retail drawer was omitted or misbound in world index");
                        if(!host && !found->second.received) return 0;
                    }
                }
            }
            gpBase->mpPlayer->GetCharacterBody()->SetPosition(drawer->GetWorldPosition()+cVector3f(0,0,1.5f));
            gpBase->mpPlayer->GetCharacterBody()->SetYaw(0);
            gpBase->mpPlayer->GetCamera()->SetYaw(0);
            gpBase->mpPlayer->GetCamera()->SetPitch(0.7f);
            initialDrawerPosition=drawer->GetWorldPosition();
            mark(role+"-"+marker("indexed.txt"),"eight unique body IDs; six replicated drawers");next();return 0;
        }
        if(phase==2) {
            if(!both(marker("indexed.txt")) || age<700) return 0;
            if(actor) {
                if(!prop->CanInteract(drawer)) return fail(error,"retail drawer lost its native interaction");
                hold(true);
                prop->OnInteract(drawer,drawer->GetWorldPosition()+cVector3f(0,0,0.42f));
            }
            next();return 0;
        }
        if(phase==3) {
            if(actor) {
                if(gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_InteractSlide) return 0;
                for(int id=32;id<=34;++id)
                    if(!world->OwnsInteraction(prop->GetBodyFromID(id))) return fail(error,"slide did not lease all three drawers");
            } else {
                if(!world->IsInteractionOwnedByOther(drawer)) return 0;
                if(!attempted) {
                    attempted=true;
                    iPhysicsBody* sibling=prop->GetBodyFromID(33);
                    prop->OnInteract(sibling,sibling->GetWorldPosition());
                    if(gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_Normal || world->mlPendingRequest)
                        return fail(error,"another player started a competing drawer controller");
                }
            }
            mark(role+"-"+marker("leased.txt"),"real slide controller and exclusive cabinet ownership");
            if(!both(marker("leased.txt"))) return 0;
            next();return 0;
        }
        if(phase==4) {
            if(actor) {
                if(gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_InteractSlide)
                    return fail(error,"slide controller stopped before drawer moved");
                // Drive the same pitch input callback used by mouse movement.
                gpBase->mpPlayer->GetCharacterBody()->SetYaw(0);
                gpBase->mpPlayer->GetCamera()->SetYaw(0);
                gpBase->mpPlayer->GetCamera()->SetPitch(0.7f);
                const float projection=cMath::Vector3Dot(gpBase->mpPlayer->GetCamera()->GetUp(),drawer->GetJoint(0)->GetPinDir());
                if(std::fabs(projection)<0.1f) {
                    const cVector3f pin=drawer->GetJoint(0)->GetPinDir();
                    const cVector3f up=gpBase->mpPlayer->GetCamera()->GetUp();
                    std::printf("drawer camera projection=%.9g pitch=%.9g pin=(%.6g %.6g %.6g) up=(%.6g %.6g %.6g)\n",
                        projection,gpBase->mpPlayer->GetCamera()->GetPitch(),pin.x,pin.y,pin.z,up.x,up.y,up.z);
                    return fail(error,"test camera cannot drive the slider axis");
                }
                gpBase->mpPlayer->GetCurrentStateData()->OnAddPitch(0.02f/projection);
                if(age<500 || cMath::Vector3Dist(drawer->GetWorldPosition(),initialDrawerPosition)<0.15f) return 0;
                hold(false);gpBase->mpPlayer->ChangeState(eLuxPlayerState_Normal);
                mark(role+"-"+marker("released.txt"),"drawer moved at least 15 cm through native PID");
            }
            if(!exists((trial?"client-":"host-")+marker("released.txt"))) return 0;
            next();return 0;
        }
        if(phase==5) {
            if(age<800) return 0;
            if(world->IsEntityLeased(prop)) return 0;
            if(actor && !exists((trial?"client-":"host-")+marker("target.txt"))) {
                const cVector3f position=drawer->GetWorldPosition();
                FILE* file=NULL;fopen_s(&file,(outputDir+"/"+role+"-"+marker("target.txt")).c_str(),"wb");
                if(!file) return fail(error,"could not record drawer target");
                std::fprintf(file,"%.9g %.9g %.9g",position.x,position.y,position.z);std::fclose(file);
            }
            FILE* file=NULL;fopen_s(&file,(outputDir+"/"+(trial?"client-":"host-")+marker("target.txt")).c_str(),"rb");
            if(!file) return 0;
            cVector3f expected;const bool complete=std::fscanf(file,"%f %f %f",&expected.x,&expected.y,&expected.z)==3;
            std::fclose(file);
            if(!complete || cMath::Vector3Dist(drawer->GetWorldPosition(),expected)>0.06f) return 0;
            if(cMath::Vector3Dist(drawer->GetWorldPosition(),initialDrawerPosition)<0.15f)
                return fail(error,"opened drawer returned to its closed position");
            mark(role+"-"+marker("passed.txt"),"retail drawer motion replicated and assembly lease released");
            if(!both(marker("passed.txt"))) return 0;
            if(trial==0) {trial=1;phase=1;entered=SDL_GetTicks();return 0;}
            trial=0;phase=6;entered=SDL_GetTicks();return 0;
        }
        return 0;
    }
};
#endif
