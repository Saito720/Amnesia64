#include "hpl.h"
#include "LuxTypes.h"
#include "impl/LowLevelGraphicsSDL.h"
#include "impl/LowLevelInputSDL.h"
#include "network/NetworkTransport.h"
#include "LuxMultiplayerCache.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#undef main

// Metadata calls are isolated from the real user's downloaded maps. The
// production UI still chooses when to query and renders the actual label.
static cLuxMultiplayerMapCacheStats cacheStats={2621440,3};
static unsigned cacheQueries=0;
hpl::tWString LuxMultiplayerCacheRoot() { return _W(""); }
cLuxMultiplayerMapCacheStats LuxGetMultiplayerMapCacheStats(const hpl::tWString&) {
    ++cacheQueries;return cacheStats;
}

// Exercise production UI and real SDL/HPL2/OpenGL integration without launching
// a campaign or opening network sockets. Only coordinator/application methods
// are replaced; no production UI/event/rendering code is copied here.
#define LUX_BASE_H
#define LUX_INPUT_HANDLER_H
#define LUX_MULTIPLAYER_H
class cLuxMultiplayerUI;
struct cLuxMultiplayerSettings {
    tString map, startPos;
    unsigned short port = 27015;
    unsigned maxPlayers = 4;
    bool allowClientMapChanges = false;
    bool allPlayersTriggerScripts = true;
    bool playerCollision = false;
    bool useSteam = true;
    bool publicLobby = false;
};
struct cLuxMultiplayer {
    cLuxMultiplayerUI* ui = NULL;
    bool IsWindowVisible() const;
    bool active = false, host = false;
    bool ready = true;
    int hosts = 0, currentHosts = 0, joins = 0, stops = 0, cacheClears = 0;
    bool currentMapAvailable = false;
    bool steamAvailable = false, steamSession = false, steamSearchPending = false;
    bool steamOverlayActive = false;
    uint64_t lobbyID = 0, pendingInvite = 0;
    int steamJoins = 0, steamRefreshes = 0, steamInvites = 0, steamAccepts = 0, steamDismisses = 0, steamRetries = 0;
    tString lastLobbyCode;
    std::vector<hpl::cSteamLobbyInfo> lobbies;
    cLuxMultiplayerSettings lastSettings;
    tString lastAddress, status = "No multiplayer session.";
    bool IsActive() const { return active; }
    bool IsHost() const { return host; }
    bool IsClient() const { return active && !host; }
    bool IsReady() const { return ready; }
    const tString& GetStatus() const { return status; }
    bool Host(const cLuxMultiplayerSettings& settings) {
        ++hosts; lastSettings=settings; host=active=true; steamSession=settings.useSteam;
        lobbyID=steamSession ? 109775244398475112ull : 0; status="Hosting test session."; return true;
    }
    bool GetCurrentMapForHosting(tString& map, tString& reason) const {
        map=currentMapAvailable?"maps/main/ch01/00_rainy_hall.map":"";
        reason=currentMapAvailable?"":"Load a game or map first to host it without restarting.";
        return currentMapAvailable;
    }
    bool HostCurrentMap(const cLuxMultiplayerSettings& settings) {
        ++currentHosts;lastSettings=settings;host=active=true;return true;
    }
    bool Join(const tString& address) { ++joins; lastAddress=address; active=true; host=false; steamSession=false; return true; }
    void Stop(const tString&) { ++stops; active=false; steamSession=false; lobbyID=0; status="No multiplayer session."; }
    void ClearMapCache() { ++cacheClears;cacheStats=cLuxMultiplayerMapCacheStats(); }
    bool JoinSteamLobby(const tString& code) {
        ++steamJoins; lastLobbyCode=code; active=steamSession=true; host=false; return true;
    }
    bool IsSteamAvailable() const { return steamAvailable; }
    bool IsSteamOverlayActive() const { return steamOverlayActive; }
    void RetrySteam() { ++steamRetries; steamAvailable=true; }
    tString GetSteamStatus() const { return steamAvailable ? "Steam is ready. Signed in as Test Player." :
        "Steam is unavailable. Start Steam, sign in, and restart the game. Offline play remains available."; }
    bool IsSteamSession() const { return steamSession; }
    uint64_t GetSteamLobbyID() const { return lobbyID; }
    void RefreshSteamLobbies() { ++steamRefreshes; steamSearchPending=true; }
    bool IsSteamLobbySearchPending() const { return steamSearchPending; }
    const std::vector<hpl::cSteamLobbyInfo>& GetSteamLobbies() const { return lobbies; }
    void InviteSteamFriends() { ++steamInvites; }
    uint64_t GetPendingSteamInvite() const { return pendingInvite; }
    void AcceptSteamInvite() { ++steamAccepts; pendingInvite=0; }
    void DismissSteamInvite() { ++steamDismisses; pendingInvite=0; }
};
struct TestPlayer {
    int releases = 0, stopRun = 0;
    void DoAction(eLuxPlayerAction, bool down) { if(!down) ++releases; }
    void Run(bool down) { if(!down) ++stopRun; }
    void Jump(bool) {}
    void Crouch(bool) {}
    void SetLean(int) {}
};
struct TestMapHandler {
    bool loaded = false;
    void* GetCurrentMap() { return loaded ? this : NULL; }
};
struct cLuxInputHandler {
    eLuxInputState state = eLuxInputState_MainMenu;
    eLuxInputState& mState = state;
    bool mbMultiplayerCapturing = false;
    cInput* mpInput = NULL;
    TestPlayer* mpPlayer = NULL;
    int globalUpdates = 0, gameUpdates = 0, menuUpdates = 0;
    int resets = 0;
    eLuxInputState GetState() { return state; }
    void ResetSmoothMousePos() { ++resets; }
    void Update(float);
    void UpdateGlobalInput() { ++globalUpdates; }
    void UpdateGameInput() { ++gameUpdates; }
    void UpdateMainMenuInput() { ++menuUpdates; }
    void UpdatePreMenuInput() {}
    void UpdateInventoryInput() {}
    void UpdateJournalInput() {}
    void UpdateDebugInput() {}
    void UpdateCreditsInput() {}
    void UpdateDemoEndInput() {}
    void UpdateLoadScreenInput() {}
};
struct cLuxBase {
    cEngine* mpEngine;
    cLuxInputHandler* mpInputHandler;
    TestMapHandler* mpMapHandler;
    cLuxMultiplayer* mpMultiplayer;
    tString msStartMapFile="00_rainy_hall.map", msStartMapFolder="maps/main/ch01/";
};
static cLuxBase base;
static tString outputDirectory;
cLuxBase* gpBase = &base;
#define private public
#include "LuxMultiplayerUI.h"
#undef private
#include "LuxMultiplayerUI.cpp"
#include "LuxMultiplayerContent.cpp"
#include "generated_update.cpp"
#include "imgui_internal.h"
bool cLuxMultiplayer::IsWindowVisible() const { return ui && ui->IsVisible(); }

static void require(bool result, const char* text) {
    if(!result) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(2); }
}
static void event(Uint32 type, SDL_Scancode scan, SDL_Keycode sym, Uint8 repeat=0) {
    SDL_Event e={}; e.type=type; e.key.type=type; e.key.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
    e.key.state=type==SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode=scan; e.key.keysym.sym=sym; e.key.repeat=repeat;
    require(SDL_PushEvent(&e)==1,"inject SDL keyboard event");
    base.mpEngine->GetInput()->Update(1.0f/60);
}
static void draw(cLuxMultiplayerUI& ui) {
    ui.Update(1.0f/60);
    iLowLevelGraphics* low=base.mpEngine->GetGraphics()->GetLowLevel();
    low->SetCurrentFrameBuffer(NULL);
    low->SetClearColor(cColor(0.055f,0.063f,0.08f,1));
    low->ClearFrameBuffer(eClearFrameBufferFlag_Color|eClearFrameBufferFlag_Depth);
    ui.Draw();
    low->WaitAndFinishRendering();
}
static void screenshot(const char* name) {
    cBitmap* bitmap=base.mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
    require(bitmap!=NULL,"framebuffer readback");
    require(base.mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(bitmap,
        cString::To16Char(outputDirectory+"/"+name),0),"save screenshot");
    hplDelete(bitmap);
}
static void selectTab(const char* name) {
    ImGuiWindow* window=ImGui::FindWindowByName("Multiplayer");
    require(window!=NULL,"multiplayer window exists");
    ImGuiTabBar* tabBar=ImGui::GetCurrentContext()->TabBars.GetByKey(window->GetID("SessionMode"));
    require(tabBar!=NULL,"session tab bar exists");
    bool found=false;
    for(int i=0;i<tabBar->Tabs.Size;++i) {
        ImGuiTabItem& tab=tabBar->Tabs[i];
        if(std::strcmp(ImGui::TabBarGetTabName(tabBar,&tab),name)==0) {
            tabBar->NextSelectedTabId=tab.ID; found=true; break;
        }
    }
    require(found,"requested session tab exists");
}
static void resizeOverlay(cLuxMultiplayerUI& ui,int width,int height) {
    SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),width,height);
    base.mpEngine->GetInput()->Update(1.0f/60);
    auto* low=base.mpEngine->GetGraphics()->GetLowLevel();
    if(low->UpdateScreenSize()) base.mpEngine->GetUpdater()->BroadcastMessageToAll(eUpdateableMessage_OnScreenResize);
    for(int i=0;i<4;++i) draw(ui);
}
static void requireVisibleWindow(const char* name,int width,int height) {
    ImGuiWindow* window=ImGui::FindWindowByName(name);
    require(window && window->Active,"resized overlay remains active");
    require(window->Pos.x>=0 && window->Pos.y>=0 && window->Pos.x+window->Size.x<=width+1 &&
        window->Pos.y+window->Size.y<=height+1,"resized overlay including its title and close controls stays inside the viewport");
}
int main(int argc,char** argv) {
    if(argc!=4) {std::fprintf(stderr,"Usage: ui width height output-directory\n");return 2;}
    outputDirectory=argv[3];
    SetLogFile(cString::To16Char(outputDirectory+"/hpl.log"));
    cResources::SetForceCacheLoadingAndSkipSaving(true);
    cEngineInitVars vars;
    vars.mGraphics.mvScreenSize=argc>=3 ? cVector2l(std::atoi(argv[1]),std::atoi(argv[2])) : cVector2l(1024,768);
    vars.mGraphics.mvWindowPosition=cVector2l(-10000,-10000);
    vars.mGraphics.msWindowCaption="Multiplayer overlay verification";
    vars.mSound.mbUseHRTF=false; vars.mSound.mbUseThreading=false;
    base.mpEngine=CreateHPLEngine(eHplAPI_OpenGL,eHplSetup_Screen,&vars);
    require(base.mpEngine!=NULL,"engine creation");
    SDL_HideWindow(SDL_GL_GetCurrentWindow());
    cLuxInputHandler input; base.mpInputHandler=&input;
    cLuxMultiplayer session;
    cGuiSet* menuSet=base.mpEngine->GetGui()->CreateSet("TestMainMenu",NULL);
    base.mpEngine->GetGui()->SetFocus(menuSet);
    TestMapHandler map; TestPlayer player;
    base.mpMultiplayer=&session; base.mpMapHandler=&map;
    input.mpInput=base.mpEngine->GetInput(); input.mpPlayer=&player;
    {
        cLuxMultiplayerUI ui(&session);
        session.ui=&ui;
        require(ui.mpContext!=NULL,"real SDL2/OpenGL ImGui backend initialization");
        require(!ui.IsVisible(),"starts hidden in main menu");
        event(SDL_KEYDOWN,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE);
        require(ui.IsVisible() && ui.IsCapturingInput(),"tilde opens globally in main menu");
        require(!menuSet->GetDrawMouse(),"overlay hides underlying HPL cursor");
        input.Update(1.0f/60);
        require(input.globalUpdates==0 && input.menuUpdates==0,"production input Update blocks underlying menu and global keys");
        require(player.releases==0,"opening in main menu without map does not access player");
        event(SDL_KEYDOWN,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE,1);
        require(ui.IsVisible(),"key repeat does not toggle");
        event(SDL_KEYUP,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE);
        event(SDL_KEYDOWN,SDL_SCANCODE_W,SDLK_w);
        require(base.mpEngine->GetInput()->GetKeyboard()->KeyIsDown(eKey_W),"observer preserves engine keydown");
        event(SDL_KEYUP,SDL_SCANCODE_W,SDLK_w);
        require(!base.mpEngine->GetInput()->GetKeyboard()->KeyIsDown(eKey_W),"observer preserves releases while captured");
        for(int i=0;i<3;++i) draw(ui);
        require(ImGui::GetDrawData()->TotalVtxCount>200,"advanced window rendered real geometry");
        require(cacheQueries==0,"collapsed downloaded-map controls never enumerate the cache");
        screenshot("advanced.png");
        require(ui.mbUseSteam && !ui.mbPublicLobby,"advanced defaults to Steam friends-only");
        require(session.hosts==0 && session.joins==0,"Steam unavailable rendering never falls back to direct IP");
        const int frame=ImGui::GetFrameCount();
        base.mpEngine->GetGraphics()->GetLowLevel()->SwapBuffers();
        require(ImGui::GetFrameCount()==frame+1,"engine pre-swap callback renders globally");
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        require(!ui.IsVisible(),"Escape closes overlay");
        require(menuSet->GetDrawMouse(),"closing restores HPL cursor");
        input.Update(1.0f/60);
        require(input.menuUpdates==0,"closing key is swallowed for the release frame");
        input.Update(1.0f/60);
        require(input.menuUpdates==1,"underlying menu input resumes next tick");
        event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        require(input.resets>0,"close resets mouse smoothing");
        require(input.state==eLuxInputState_MainMenu,"overlay preserves main menu container/input state");
        ui.Show(true);
        for(int i=0;i<3;++i) draw(ui);
        screenshot("campaign.png");
        require(ui.mbCampaign,"main menu entry uses campaign defaults");
        // Coordinator action dispatch is deliberately tested separately from rendering:
        // an action queued in a render pass must not load a map before the next tick.
        ui.mlPort=31234; ui.mlMaxPlayers=12; ui.mbUseSteam=false; ui.mbPublicLobby=true; ui.mbPlayerCollision=true;
        std::strcpy(ui.msMap,"ignored-by-campaign.map");
        ui.mlPendingAction=10; ui.Draw();
        require(session.steamRetries==0 && !session.steamAvailable,"render does not retry Steam initialization");
        ui.Update(1.0f/60);
        require(session.steamRetries==1 && session.steamAvailable,"Steam can be retried after an unavailable launch");
        ui.mlPendingAction=1;
        ui.Draw();
        require(session.hosts==0,"render does not execute host/map operations");
        ui.Update(1.0f/60);
        require(session.hosts==1 && session.lastSettings.map.empty() && session.lastSettings.port==27015 && session.lastSettings.maxPlayers==4 && session.lastSettings.useSteam && !session.lastSettings.publicLobby && !session.lastSettings.playerCollision,"campaign ignores advanced settings and uses Steam friends-only");
        for(int i=0;i<3;++i) draw(ui);
        screenshot("steam-host.png");
        ImGuiWindow* cacheWindow=ImGui::FindWindowByName("Multiplayer");
        require(cacheWindow!=NULL,"active multiplayer window exists for downloaded-map settings");
        cacheWindow->StateStorage.SetInt(cacheWindow->GetID("Downloaded maps"),1);
        ui.Draw();
        require(cacheQueries==0,"expanding downloaded-map controls defers metadata enumeration from rendering");
        for(int i=0;i<3;++i) draw(ui);
        require(cacheQueries==1 && ui.mCacheStats.maps==3 && ui.mCacheStats.bytes==2621440 && ui.mbCacheStatsKnown,
            "expanded cache displays the stored map count and total byte size");
        screenshot("downloaded-maps.png");
        for(int i=0;i<20;++i) ui.Update(0.1f);
        require(cacheQueries==1,"visible cache metadata is throttled instead of scanned every frame");
        cacheStats={3145728,4};
        ui.Update(5.0f);
        require(cacheQueries==2 && ui.mCacheStats.bytes==3145728 && ui.mCacheStats.maps==4,
            "periodic refresh notices completed downloads and other cache changes");
        cacheWindow->StateStorage.SetInt(cacheWindow->GetID("Downloaded maps"),0);ui.Draw();
        ui.Update(10.0f);
        require(cacheQueries==2,"collapsed cache section does not poll metadata");
        cacheWindow->StateStorage.SetInt(cacheWindow->GetID("Downloaded maps"),1);ui.Draw();
        require(cacheQueries==2,"reopening section never scans during rendering");
        ui.Update(1.0f/60);
        require(cacheQueries==3,"reopened section immediately refreshes metadata on the next update");
        ui.mlPendingAction=11; ui.Draw();
        require(session.cacheClears==0,"cache deletion never runs while rendering the multiplayer window");
        ui.Update(1.0f/60);
        require(session.cacheClears==1 && session.active,"cache deletion is deferred and preserves an active session");
        require(cacheQueries==4 && ui.mCacheStats.bytes==0 && ui.mCacheStats.maps==0,
            "deleting downloaded maps immediately refreshes displayed size to zero");
        for(int i=0;i<3;++i) draw(ui);
        screenshot("downloaded-maps-empty.png");
        ui.mlPendingAction=6;
        ui.Draw();
        require(session.steamInvites==0,"render does not open the Steam overlay");
        ui.Update(1.0f/60);
        require(session.steamInvites==1,"explicit Invite friends action is deferred");
        char* previousClipboard=SDL_GetClipboardText();
        ui.mlPendingAction=9; ui.Update(1.0f/60);
        char* clipboard=SDL_GetClipboardText();
        require(clipboard && std::strcmp(clipboard,"109775244398475112")==0,"copy preserves full 64-bit lobby code");
        SDL_free(clipboard);
        SDL_SetClipboardText(previousClipboard ? previousClipboard : "");
        SDL_free(previousClipboard);
        session.pendingInvite=109775244398475113ull;
        for(int i=0;i<3;++i) draw(ui);
        screenshot("steam-invitation.png");
        require(session.steamAccepts==0 && session.stops==0,"incoming invitation never leaves an active session automatically");
        ui.mlPendingAction=8; ui.Update(1.0f/60);
        require(session.steamDismisses==1 && !session.pendingInvite && session.active,"dismiss invitation preserves active session");
        session.pendingInvite=109775244398475114ull;
        ui.mlPendingAction=7; ui.Draw();
        require(session.steamAccepts==0,"accept invitation defers coordinator work from rendering");
        ui.Update(1.0f/60);
        require(session.steamAccepts==1,"explicit Join invited session action dispatched");
        ui.mlPendingAction=3; ui.Update(1.0f/60);
        ui.mlPendingAction=11; ui.Update(1.0f/60);
        require(session.cacheClears==2 && !session.active,"downloaded maps can also be deleted while disconnected");
        ui.Toggle(); input.state=eLuxInputState_Game;
        map.loaded=true;
        event(SDL_KEYDOWN,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE);
        require(ui.IsVisible() && !ui.mbCampaign,"tilde opens advanced window in game");
        input.Update(1.0f/60);
        input.Update(1.0f/60);
        require(input.gameUpdates==0 && player.releases==3 && player.stopRun==1,"production input releases held interactions once and suppresses game updates");
        event(SDL_KEYUP,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE);
        ui.mlPendingAction=1; ui.Update(1.0f/60);
        require(session.hosts==2 && session.lastSettings.map=="ignored-by-campaign.map" && session.lastSettings.port==31234 && session.lastSettings.maxPlayers==12 && !session.lastSettings.useSteam && session.lastSettings.playerCollision,"advanced direct-IP and player-collision settings passed to coordinator");
        ui.mlPendingAction=3; ui.Update(1.0f/60);
        ui.mbUseSteam=true;
        ui.mlPendingAction=1; ui.Update(1.0f/60);
        require(session.hosts==3 && session.lastSettings.useSteam && session.lastSettings.publicLobby && session.lastSettings.map=="ignored-by-campaign.map","advanced Steam visibility and map settings passed to coordinator");
        ui.mlPendingAction=3; ui.Update(1.0f/60);
        ui.mbHostCurrentMap=true;
        ui.Update(1.1f);
        for(int i=0;i<3;++i) draw(ui);
        require(!ui.mbCanHostCurrentMap && !ui.mbHostCurrentMap && !ui.msCurrentMapReason.empty(),"menu/background availability hides and clears current-map hosting");
        ui.mlPendingAction=12;ui.Update(1.0f/60);
        require(session.currentHosts==0 && !session.active,"stale current-map action cannot host an unavailable or menu background world");
        screenshot("host-current-unavailable.png");
        session.currentMapAvailable=true;ui.Update(1.1f);
        require(ui.mbCanHostCurrentMap && ui.msCurrentMap=="maps/main/ch01/00_rainy_hall.map","current-map availability refreshes outside rendering");
        ui.mbHostCurrentMap=true;
        for(int i=0;i<3;++i) draw(ui);
        screenshot("host-current.png");
        ui.mlPendingAction=12;ui.Draw();
        require(session.currentHosts==0,"render does not start hosting the current world");
        ui.Update(1.0f/60);
        require(session.currentHosts==1 && session.hosts==3 && session.lastSettings.maxPlayers==12 && session.lastSettings.playerCollision,
            "current-map hosting uses the preserving coordinator path and advanced settings");
        ui.mlPendingAction=3;ui.Update(1.0f/60);ui.mbHostCurrentMap=false;
        const tWString browserFixture=cString::To16Char(outputDirectory)+_W("/map-browser");
        require(cPlatform::CreateFolder(browserFixture),"create isolated map-browser fixture");
        require(cPlatform::CreateFolder(browserFixture+_W("/subfolder")),"create nested map-browser folder");
        for(const wchar_t* name:{L"selected.map",L"uppercase.MAP",L"ignored.hps"}) {
            FILE* file=cPlatform::OpenFile(browserFixture+_W('/')+name,_W("wb"));
            require(file!=NULL,"create map-browser fixture file");
            std::fputs(std::wcscmp(name,L"selected.map")==0 ?
                "<Level><MapData><MapContents><Entities><Area Name=\"Z_Start\" AreaType=\"PlayerStart\" Active=\"false\"/>"
                "<Area Name=\"A_Start\" AreaType=\"PlayerStart\"/><Area Name=\"Z_Start\" AreaType=\"PlayerStart\"/>"
                "<Area Name=\"Trigger\" AreaType=\"Script\"/><Entity Name=\"NotAStart\" AreaType=\"PlayerStart\"/>"
                "</Entities></MapContents></MapData></Level>" : "<Level><MapData><MapContents><Entities/></MapContents></MapData></Level>",file);
            std::fclose(file);
        }
        ui.msMapBrowserDirectory=browserFixture;ui.mlPendingAction=13;ui.Draw();
        require(!ui.mbMapBrowserOpen && ui.mlstMapBrowserFiles.empty(),"browser opening and directory enumeration are deferred from rendering");
        ui.Update(1.0f/60);
        require(ui.mbMapBrowserOpen && ui.mlstMapBrowserFiles.size()==2 && ui.mlstMapBrowserFolders.size()==1,
            "browser includes only XML map extensions and navigable folders");
        for(int i=0;i<3;++i) draw(ui);
        screenshot("map-browser.png");
        const tWString originalBrowserDirectory=ui.msMapBrowserDirectory;
        ui.msPendingMapBrowserDirectory=browserFixture+_W("/missing");ui.Update(1.0f/60);
        require(!ui.msMapBrowserError.empty() && ui.msMapBrowserDirectory==originalBrowserDirectory && ui.mlstMapBrowserFiles.size()==2,
            "failed folder navigation preserves the previous listing and reports an error");
        ui.msPendingMapBrowserDirectory=browserFixture+_W("/subfolder");ui.Draw();
        require(ui.mlstMapBrowserFiles.size()==2,"folder navigation does no filesystem work while rendering");
        ui.Update(1.0f/60);
        require(ui.mlstMapBrowserFiles.empty() && ui.msMapBrowserError.empty(),"folder navigation refreshes the listing in Update");
        ui.QueueMapBrowserParent();ui.Draw();
        require(ui.mlstMapBrowserFiles.empty(),"Up directory navigation is deferred from rendering");
        ui.Update(1.0f/60);
        require(ui.msMapBrowserDirectory==originalBrowserDirectory && ui.mlstMapBrowserFiles.size()==2,
            "Up correctly leaves an extensionless directory and restores its parent's listing");
        ui.msSelectedMap=ui.msMapBrowserDirectory+_W("selected.map");ui.mlPendingAction=14;
        const tString previousMap=ui.msMap;ui.Draw();
        require(ui.msMap==previousMap,"map selection acceptance waits for Update");
        ui.Update(1.0f/60);ui.Draw();
        require(!ui.mbMapBrowserOpen && ui.msMap==cString::To8Char(ui.msSelectedMap) && session.hosts==3,
            "browser fills the selected map without starting or restarting a session");
        require(ui.mvStartPositions.empty() && ui.mbStartPositionsDirty,"map acceptance clears stale start positions and defers map parsing");
        ui.Draw();require(ui.mvStartPositions.empty(),"rendering never parses a selected map");
        ui.Update(0.3f);
        require(ui.msStartPositionError.empty() && ui.mvStartPositions==std::vector<tString>({"Z_Start","A_Start"}),
            "start dropdown uses authored PlayerStart area order, includes inactive starts, and excludes other entity types and duplicate names");
        std::strcpy(ui.msStartPos,"A_Start");ui.mlPendingAction=1;ui.Update(1.0f/60);
        require(session.lastSettings.startPos=="A_Start","chosen start position reaches hosting settings");
        ui.mlPendingAction=3;ui.Update(1.0f/60);
        std::strcpy(ui.msMap,cString::To8Char(browserFixture+_W("/uppercase.MAP")).c_str());
        ui.Update(0.1f);
        require(ui.msStartPos[0]=='\0' && ui.mvStartPositions.empty() && ui.mbStartPositionsDirty,
            "changing a typed map clears the previous selection before the debounce or a host action");
        ui.Update(0.3f);
        require(ui.mvStartPositions.empty() && ui.msStartPositionError.empty() && !ui.mbStartPositionsDirty,
            "a valid map without starts offers only the map default");
        std::strcpy(ui.msMap,cString::To8Char(browserFixture+_W("/missing.map")).c_str());ui.Update(0.3f);
        require(ui.mvStartPositions.empty() && !ui.msStartPositionError.empty(),"an unavailable map reports a read error without retaining stale starts");
        const tString broken="<Level><MapData><MapContents>";
        std::vector<tString> parsedStarts; tString parseError;
        require(!LuxCollectMultiplayerStartPositions(std::vector<uint8_t>(broken.begin(),broken.end()),parsedStarts,parseError),
            "malformed XML is rejected by the bounded start-position parser");
        ui.mlPendingAction=13;ui.Update(1.0f/60);ui.Draw();
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);ui.Draw();
        require(!ui.mbMapBrowserOpen && ui.IsVisible(),"Escape closes the file browser while keeping multiplayer controls open");
        event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        for(const wchar_t* name:{L"selected.map",L"uppercase.MAP",L"ignored.hps"}) cPlatform::RemoveFile(browserFixture+_W('/')+name);
        require(cPlatform::RemoveFolder(browserFixture+_W("/subfolder"),false,false) && cPlatform::RemoveFolder(browserFixture,false,false),
            "clean only isolated browser fixture files and empty directories");
        for(int i=0;i<3;++i) draw(ui);
        selectTab("Join");
        for(int i=0;i<3;++i) draw(ui);
        ui.mlPendingAction=5; ui.Draw();
        require(session.steamRefreshes==0,"refresh does not call Steam during rendering");
        ui.Update(1.0f/60);
        require(session.steamRefreshes==1 && ui.mbSearchedSteamLobbies,"refresh sessions is deferred");
        for(int i=0;i<3;++i) draw(ui);
        screenshot("steam-searching.png");
        session.steamSearchPending=false;
        hpl::cSteamLobbyInfo lobby;
        lobby.id=109775244398475115ull; lobby.name="Test Player's campaign";
        lobby.map="01_old_archives.map"; lobby.players=2; lobby.maxPlayers=4;
        session.lobbies.push_back(lobby);
        lobby.id=109775244398475116ull; lobby.name="Custom map night";
        lobby.map="castle.map"; lobby.players=4; lobby.maxPlayers=4;
        session.lobbies.push_back(lobby);
        ui.mlSelectedSteamLobby=session.lobbies[0].id;
        std::strcpy(ui.msLobbyCode,"109775244398475115");
        for(int i=0;i<3;++i) draw(ui);
        screenshot("steam-join.png");
        ui.mlPendingAction=4; ui.Draw();
        require(session.steamJoins==0,"Steam lobby join is deferred from rendering");
        ui.Update(1.0f/60);
        require(session.steamJoins==1 && session.lastLobbyCode=="109775244398475115","Steam join preserves full numeric lobby code");
        ui.mlPendingAction=3; ui.Update(1.0f/60);
        ui.mbUseSteam=false;
        for(int i=0;i<3;++i) draw(ui);
        screenshot("direct-ip-join.png");
        std::strcpy(ui.msAddress,"[::1]:27015"); ui.mlPendingAction=2;
        ui.Update(1.0f/60);
        require(session.joins==1 && session.lastAddress=="[::1]:27015","join preserves direct address");
        ui.Toggle();
        require(!ui.IsVisible() && input.state==eLuxInputState_Game,"closing preserves game state");
        input.Update(1.0f/60);
        session.ready=false;
        input.Update(1.0f/60);
        require(input.gameUpdates==0,"client input stays blocked during map transfer without overlay");
        session.ready=true;
        input.Update(1.0f/60);
        input.Update(1.0f/60);
        require(input.gameUpdates==1,"game input resumes when client map becomes ready");
        const int releasesBeforeSteamOverlay=player.releases;
        session.steamOverlayActive=true;
        ui.Show(false);
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        require(ui.IsVisible(),"Steam overlay Escape does not close the multiplayer window underneath");
        event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        event(SDL_KEYDOWN,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE);
        require(ui.IsVisible(),"Steam overlay tilde does not toggle the multiplayer window underneath");
        event(SDL_KEYUP,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_A,true);
        ImGui::GetIO().AddMouseButtonEvent(0,true);
        ui.Draw();
        require(!ImGui::IsKeyDown(ImGuiKey_A) && !ImGui::GetIO().MouseDown[0],"Steam overlay clears held and queued ImGui input");
        ui.Toggle();
        input.Update(1.0f/60);
        input.Update(1.0f/60);
        require(input.gameUpdates==1 && player.releases==releasesBeforeSteamOverlay+3,"Steam overlay blocks gameplay and releases interactions only once");
        session.steamOverlayActive=false;
        input.Update(1.0f/60);
        require(input.gameUpdates==1,"Steam overlay closing inputs are swallowed for release frame");
        input.Update(1.0f/60);
        require(input.gameUpdates==2,"game input resumes after Steam overlay closes");
        session.active=false;
        ui.Show(false);
        resizeOverlay(ui,960,720);
        selectTab("Host");
        resizeOverlay(ui,320,240);
        requireVisibleWindow("Multiplayer",320,240);
        screenshot("minimum-window.png");
        resizeOverlay(ui,960,720);
        ui.OpenMapBrowser();
        for(int i=0;i<3;++i) draw(ui);
        resizeOverlay(ui,320,240);
        requireVisibleWindow("Select a map",320,240);
        require(ui.mbMapBrowserOpen,"resizing retains the open map browser");
        screenshot("minimum-map-browser.png");
        ui.mbCloseMapBrowser=true;draw(ui);
        resizeOverlay(ui,800,600);
        requireVisibleWindow("Multiplayer",800,600);
    }
    base.mpEngine->GetGraphics()->GetLowLevel()->SwapBuffers();
    event(SDL_KEYDOWN,SDL_SCANCODE_W,SDLK_w);
    event(SDL_KEYUP,SDL_SCANCODE_W,SDLK_w);
    base.mpEngine->GetGui()->DestroySet(menuSet);
    DestroyHPLEngine(base.mpEngine);
    std::printf("PASS: real ImGui rendering; tilde/escape; global pre-swap/event hooks; key releases; Steam unavailable/hosting/searching/join/invitation; Steam overlay input capture; campaign defaults; current-map background guard; deferred map browser and start-position dropdown; stale/missing/invalid map handling; cache deletion while connected/disconnected; direct IP; teardown.\n");
    return 0;
}
int hplMain(const tString&) { return main(0,NULL); }
