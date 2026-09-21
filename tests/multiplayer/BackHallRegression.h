#ifndef MULTIPLAYER_BACK_HALL_REGRESSION_H
#define MULTIPLAYER_BACK_HALL_REGRESSION_H
#include "LuxSavedGame.h"

// Use the unmodified retail map: it contains legitimate props sharing names
// while their authored IDs, positions, and (for one pair) resources differ.
class cBackHallRegression {
    unsigned phase=0;
    Uint32 entered=0;
    bool acted=false;
    uint32_t firstPeer=0,epoch=0;
    uint64_t hostSession=0;
    tString lastCheck;
    std::map<int,uint64_t> runtimes;
    const int ids[4]={1030,1040,1047,1055};
    const char* names[4]={"Even01Slime04","Even01Slime04","Even01Slime02_3","Even01Slime02_3"};
    const cVector3f positions[4]={cVector3f(22.2127f,-1.06992f,21.3678f),cVector3f(5.49214f,1.66092f,32.2321f),
        cVector3f(5.19188f,-4.0068f,14.8997f),cVector3f(6.20039f,-3.97884f,12.7324f)};
    void next() {++phase;entered=SDL_GetTicks();acted=false;}
    bool both(const char* suffix) const {return exists(tString("host-backhall-")+suffix) && exists(tString("client-backhall-")+suffix);}
    void done(const char* suffix) const {mark(role+"-backhall-"+suffix,"passed");}
    int fail(tString& error,const tString& message) const {
        auto* map=gpBase->mpMapHandler->GetCurrentMap();
        for(unsigned i=0;i<4;++i) if(auto* entity=prop(map,i)) {
            const auto p=entity->GetOnLoadTransform().GetTranslation();
            std::printf("%s Back Hall ID=%d name=%s runtime=%llu active=%d position=(%.6g %.6g %.6g)\n",
                role.c_str(),ids[i],entity->GetName().c_str(),static_cast<unsigned long long>(entity->GetRuntimeID()),
                entity->IsActive(),p.x,p.y,p.z);
        }
        std::fflush(stdout);
        error="Back Hall phase "+cString::ToString(int(phase))+": "+message;return -1;
    }
    iLuxProp* prop(cLuxMap* map,unsigned index) const {
        return map?static_cast<iLuxProp*>(map->GetEntityByID(ids[index],eLuxEntityType_Prop)):NULL;
    }
    bool inspect(tString& error,bool requireStates,bool retainRuntime) {
        auto* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!map || map->GetName()!="09_back_hall") {error="expected retail Back Hall map";return false;}
        for(unsigned i=0;i<4;++i) {
            auto* entity=prop(map,i);
            if(!entity || entity->GetDestroyMe() || entity->GetName()!=names[i] || !entity->GetMeshEntity()) {
                error="missing independent prop ID "+cString::ToString(ids[i])+" ('"+names[i]+"')";return false;
            }
            // Body-bound slime submeshes can keep an identity mesh root.
            // Compare the actual authored prop transform instead.
            if(cMath::Vector3Dist(entity->GetOnLoadTransform().GetTranslation(),positions[i])>0.05f) {
                error="duplicate-name prop moved to another instance's transform, ID "+cString::ToString(ids[i]);return false;
            }
            if(requireStates && entity->IsActive()!=(i%2==0)) {
                error="independent active state did not arrive for ID "+cString::ToString(ids[i]);return false;
            }
            if(retainRuntime && runtimes[ids[i]]!=entity->GetRuntimeID()) {
                error="repeated baseline replaced an existing prop, ID "+cString::ToString(ids[i]);return false;
            }
            if(!retainRuntime) runtimes[ids[i]]=entity->GetRuntimeID();
        }
        return true;
    }
    bool ready() const {
        auto* session=gpBase->mpMultiplayer;
        return session->IsReady() && !session->GetWorld()->GetRemotePlayers().empty() && gpBase->mpMapHandler->GetCurrentMap();
    }
    bool savedStates(tString& error) const {
        auto* saved=gpBase->mpMapHandler->GetSavedMapCollection()->GetSavedMap("09_back_hall",false);
        if(!saved) {error="Back Hall saved map disappeared before the return visit";return false;}
        std::set<int> disabled;
        auto disabledIt=saved->mlstDisabledEntities.GetIterator();
        while(disabledIt.HasNext()) disabled.insert(disabledIt.Next());
        for(unsigned i=0;i<4;++i) {
            bool active=disabled.count(ids[i])==0;
            auto fullIt=saved->mlstFullEntities.GetIterator();
            while(fullIt.HasNext()) {
                auto* entity=fullIt.Next();if(entity->mlID==ids[i]) {active=entity->mbActive;break;}
            }
            if(active!=(i%2==0)) {
                error="Back Hall saved active state disagreed before reloading ID "+cString::ToString(ids[i]);return false;
            }
        }
        return true;
    }
    void parkPlayer() const {
        // Keep authored player-triggered events from altering the four fixtures
        // while their native snapshots and saved-map state are under test.
        auto* character=gpBase->mpPlayer->GetCharacterBody();
        character->StopMovement();character->SetGravityActive(false);character->SetFeetPosition(cVector3f(0,100,0));
    }
public:
    int Update(tString& error) {
        auto* session=gpBase->mpMultiplayer;const bool host=role=="host";
        const Uint32 age=SDL_GetTicks()-entered;
        if(phase && age>30000) return fail(error,"timed out: "+session->GetStatus()+"; last check: "+lastCheck);
        if(phase==0) {
            if(host) {
                cLuxMultiplayerSettings settings;settings.map="maps/main/ch01/09_back_hall.map";
                settings.port=port;settings.useSteam=false;settings.maxPlayers=2;
                if(!session->Host(settings)) return fail(error,session->GetStatus());
                hostSession=session->GetSessionSerial();done("listening.txt");
            } else {
                if(!exists("host-backhall-listening.txt")) return 0;
                if(!session->Join("127.0.0.1:"+cString::ToString(int(port)))) return fail(error,session->GetStatus());
            }
            next();return 0;
        }
        if(phase==1) {
            if(!ready()) return 0;
            parkPlayer();
            if(!inspect(error,false,false)) return fail(error,error);
            firstPeer=host?session->GetWorld()->GetRemotePlayers().begin()->first:session->GetLocalPeerId();
            epoch=session->GetMapEpoch();done("initial.txt");
            printStatus("Back Hall joined with all four duplicate-name props intact");next();return 0;
        }
        if(phase>=2 && phase<=4 && (!session->IsActive() || !gpBase->mpMapHandler->GetCurrentMap()))
            return fail(error,"session ended before reconnect test: "+session->GetStatus());
        if(phase==2) {
            parkPlayer();if(!both("initial.txt")) return 0;
            if(host && !acted) {
                auto* map=gpBase->mpMapHandler->GetCurrentMap();
                for(unsigned i=0;i<4;++i) prop(map,i)->SetActive(i%2==0);
                acted=true;
            }
            if(!inspect(error,true,true)) {lastCheck=error;return 0;}
            done("independent.txt");next();return 0;
        }
        if(phase==3) {
            parkPlayer();if(!both("independent.txt")) return 0;
            if(host && !acted) {
                if(!session->GetEntities()->SendInitialState(firstPeer)) return fail(error,session->GetEntities()->GetLastError());
                done("baseline-sent.txt");acted=true;entered=SDL_GetTicks();return 0;
            }
            if(!exists("host-backhall-baseline-sent.txt")) return 0;
            if(!acted) {acted=true;entered=SDL_GetTicks();return 0;}
            if(age<1500) return 0;
            if(!inspect(error,true,true)) return fail(error,error);
            done("baseline.txt");next();return 0;
        }
        if(phase==4) {
            parkPlayer();if(!both("baseline.txt")) return 0;
            if(!host) {
                // An incomplete handshake packet exercises the real host
                // rejection path without reaching into a private session API.
                if(!session->Send(0,std::vector<uint8_t>{luxnet::Hello},true))
                    return fail(error,"could not queue intentional malformed handshake");
                done("rejection-requested.txt");
            } else {
                if(!exists("client-backhall-rejection-requested.txt")) return 0;
            }
            next();return 0;
        }
        if(phase==5) {
            if(host) {
                if(!session->IsHost() || !session->IsReady() || session->GetSessionSerial()!=hostSession || session->GetMapEpoch()!=epoch)
                    return fail(error,"rejecting one peer shut down or replaced the host session");
                if(!session->GetWorld()->GetRemotePlayers().empty() || !session->mPeers.empty()) return 0;
                done("listener-survived.txt");
            } else {
                if(!exists("host-backhall-listener-survived.txt") || session->IsActive() || gpBase->mpMapHandler->GetCurrentMap()) return 0;
                if(!session->Join("127.0.0.1:"+cString::ToString(int(port)))) return fail(error,"rejoin failed: "+session->GetStatus());
            }
            next();return 0;
        }
        if(phase==6) {
            if(!ready()) return 0;
            parkPlayer();
            const uint32_t peer=host?session->GetWorld()->GetRemotePlayers().begin()->first:session->GetLocalPeerId();
            if(peer==firstPeer) return fail(error,"rejoin reused the disconnected peer identity");
            if(!inspect(error,true,host)) {lastCheck=error;return 0;}
            done("rejoined.txt");
            printStatus("Back Hall rejected-peer reconnect preserved the live listener and independent prop states");
            next();return 0;
        }
        if(phase==7) {
            if(!both("rejoined.txt")) return 0;
            // Old Archives intentionally calls ClearSavedMaps on entry. Use
            // the neighboring Study to exercise an actual saved-map revisit.
            if(host) {
                if(!inspect(error,true,true)) return fail(error,error);
                gpBase->mpMapHandler->ChangeMap("11_study.map","PlayerStartArea_1","","");
            }
            next();return 0;
        }
        if(phase==8) {
            if(exists(role+"-backhall-away.txt")) {
                if(!both("away.txt")) return 0;
                if(host) {
                    if(!savedStates(error)) return fail(error,error);
                    gpBase->mpMapHandler->ChangeMap("09_back_hall.map","PlayerStartArea_1","","");
                }
                next();return 0;
            }
            if(!ready() || session->GetMapEpoch()<=epoch || gpBase->mpMapHandler->GetCurrentMap()->GetName()!="11_study") return 0;
            parkPlayer();epoch=session->GetMapEpoch();done("away.txt");return 0;
        }
        if(phase==9) {
            if(exists(role+"-backhall-revisited.txt")) {
                if(!both("revisited.txt")) return 0;
                printStatus("Back Hall map revisit retained independent duplicate-name props and saved active states");
                if(host) session->Stop("Back Hall regression complete.");
                next();return 0;
            }
            if(!ready() || session->GetMapEpoch()<=epoch || gpBase->mpMapHandler->GetCurrentMap()->GetName()!="09_back_hall") return 0;
            parkPlayer();if(!inspect(error,true,false)) {lastCheck=error;return 0;}
            done("revisited.txt");return 0;
        }
        if(phase==10) {
            if(session->IsActive()) return 0;
            done("passed.txt");return 1;
        }
        return 0;
    }
};
#endif
