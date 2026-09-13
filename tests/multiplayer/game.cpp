#include "LuxBase.h"
#include "LuxMap.h"
#include "LuxMainMenu.h"
#include "LuxInputHandler.h"
#include "LuxInventory.h"
#include "LuxJournal.h"
#include "LuxPlayer.h"
#include "LuxSaveHandler.h"
#include <map>
#include <set>
#include <vector>
#include <deque>
#include <filesystem>
#define private public
#include "LuxPlayerHelpers.h"
#include "LuxEffectRenderer.h"
#include "LuxEffectHandler.h"
#include "LuxMapHandler.h"
#include "LuxMultiplayer.h"
#include "LuxMultiplayerWorld.h"
#include "LuxMultiplayerEffects.h"
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
#include "NativeRegression.h"
#include "DrawerRegression.h"
#include "SoundRegression.h"
#include "IncidentalEffectsRegression.h"
#include "NativeEffectsRegression.h"
#include "LanternRegression.h"
#include "JointLifecycleRegression.h"
#include "LoadingRegression.h"
#include "MapCacheRegression.h"
#include "HostingRegression.h"
#include "GuiAspectRegression.h"
#include "WindowResizeRegression.h"
class cGameSmoke : public iUpdateable, public iRendererCallback {
    int state=0;
    Uint32 started=0, readyAt=0, statusAt=0;
    uint32_t sequence=0;
    uint32_t oldEpoch=0, oldSequence=0;
    bool screenshotPending=false, screenshotDone=false;
    bool loadingScreenshotDone=false;
    int menuTrial=0, menuRenderFrame=0;
    int renderedWorldFrames=0;
    tString menuScreenshot;
    cLuxMap* deathMap=NULL;
    Uint32 deathStarted=0;
    uint64_t steamLobby=0;
    cNativeRegression nativeRegression;
    cDrawerRegression drawerRegression;
    cSoundRegression soundRegression;
    cIncidentalEffectsRegression incidentalRegression;
    cNativeEffectsRegression nativeEffectsRegression;
    cLanternRegression lanternRegression;
    cJointLifecycleRegression jointRegression;
    cWindowResizeRegression resizeRegression;
    bool resizeTests=std::getenv("CODEX_MP_RESIZE")!=NULL;
    cVector2l resizeValidated=0;
    bool genericEffectsDone=false;
    cMapCacheRegression mapCacheRegression;
    cLoadingPhaseObserver loadObserver;
    cLoadingRegression loadingRegression;
    bool heldNormalMapChange=false;
    unsigned normalPreparingFrames=0;
    Uint32 normalPreparationStarted=0;
    bool incidentalOnly=std::getenv("CODEX_MP_INCIDENTAL_ONLY")!=NULL;
    bool lifecycleOnly=std::getenv("CODEX_MP_LIFECYCLE_ONLY")!=NULL;
    bool nativeOnly=std::getenv("CODEX_MP_NATIVE_ONLY")!=NULL || incidentalOnly || lifecycleOnly;
    tWString initialInstalledMap, currentCacheMap;
    tString initialInstalledHash;

    int updateEffects(tString& error,float dt) {
        if(!genericEffectsDone) {
            const int result=incidentalRegression.Update(error,dt);
            if(result<=0) return result;
            genericEffectsDone=true;
        }
        return nativeEffectsRegression.Update(error,dt);
    }

    static tWString normalizedPath(const tWString& path) {
        std::error_code error;
        const auto normalized=std::filesystem::weakly_canonical(std::filesystem::path(path),error);
        if(error) return _W("");
        tWString result=cString::ToLowerCaseW(normalized.generic_wstring());
        while(result.size()>3 && result.back()=='/') result.pop_back();
        return result;
    }
    bool cachePathIsValid(bool transition=false) {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        if(cPlatform::FolderExists((std::filesystem::path(gpBase->msBaseSavePath)/_W("multiplayer_cache")).wstring())) {
            fail("multiplayer created a cache directory among game profiles");return false;
        }
        if(role!="client") {
            if(!mp->msReceivedMapPath.empty()) {fail("host unexpectedly owns a downloaded map cache path");return false;}
            return true;
        }
        const tWString path=cString::To16Char(mp->msLoadedMapPath);
        std::vector<uint8_t> bytes;
        if(path.empty() || mp->msExistingMapPath.empty() || !mp->msReceivedMapPath.empty() ||
           !mp->mbReusingMap || mp->mlDownloadedMapBytes!=0 ||
           loadObserver.samples[eLuxMultiplayerLoadPhase_Downloading]!=0 ||
           mp->GetLoadPhase()!=eLuxMultiplayerLoadPhase_None ||
           normalizedPath(path)!=normalizedPath(cString::To16Char(mp->msExistingMapPath)) ||
           !LuxReadMultiplayerMap(path,bytes) || luxnet::MapHash(bytes)!=mp->msMapHash) {
            fail("matching installed retail map was not reused directly without an XML download");return false;
        }
        if(transition) {
            if(initialInstalledMap.empty() || normalizedPath(path)==normalizedPath(initialInstalledMap) ||
               !LuxReadMultiplayerMap(initialInstalledMap,bytes) || luxnet::MapHash(bytes)!=initialInstalledHash) {
                fail("map transition changed or removed the previous installed retail map");return false;
            }
        }
        if(initialInstalledMap.empty()) {initialInstalledMap=path;initialInstalledHash=mp->msMapHash;}
        return true;
    }
    bool cacheWasRemoved() {
        if(!gpBase->mpMultiplayer->msReceivedMapPath.empty()) {fail("disconnect retained a received-map cache path");return false;}
        if(role!="client") return true;
        if((!currentCacheMap.empty() && (cPlatform::FileExists(currentCacheMap) ||
           cPlatform::FolderExists(std::filesystem::path(currentCacheMap).parent_path().wstring()) ||
           !gpBase->mpMultiplayer->msLoadedMapPath.empty())) ||
           cPlatform::FolderExists((std::filesystem::path(gpBase->msBaseSavePath)/_W("multiplayer_cache")).wstring())) {
            fail("disconnect retained the downloaded map, its directory, or a profile multiplayer cache");return false;
        }
        std::vector<uint8_t> bytes;
        if(initialInstalledMap.empty() || !LuxReadMultiplayerMap(initialInstalledMap,bytes) ||
           luxnet::MapHash(bytes)!=initialInstalledHash) {fail("disconnect changed or removed an installed retail map");return false;}
        return true;
    }
public:
    cGameSmoke() : iUpdateable("MultiplayerGameSmoke") {}
    void OnStart() {
        started=SDL_GetTicks();
        gpBase->mpEngine->SetWaitIfAppOutOfFocus(false);
        gpBase->mpEngine->GetSound()->GetLowLevel()->SetVolume(0);
        SDL_HideWindow(SDL_GL_GetCurrentWindow());
        gpBase->mpMapHandler->GetViewport()->AddRendererCallback(this);
    }
    void OnPostSolidDraw(cRendererCallbackFunctions*) { ++renderedWorldFrames; }
    void OnPostTranslucentDraw(cRendererCallbackFunctions*) {}
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
        LuxWorldWire::WriteLantern(packet,LuxWorldWire::Lantern());
        for(const auto& peer:gpBase->mpMultiplayer->mPeers) gpBase->mpMultiplayer->Send(peer.first,packet.bytes,false);
    }
    void Update(float dt) {
        if(state==99) return;
        if(role=="steam-host") { updateSteamHost();return; }
        if(exists(role=="host" ? "client-failed.txt" : "host-failed.txt")) {fail("peer's test process failed; stopping this instance");return;}
        if(SDL_GetTicks()-started>180000) {fail("180-second handshake/test timeout: "+gpBase->mpMultiplayer->GetStatus());return;}
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        tString loadingError;
        if(!loadObserver.Observe(loadingError)) {fail(loadingError);return;}
        if(SDL_GetTicks()-statusAt>2000) {
            printStatus(mp->GetStatus().c_str());statusAt=SDL_GetTicks();
        }
        if(state==0) {
            if(resizeTests) {
                const int resize=resizeRegression.Initial(loadingError);
                if(resize<0) {fail(loadingError);return;}
                if(!resize) return;
            }
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
                if(nativeOnly) settings.map="maps/main/ch01/01_old_archives.map";
                tString hostingError;
                if(!StartCurrentMapHost(settings,hostingError)) {fail("Host current map failed: "+hostingError);return;}
                mark("host-listening.txt",mp->GetStatus());
                printStatus("campaign loaded and listening");
            }
            else {
                if(!exists("host-listening.txt")) return;
                if(!mp->Join("127.0.0.1:"+cString::ToString(static_cast<int>(port)))) {fail("Join failed: "+mp->GetStatus());return;}
                if(mp->GetLoadPhase()!=eLuxMultiplayerLoadPhase_Connecting || !loadObserver.Observe(loadingError)) {
                    fail(loadingError.empty()?"joining did not immediately enter the connecting phase":loadingError);return;
                }
                cGuiSet* loadingSet=gpBase->mpEngine->GetGui()->GetSetFromName("LoadScreen");
                if(gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!="MultiplayerLoading" || !loadingSet || !loadingSet->IsActive()) {
                    fail("joining did not activate the multiplayer loading screen");return;
                }
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
            const int currentPickup=VerifyCurrentMapHostPickup(loadingError);
            if(currentPickup<0) {fail(loadingError);return;}
            if(currentPickup==0) return;
            if(resizeTests) {
                const int resize=resizeRegression.Session(loadingError);
                if(resize<0) {fail(loadingError);return;}
                if(!resize) return;
            }
            if(!cachePathIsValid()) return;
            if(gpBase->mpSaveHandler->AutoSave()) {fail("offline autosave was accepted during an active multiplayer session");return;}
            uint64_t bodyHash=0;
            for(const auto& body:mp->GetWorld()->mBodies) bodyHash+=body.first;
            const tString info="map="+mp->msMapName+" bodies="+cString::ToString(static_cast<int>(mp->GetWorld()->mBodies.size()))+
                " remotes="+cString::ToString(static_cast<int>(mp->GetWorld()->GetRemotePlayers().size()))+
                " epoch="+cString::ToString(static_cast<int>(mp->GetMapEpoch()));
            std::printf("%s READY %s body_hash=%llu\n",role.c_str(),info.c_str(),static_cast<unsigned long long>(bodyHash));std::fflush(stdout);
            mark(role+"-ready.txt",info);
            if(nativeOnly) {state=lifecycleOnly?48:incidentalOnly?46:40;return;}
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
            state=30;return;
        }
        if(state==30) {
            if(!exists("host-menus-passed.txt") || !exists("client-menus-passed.txt")) return;
            static const char* menus[]={"MainMenu","Inventory","Journal"};
            if(menuTrial==3) {
                mark(role+"-death-passed.txt","PASS: live rendering and local death recovery from pause, inventory, and journal, preserving session/map/inventory.");
                printStatus("PASS: pause/inventory/journal live scene rendering and local death recovery");
                state=4;return;
            }
            if(mp->IsWindowVisible()) mp->ToggleWindow();
            gpBase->mpEngine->GetUpdater()->SetContainer("Default");
            // Spawn/respawn is shared. Separate the players before screenshots
            // so the camera is not inside the other player's solid cylinder.
            iCharacterBody* character=gpBase->mpPlayer->GetCharacterBody();
            character->SetPosition(character->GetPosition()+cVector3f(role=="host" ? -0.75f : 0.75f,0,0));
            if(menuTrial==2) {
                gpBase->mpEngine->GetUpdater()->SetContainer("Inventory");
                gpBase->mpJournal->SetOpenedFromInventory(true);
            }
            gpBase->mpEngine->GetUpdater()->SetContainer(menus[menuTrial]);
            if(!gpBase->mpMapHandler->GetViewport()->IsVisible() || !gpBase->mpMapHandler->GetViewport()->IsActive()) {
                fail(tString("world viewport hidden behind ")+menus[menuTrial]);return;
            }
            menuRenderFrame=renderedWorldFrames;
            menuScreenshot=role+"-live-"+menus[menuTrial]+".png";
            readyAt=SDL_GetTicks();state=31;return;
        }
        if(state==31) {
            if(SDL_GetTicks()-readyAt<700) return;
            if(renderedWorldFrames<=menuRenderFrame) {fail("3D scene did not render while a game menu was open");return;}
            deathMap=gpBase->mpMapHandler->GetCurrentMap();
            mp->ShowWindow();
            gpBase->mpPlayer->GetHelperDeath()->SetShowHint(false);
            gpBase->mpPlayer->SetHealth(0);
            if(gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!="Default" ||
               gpBase->mpInputHandler->GetState()!=eLuxInputState_Game || mp->IsWindowVisible() ||
               gpBase->mpMainMenu->GetSet()->IsActive() || gpBase->mpInventory->GetSet()->IsActive() || gpBase->mpJournal->GetSet()->IsActive()) {
                fail("death left a menu/overlay active or input trapped in the wrong container");return;
            }
            deathStarted=SDL_GetTicks();state=32;return;
        }
        if(state==32) {
            if(!mp->IsActive() || gpBase->mpMapHandler->GetCurrentMap()!=deathMap || mp->GetMapEpoch()!=oldEpoch) {
                fail("local death reset the shared session or map");return;
            }
            gpBase->mpPlayer->GetHelperDeath()->OnPressButton();
            if(SDL_GetTicks()-deathStarted>18000) {fail("death recovery stalled with a game menu open");return;}
            if(SDL_GetTicks()-deathStarted<2000 || gpBase->mpPlayer->IsDead() || gpBase->mpPlayer->GetHelperDeath()->GetFadeAlpha()>0) return;
            if(!gpBase->mpInventory->GetItem("codex_transition_sentinel")) {fail("local death cleared inventory");return;}
            ++menuTrial;state=30;return;
        }
        if(state==4 && exists("host-death-passed.txt") && exists("client-death-passed.txt")) {
            normalPreparingFrames=loadObserver.frames[eLuxMultiplayerLoadPhase_Preparing];
            normalPreparationStarted=SDL_GetTicks();
            if(role=="host") {
                gpBase->mpMapHandler->ChangeMap("01_old_archives.map","PlayerStartArea_1","","");
                // Hold only this accepted test request until a real client
                // preparation frame is confirmed; production adds no delay.
                if(!gpBase->mpMapHandler->mMapChangeData.mbActive) {fail("normal map change was not accepted");return;}
                gpBase->mpMapHandler->mMapChangeData.mbActive=false;heldNormalMapChange=true;
            }
            state=5;return;
        }
        if(state==5) {
            if(role=="host" && heldNormalMapChange) {
                if(SDL_GetTicks()-normalPreparationStarted>10000) {fail("client did not render preparation before normal host map load");return;}
                if(exists("client-normal-map-preparing-rendered.txt")) {
                    gpBase->mpMapHandler->mMapChangeData.mbActive=true;heldNormalMapChange=false;
                    mark("host-normal-map-load-released.txt","client preparation rendered before normal host load");
                }
            } else if(role=="client" && mp->GetLoadPhase()==eLuxMultiplayerLoadPhase_Preparing &&
                      loadObserver.frames[eLuxMultiplayerLoadPhase_Preparing]>normalPreparingFrames) {
                if(mp->GetMapEpoch()!=oldEpoch || gpBase->mpMapHandler->GetCurrentMap()!=deathMap) {
                    fail("preparation arrived after the old map had already changed");return;
                }
                mark("client-normal-map-preparing-rendered.txt","old map suspended and loading screen rendered before host load");
            }
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
            if(!cachePathIsValid(true)) return;
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
            state=40;return;
        }
        if(state==40) {
            tString error;const int native=nativeRegression.Update(error);
            if(native<0) {fail(error);return;}
            if(native>0) {
                mark(role+"-native-passed.txt","PASS: exclusive host/client pickups and callbacks, failed/successful ignition, door states, static bookshelf movement, and late-join native baseline replay.");
                printStatus("PASS: native pickup/ignition authority, door/mover replication, and late-join baseline replay");
                state=41;
            }
            return;
        }
        if(state==41) {
            tString error;const int drawers=drawerRegression.Update(error);
            if(drawers<0) {fail(error);return;}
            if(drawers>0) {
                mark(role+"-drawers-passed.txt","PASS: two unchanged retail nice chests, unique drawer IDs, host/client slide controllers, exclusive leases, and replicated movement.");
                printStatus("PASS: retail nice-chest host/client drawer interaction, ownership, and replication");
                state=44;
            }
            return;
        }
        if(state==44) {
            if(!exists("host-drawers-passed.txt") || !exists("client-drawers-passed.txt")) return;
            tString error;const int sounds=soundRegression.Update(error);
            if(sounds<0) {fail(error);return;}
            if(sounds>0) {
                mark(role+"-sounds-passed.txt","PASS: real host optional sound preloads remain nonfatal; malformed preloads and missing playback stay rejected.");
                printStatus("PASS: optional sound preloads, reliable script barrier, and strict actual playback");
                state=45;
            }
            return;
        }
        if(state==45) {
            if(!exists("host-sounds-passed.txt") || !exists("client-sounds-passed.txt")) return;
            tString error;const int effects=updateEffects(error,dt);
            if(effects<0) {fail(error);return;}
            if(effects>0) {
                mark(role+"-incidental-passed.txt","PASS: shared native sound/particle lifecycle, player presentation, and echo guards.");
                printStatus("PASS: shared native sound/particle lifecycle and echo guards");state=48;
            }
            return;
        }
        if(state==48) return; // Presentation is checked after the native PostUpdate.
        if(state==49) {
            if(!exists("host-lantern-finished.txt") || !exists("client-lantern-finished.txt")) return;
            tString error;const int joints=jointRegression.Update(error,dt);
            if(joints<0) {fail(error);return;}
            if(joints>0) {
                printStatus("PASS: native joint destruction, retained references, save/baseline and pending interaction lifecycle");state=lifecycleOnly?50:43;
            }
            return;
        }
        if(state==50) {
            if(!exists("host-joint-passed.txt") || !exists("client-joint-passed.txt")) return;
            mp->Stop("Player and joint lifecycle regression complete.");
            mark(role+"-passed.txt","PASS: remote lantern, menu player updates and joint destruction lifecycle.");
            result=0;gpBase->mpEngine->Exit();state=99;return;
        }
        if(state==46) {
            tString error;const int effects=updateEffects(error,dt);
            if(effects<0) {fail(error);return;}
            if(effects>0) {
                mark(role+"-incidental-passed.txt","PASS: focused shared native sound/particle lifecycle and echo guards.");
                printStatus("PASS: focused shared native sound/particle lifecycle and echo guards");state=47;
            }
            return;
        }
        if(state==47) {
            if(!exists("host-incidental-passed.txt") || !exists("client-incidental-passed.txt")) return;
            mp->Stop("Incidental effects regression complete.");
            mark(role+"-passed.txt","PASS: shared native sound/particle lifecycle, player presentation, and echo guards.");
            result=0;gpBase->mpEngine->Exit();state=99;return;
        }
        if(state==43) {
            if(!exists("host-sounds-passed.txt") || !exists("client-sounds-passed.txt")) return;
            tString error;const int loading=loadingRegression.Update(loadObserver,error);
            if(loading<0) {fail(error);return;}
            if(loading>0) {
                mark(role+"-loading-passed.txt","PASS: same-map teleport stays live; normal/debug missing-map preparation renders before load and cancellation restores old world.");
                printStatus("PASS: early client preparation, live same-map teleport, and failed map-load recovery");
                state=42;
            }
            return;
        }
        if(state==42) {
            if(!exists("host-loading-passed.txt") || !exists("client-loading-passed.txt")) return;
            tString error;const int cache=mapCacheRegression.Update(loadObserver,error);
            if(cache<0) {fail(error);return;}
            if(cache>0) {
                currentCacheMap=mapCacheRegression.CurrentWorkingMap();
                mark(role+"-cache-negotiation-passed.txt","PASS: installed reuse, cold download, persistent cache hit, corrupt-cache repair, and same-name host edit.");
                if(nativeOnly) mark(role+"-passed.txt","PASS: focused native multiplayer, retail drawer, and map cache negotiation regression.");
                printStatus("PASS: installed map reuse, cold download, cache hit, corrupt-cache repair, and changed host content");
                state=7;
            }
            return;
        }
        if(state==7 && exists("host-cache-negotiation-passed.txt") && exists("client-cache-negotiation-passed.txt")) {
            gpBase->mpEngine->GetUpdater()->SetContainer("MainMenu");
            mp->Stop("Integration test complete.");
            if(!cacheWasRemoved()) return;
            mark(role+"-cache-passed.txt",role=="client" ?
                "PASS: installed XML is preserved; downloaded working copies stay outside profiles and are removed on transition/disconnect.":
                "PASS: host never owns a received-map cache path.");
            mark(role+"-session-stopped.txt","session stopped and working copy cleaned");
            if(gpBase->mpSaveHandler->AutoSave()) {fail("offline autosave was accepted for the disconnected multiplayer world");return;}
            mark(role+"-save-guard-passed.txt","PASS: autosave rejected during session and after local disconnect.");
            if(role=="host") {
                gpBase->Reset();
                gpBase->mpMainMenu->OnLeaveContainer("");
                gpBase->mpMainMenu->OnEnterContainer("");
            }
            readyAt=SDL_GetTicks();state=8;return;
        }
        if(state==8) {
            if(!exists("host-session-stopped.txt") || !exists("client-session-stopped.txt")) return;
            tString error;
            if(!mapCacheRegression.Cleanup(error)) {fail(error);return;}
            if(SDL_GetTicks()-readyAt<1500) return;
            if(gpBase->mpMapHandler->GetCurrentMap() || gpBase->mpMapHandler->GetViewport()->GetWorld()) {
                fail("return to title menu retained a gameplay world after disconnect/reset");return;
            }
            mark(role+"-disconnect-render-passed.txt","PASS: title menu renders after disconnect/reset with no stale gameplay world.");
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
            if(!cachePathIsValid()) return;
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
            if(!cacheWasRemoved()) return;
            if(mp->IsActive() || mp->GetSteamLobbyID()) {fail("Steam lobby remained active after stop");return;}
            mark(role+"-passed.txt","PASS: real Steam campaign lobby, continuous menu updates, map transition with same lobby, clean disconnect.");
            printStatus("PASS: Steam campaign hosting, menu updates, map transition, and lobby teardown");
            result=0;state=99;gpBase->mpEngine->Exit();
        }
    }
    void PostUpdate(float dt) {
        if(state!=48 || role=="steam-host") return;
        if(!lifecycleOnly && (!exists("host-incidental-passed.txt") || !exists("client-incidental-passed.txt"))) return;
        tString error;const int lantern=lanternRegression.Update(error,dt);
        if(lantern<0) {fail(error);return;}
        if(lantern>0) {
            printStatus("PASS: remote lantern main light, holster/oil lifecycle, and native player updates in menus");state=49;
        }
    }
    void OnPostRender(float dt) {
        tString loadingError;
        const cVector2l renderSize=gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt();
        if(resizeTests && renderSize!=resizeValidated) {
            if(!cWindowResizeRegression::ValidateTargets(loadingError)) {fail(loadingError);return;}
            if(!RunGuiAspectRegression(loadingError)) {fail(loadingError);return;}
            resizeValidated=renderSize;
        }
        if(!loadObserver.OnPostRender(loadingError)) {fail(loadingError);return;}
        if(!loadingScreenshotDone && gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()=="MultiplayerLoading") {
            cBitmap* bitmap=gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
            if(!bitmap) {fail("loading screen screenshot readback failed");return;}
            gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(bitmap,cString::To16Char(outputDir+"/"+role+"-loading.png"),0);
            hplDelete(bitmap);loadingScreenshotDone=true;
        }
        if(!menuScreenshot.empty() && SDL_GetTicks()-readyAt>=450) {
            cBitmap* bitmap=gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
            if(!bitmap) {fail("live menu screenshot readback failed");return;}
            gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(bitmap,cString::To16Char(outputDir+"/"+menuScreenshot),0);
            hplDelete(bitmap);menuScreenshot.clear();
        }
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
