#include "LuxBase.h"
#include "LuxMapHandler.h"
#include "LuxMap.h"
#include "LuxMainMenu.h"
#include "LuxInputHandler.h"
#include "LuxInventory.h"
#include "LuxSaveHandler.h"
#include <map>
#include <set>
#include <vector>
#include <deque>
#define private public
#include "LuxMultiplayer.h"
#include "LuxMultiplayerWorld.h"
#include "LuxMultiplayerUI.h"
#undef private
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#undef main

cLuxBase* gpBase=NULL;
static tString role, outputDir;
static unsigned short port;
static int result=2;

static void mark(const tString& name, const tString& message) {
    FILE* file=NULL;
    fopen_s(&file,(outputDir+"/"+name).c_str(),"wb");
    if(file) {std::fwrite(message.data(),1,message.size(),file);std::fclose(file);}
}
static bool exists(const tString& name) {
    return cPlatform::FileExists(cString::To16Char(outputDir+"/"+name));
}
static void printStatus(const char* message) {
    std::printf("%s: %s\n",role.c_str(),message);std::fflush(stdout);
}
class cGameSmoke : public iUpdateable {
    int state=0;
    Uint32 started=0, readyAt=0, statusAt=0;
    uint32_t sequence=0;
    uint32_t oldEpoch=0, oldSequence=0;
    bool screenshotPending=false, screenshotDone=false;
    uint64_t steamLobby=0;
public:
    cGameSmoke() : iUpdateable("MultiplayerGameSmoke") {}
    void OnStart() {
        started=SDL_GetTicks();
        gpBase->mpEngine->SetWaitIfAppOutOfFocus(false);
        gpBase->mpEngine->GetSound()->GetLowLevel()->SetVolume(0);
        SDL_HideWindow(SDL_GL_GetCurrentWindow());
    }
    void fail(const tString& error) {
        printStatus(("FAIL: "+error).c_str());
        mark(role+"-failed.txt",error);
        result=2;gpBase->mpEngine->Exit();state=99;
    }
    void stalePose() {
        if(role!="host" || gpBase->mpMultiplayer->GetMapEpoch()<=oldEpoch) return;
        LuxWorldWire::Writer packet(LuxWorldWire::Pose,oldEpoch);
        packet.U32(0);packet.U32(oldSequence+100000);
        packet.F32(12345);packet.F32(0);packet.F32(0);
        packet.F32(0.6f);packet.F32(1.8f);packet.F32(0.6f);packet.F32(0);
        for(const auto& peer:gpBase->mpMultiplayer->mPeers) gpBase->mpMultiplayer->Send(peer.first,packet.bytes,false);
    }
    void Update(float dt) {
        if(state==99) return;
        if(role=="steam-host") { updateSteamHost();return; }
        if(exists(role=="host" ? "client-failed.txt" : "host-failed.txt")) {fail("peer's test process failed; stopping this instance");return;}
        if(SDL_GetTicks()-started>90000) {fail("90-second handshake/test timeout: "+gpBase->mpMultiplayer->GetStatus());return;}
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        if(SDL_GetTicks()-statusAt>2000) {
            printStatus(mp->GetStatus().c_str());statusAt=SDL_GetTicks();
        }
        if(state==0) {
            if(role=="host") {
                gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_StartGame);
                mp->ShowWindow(true);screenshotPending=true;
            }
            state=1;return;
        }
        if(state==1) {
            if(role=="host") {
                if(!screenshotDone) return;
                if(mp->IsWindowVisible()) mp->ToggleWindow();
                cLuxMultiplayerSettings settings;settings.useSteam=false;settings.port=port;settings.maxPlayers=2;
                if(!mp->Host(settings)) {fail("Host failed: "+mp->GetStatus());return;}
                mark("host-listening.txt",mp->GetStatus());
                printStatus("campaign loaded and listening");
            }
            else {
                if(!exists("host-listening.txt")) return;
                if(!mp->Join("127.0.0.1:"+cString::ToString(static_cast<int>(port)))) {fail("Join failed: "+mp->GetStatus());return;}
            }
            state=2;return;
        }
        if(state==2) {
            if(!mp->IsActive()) {fail("session stopped: "+mp->GetStatus());return;}
            bool ready=mp->mbReady && gpBase->mpMapHandler->GetCurrentMap() && !mp->GetWorld()->GetRemotePlayers().empty();
            if(role=="host") {
                ready=ready && mp->mPeers.size()==1;
                if(ready) ready=mp->mPeers.begin()->second.ready;
            }
            else {
                size_t received=0;
                for(const auto& body:mp->GetWorld()->mBodies) if(body.second.received) ++received;
                ready=ready && received>0;
            }
            if(!ready) return;
            if(gpBase->mpSaveHandler->AutoSave()) {fail("offline autosave was accepted during an active multiplayer session");return;}
            uint64_t bodyHash=0;
            for(const auto& body:mp->GetWorld()->mBodies) bodyHash+=body.first;
            const tString info="map="+mp->msMapName+" bodies="+cString::ToString(static_cast<int>(mp->GetWorld()->mBodies.size()))+
                " remotes="+cString::ToString(static_cast<int>(mp->GetWorld()->GetRemotePlayers().size()))+
                " epoch="+cString::ToString(static_cast<int>(mp->GetMapEpoch()));
            std::printf("%s READY %s body_hash=%llu\n",role.c_str(),info.c_str(),static_cast<unsigned long long>(bodyHash));std::fflush(stdout);
            mark(role+"-ready.txt",info);
            gpBase->mpEngine->GetUpdater()->SetContainer("MainMenu");
            mp->ShowWindow();
            sequence=mp->GetWorld()->mlSequence;
            readyAt=SDL_GetTicks();state=3;return;
        }
        if(state==3) {
            if(!mp->IsActive()) {fail("session disconnected while menu was open: "+mp->GetStatus());return;}
            if(SDL_GetTicks()-readyAt<3000) return;
            if(mp->GetWorld()->mlSequence<=sequence) {fail("network world did not advance with pause menu open");return;}
            if(mp->GetWorld()->GetRemotePlayers().empty()) {fail("remote player disappeared with pause menu open");return;}
            if(gpBase->mpInputHandler->GetState()!=eLuxInputState_MainMenu) {fail("local pause menu state unexpectedly changed");return;}
            if(!gpBase->mpInventory->AddItem("codex_transition_sentinel",eLuxItemType_Puzzle,"KeyTower","key_tower.tga",1,"","")) {
                fail("sentinel inventory item could not be created");return;
            }
            oldEpoch=mp->GetMapEpoch();oldSequence=mp->GetWorld()->mlSequence;
            mark(role+"-menus-passed.txt","PASS: campaign map transfer, initial physics state, remote player poses, continuous paused-menu session.");
            printStatus("PASS phase 1: map transfer, initial physics, remote poses, continuous menu session");
            state=4;return;
        }
        if(state==4 && exists("host-menus-passed.txt") && exists("client-menus-passed.txt")) {
            if(role=="host") gpBase->mpMapHandler->ChangeMap("01_old_archives.map","PlayerStartArea_1","","");
            state=5;return;
        }
        if(state==5) {
            stalePose();
            if(!mp->IsActive()) {fail("session stopped during map transition: "+mp->GetStatus());return;}
            if(mp->GetMapEpoch()<=oldEpoch || !mp->IsReady() || mp->msMapName!="01_old_archives.map" || mp->GetWorld()->GetRemotePlayers().empty()) return;
            if(role=="host" && (mp->mPeers.empty() || !mp->mPeers.begin()->second.ready)) return;
            if(role=="client") {
                size_t received=0;
                for(const auto& body:mp->GetWorld()->mBodies) if(body.second.received) ++received;
                if(!received) return;
            }
            if(!gpBase->mpInventory->GetItem("codex_transition_sentinel")) {fail("inventory was reset during synchronized map transition");return;}
            readyAt=SDL_GetTicks();state=6;return;
        }
        if(state==6) {
            stalePose();
            if(!mp->IsActive() || mp->GetWorld()->GetRemotePlayers().empty()) {fail("stale map packets disrupted new session world");return;}
            for(const auto& player:mp->GetWorld()->GetRemotePlayers()) {
                if(player.second.position.x>1000) {fail("old-epoch UDP pose was applied to the new map");return;}
            }
            if(SDL_GetTicks()-readyAt<3000) return;
            mark(role+"-passed.txt","PASS: synchronized second map, preserved inventory, ignored stale UDP poses.");
            printStatus("PASS phase 2: synchronized map change, inventory persistence, stale UDP rejection");
            state=7;return;
        }
        if(state==7 && exists("host-passed.txt") && exists("client-passed.txt")) {
            mp->Stop("Integration test complete.");
            if(gpBase->mpSaveHandler->AutoSave()) {fail("offline autosave was accepted for the disconnected multiplayer world");return;}
            mark(role+"-save-guard-passed.txt","PASS: autosave rejected during session and after local disconnect.");
            result=0;state=99;gpBase->mpEngine->Exit();
        }
    }
    void updateSteamHost() {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        if(SDL_GetTicks()-started>90000) {fail("Steam host timeout: "+mp->GetStatus()+"; "+mp->GetSteamStatus());return;}
        if(state==0) {
            gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_StartGame);
            mp->ShowWindow(true);screenshotPending=true;state=1;return;
        }
        if(state==1) {
            if(!screenshotDone) return;
            cLuxMultiplayerSettings settings;settings.maxPlayers=2;
            if(!mp->Host(settings)) {fail("Steam campaign host: "+mp->GetStatus());return;}
            state=2;return;
        }
        if(!mp->IsActive()) {fail("Steam session stopped: "+mp->GetStatus());return;}
        if(state==2) {
            if(!mp->GetSteamLobbyID()) return;
            if(!mp->IsHost() || !mp->IsSteamSession() || !mp->IsReady() || mp->GetWorld()->mBodies.empty()) {
                fail("Steam lobby did not initialize the hosted world");return;
            }
            steamLobby=mp->GetSteamLobbyID();
            printStatus(mp->GetSteamStatus().c_str());
            printStatus("Steam campaign lobby created with dynamic world; checking menu updates");
            gpBase->mpEngine->GetUpdater()->SetContainer("MainMenu");
            mp->ShowWindow();screenshotPending=true;
            sequence=mp->GetWorld()->mlSequence;readyAt=SDL_GetTicks();state=3;return;
        }
        if(state==3) {
            if(SDL_GetTicks()-readyAt<3000) return;
            if(mp->GetWorld()->mlSequence<=sequence) {fail("Steam world stopped while menu open");return;}
            if(!mp->HostChangeMap("01_old_archives.map")) {fail("Steam map change refused");return;}
            oldEpoch=mp->GetMapEpoch();state=4;return;
        }
        if(state==4) {
            if(mp->GetMapEpoch()<=oldEpoch) return;
            if(mp->GetSteamLobbyID()!=steamLobby || !mp->IsReady() || mp->msMapName!="01_old_archives.map") {
                fail("Steam lobby did not survive map transition");return;
            }
            if(gpBase->mpSaveHandler->AutoSave()) {fail("Steam session allowed offline autosave");return;}
            mp->Stop("Steam integration test complete.");
            if(mp->IsActive() || mp->GetSteamLobbyID()) {fail("Steam lobby remained active after stop");return;}
            mark(role+"-passed.txt","PASS: real Steam campaign lobby, continuous menu updates, map transition with same lobby, clean disconnect.");
            printStatus("PASS: Steam campaign hosting, menu updates, map transition, and lobby teardown");
            result=0;state=99;gpBase->mpEngine->Exit();
        }
    }
    void OnPostRender(float dt) {
        if(!screenshotPending) return;
        screenshotPending=false;
        gpBase->mpMultiplayer->mpUI->Draw();
        cBitmap* bitmap=gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
        if(!bitmap) {fail("main-menu screenshot readback failed");return;}
        const bool saved=gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(bitmap,
            cString::To16Char(outputDir+"/main-menu-multiplayer.png"),0);
        hplDelete(bitmap);
        if(!saved) {fail("main-menu screenshot save failed");return;}
        screenshotDone=true;
    }
};
int main(int argc,char** argv) {
    if(argc!=5) {std::fprintf(stderr,"Usage: smoke host|client|steam-host init.cfg output-dir port\n");return 2;}
    role=argv[1];outputDir=argv[3];port=static_cast<unsigned short>(std::atoi(argv[4]));
    gpBase=hplNew(cLuxBase,());
    if(!gpBase->Init(argv[2])) {
        printStatus(("Init failed: "+cString::To8Char(gpBase->msErrorMessage)).c_str());return 2;
    }
    gpBase->mbSaveConfigAtExit=false;
    gpBase->mpEngine->GetSound()->GetLowLevel()->SetVolume(0);
    SDL_HideWindow(SDL_GL_GetCurrentWindow());
    cGameSmoke smoke;
    gpBase->mpEngine->GetUpdater()->AddGlobalUpdate(&smoke);
    gpBase->Run();
    gpBase->Exit();
    hplDelete(gpBase);gpBase=NULL;
    return result;
}
int hplMain(const tString&) { return 2; }
