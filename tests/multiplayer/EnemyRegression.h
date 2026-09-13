#ifndef MULTIPLAYER_ENEMY_REGRESSION_H
#define MULTIPLAYER_ENEMY_REGRESSION_H
#include "EnemyProtocolRegression.h"
#include "LuxEnemyPathfinder.h"
#include "LuxMapHelper.h"
#include "scene/LightPoint.h"

// Real retail enemies, native sensing/state machines and animation attack
// markers run on the host. File markers synchronize test stages, never supply
// authoritative enemy state or damage to the other instance.
class cEnemyRegression {
    unsigned phase=0,attackTrial=0;
    Uint32 entered=0,diagnosticAt=0;
    bool acted=false,attackStarted=false;
    cLuxMap* installed=NULL;
    iPhysicsBody* wall=NULL;
    uint32_t clientPeer=0,epoch=0;
    int hitCount=0;
    cVector3f remembered=0;
    std::vector<uint8_t> oldState;
    tString pendingScreenshot;
    void next() {++phase;entered=SDL_GetTicks();acted=false;attackStarted=false;}
    bool both(const tString& suffix) const {return exists("host-enemy-"+suffix) && exists("client-enemy-"+suffix);}
    void done(const tString& suffix) const {mark(role+"-enemy-"+suffix,"passed");}
    int fail(tString& error,const tString& message) const {
        error="enemy phase "+cString::ToString(int(phase))+": "+message;return -1;
    }
    iLuxEnemy* find(const char* name) const {
        auto* map=gpBase->mpMapHandler->GetCurrentMap();
        return map?static_cast<iLuxEnemy*>(map->GetEntityByName(name,eLuxEntityType_Enemy)):NULL;
    }
    void placePlayer(const cVector3f& feet) const {
        auto* character=gpBase->mpPlayer->GetCharacterBody();
        character->StopMovement();character->SetGravityActive(false);character->SetFeetPosition(feet);
        character->SetYaw(kPif);gpBase->mpPlayer->GetCamera()->SetYaw(kPif);
        gpBase->mpPlayer->GetCamera()->SetPitch(0);
    }
    void anchor(iLuxEnemy* enemy,const cVector3f& feet) const {
        enemy->GetCharacterBody()->StopMovement();enemy->GetCharacterBody()->SetFeetPosition(feet);
        enemy->GetCharacterBody()->SetYaw(0);
    }
    void stationary(iLuxEnemy* enemy) const {
        // Freeze only locomotion for deterministic LOS/attack stages. Sensing,
        // decisions, animation markers, damage and all replication remain native.
        enemy->mfForwardSpeed=enemy->mfBackwardSpeed=0;
        for(int pose=0;pose<eLuxEnemyPoseType_LastEnum;++pose)
            for(int speed=0;speed<eLuxEnemyMoveSpeed_LastEnum;++speed)
                enemy->mfDefaultForwardSpeed[pose][speed]=enemy->mfDefaultBackwardSpeed[pose][speed]=0;
    }
    bool install(tString& error) {
        auto* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!map) {error="fixture map unavailable";return false;}
        if(installed==map) return true;
        installed=map;
        auto* physics=map->GetPhysicsWorld();
        // Native map setup shrinks broadphase bounds around the authored rooms.
        // The isolated encounter platform is deliberately above those rooms.
        physics->SetWorldSize(cVector3f(-300),cVector3f(300));
        auto* light=map->GetWorld()->CreateLightPoint("CodexEnemyFixtureLight","",false);
        light->SetPosition(cVector3f(0,45,-4));light->SetRadius(90);light->SetDiffuseColor(cColor(1,1,1,1));
        auto* floor=physics->CreateBody("CodexEnemyFloor",physics->CreateBoxShape(cVector3f(160,1,100),NULL));
        floor->SetMass(0);floor->SetPosition(cVector3f(0,39.5f,0));
        wall=physics->CreateBody("CodexEnemyOccluder",physics->CreateBoxShape(cVector3f(36,8,1),NULL));
        wall->SetMass(0);wall->SetPosition(cVector3f(0,43,-4));wall->SetActive(false);
        gpBase->mbHardMode=false;gpBase->mpPlayer->SetHealth(100);gpBase->mpPlayer->SetSanity(100);
        gpBase->mpPlayer->GetHelperDeath()->SetShowHint(false);
        gpBase->mpEngine->GetUpdater()->SetContainer("Default");
        gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
        for(const char* name: {"codex_enemy_grunt","codex_enemy_brute","codex_enemy_water"}) {
            auto* enemy=find(name);
            if(!enemy || !enemy->GetPathFinder()->GetNodeContainer() || enemy->GetPathFinder()->GetNodeContainer()->GetNodeNum()<60) {
                error=tString("authored enemy/navigation fixture missing: ")+name;return false;
            }
            if(gpBase->mpMultiplayer->IsHost()) {
                if(enemy==find("codex_enemy_grunt")) {
                    // Hosting an existing encounter can inherit a local fear
                    // source before the enemy has selected a session target.
                    enemy->SetActive(true);gpBase->mpPlayer->AddTerrorEnemy(enemy);
                    gpBase->mpPlayer->SetTerror(0);
                }
                enemy->SetActive(false);enemy->SetSanityDecreaseActive(false);
                enemy->mNormalAttackDamage.mfMinDamage=enemy->mNormalAttackDamage.mfMaxDamage=13;
                enemy->mNormalAttackDamage.mfForce=enemy->mNormalAttackDamage.mfMaxImpulse=0;
                enemy->mbSkipVisibilityRangeHandicaps=true;
            }
        }
        return true;
    }
    bool clientReplicaInvariant(tString& error) const {
        if(role!="client") return true;
        for(const char* name: {"codex_enemy_grunt","codex_enemy_brute","codex_enemy_water"}) {
            auto* enemy=find(name);if(!enemy) continue;
            if(enemy->GetCharacterBody()->IsActive() || !enemy->mPlayerAwareness.empty() || !enemy->mlstMessages.empty()) {
                error=tString("client ran a local enemy controller, senses or state-message queue: ")+name;return false;
            }
        }
        return true;
    }
    bool ordering(tString& error,const LuxEnemyWire::State& baseline) const {
        auto* service=gpBase->mpMultiplayer->GetEnemies();
        LuxEnemyWire::State state=baseline;state.name="codex_enemy_packet_ordering";
        state.generation=20;state.sequence=100;
        state.flags&=~LuxEnemyWire::Baseline;
        bool valid=true;
        const auto require=[&](bool condition,const char* message) {if(!condition && valid) {error=message;valid=false;}};
        const auto send=[&]() {return service->HandleMessage(0,LuxEnemyWire::EncodeState(state));};
        require(send(),"initial pending enemy snapshot rejected");
        require(!service->mReplicas[state.name].confirmed,"unreliable enemy pose confirmed entity creation");
        state.sequence=99;state.flags|=LuxEnemyWire::Baseline;
        require(send(),"overtaken reliable enemy baseline rejected");
        require(service->mReplicas[state.name].confirmed && service->mReplicas[state.name].state.sequence==100,
            "overtaken reliable baseline failed to confirm without rewinding the newer pose");
        state.flags&=~LuxEnemyWire::Baseline;
        state.generation=19;state.sequence=102;require(send(),"old generation was treated as malformed");
        require(service->mReplicas[state.name].state.generation==20 && service->mReplicas[state.name].state.sequence==100,
            "later sequence resurrected an old enemy generation");
        state.generation=21;state.sequence=101;require(send(),"new enemy generation rejected");
        LuxEnemyWire::Removed removed;removed.epoch=state.epoch;removed.name=state.name;removed.generation=21;removed.sequence=102;
        require(service->HandleMessage(0,LuxEnemyWire::EncodeRemoved(removed)),"enemy removal rejected");
        state.sequence=103;require(send(),"delayed pre-removal snapshot treated as malformed");
        require(service->mReplicas[state.name].removed,"delayed snapshot resurrected a removed enemy");
        state.generation=22;state.sequence=104;require(send(),"replacement enemy generation rejected");
        require(!service->mReplicas[state.name].removed && service->mReplicas[state.name].state.generation==22,
            "old tombstone removed replacement enemy");
        auto truncated=LuxEnemyWire::EncodeState(state);truncated.pop_back();
        require(!service->HandleMessage(0,truncated),"malformed enemy packet accepted");
        require(!service->HandleMessage(99,LuxEnemyWire::EncodeState(state)),"non-host enemy snapshot accepted");
        require(service->mReplicas[state.name].state.sequence==104,"rejected enemy packet changed live decoder state");
        // This pending name deliberately has no native entity and never survives
        // to an update or send. Only production packet ordering is under test.
        service->mReplicas.erase(state.name);
        // An unreliable replacement can beat the reliable script that still
        // needs this old instance's transform. Keep the old allocation alive
        // until that ordered script produces a new runtime identity.
        auto* native=find(baseline.name.c_str());
        const auto original=service->mReplicas.at(baseline.name);
        if(native) {
            const uint64_t runtime=native->GetRuntimeID();
            state=baseline;state.generation+=1;state.sequence+=2;state.flags&=~LuxEnemyWire::Baseline;
            require(send(),"early unreliable replacement pose rejected");
            require(!native->GetDestroyMe() && service->mReplicas[state.name].waitingRuntime==runtime &&
                !service->mReplicas[state.name].confirmed,"early replacement destroyed the script's old source entity");
            state.sequence-=1;state.flags|=LuxEnemyWire::Baseline;
            require(send(),"ordered replacement baseline rejected");
            require(!native->GetDestroyMe() && service->mReplicas[state.name].confirmed &&
                service->mReplicas[state.name].waitingRuntime==runtime && !service->mReplicas[state.name].applied,
                "replacement baseline bound to the old runtime before script replacement");
            service->mReplicas.erase(state.name);
            state=original.state;state.flags|=LuxEnemyWire::Baseline;
            require(send(),"restoring the test's original replica failed");
        }
        return valid;
    }
    bool hearing(iLuxEnemy* enemy,tString& error) const {
        const auto messages=enemy->mlstMessages;
        const auto sample=[&](float distance,float minimum,float maximum) {
            enemy->mlstMessages=messages;
            installed->BroadcastEnemySoundMessage(enemy->GetCharacterBody()->GetCurrentBody()->GetBoundingVolume()->GetWorldCenter()+
                cVector3f(distance,0,0),1,minimum,maximum);
            return enemy->mlstMessages.size()>messages.size()?enemy->mlstMessages.back().mfCustomValue:0.0f;
        };
        const float full=sample(3,4,8),half=sample(6,4,8),equal=sample(6,6,6),outside=sample(8.1f,4,8);
        enemy->mlstMessages=messages;
        if(std::fabs(full-1)>0.001f || std::fabs(half-0.5f)>0.001f || !std::isfinite(equal) || std::fabs(equal-1)>0.001f || outside!=0) {
            error="native hearing falloff/equal-distance boundary is incorrect";return false;
        }
        return true;
    }
    int attack(tString& error,iLuxEnemy* grunt,Uint32 age) {
        const bool host=role=="host",victim=(attackTrial==0?!host:host);
        const tString prefix="attack-"+cString::ToString(int(attackTrial))+"-";
        placePlayer(victim?cVector3f(0,40,-1.2f):cVector3f(60,40,0));
        if(!acted) {
            gpBase->mpPlayer->SetHealth(100);
            if(host) {
                anchor(grunt,cVector3f(0,40,0));grunt->SetDisabled(false);grunt->SetActive(true);
                grunt->ChangeState(eLuxEnemyState_Wait);hitCount=grunt->mlAttackHitCounter;
            }
            done(prefix+"ready.txt");acted=true;
        }
        if(exists(role+"-enemy-"+prefix+"passed.txt")) {
            if(!both(prefix+"passed.txt")) return 0;
            if(attackTrial==0) {attackTrial=1;entered=SDL_GetTicks();acted=false;attackStarted=false;return 0;}
            next();return 0;
        }
        if(!both(prefix+"ready.txt") || age<1100) return 0;
        if(host && !attackStarted) {
            anchor(grunt,cVector3f(0,40,0));
            const uint32_t target=attackTrial==0?clientPeer:gpBase->mpMultiplayer->GetLocalPeerId();
            if(grunt->GetTargetPeer()!=target) return 0;
            hitCount=grunt->mlAttackHitCounter;grunt->ChangeState(eLuxEnemyState_AttackMeleeShort);attackStarted=true;
        }
        if(host && attackStarted && grunt->mlAttackHitCounter>hitCount) {
            grunt->SetDisabled(true);done(prefix+"hit.txt");
        }
        if(!exists("host-enemy-"+prefix+"hit.txt")) return 0;
        const float expected=victim?87.0f:100.0f;
        if(gpBase->mpPlayer->GetHealth()!=expected) {
            if(victim && gpBase->mpPlayer->GetHealth()==100) return 0;
            return fail(error,"native attack hit the wrong victim or applied more than once");
        }
        if(!host && attackTrial==0 && !exists("client-enemy-damage-replay.txt")) {
            auto* world=gpBase->mpMultiplayer->GetWorld();
            luxnet::Writer replay(luxnet::EnemyDamage);
            replay.U32(gpBase->mpMultiplayer->GetMapEpoch());replay.U32(world->mlLastDamageSequence);
            replay.U32(world->GetLocalPlayerLife());
            replay.Float(13);replay.U32(1);replay.U8(eLuxDamageType_Claws);replay.U8(1);
            replay.Float(0);replay.Float(0);replay.Float(0);
            if(!world->HandlePlayerEvent(0,replay.data) || !world->HandlePlayerEvent(0,replay.data) || gpBase->mpPlayer->GetHealth()!=87)
                return fail(error,"duplicate reliable attack result changed health again");
            if(world->HandlePlayerEvent(99,replay.data)) return fail(error,"non-host damage result was accepted");
            done("damage-replay.txt");
        }
        done(prefix+"passed.txt");
        if(!both(prefix+"passed.txt")) return 0;
        if(attackTrial==0) {attackTrial=1;entered=SDL_GetTicks();acted=false;attackStarted=false;return 0;}
        next();return 0;
    }
public:
    bool OnPostRender(tString& error) {
        if(pendingScreenshot.empty()) return true;
        auto* bitmap=gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
        if(!bitmap) {error="enemy encounter screenshot readback failed";return false;}
        const bool saved=gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(bitmap,
            cString::To16Char(outputDir+"/"+pendingScreenshot),0);
        hplDelete(bitmap);pendingScreenshot.clear();
        if(!saved) error="enemy encounter screenshot save failed";
        return saved;
    }
    int Update(tString& error,float dt) {
        auto* session=gpBase->mpMultiplayer;const bool host=role=="host";
        const Uint32 age=SDL_GetTicks()-entered;
        if(phase && age>30000) return fail(error,"timed out waiting for native enemy behavior");
        if(phase==0) {
            if(!RunEnemyProtocolRegression(error)) return -1;
            if(host) {
                const char* path=std::getenv("CODEX_MP_ENEMY_MAP");if(!path) return fail(error,"map path missing");
                cLuxMultiplayerSettings settings;settings.map=path;settings.port=port;settings.useSteam=false;
                settings.maxPlayers=2;settings.playerCollision=false;
                if(!session->Host(settings)) return fail(error,session->GetStatus());
                done("listening.txt");
            } else {
                if(!exists("host-enemy-listening.txt")) return 0;
                if(!session->Join("127.0.0.1:"+cString::ToString(int(port)))) return fail(error,session->GetStatus());
            }
            next();return 0;
        }
        if(phase==1) {
            if(!session->IsReady() || session->GetWorld()->GetRemotePlayers().empty()) return 0;
            if(!install(error)) return -1;
            if(host) clientPeer=session->GetWorld()->GetRemotePlayers().begin()->first;
            else clientPeer=session->GetLocalPeerId();
            placePlayer(host?cVector3f(60,40,0):cVector3f(0,40,-8));
            done("setup.txt");next();return 0;
        }
        // Reconnect and map-change phases intentionally pass through no-session/no-map states.
        if(phase<12 && (!session->IsActive() || !installed)) return fail(error,"session disappeared");
        auto* grunt=find("codex_enemy_grunt");auto* water=find("codex_enemy_water");auto* brute=find("codex_enemy_brute");
        if(phase<12 && (!grunt || !water || !brute)) return fail(error,"authored enemy disappeared");
        if(phase<12 && !clientReplicaInvariant(error)) return -1;
        if(phase<12 && SDL_GetTicks()-diagnosticAt>1500) {
            diagnosticAt=SDL_GetTicks();const auto pos=grunt->GetCharacterBody()->GetFeetPosition();
            std::printf("%s enemy phase=%u state=%d target=%u pos=(%.3f %.3f %.3f) health=%.1f active=%d\n",
                role.c_str(),phase,int(grunt->GetCurrentEnemyState()),grunt->GetTargetPeer(),pos.x,pos.y,pos.z,gpBase->mpPlayer->GetHealth(),grunt->IsActive());
            std::fflush(stdout);
        }
        if(phase==2) {
            placePlayer(host?cVector3f(60,40,0):cVector3f(0,40,-8));
            if(host && !acted && age>500 && gpBase->mpPlayer->GetTerror()>0.001f)
                return fail(error,"deactivating an enemy retained a local fear source before target selection");
            if(!both("setup.txt") || age<900) return 0;
            if(host && !acted) {grunt->SetActive(true);acted=true;}
            if(host) {
                const auto players=session->GetWorld()->GetEnemyPlayers();bool proxy=false;
                for(const auto& player:players) if(player.peer==clientPeer) {
                    proxy=player.Eligible() && !player.body->IsActive() && !player.body->GetCurrentBody()->GetCollide();
                }
                if(!proxy || !session->GetWorld()->mPlayerColliders.empty()) return fail(error,"collision-disabled client has no independent attack query proxy");
            }
            if(!exists(role+"-enemy-client-chase.txt")) {
                if(grunt->GetTargetPeer()!=clientPeer || !grunt->GetPlayerDetected() ||
                   (grunt->GetCharacterBody()->GetFeetPosition()-cVector3f(0,40,0)).Length()<0.25f) return 0;
                if(!grunt->IsActive()) return fail(error,"enemy despawned while only the client was nearby");
                if(!grunt->GetMeshEntity()->IsVisible() || !grunt->GetCurrentAnimation() ||
                   (grunt->GetMeshEntity()->GetWorldPosition()-grunt->GetCharacterBody()->GetFeetPosition()).Length()>4)
                    return fail(error,"moving enemy has no visible animated mesh following its character");
                if(!host) pendingScreenshot="client-enemy-chase.png";
                done("client-chase.txt");
            }
            if(!both("client-chase.txt")) return 0;
            if(host) {stationary(grunt);anchor(grunt,cVector3f(0,40,0));}
            next();return 0;
        }
        if(phase==3) {
            placePlayer(host?cVector3f(1,40,-7):cVector3f(0,40,-8));
            if(host && !exists(role+"-enemy-stable-target.txt")) {
                anchor(grunt,cVector3f(0,40,0));
                if(!acted) {if(!hearing(grunt,error)) return -1;done("native-hearing.txt");acted=true;}
                if(grunt->GetTargetPeer()!=clientPeer) return fail(error,"a similarly placed challenger caused target oscillation");
                if(age<1800 || !grunt->GetPlayerDetectedForPeer(session->GetLocalPeerId())) return 0;
            } else if(age<1800) return 0;
            done("stable-target.txt");if(!both("stable-target.txt")) return 0;
            wall->SetActive(true);next();return 0;
        }
        if(phase==4) {
            placePlayer(host?cVector3f(60,40,0):cVector3f(0,40,-8));
            if(host) {
                anchor(grunt,cVector3f(0,40,0));
                const auto found=grunt->mPlayerAwareness.find(clientPeer);
                if(found==grunt->mPlayerAwareness.end() || !found->second.hasLastKnown) return fail(error,"client detection lost its individual memory");
                if(found->second.visible || found->second.detected || age<900) return 0;
                remembered=found->second.lastKnown;done("occluded.txt");
            }
            if(!exists("host-enemy-occluded.txt")) return 0;
            next();return 0;
        }
        if(phase==5) {
            placePlayer(host?cVector3f(60,40,0):cVector3f(10,40,-12));
            if(host && !exists(role+"-enemy-hidden-memory.txt")) {
                anchor(grunt,cVector3f(0,40,0));
                const auto found=grunt->mPlayerAwareness.find(clientPeer);
                if(found==grunt->mPlayerAwareness.end() || found->second.visible ||
                   (found->second.lastKnown-remembered).Length()>0.001f)
                    return fail(error,"occluded client movement leaked into the enemy's remembered target position");
            }
            if(age<1200) return 0;
            done("hidden-memory.txt");if(!both("hidden-memory.txt")) return 0;
            wall->SetActive(false);next();return 0;
        }
        if(phase==6) return attack(error,grunt,age);
        if(phase==7) {
            placePlayer(host?cVector3f(60,40,0):cVector3f(20,40,-4));
            if(host && !acted) {grunt->SetActive(false);brute->SetActive(true);stationary(brute);acted=true;}
            if(host) anchor(brute,cVector3f(20,40,0));
            if(!exists(role+"-enemy-brute-client.txt") && (brute->GetTargetPeer()!=clientPeer || !brute->GetPlayerDetected())) return 0;
            done("brute-client.txt");if(!both("brute-client.txt")) return 0;
            if(host) brute->SetActive(false);
            next();return 0;
        }
        if(phase==8) {
            placePlayer(host?cVector3f(60,40,0):cVector3f(-20,43,-3));
            if(host && !acted) {water->SetActive(true);stationary(water);acted=true;}
            if(host) {
                anchor(water,cVector3f(-20,40,0));
                if(age>900 && water->GetPlayerDetectedForPeer(clientPeer)) return fail(error,"water lurker detected an elevated client as standing in its water");
            }
            if(age<1400) return 0;
            done("water-high.txt");if(!both("water-high.txt")) return 0;
            next();return 0;
        }
        if(phase==9) {
            placePlayer(host?cVector3f(60,40,0):cVector3f(-20,40,-3));
            if(host) anchor(water,cVector3f(-20,40,0));
            if(!exists(role+"-enemy-water-low.txt") && (water->GetTargetPeer()!=clientPeer || !water->GetPlayerDetected())) return 0;
            if(water->GetMeshEntity()->IsVisible()) return fail(error,"water lurker replica exposed its invisible helper mesh");
            done("water-low.txt");if(!both("water-low.txt")) return 0;
            if(!host) gpBase->mpPlayer->SetHealth(0);
            next();return 0;
        }
        if(phase==10) {
            if(host) {
                anchor(water,cVector3f(-20,40,0));
                if(age<1200) return 0;
                for(const auto& player:session->GetWorld()->GetEnemyPlayers())
                    if(player.peer==clientPeer && player.Eligible()) return fail(error,"dead client remains an eligible target");
                if(water->GetTargetPeer()==clientPeer) return fail(error,"enemy retained a dead client as its target");
                done("death-cleanup.txt");
            } else {
                if(!exists("host-enemy-death-cleanup.txt")) return 0;
                gpBase->mpPlayer->GetHelperDeath()->OnPressButton();
                if(gpBase->mpPlayer->IsDead() || gpBase->mpPlayer->GetHelperDeath()->GetFadeAlpha()>0) return 0;
                done("respawn.txt");
            }
            if(!exists("client-enemy-respawn.txt")) return 0;
            next();return 0;
        }
        if(phase==11) {
            placePlayer(host?cVector3f(12,40,-8):cVector3f(65,40,0));
            if(host && !acted) {
                water->SetActive(false);grunt->SetActive(true);grunt->SetDisabled(false);
                anchor(grunt,cVector3f(0,40,0));grunt->ChangeState(eLuxEnemyState_Wait);acted=true;
            }
            if(host && age>300 && !attackStarted && grunt->GetCurrentEnemyState()==eLuxEnemyState_Wait) {
                grunt->ChangeState(eLuxEnemyState_AttackMeleeShort);attackStarted=true;
            }
            if(host && attackStarted && grunt->GetCurrentEnemyState()==eLuxEnemyState_AttackMeleeShort &&
               grunt->GetCurrentAnimation() && grunt->GetCurrentAnimation()->GetTimePosition()>0.15f) {
                grunt->GetCurrentAnimation()->SetPaused(true);grunt->SetDisabled(true);done("latejoin-frozen.txt");
            }
            if(!exists("host-enemy-latejoin-frozen.txt")) return 0;
            if(!host) {installed=NULL;wall=NULL;session->Stop("Enemy late-join regression.");done("reconnecting.txt");}
            next();return 0;
        }
        if(phase==12) {
            if(host) {
                if(!exists("client-enemy-reconnecting.txt") || !session->GetWorld()->GetRemotePlayers().empty()) return 0;
                if(!session->GetWorld()->mEnemyPlayerBodies.empty()) return fail(error,"disconnect retained a dead query proxy");
                done("disconnect-cleanup.txt");
            } else {
                if(!exists("host-enemy-disconnect-cleanup.txt") || session->IsActive() || gpBase->mpMapHandler->GetCurrentMap()) return 0;
                if(!session->Join("127.0.0.1:"+cString::ToString(int(port)))) return fail(error,"late join failed");
            }
            next();return 0;
        }
        if(phase==13) {
            if(exists(role+"-enemy-latejoin-baseline.txt")) {
                if(!both("latejoin-baseline.txt")) return 0;
                installed=NULL;wall=NULL;
                if(host && !session->HostChangeMap("maps/main/ch01/01_old_archives.map")) return fail(error,"map transition request rejected");
                next();return 0;
            }
            if(!session->IsReady() || session->GetWorld()->GetRemotePlayers().empty()) return 0;
            if(!host && !install(error)) return -1;
            grunt=find("codex_enemy_grunt");if(!grunt) return fail(error,"late join omitted authored enemy");
            if(!grunt->IsDisabled() || grunt->GetCurrentEnemyState()!=eLuxEnemyState_AttackMeleeShort ||
               !grunt->GetCurrentAnimation() || !grunt->GetCurrentAnimation()->IsPaused() || grunt->GetCurrentAnimation()->GetTimePosition()<0.1f) return 0;
            if(!clientReplicaInvariant(error)) return -1;
            if(!host) {
                auto* service=session->GetEnemies();const auto found=service->mReplicas.find("codex_enemy_grunt");
                if(found==service->mReplicas.end() || !found->second.applied) return 0;
                oldState=LuxEnemyWire::EncodeState(found->second.state);
                if(!ordering(error,found->second.state)) return -1;
            }
            epoch=session->GetMapEpoch();done("latejoin-baseline.txt");return 0;
        }
        if(phase==14) {
            if(exists(role+"-enemy-map-cleanup.txt")) {
                if(!both("map-cleanup.txt")) return 0;
                if(host) session->Stop("Enemy regression complete.");
                next();return 0;
            }
            if(!session->IsReady() || session->GetMapEpoch()<=epoch || !gpBase->mpMapHandler->GetCurrentMap()) return 0;
            if(find("codex_enemy_grunt") || !session->GetEnemies()->mReplicas.empty() || !session->GetEnemies()->mHost.empty())
                return fail(error,"old enemy records survived the map epoch");
            if(!host && (!session->GetEnemies()->HandleMessage(0,oldState) || !session->GetEnemies()->mReplicas.empty()))
                return fail(error,"late old-map snapshot repopulated the new map");
            done("map-cleanup.txt");return 0;
        }
        if(phase==15) {
            if(session->IsActive()) return 0;
            if(!session->GetWorld()->mEnemyPlayerBodies.empty() || !session->GetEnemies()->mReplicas.empty() || !session->GetEnemies()->mHost.empty())
                return fail(error,"shutdown retained enemy state or target proxies");
            done("passed.txt");return 1;
        }
        return 0;
    }
};
#endif
