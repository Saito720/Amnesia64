#ifndef MULTIPLAYER_DOOR_BREAK_REGRESSION_H
#define MULTIPLAYER_DOOR_BREAK_REGRESSION_H
#include "LuxProp_Object.h"

static void __stdcall CodexDoorMutationCallback(std::string& entity,std::string& event) {
    const unsigned count=++nativeCallbacks[entity+":"+event];
    if(event!="Break") return;
    cMatrixf transform=cMatrixf::Identity;transform.SetTranslation(cVector3f(float(count)*2,40,0));
    gpBase->mpMapHandler->GetCurrentMap()->CreateEntity("codex_door_callback_"+cString::ToString(int(count)),
        "entities/item/tinderbox/tinderbox.ent",transform,1);
}

static void __stdcall CodexObjectMutationCallback(std::string& entity,std::string& event) {
    ++nativeCallbacks[entity+":"+event];if(event!="Break") return;
    auto* map=gpBase->mpMapHandler->GetCurrentMap();
    // Run real script bindings inside the destruction callback. These typed
    // operations reach the client after ObjectBreak, before the host retires
    // the original object's ID at the end of its destruction callback.
    map->RunScript("CreateEntityAtArea(\"codex_object_callback\",\"entities/item/tinderbox/tinderbox.ent\",\"AreaGrunt\",false); "
        "SetPropHealth(\"codex_object_callback\",73); SetEntityInteractionDisabled(\"codex_object_callback\",true);");
    auto* created=map->GetEntityByName("codex_object_callback");
    if(created) mark("host-door-object-callback-id.txt",cString::ToString(created->GetID()));
}

// The retail Grunt attacks the retail crowbar door through its real animation
// marker, shape damage, prop health, and network replication paths. Only attack
// placement and damage are fixed to make each visible damage stage repeatable.
class cDoorBreakRegression : public iWorldEffectCallback {
    unsigned phase=0;
    Uint32 entered=0;
    bool acted=false;
    uint32_t epoch=0;
    uint64_t doorRuntime=0,debrisRuntime=0;
    cWorld* effectWorld=NULL;
    iWorldEffectCallback* previousEffectCallback=NULL;
    unsigned scriptSounds=0,scriptParticles=0;
    int objectSourceID=-1;
    tString lastCheck;
    void next() {++phase;entered=SDL_GetTicks();acted=false;}
    bool both(const char* suffix) const {return exists(tString("host-door-")+suffix) && exists(tString("client-door-")+suffix);}
    void done(const char* suffix) const {mark(role+"-door-"+suffix,"passed");}
    int fail(tString& error,const tString& message) const {
        restoreEffectObserver();
        if(auto* prop=door()) {
            auto* enemy=grunt();
            std::printf("%s door diagnostics: health=%.3f damage=%d broken=%d debris=%d gruntState=%d active=%d\n",
                role.c_str(),prop->GetHealth(),prop->GetCurrentDamageLevel(),prop->IsBroken(),prop->GetBrokenEntityID(),
                enemy?int(enemy->GetCurrentEnemyState()):-1,enemy?enemy->IsActive():0);
            std::fflush(stdout);
        }
        if(auto* prop=scriptDoor()) {
            std::printf("%s scripted door diagnostics: health=%.3f damage=%d broken=%d debris=%d callbacks=%u sounds=%u particles=%u\n",
                role.c_str(),prop->GetHealth(),prop->GetCurrentDamageLevel(),prop->IsBroken(),prop->GetBrokenEntityID(),
                nativeCallbacks["codex_script_door:Break"],scriptSounds,scriptParticles);
            std::fflush(stdout);
        }
        error="door break phase "+cString::ToString(int(phase))+": "+message;return -1;
    }
    cLuxProp_SwingDoor* door() const {
        auto* map=gpBase->mpMapHandler->GetCurrentMap();
        return map?static_cast<cLuxProp_SwingDoor*>(map->GetEntityByName("mansion_1",eLuxEntityType_Prop,eLuxPropType_SwingDoor)):NULL;
    }
    iLuxEnemy* grunt() const {
        auto* map=gpBase->mpMapHandler->GetCurrentMap();
        return map?static_cast<iLuxEnemy*>(map->GetEntityByName("grunt_normal_1",eLuxEntityType_Enemy)):NULL;
    }
    bool ready(const char* name="10_daniels_room") const {
        auto* session=gpBase->mpMultiplayer;auto* map=gpBase->mpMapHandler->GetCurrentMap();
        return session->IsReady() && !session->GetWorld()->GetRemotePlayers().empty() && map && map->GetName()==name;
    }
    void parkPlayer() const {
        auto* character=gpBase->mpPlayer->GetCharacterBody();
        character->StopMovement();character->SetGravityActive(false);character->SetFeetPosition(cVector3f(0,100,0));
    }
    bool attack(float damage,tString& error) {
        auto* enemy=grunt();auto* prop=door();
        auto* slab=prop?prop->GetBodyFromID(10):NULL;
        if(!enemy || !slab) {error="retail Grunt or door slab is missing";return false;}
        enemy->SetActive(true);enemy->SetDisabled(false);enemy->SetSanityDecreaseActive(false);
        enemy->SetDisableTriggers(true);enemy->ClearPatrolNodes();
        // Keep the parked players in activation range as their network poses
        // settle. Trigger suppression does not block PlayerOutOfRange, which
        // would deactivate the Grunt before its native attack marker fires.
        enemy->mfActivationDistance=1000;enemy->mbPlayerInRange=true;
        enemy->mlstMessages.clear();
        enemy->mfForwardSpeed=enemy->mfBackwardSpeed=0;
        for(int pose=0;pose<eLuxEnemyPoseType_LastEnum;++pose)
            for(int speed=0;speed<eLuxEnemyMoveSpeed_LastEnum;++speed)
                enemy->mfDefaultForwardSpeed[pose][speed]=enemy->mfDefaultBackwardSpeed[pose][speed]=0;
        auto* body=enemy->GetCharacterBody();
        body->StopMovement();body->SetGravityActive(false);
        body->SetPosition(slab->GetWorldPosition()+cVector3f(0.15f,0,0));
        body->SetActive(false);
        enemy->mvTempPos=slab->GetWorldPosition();enemy->mlStuckDoorID=prop->GetID();
        enemy->mNormalAttackSize.mvOffset=0;
        if(enemy->mNormalAttackSize.mlShapeIdx>=0) {
            // Keep the enemy-owned shape lifecycle unchanged. A small attack
            // box touches the slab without also hitting its separate hinge.
            auto* shape=gpBase->mpMapHandler->GetCurrentMap()->GetPhysicsWorld()->CreateBoxShape(cVector3f(0.5f),NULL);
            enemy->mvAttackShapes.push_back(shape);
            enemy->mNormalAttackSize.mlShapeIdx=int(enemy->mvAttackShapes.size()-1);
        }
        enemy->mBreakDoorAttackDamage.mfMinDamage=enemy->mBreakDoorAttackDamage.mfMaxDamage=damage;
        enemy->mBreakDoorAttackDamage.mfForce=enemy->mBreakDoorAttackDamage.mfMaxImpulse=0;
        enemy->mBreakDoorAttackDamage.mbCheckPlayer=false;
        enemy->mBreakDoorAttackDamage.mbCheckProps=true;
        enemy->ChangeState(eLuxEnemyState_Wait);
        enemy->OnUpdate(gpBase->mpEngine->GetStepSize());
        enemy->ChangeState(eLuxEnemyState_BreakDoor);
        return true;
    }
    bool inspectVisuals(cLuxProp_SwingDoor* prop,tString& error,int damageLevel,bool broken) const {
        if(!prop || !prop->GetMeshEntity()) {error="door visual fixture missing";return false;}
        cMeshEntity* meshes[3]={prop->GetMeshEntity(),NULL,NULL};
        auto meshIt=gpBase->mpMapHandler->GetCurrentMap()->GetWorld()->GetDynamicMeshEntityIterator();
        while(meshIt.HasNext()) {
            auto* mesh=meshIt.Next();
            for(int stage=1;stage<3;++stage)
                if(mesh->GetName()==prop->GetName()+"damage"+cString::ToString(stage)) meshes[stage]=mesh;
        }
        for(int stage=0;stage<3;++stage) {
            auto* mesh=meshes[stage];
            const bool selected=prop->IsActive() && stage==damageLevel;
            if(!mesh || mesh->IsVisible()!=selected || mesh->IsActive()!=selected) {
                error="door damage mesh selection disagrees at stage "+cString::ToString(stage);return false;
            }
            unsigned renderableCount=0;
            for(int sub=0;sub<mesh->GetSubMeshEntityNum();++sub) {
                auto* piece=mesh->GetSubMeshEntity(sub);
                if(piece->GetSubMesh()->IsCollideShape()) continue;
                ++renderableCount;
                bool originalLeaf=false;
                if(stage==0) for(int body=0;body<prop->GetBodyNum();++body)
                    if(prop->GetBody(body)->GetMass()!=0 && piece->GetEntityParent()==prop->GetBody(body)) originalLeaf=true;
                const bool visible=selected && !(broken && originalLeaf);
                // The renderer registers each submesh separately. A hidden
                // mesh wrapper does not prevent an explicitly visible child
                // from overlaying the selected damage mesh.
                if(piece->IsVisible()!=visible) {
                    error="door renderable visibility disagrees: stage="+cString::ToString(stage)+
                        " piece="+piece->GetName()+" expected="+cString::ToString(int(visible));return false;
                }
            }
            if(!renderableCount) {error="door damage mesh has no renderable pieces";return false;}
        }
        return true;
    }
    bool replayVisualState(cLuxProp_SwingDoor* prop,tString& error) const {
        const float health=prop->GetHealth();
        const int damageLevel=prop->GetCurrentDamageLevel();const bool broken=prop->IsBroken();
        const bool disableBreakable=prop->GetDisableBreakable();const int brokenEntityID=prop->GetBrokenEntityID();
        const auto apply=[&]() {prop->ApplyNetworkState(health,damageLevel,broken,disableBreakable,brokenEntityID);};
        if(role=="client") for(int repeat=0;repeat<2;++repeat) {
            apply();
            if(!inspectVisuals(prop,error,damageLevel,broken)) return false;
        }
        prop->SetActive(false);
        if(!inspectVisuals(prop,error,damageLevel,broken)) return false;
        if(role=="client") {
            apply();
            if(!inspectVisuals(prop,error,damageLevel,broken)) return false;
        }
        prop->SetActive(true);
        if(!inspectVisuals(prop,error,damageLevel,broken)) return false;
        if(role=="client") {
            apply();
            if(!inspectVisuals(prop,error,damageLevel,broken)) return false;
        }
        return true;
    }
    bool inspect(tString& error,int damageLevel,bool broken,bool retainRuntime=false,bool expectDebris=true) {
        auto* prop=door();auto* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!prop || !prop->GetMeshEntity()) {error="retail mansion_1 door missing";return false;}
        if(prop->GetCurrentDamageLevel()!=damageLevel || prop->IsBroken()!=broken) {
            error="native door damage level or broken flag disagrees";return false;
        }
        if(!inspectVisuals(prop,error,damageLevel,broken)) return false;
        auto* slab=prop->GetBodyFromID(10);auto* slabMesh=prop->GetMeshEntity()->GetSubMeshEntity(0);
        auto* hinges=prop->GetMeshEntity()->GetSubMeshEntity(1);
        if(!slab || !slabMesh || !hinges || (broken && (slab->IsActive() || slabMesh->IsVisible() || slabMesh->IsActive() || !hinges->IsVisible()))) {
            error="broken door slab remains visible/collidable, or its surviving hinges disappeared";return false;
        }
        unsigned count=0;iLuxEntity* debris=NULL;
        auto it=map->GetEntityIterator();
        while(it.HasNext()) {
            auto* entity=it.Next();
            if(entity->GetName()=="mansion_1_broken" && !entity->GetDestroyMe()) {++count;debris=entity;}
        }
        if(count!=((broken && expectDebris)?1u:0u) || (broken && expectDebris &&
           (!debris || debris->GetID()!=prop->GetBrokenEntityID() || !debris->GetBodyNum()))) {
            error="native broken-door debris is missing, duplicated, or has the wrong identity";return false;
        }
        if(nativeCallbacks["mansion_1:Break"]!=((role=="host" && broken)?1u:0u)) {
            error="break callback was missing or replayed outside the host";return false;
        }
        if(retainRuntime && (doorRuntime!=prop->GetRuntimeID() || (debris && debrisRuntime!=debris->GetRuntimeID()))) {
            error="repeated baseline replaced a live door or debris instance";return false;
        }
        doorRuntime=prop->GetRuntimeID();debrisRuntime=debris?debris->GetRuntimeID():0;
        return true;
    }
    cLuxProp_SwingDoor* scriptDoor() const {
        auto* map=gpBase->mpMapHandler->GetCurrentMap();
        return map?static_cast<cLuxProp_SwingDoor*>(map->GetEntityByName("codex_script_door",eLuxEntityType_Prop,eLuxPropType_SwingDoor)):NULL;
    }
    bool scriptedEffects(unsigned expected) const {
        if(scriptSounds!=expected || scriptParticles!=expected) return false;
        // Typed script replay creates these effects once in each native world.
        // They must not also enter the separate incidental-effect stream.
        auto* service=gpBase->mpMultiplayer->GetEffects();
        for(const auto& entry:service->mLocal)
            if(entry.second.effect.name=="codex_script_door_BreakSound" || entry.second.effect.name=="codex_script_door_BreakPS") return false;
        for(const auto& entry:service->mRemote)
            if(entry.second.effect.name=="codex_script_door_BreakSound" || entry.second.effect.name=="codex_script_door_BreakPS") return false;
        return true;
    }
    void restoreEffectObserver() const {
        if(effectWorld && gpBase->mpEngine->GetScene()->WorldExists(effectWorld) && effectWorld->GetEffectCallback()==this)
            effectWorld->SetEffectCallback(previousEffectCallback);
    }
    static int callbackID() {
        FILE* file=NULL;fopen_s(&file,(outputDir+"/host-door-object-callback-id.txt").c_str(),"rb");
        if(!file) return -1;
        int id=-1;const int count=fscanf_s(file,"%d",&id);fclose(file);return count==1?id:-1;
    }
    bool inspectObjectCallback(tString& error) const {
        auto* map=gpBase->mpMapHandler->GetCurrentMap();const int expected=callbackID();
        if(!map || expected<0) {error="object callback has not recorded its host ID";return false;}
        unsigned count=0;iLuxProp* created=NULL;auto it=map->GetEntityIterator();
        while(it.HasNext()) {
            auto* entity=it.Next();if(entity->GetName()!="codex_object_callback" || entity->GetDestroyMe()) continue;
            ++count;if(entity->GetEntityType()==eLuxEntityType_Prop) created=static_cast<iLuxProp*>(entity);
        }
        if(count!=1 || !created || created->GetID()!=expected || expected==objectSourceID ||
           created->GetHealth()!=73 || !created->GetInteractionDisabled()) {
            error="callback prop ID, count, health73 or interaction-disabled state disagrees after native object destruction";return false;
        }
        if(nativeCallbacks["codex_object_source:Break"]!=(role=="host"?1u:0u)) {
            error="native object destruction replayed or lost its host callback";return false;
        }
        auto* source=map->GetEntityByName("codex_object_source");
        if(source && !source->GetDestroyMe()) {error="native broken source object survived";return false;}
        return true;
    }
public:
    void OnSoundCreated(cWorld* world,cSoundEntity* sound,iPhysicsBody* source) {
        if(previousEffectCallback) previousEffectCallback->OnSoundCreated(world,sound,source);
        if(sound->GetName()=="codex_script_door_BreakSound") ++scriptSounds;
    }
    void OnParticleCreated(cWorld* world,cParticleSystem* particle,const tString& asset,const cVector3f& size,iPhysicsBody* source) {
        if(previousEffectCallback) previousEffectCallback->OnParticleCreated(world,particle,asset,size,source);
        if(particle->GetName()=="codex_script_door_BreakPS") ++scriptParticles;
    }
    int Update(tString& error) {
        auto* session=gpBase->mpMultiplayer;const bool host=role=="host";
        const Uint32 age=SDL_GetTicks()-entered;
        if(phase && age>30000) return fail(error,"timed out: "+session->GetStatus()+"; last check: "+lastCheck);
        if(phase==0) {
            if(host) {
                cLuxMultiplayerSettings settings;settings.map="maps/main/ch01/10_daniels_room.map";
                settings.port=port;settings.useSteam=false;settings.maxPlayers=2;
                if(!session->Host(settings)) return fail(error,session->GetStatus());
                done("listening.txt");
            } else {
                if(!exists("host-door-listening.txt")) return 0;
                if(!session->Join("127.0.0.1:"+cString::ToString(int(port)))) return fail(error,session->GetStatus());
            }
            next();return 0;
        }
        if(phase==1) {
            if(!ready()) return 0;
            parkPlayer();
            if(!gpBase->mpEngine->GetSystem()->GetLowLevel()->AddScriptFunc(
                "void CodexNativeCallback(string &in asEntity, string &in asEvent)",(void*)CodexNativeCallback))
                return fail(error,"break callback observer registration failed");
            if(!door() || !grunt()) return fail(error,"retail crowbar-door encounter fixtures missing");
            if(host) {door()->SetCallbackFunc("CodexNativeCallback");grunt()->SetActive(false);}
            if(!inspect(error,0,false) || std::fabs(door()->GetHealth()-100)>0.01f) return fail(error,"initial door state: "+error);
            epoch=session->GetMapEpoch();done("initial.txt");next();return 0;
        }
        if(phase>=2 && phase<=4) {
            if(!ready()) return fail(error,"session ended during native enemy attacks");
            parkPlayer();
            const char* prior=phase==2?"initial.txt":phase==3?"damage1.txt":"damage2.txt";
            if(!both(prior)) return 0;
            const float target=phase==2?65.0f:phase==3?30.0f:-10.0f;
            if(host && !acted) {
                if(!attack(phase==4?40.0f:35.0f,error)) return fail(error,error);
                std::printf("%s door attack started: phase=%u state=%d active=%d health=%.3f\n",
                    role.c_str(),phase,int(grunt()->GetCurrentEnemyState()),grunt()->IsActive(),door()->GetHealth());
                std::fflush(stdout);
                acted=true;
            }
            if(door()->GetHealth()>target+0.01f) return 0;
            if(host) grunt()->SetActive(false);
            if(std::fabs(door()->GetHealth()-target)>0.01f) return fail(error,"one Grunt strike damaged the door more than once");
            if(!inspect(error,phase==4?0:int(phase)-1,phase==4)) {lastCheck=error;return 0;}
            if(!replayVisualState(door(),error)) return fail(error,error);
            if(!host && phase<4) {
                // A repair snapshot must restore the original leaf even when
                // the entity's active flag never changes, then allow damage
                // to select its replacement mesh again.
                auto* prop=door();const float health=prop->GetHealth();
                const int damageLevel=prop->GetCurrentDamageLevel();
                prop->ApplyNetworkState(100,0,false,prop->GetDisableBreakable(),-1);
                if(!inspectVisuals(prop,error,0,false))
                    return fail(error,"intact repair snapshot: "+error);
                prop->ApplyNetworkState(health,damageLevel,false,prop->GetDisableBreakable(),-1);
                if(!inspectVisuals(prop,error,damageLevel,false))
                    return fail(error,"damage after repair snapshot: "+error);
            }
            done(phase==2?"damage1.txt":phase==3?"damage2.txt":"broken.txt");
            printStatus(phase==4?"Grunt break hid the door slab, retained hinges, and replicated one debris prop":"Grunt strike replicated the retail door's damaged mesh");
            next();return 0;
        }
        if(phase==5) {
            parkPlayer();if(!both("broken.txt")) return 0;
            if(host && !acted) {
                if(!session->GetEntities()->SendInitialState(session->GetWorld()->GetRemotePlayers().begin()->first))
                    return fail(error,session->GetEntities()->GetLastError());
                done("baseline-sent.txt");acted=true;entered=SDL_GetTicks();return 0;
            }
            if(!exists("host-door-baseline-sent.txt")) return 0;
            if(!acted) {acted=true;entered=SDL_GetTicks();return 0;}
            if(age<1000) return 0;
            if(!inspect(error,0,true,true)) return fail(error,error);
            done("baseline.txt");next();return 0;
        }
        if(phase==6) {
            if(!both("baseline.txt")) return 0;
            // Retail mansion_broken.ent deliberately expires after four
            // seconds. A reconnect must keep the slab broken without bringing
            // its already-expired debris back into the map.
            if(age<5500) return 0;
            if(!inspect(error,0,true,false,false)) {lastCheck=error;return 0;}
            done("expired.txt");if(!both("expired.txt")) return 0;
            if(!host) session->Stop("Door regression reconnect.");
            next();return 0;
        }
        if(phase==7) {
            if(host) {
                parkPlayer();
                if(!session->IsHost()) return fail(error,"client disconnect stopped the host");
                if(!session->mPeers.empty() || !session->GetWorld()->GetRemotePlayers().empty()) return 0;
                done("disconnected.txt");
            } else {
                if(!exists("host-door-disconnected.txt") || session->IsActive() || gpBase->mpMapHandler->GetCurrentMap()) return 0;
                if(!session->Join("127.0.0.1:"+cString::ToString(int(port)))) return fail(error,session->GetStatus());
            }
            next();return 0;
        }
        if(phase==8) {
            if(!ready()) return 0;
            parkPlayer();if(!inspect(error,0,true,host,false)) {lastCheck=error;return 0;}
            done("rejoined.txt");next();return 0;
        }
        if(phase==9) {
            if(!both("rejoined.txt")) return 0;
            if(host) gpBase->mpMapHandler->ChangeMap("11_study.map","PlayerStartArea_1","","");
            next();return 0;
        }
        if(phase==10) {
            if(exists(role+"-door-away.txt")) {
                if(!both("away.txt")) return 0;
                if(host) gpBase->mpMapHandler->ChangeMap("10_daniels_room.map","PlayerStartArea_1","","");
                next();return 0;
            }
            if(!ready("11_study") || session->GetMapEpoch()<=epoch) return 0;
            parkPlayer();epoch=session->GetMapEpoch();done("away.txt");return 0;
        }
        if(phase==11) {
            if(exists(role+"-door-revisited.txt")) {
                if(!both("revisited.txt")) return 0;
                printStatus("Broken door state and unique native debris survived baseline, reconnect, and saved-map revisit");
                next();return 0;
            }
            if(!ready() || session->GetMapEpoch()<=epoch) return 0;
            parkPlayer();if(!inspect(error,0,true,false,false)) {lastCheck=error;return 0;}
            done("revisited.txt");return 0;
        }
        if(phase==12) {
            parkPlayer();
            if(!acted) {
                if(!gpBase->mpEngine->GetSystem()->GetLowLevel()->AddScriptFunc(
                    "void CodexDoorMutationCallback(string &in asEntity, string &in asEvent)",(void*)CodexDoorMutationCallback))
                    return fail(error,"mutating callback observer registration failed");
                // Observe registered creation events, forwarding every event to
                // production. Counts survive short lifetimes and inaudibility
                // without changing the actual effects or their removal rules.
                effectWorld=gpBase->mpMapHandler->GetCurrentMap()->GetWorld();
                previousEffectCallback=effectWorld->GetEffectCallback();effectWorld->SetEffectCallback(this);
                if(host) {
                    cMatrixf transform=cMatrixf::Identity;transform.SetTranslation(cVector3f(0,40,0));
                    auto* map=gpBase->mpMapHandler->GetCurrentMap();
                    map->CreateEntity("codex_script_door","entities/door/mansion/mansion.ent",transform,1);
                    auto* prop=scriptDoor();if(!prop) return fail(error,"script-health fixture creation failed");
                    prop->SetCallbackFunc("CodexDoorMutationCallback");
                    for(int i=0;i<prop->GetBodyNum();++i) {prop->GetBody(i)->SetGravity(false);prop->GetBody(i)->SetCollide(false);}
                }
                acted=true;
            }
            if(!scriptDoor()) return 0;
            if(scriptDoor()->IsBroken() || scriptDoor()->GetCurrentDamageLevel()!=0 || scriptDoor()->GetHealth()!=100)
                return fail(error,"created script-health door already damaged");
            done("script-ready.txt");next();return 0;
        }
        if(phase==13) {
            parkPlayer();if(!both("script-ready.txt")) return 0;
            if(host && !acted) {
                gpBase->mpMapHandler->GetCurrentMap()->RunScript("SetPropHealth(\"codex_script_door\",65);");acted=true;
            }
            auto* prop=scriptDoor();
            if(!prop || prop->GetHealth()!=65 || prop->GetCurrentDamageLevel()!=1 || !scriptedEffects(1)) {
                lastCheck="scripted damage snapshot or native sound/particle replication missing";return 0;
            }
            if(!inspectVisuals(prop,error,1,false)) return fail(error,error);
            if(!replayVisualState(prop,error)) return fail(error,error);
            if(nativeCallbacks["codex_script_door:Break"]!=0)
                return fail(error,"scripted damage produced the wrong visual or break callback");
            done("script-damaged.txt");next();return 0;
        }
        if(phase==14) {
            parkPlayer();if(!both("script-damaged.txt")) return 0;
            auto* map=gpBase->mpMapHandler->GetCurrentMap();
            if(host && !acted) {
                // Both breaks happen before another normal replication tick.
                // The first callback allocates a prop between the debris IDs;
                // the reset removes the first debris before its replacement.
                map->RunScript("SetPropHealth(\"codex_script_door\",0); ResetProp(\"codex_script_door\");");
                map->FlushNetworkEntityDestruction();
                map->RunScript("SetPropHealth(\"codex_script_door\",0);");acted=true;
            }
            auto* prop=scriptDoor();if(!prop || !prop->IsBroken()) return 0;
            unsigned count=0;iLuxEntity* debris=NULL;auto it=map->GetEntityIterator();
            while(it.HasNext()) {auto* entity=it.Next();if(entity->GetName()=="codex_script_door_broken" && !entity->GetDestroyMe()) {++count;debris=entity;}}
            if(count!=1 || !debris || debris->GetID()!=prop->GetBrokenEntityID() ||
               !map->GetEntityByName("codex_door_callback_1") || !map->GetEntityByName("codex_door_callback_2")) {
                lastCheck="rapid-break debris identity/count or callback-created props disagree";return 0;
            }
            if(prop->GetMeshEntity()->GetSubMeshEntity(0)->IsVisible() ||
               nativeCallbacks["codex_script_door:Break"]!=(host?2u:0u))
                return fail(error,"rapid scripted break/reset duplicated callbacks or restored the intact slab");
            if(!scriptedEffects(3)) return fail(error,"scripted break effects were lost or duplicated");
            done("script-broken.txt");next();return 0;
        }
        if(phase==15) {
            if(!both("script-broken.txt")) return 0;
            if(host && !acted) {
                gpBase->mpMapHandler->GetCurrentMap()->RunScript("ResetProp(\"codex_script_door\");");acted=true;
            }
            auto* prop=scriptDoor();
            if(!prop || prop->IsBroken() || prop->GetHealth()!=100 || prop->GetCurrentDamageLevel()!=0) return 0;
            if(!inspectVisuals(prop,error,0,false) || !replayVisualState(prop,error)) return fail(error,error);
            if(!scriptedEffects(3) || nativeCallbacks["codex_script_door:Break"]!=(host?2u:0u))
                return fail(error,"door repair replayed break callbacks or effects");
            done("script-repaired.txt");if(!both("script-repaired.txt")) return 0;
            restoreEffectObserver();
            printStatus("Door damage, repeated snapshots, activation, and repair retained exclusive rendered meshes and host-only break callbacks");
            next();return 0;
        }
        if(phase==16) {
            parkPlayer();if(age<5000) return 0;
            auto* map=gpBase->mpMapHandler->GetCurrentMap();
            if(!acted) {
                if(!gpBase->mpEngine->GetSystem()->GetLowLevel()->AddScriptFunc(
                    "void CodexObjectMutationCallback(string &in asEntity, string &in asEvent)",(void*)CodexObjectMutationCallback))
                    return fail(error,"native object callback registration failed");
                if(host) {
                    objectSourceID=0;while(map->GetEntityByID(objectSourceID)) ++objectSourceID;
                    cMatrixf transform=cMatrixf::Identity;transform.SetTranslation(cVector3f(4,40,0));
                    map->CreateEntity("codex_object_source","entities/container/vase01/vase01.ent",transform,1);
                    auto* source=static_cast<iLuxProp*>(map->GetEntityByName("codex_object_source",eLuxEntityType_Prop,eLuxPropType_Object));
                    if(!source || source->GetID()!=objectSourceID) return fail(error,"native object did not receive the lowest free ID");
                    source->SetCallbackFunc("CodexObjectMutationCallback");
                    for(int i=0;i<source->GetBodyNum();++i) {source->GetBody(i)->SetGravity(false);source->GetBody(i)->SetCollide(false);}
                }
                acted=true;
            }
            auto* source=map->GetEntityByName("codex_object_source",eLuxEntityType_Prop,eLuxPropType_Object);
            if(!source) return 0;
            objectSourceID=source->GetID();done("object-source.txt");next();return 0;
        }
        if(phase==17) {
            parkPlayer();if(!both("object-source.txt")) return 0;
            if(host) {
                auto* source=static_cast<cLuxProp_Object*>(gpBase->mpMapHandler->GetCurrentMap()->GetEntityByName(
                    "codex_object_source",eLuxEntityType_Prop,eLuxPropType_Object));
                if(!source) return fail(error,"native object disappeared before its damage trigger");
                source->GiveDamage(1000,10);
            }
            next();return 0;
        }
        if(phase==18) {
            parkPlayer();if(!inspectObjectCallback(error)) {lastCheck=error;return 0;}
            done("object-callback.txt");next();return 0;
        }
        if(phase==19) {
            parkPlayer();if(!both("object-callback.txt") || age<2000) return 0;
            if(!inspectObjectCallback(error)) return fail(error,error);
            done("object-settled.txt");next();return 0;
        }
        if(phase==20) {
            if(!both("object-settled.txt")) return 0;
            printStatus("Native object break preserved the callback-created prop's host ID, unique identity, health73 and disabled interaction");
            if(host) session->Stop("Door regression complete.");
            next();return 0;
        }
        if(phase==21) return session->IsActive()?0:1;
        return 0;
    }
};
#endif
