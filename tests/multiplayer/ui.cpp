#include "hpl.h"
#include "LuxTypes.h"
#include "impl/LowLevelGraphicsSDL.h"
#include "impl/LowLevelInputSDL.h"
#include "network/NetworkTransport.h"
#include "LuxMultiplayerCache.h"
#include "LuxMultiplayerChatProtocol.h"
#include "LuxMultiplayerChatEmoji.h"
#include <SDL2/SDL.h>
#include <IL/il.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
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
#define LUX_PLAYER_H
#define LUX_MESSAGE_HANDLER_H
#define LUX_EFFECT_HANDLER_H
#define LUX_DEBUG_HANDLER_H
#define LUX_MAP_HANDLER_H
#define LUX_MAIN_MENU_H
#define LUX_INVENTORY_H
#define LUX_JOURNAL_H
class cLuxMultiplayerUI;
enum eLuxMultiplayerLoadPhase {
    eLuxMultiplayerLoadPhase_None,eLuxMultiplayerLoadPhase_Connecting,
    eLuxMultiplayerLoadPhase_Preparing,eLuxMultiplayerLoadPhase_Checking,
    eLuxMultiplayerLoadPhase_Downloading,eLuxMultiplayerLoadPhase_Loading
};
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
    bool IsChatCapturingInput() const;
    void CloseChat();
    bool active = false, host = false;
    bool ready = true;
    bool loading=false,mapPreparing=false;
    tString pendingHostMap;
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
    eLuxMultiplayerLoadPhase loadPhase=eLuxMultiplayerLoadPhase_None;
    std::deque<luxnet::ChatMessage> chat;
    int chatSends=0;
    bool chatSendAllowed=true;
    bool IsActive() const { return active; }
    bool IsHost() const { return host; }
    bool IsClient() const { return active && !host; }
    bool IsReady() const { return ready; }
    bool IsChangingMap() const {return loading || mapPreparing || !pendingHostMap.empty();}
    eLuxMultiplayerLoadPhase GetLoadPhase() const {return loadPhase;}
    const std::deque<luxnet::ChatMessage>& GetChatMessages() const {return chat;}
    bool SendChatMessage(const tString& value) {
        tString normalized;
        if(!chatSendAllowed || !luxnet::NormalizeChatText(value,normalized)) return false;
        luxnet::ChatMessage entry;entry.peer=0;entry.name="Test Steam Player";entry.text=normalized;
        entry.nameColor=luxchat::DefaultNameColor;
        luxnet::AppendChatMessage(chat,entry);++chatSends;return true;
    }
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
    bool active=true,dead=false;
    bool IsActive() const {return active;}
    bool IsDead() const {return dead;}
    void DoAction(eLuxPlayerAction, bool down) { if(!down) ++releases; }
    void Run(bool down) { if(!down) ++stopRun; }
    void Jump(bool) {}
    void Crouch(bool) {}
    void SetLean(int) {}
};
struct TestMessageHandler {
    bool paused=false;
    bool IsPauseMessageActive() const {return paused;}
};
struct TestEffectHandler {
    bool paused=false;
    bool GetPlayerIsPaused() const {return paused;}
};
struct TestDebugHandler {
    bool active=false;
    bool GetDebugWindowActive() const {return active;}
};
struct TestMapHandler {
    bool loaded = false;
    void* GetCurrentMap() { return loaded ? this : NULL; }
};
struct cLuxInputHandler {
    eLuxInputState state = eLuxInputState_MainMenu;
    eLuxInputState& mState = state;
    bool mbMultiplayerCapturing = false;
    bool mbQuitRequested = false;
    cInput* mpInput = NULL;
    TestPlayer* mpPlayer = NULL;
    int globalUpdates = 0, gameUpdates = 0, menuUpdates = 0, inventoryUpdates = 0, journalUpdates = 0;
    int resets = 0;
    eLuxInputState GetState() { return state; }
    void ResetSmoothMousePos() { ++resets; }
    void Update(float);
    void OnQuit();
    void UpdateGlobalInput() { ++globalUpdates; }
    void UpdateGameInput() { ++gameUpdates; }
    void UpdateMainMenuInput() { ++menuUpdates; }
    void UpdatePreMenuInput() {}
    void UpdateInventoryInput() { ++inventoryUpdates; }
    void UpdateJournalInput() { ++journalUpdates; }
    void UpdateDebugInput() {}
    void UpdateCreditsInput() {}
    void UpdateDemoEndInput() {}
    void UpdateLoadScreenInput() {}
};
struct TestMainMenu {
    float backdropBlurAmount=1;
    bool RequestQuit() { return true; }
    cViewport* GetViewport() { return NULL; }
    float GetChatBackdropBlurAmount() const { return backdropBlurAmount; }
};
struct cLuxBase {
    cEngine* mpEngine;
    cLuxInputHandler* mpInputHandler;
    TestMapHandler* mpMapHandler;
    cLuxMultiplayer* mpMultiplayer;
    TestMainMenu* mpMainMenu;
    TestMainMenu* mpInventory=NULL;
    TestMainMenu* mpJournal=NULL;
    TestPlayer* mpPlayer;
    TestMessageHandler* mpMessageHandler;
    TestEffectHandler* mpEffectHandler;
    TestDebugHandler* mpDebugHandler;
    tString msStartMapFile="00_rainy_hall.map", msStartMapFolder="maps/main/ch01/";
};
static cLuxBase base;
static tString outputDirectory;
static tString savedDesktopClipboard;
static bool desktopClipboardSaved=false;
static bool setFixtureClipboard(const char* text) {
    // Other desktop applications can briefly hold the Windows clipboard lock.
    // Retry only test setup/cleanup; production copy/paste remains unchanged.
    const Uint32 started=SDL_GetTicks();tString error;
    for(;;) {
        if(SDL_SetClipboardText(text)==0) return true;
        error=SDL_GetError();
        const Uint32 elapsed=SDL_GetTicks()-started;
        if(elapsed>=300) {
            std::fprintf(stderr,"Clipboard fixture write failed after %u ms; SDL error: %s\n",
                unsigned(elapsed),error.empty()?"(no SDL error reported)":error.c_str());
            return false;
        }
        SDL_Delay(elapsed>290?300-elapsed:10);
    }
}
static char* readFixtureClipboard(const char* expected=NULL) {
    const Uint32 started=SDL_GetTicks();
    for(;;) {
        char* text=SDL_GetClipboardText();const tString error=SDL_GetError();
        const bool hasText=SDL_HasClipboardText()==SDL_TRUE;
        // Windows can return an allocated empty string while another app owns
        // the clipboard lock. Fixture captures must not save that as user data.
        const bool ready=text && (expected?std::strcmp(text,expected)==0:(text[0] || !hasText));
        if(ready) return text;
        const Uint32 elapsed=SDL_GetTicks()-started;
        if(elapsed>=300) {
            std::fprintf(stderr,"Clipboard fixture read failed after %u ms; actualBytes=%llu expectedBytes=%lld hasText=%d returnedText=%d SDL error: %s\n",
                unsigned(elapsed),static_cast<unsigned long long>(text?std::strlen(text):0),
                expected?static_cast<long long>(std::strlen(expected)):-1LL,int(hasText),int(text!=NULL),
                error.empty()?"(no SDL error reported)":error.c_str());
            // Expected-byte callers keep their strict comparison against the
            // final read. Ambiguous fixture captures fail before any writes.
            if(expected) return text;
            SDL_free(text);return NULL;
        }
        SDL_free(text);SDL_Delay(elapsed>290?300-elapsed:10);
    }
}
static bool restoreDesktopClipboard() {
    if(!desktopClipboardSaved) return true;
    if(!SDL_WasInit(SDL_INIT_VIDEO)) return false;
    if(!setFixtureClipboard(savedDesktopClipboard.c_str())) return false;
    desktopClipboardSaved=false;return true;
}
cLuxBase* gpBase = &base;
#define private public
#include "LuxMultiplayerUI.h"
#undef private
#include "LuxMultiplayerUI.cpp"
#include "LuxMultiplayerContent.cpp"
#include "generated_update.cpp"
#include "imgui_internal.h"
bool cLuxMultiplayer::IsWindowVisible() const { return ui && ui->IsVisible(); }
bool cLuxMultiplayer::IsChatCapturingInput() const {return ui && ui->IsChatCapturingInput();}
void cLuxMultiplayer::CloseChat() {if(ui) ui->CloseChat();}

static void require(bool result, const char* text) {
    if(!result) {
        std::fprintf(stderr,"FAIL: %s\n",text);
        if(!restoreDesktopClipboard()) std::fprintf(stderr,"FAIL: restore original desktop clipboard\n");
        std::exit(2);
    }
}
static void event(Uint32 type, SDL_Scancode scan, SDL_Keycode sym, Uint8 repeat=0,Uint16 modifiers=0) {
    SDL_Event e={}; e.type=type; e.key.type=type; e.key.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
    e.key.state=type==SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode=scan; e.key.keysym.sym=sym; e.key.keysym.mod=modifiers; e.key.repeat=repeat;
    require(SDL_PushEvent(&e)==1,"inject SDL keyboard event");
    base.mpEngine->GetInput()->Update(1.0f/60);
}
static void textEvent(const tString& value) {
    size_t position=0;
    while(position<value.size()) {
        size_t end=(std::min)(position+size_t(SDL_TEXTINPUTEVENT_TEXT_SIZE-1),value.size());
        while(end<value.size() && (static_cast<unsigned char>(value[end])&0xc0)==0x80) --end;
        SDL_Event event={};event.type=SDL_TEXTINPUT;
        event.text.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
        std::memcpy(event.text.text,value.data()+position,end-position);event.text.text[end-position]='\0';
        require(SDL_PushEvent(&event)==1,"inject SDL text input");position=end;
    }
    base.mpEngine->GetInput()->Update(1.0f/60);
}
static void draw(cLuxMultiplayerUI& ui) {
    ui.Update(1.0f/60);
    iLowLevelGraphics* low=base.mpEngine->GetGraphics()->GetLowLevel();
    low->SetCurrentFrameBuffer(NULL);
    low->SetClearColor(cColor(0.055f,0.063f,0.08f,1));
    low->ClearFrameBuffer(eClearFrameBufferFlag_Color|eClearFrameBufferFlag_Depth);
    const tString container=base.mpEngine->GetUpdater()->GetCurrentContainerName();
    if(container=="MainMenu" || container=="Inventory" || container=="Journal") ui.DrawMenuBackdrop();
    ui.Draw();
    low->WaitAndFinishRendering();
}
static void queueMouse(bool down,int x,int y) {
    SDL_Event motion={};motion.type=SDL_MOUSEMOTION;motion.motion.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
    motion.motion.x=x;motion.motion.y=y;require(SDL_PushEvent(&motion)==1,"move SDL mouse to rendered control");
    SDL_Event button={};button.type=down?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;button.button.windowID=motion.motion.windowID;
    button.button.button=SDL_BUTTON_LEFT;button.button.state=down?SDL_PRESSED:SDL_RELEASED;button.button.x=x;button.button.y=y;
    require(SDL_PushEvent(&button)==1,"inject rendered control through SDL");
}
static void click(cLuxMultiplayerUI& ui,const cVector2f& position,const cVector2f& size) {
    require(size.x>0 && size.y>0,"mouse target has a rendered rectangle");
    const int x=static_cast<int>(position.x+size.x*0.5f),y=static_cast<int>(position.y+size.y*0.5f);
    queueMouse(true,x,y);
    base.mpEngine->GetInput()->Update(1.0f/60);base.mpInputHandler->Update(1.0f/60);draw(ui);
    queueMouse(false,x,y);
    base.mpEngine->GetInput()->Update(1.0f/60);base.mpInputHandler->Update(1.0f/60);draw(ui);
    for(int i=0;i<3;++i) draw(ui);
}
static void openPicker(cLuxMultiplayerUI& ui) {
    click(ui,ui.mvChatEmojiButtonPos,ui.mvChatEmojiButtonSize);
    if(!ui.mbChatEmojiPickerOpen) {
        const ImGuiContext* context=ImGui::GetCurrentContext();const ImGuiIO& io=ImGui::GetIO();
        SDL_Window* window=SDL_GL_GetCurrentWindow();
        std::fprintf(stderr,"Picker open chat=%d ready=%d capture=%d controls=%d overlay=%d load=%d focusRequest=%d restore=%d activeID=%u hoveredID=%u hoveredWindow=%s navWindow=%s mouse=%.1f,%.1f down=%d clickCount=%u appFocusLost=%d keyboardFocus=%d mouseFocus=%d windowFlags=%u button=%.1f,%.1f %.1fx%.1f\n",
            int(ui.IsChatOpen()),int(base.mpMultiplayer->IsReady()),int(ui.IsChatCapturingInput()),int(ui.IsVisible()),
            int(base.mpMultiplayer->IsSteamOverlayActive()),int(base.mpMultiplayer->GetLoadPhase()),
            int(ui.mbChatFocusInput),int(ui.mbChatRestoreSelection),context->ActiveId,context->HoveredId,
            context->HoveredWindow?context->HoveredWindow->Name:"none",context->NavWindow?context->NavWindow->Name:"none",
            io.MousePos.x,io.MousePos.y,int(io.MouseDown[0]),unsigned(io.MouseClickedLastCount[0]),int(io.AppFocusLost),
            int(SDL_GetKeyboardFocus()==window),int(SDL_GetMouseFocus()==window),unsigned(SDL_GetWindowFlags(window)),
            ui.mvChatEmojiButtonPos.x,ui.mvChatEmojiButtonPos.y,ui.mvChatEmojiButtonSize.x,ui.mvChatEmojiButtonSize.y);
    }
    require(ui.mbChatEmojiPickerOpen,"SDL clicking the emoji button opens the picker");
}
static void moveMouse(cLuxMultiplayerUI& ui,int x,int y) {
    SDL_Event motion={};motion.type=SDL_MOUSEMOTION;motion.motion.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
    motion.motion.x=x;motion.motion.y=y;require(SDL_PushEvent(&motion)==1,"move actual SDL mouse for chat hover rendering");
    base.mpEngine->GetInput()->Update(1.0f/60);for(int i=0;i<3;++i) draw(ui);
}
static unsigned atlasDrawElements(cLuxMultiplayerUI& ui) {
    const ImTextureID atlas=static_cast<ImTextureID>(static_cast<uintptr_t>(ui.mpChatEmoji->GetTextureHandle()));
    unsigned count=0;ImDrawData* data=ImGui::GetDrawData();
    for(int list=0;list<data->CmdListsCount;++list) for(const ImDrawCmd& command:data->CmdLists[list]->CmdBuffer)
        if(command.TextureId==atlas) count+=command.ElemCount;
    return count;
}
static unsigned greyEmojiIconVertices(cLuxMultiplayerUI& ui) {
    const ImGuiWindow* entry=ImGui::FindWindowByName("##MultiplayerChat");
    require(entry!=NULL,"emoji icon belongs to the real entry window");
    unsigned count=0;const cVector2f end=ui.mvChatEmojiButtonPos+ui.mvChatEmojiButtonSize;
    for(const ImDrawVert& vertex:entry->DrawList->VtxBuffer) if(vertex.col==IM_COL32(164,174,187,255) &&
        vertex.pos.x>=ui.mvChatEmojiButtonPos.x && vertex.pos.x<=end.x &&
        vertex.pos.y>=ui.mvChatEmojiButtonPos.y && vertex.pos.y<=end.y) ++count;
    return count;
}
static void requireSingleEntry(cLuxMultiplayerUI& ui) {
    require(ui.mvChatEntrySize.y<=ui.mvChatTextInputSize.y+24,
        "chat draft occupies one editable row without a duplicate preview line");
    if(!ui.mlChatEntryEmoji) return;
    const cVector2f start=ui.mvChatTextInputPos,end=start+ui.mvChatTextInputSize;
    const ImTextureID atlas=static_cast<ImTextureID>(static_cast<uintptr_t>(ui.mpChatEmoji->GetTextureHandle()));
    bool clippedImage=false;
    ImDrawData* data=ImGui::GetDrawData();
    for(int list=0;list<data->CmdListsCount;++list) for(const ImDrawCmd& command:data->CmdLists[list]->CmdBuffer)
        if(command.TextureId==atlas && command.ElemCount>0 && command.ClipRect.x>=start.x && command.ClipRect.y>=start.y &&
            command.ClipRect.z<=end.x+1 && command.ClipRect.w<=end.y+1) clippedImage=true;
    require(clippedImage,"color emoji draw inside the actual editable field's clipping rectangle");
}
static void clickChatCaret(cLuxMultiplayerUI& ui,size_t rawOffset) {
    // These are independent single-click fixtures. Without elapsed real time
    // the second fixture can become a deliberate native word-select gesture.
    SDL_Delay(static_cast<Uint32>(ImGui::GetIO().MouseDoubleClickTime*1000.0f)+20);
    draw(ui);
    const float x=ui.mvChatEntryTextPos.x+luxchat::RichCaretX(ui.msChatInput,ui.mChatEntryRich,rawOffset)+1.0f;
    const float y=ui.mvChatTextInputPos.y+ui.mvChatTextInputSize.y*0.5f;
    require(x>ui.mvChatTextInputPos.x && x<ui.mvChatTextInputPos.x+ui.mvChatTextInputSize.x,
        "rich caret target is visible inside the actual input item");
    click(ui,cVector2f(x,y),cVector2f(1,1));
    if(ImGui::GetIO().MouseClickedLastCount[0]!=1)
        std::fprintf(stderr,"Rich single-click fixture count=%u previousTime=%.3f currentTime=%.3f\n",
            unsigned(ImGui::GetIO().MouseClickedLastCount[0]),ImGui::GetIO().MouseClickedTime[0],ImGui::GetCurrentContext()->Time);
    require(ImGui::GetIO().MouseClickedLastCount[0]==1,"independent rich caret fixture receives a single click rather than a double-click word selection");
}
static void shortcut(cLuxMultiplayerUI& ui,SDL_Scancode scan,SDL_Keycode sym) {
    event(SDL_KEYDOWN,SDL_SCANCODE_LCTRL,SDLK_LCTRL,0,KMOD_CTRL);draw(ui);
    event(SDL_KEYDOWN,scan,sym,0,KMOD_CTRL);draw(ui);
    event(SDL_KEYUP,scan,sym,0,KMOD_CTRL);draw(ui);
    event(SDL_KEYUP,SDL_SCANCODE_LCTRL,SDLK_LCTRL);for(int i=0;i<3;++i) draw(ui);
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
static void requireChatFont(cLuxMultiplayerUI& ui) {
    const ImGuiIO& io=ImGui::GetIO();
    const float requested=luxchat::CalculateLayout(io.DisplaySize.x,io.DisplaySize.y,false).fontSize;
    const float density=(std::max)(io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y);
    require(ui.mpChatFont && std::fabs(ui.mfChatRequestedFontSize-requested)<0.001f &&
        ui.mfChatFontSize==std::round(requested) && ui.mpChatFont->FontSize==ui.mfChatFontSize &&
        std::fabs(ui.mfChatFontDensity-density)<0.001f &&
        std::fabs(ui.mfChatRasterSize-ui.mfChatFontSize*density)<0.001f,
        "chat rasterizes at its rounded native text size and actual framebuffer density");
    if(ui.IsChatOpen()) {
        ImGuiWindow* entry=ImGui::FindWindowByName("##MultiplayerChat");
        require(entry && entry->FontWindowScale==1.0f && ui.mfChatEntryFontSize==ui.mpChatFont->FontSize,
            "the editable chat field uses the rasterized font without stretching its bitmap");
    }
    if(ui.mbChatEmojiPickerOpen) {
        ImGuiWindow* picker=ImGui::FindWindowByName("Emoji##MultiplayerChatPicker");
        require(picker && picker->FontWindowScale==1.0f,"emoji picker uses native font pixels after resize");
    }
}
static void checkChatFontDensities(cLuxMultiplayerUI& ui) {
    ImFont* chatFont=ui.mpChatFont;
    ImFont* controlsFont=ImGui::GetIO().Fonts->Fonts[0];
    const float controlsSize=controlsFont->FontSize;
    float rasterWidth=0,logicalAdvance=0;
    for(float density:{1.0f,1.25f,1.5f,2.0f}) {
        ui.EnsureChatFont(17.6f,density);
        const ImFontGlyph* glyph=chatFont->FindGlyphNoFallback('W');
        require(glyph && glyph->Visible,"font rebuild retains actual Latin glyphs");
        const float pixels=(glyph->U1-glyph->U0)*ImGui::GetIO().Fonts->TexWidth;
        if(density==1) {rasterWidth=pixels;logicalAdvance=glyph->AdvanceX;}
        require(ui.mpChatFont==chatFont && ImGui::GetIO().Fonts->Fonts[0]==controlsFont &&
            controlsFont->FontSize==controlsSize && chatFont->FontSize==18 &&
            ui.mfChatFontSize==18 && std::fabs(ui.mfChatFontDensity-density)<0.001f &&
            std::fabs(ui.mfChatRasterSize-18*density)<0.001f &&
            std::fabs(glyph->AdvanceX-logicalAdvance)<1.0f,
            "fractional/retina density rebuilds preserve font identities, logical metrics and controls size");
        if(density==2) require(pixels>=rasterWidth*1.75f,
            "retina font density increases actual glyph bitmap pixels instead of enlarging logical text");
        const unsigned generation=ui.mlChatFontAtlasGeneration;
        ui.EnsureChatFont(17.6f,density);
        require(ui.mlChatFontAtlasGeneration==generation,"unchanged font size and density reuse the existing atlas");
    }
    draw(ui);requireChatFont(ui);
}
static bool nearPixel(float first,float second) {return std::fabs(first-second)<0.01f;}
static bool sameUV(float first,float second) {return std::fabs(first-second)<0.000001f;}
static void checkChatHistoryStyle(cLuxMultiplayerUI& ui,cLuxMultiplayer& session) {
    const auto saved=session.chat;session.chat.clear();
    std::vector<uint32_t> colors;
    for(unsigned peer=0;peer<3;++peer) {
        const uint32_t color=luxchat::ChooseNameColor(colors);colors.push_back(color);
        luxnet::ChatMessage message;message.peer=peer;message.name="W";message.text="B";message.nameColor=color;
        session.chat.push_back(message);
    }
    for(float age:{0.0f,9.0f}) {
        for(auto& message:session.chat) message.age=age;
        draw(ui);
        require(ui.mlChatVisibleMessages==colors.size() && ui.mvChatHistoryStyles.size()==colors.size(),
            "all short sender styles are visible in passive history");
        const unsigned alpha=age==0?255:127;
        const ImU32 bodyColor=IM_COL32(229,232,237,alpha),outlineColor=IM_COL32(0,0,0,alpha);
        const ImDrawList* history=ImGui::GetBackgroundDrawList();
        const ImVec2 whitePixel=ImGui::GetIO().Fonts->TexUvWhitePixel;
        const ImFontGlyph* nameGlyph=ui.mpChatFont->FindGlyphNoFallback('W');
        require(nameGlyph && history->VtxBuffer.Size>0 && history->VtxBuffer.Size%4==0,
            "history contains real font glyph quads");
        unsigned names=0,bodies=0,outlines=0;
        for(int first=0;first<history->VtxBuffer.Size;first+=4) {
            const ImDrawVert* quad=history->VtxBuffer.Data+first;
            for(int corner=0;corner<4;++corner) require(!(sameUV(quad[corner].uv.x,whitePixel.x) &&
                sameUV(quad[corner].uv.y,whitePixel.y)),"passive history has no filled background panel geometry");
            const auto name=std::find_if(colors.begin(),colors.end(),[&](uint32_t color) {
                return quad[0].col==((color&0x00ffffff)|(alpha<<24));
            });
            if(quad[0].col==outlineColor) {++outlines;continue;}
            if(name!=colors.end()) {
                ++names;require(sameUV(quad[0].uv.x,nameGlyph->U0) && sameUV(quad[0].uv.y,nameGlyph->V0) &&
                    sameUV(quad[2].uv.x,nameGlyph->U1) && sameUV(quad[2].uv.y,nameGlyph->V1),
                    "assigned player color applies to the name glyph only");
            } else {require(quad[0].col==bodyColor,"colon and message body retain one neutral text color");++bodies;}
            unsigned neighbors=0;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                bool found=false;
                for(int candidate=0;candidate<history->VtxBuffer.Size && !found;candidate+=4) {
                    const ImDrawVert* black=history->VtxBuffer.Data+candidate;
                    if(black[0].col!=outlineColor) continue;
                    found=true;
                    for(int corner=0;corner<4;++corner) if(!sameUV(black[corner].uv.x,quad[corner].uv.x) ||
                        !sameUV(black[corner].uv.y,quad[corner].uv.y) ||
                        !nearPixel(black[corner].pos.x,quad[corner].pos.x+dx) ||
                        !nearPixel(black[corner].pos.y,quad[corner].pos.y+dy)) {found=false;break;}
                }
                if(found) ++neighbors;
            }
            require(neighbors==8,"each rendered history glyph has a complete one-pixel black outline");
        }
        require(names==3 && bodies==6 && outlines==(names+bodies)*8,
            "three distinct name colors leave all six punctuation/body glyphs neutral and outlined");
        for(const auto& style:ui.mvChatHistoryStyles) require(style.nameBytes==1 && style.textColor==bodyColor &&
            (style.nameColor>>24)==alpha,"name, body and outline fade together without changing sender color");
        if(age==0) screenshot("chat-history-styled.png");
    }
    session.chat=saved;draw(ui);
}
static void chatKey(cLuxMultiplayerUI& ui,SDL_Scancode scan,SDL_Keycode key);
static void checkPassiveChatMenus(cLuxMultiplayerUI& ui,cGuiSet* nativeSet) {
    auto& session=*base.mpMultiplayer;auto& input=*base.mpInputHandler;
    auto* updater=base.mpEngine->GetUpdater();
    const auto savedHistory=session.chat;
    const eLuxInputState savedState=input.state;
    const tString savedContainer=updater->GetCurrentContainerName();
    session.chat.clear();
    luxnet::ChatMessage message;message.name="Player 1";message.nameColor=luxchat::DefaultNameColor;
    message.text="History stays here \xF0\x9F\x98\x80";session.chat.push_back(message);
    message.peer=1;message.name="Player 2";message.text="Readable and unobtrusive.";
    message.nameColor=luxchat::ChooseNameColor({message.nameColor});session.chat.push_back(message);
    static const char* containers[]={"MainMenu","Inventory","Journal"};
    static const eLuxInputState states[]={eLuxInputState_MainMenu,eLuxInputState_Inventory,eLuxInputState_Journal};
    for(int menu=0;menu<3;++menu) {
        updater->AddContainer(containers[menu]);input.state=eLuxInputState_Game;updater->SetContainer("Default");
        iWidget* attention=nativeSet->GetAttentionWidget();iWidget* focus=nativeSet->GetFocusedWidget();
        chatKey(ui,SDL_SCANCODE_T,SDLK_t);textEvent(":gr");for(int i=0;i<3;++i) draw(ui);
        require(ui.IsChatOpen() && ui.mbChatCompletionOpen,"menu transition starts with the live editor and emoji suggestions");
        input.state=states[menu];require(updater->SetContainer(containers[menu]),"enter passive-history menu container");
        draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatCompletionOpen && !ui.mbChatEmojiPickerOpen && !ui.mbChatPickerToneOpen &&
            nativeSet->GetAttentionWidget()==attention && nativeSet->GetFocusedWidget()==focus,
            "menu transition closes chat editing and suggestions without taking native attention or focus");
        // Closing intentionally consumes the current batch and one native
        // release frame. Follow the normal input/update cadence before asserting
        // that a passive history frame has relinquished capture completely.
        input.Update(1.0f/60);draw(ui);input.Update(1.0f/60);draw(ui);
        require(!ui.IsChatCapturingInput() && !input.mbMultiplayerCapturing &&
            nativeSet->GetAttentionWidget()==attention && nativeSet->GetFocusedWidget()==focus,
            "passive menu history relinquishes input capture after the closing/release frames without changing native ownership");
        for(const cVector2l& size:{cVector2l(320,240),cVector2l(338,1000),cVector2l(3840,2160)}) {
            for(auto& entry:session.chat) entry.age=0;
            resizeOverlay(ui,size.x,size.y);requireChatFont(ui);
            require(ui.mlChatVisibleMessages==2 && ui.mlChatVisibleEmoji==1 && ui.mfChatHistoryAlpha==1 &&
                ui.mvChatHistoryPos.x>=0 && ui.mvChatHistoryPos.y>=0 && ui.mvChatHistorySize.x>0 && ui.mvChatHistorySize.y>0 &&
                ui.mvChatHistoryPos.x+ui.mvChatHistorySize.x<=size.x+1 && ui.mvChatHistoryPos.y+ui.mvChatHistorySize.y<=size.y+1 &&
                ui.mvChatEntrySize==cVector2f(0) && ui.mvChatPickerSize==cVector2f(0),
                "recent passive text and emoji history remain readable and bounded in small, portrait and 4K native menus");
            require(ui.IsChatHistoryInMenuBackdrop() && ui.GetChatMenuBlurPasses()>0 &&
                ui.GetChatFinalHistoryCount()==0 && ImGui::GetBackgroundDrawList()->VtxBuffer.empty(),
                "native menus blur history with the backdrop and leave no duplicate sharp text in the final overlay");
            require(ui.GetChatMenuBackdropSize()==base.mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt(),
                "menu backdrop render targets follow the resized native framebuffer dimensions");
            require(ui.GetChatMenuBlurSize()==cVector2l((size.x+1)/2,(size.y+1)/2),
                "bounded blur targets are recreated at half resolution including odd and portrait windows");
            const tString filename="chat-passive-"+tString(containers[menu])+"-"+cString::ToString(size.x)+"x"+cString::ToString(size.y)+".png";
            screenshot(filename.c_str());
        }
        int* updates=menu==0?&input.menuUpdates:menu==1?&input.inventoryUpdates:&input.journalUpdates;
        const int before=*updates;chatKey(ui,SDL_SCANCODE_T,SDLK_t);input.Update(1.0f/60);
        require(!ui.IsChatOpen() && !ui.IsChatCapturingInput() && *updates==before+1 &&
            nativeSet->GetAttentionWidget()==attention && nativeSet->GetFocusedWidget()==focus,
            "passive history leaves menu input updates, focus and attention with the native GUI and rejects T");
        luxnet::ChatMessage incoming;incoming.peer=2;incoming.name="Player 3";
        incoming.text="Incoming \xF0\x9F\x9F\xA6";incoming.nameColor=luxchat::ChooseNameColor({session.chat[0].nameColor,session.chat[1].nameColor});
        luxnet::AppendChatMessage(session.chat,incoming);draw(ui);
        require(ui.mlChatVisibleMessages==3 && ui.mlChatVisibleEmoji==2 && ui.mvChatHistoryStyles.size()==3 &&
            ui.GetChatFinalHistoryCount()==0 && ui.GetChatMenuBlurPasses()>0,
            "incoming messages refresh the blurred menu backdrop without leaving stale history or a sharp duplicate");
        session.chat.pop_back();
        if(menu==0) {
            base.mpMainMenu->backdropBlurAmount=0.35f;draw(ui);
            require(std::fabs(ui.GetChatMenuBlurAmount()-0.35f)<0.0001f && ui.GetChatFinalHistoryCount()==0,
                "chat follows the native menu's partial blur transition while staying beneath its controls");
            base.mpMainMenu->backdropBlurAmount=1;
            // Input remains in the final ImGui pass; rendering the backdrop
            // earlier must not consume queued Unicode or shortcut events.
            resizeOverlay(ui,800,600);ui.Show(false);
            const tWString savedDirectory=ui.msMapBrowserDirectory;
            ui.msMapBrowserDirectory=cString::UTF8ToWChar(outputDirectory);ui.OpenMapBrowser();draw(ui);
            ImGuiWindow* browser=ImGui::FindWindowByName("Select a map");
            require(browser && browser->Active,"map browser controls can open over a native menu backdrop");
            ImGuiContext* context=ImGui::GetCurrentContext();
            const ImGuiID directoryId=browser->GetID("##MapDirectory");
            // Queue activation for the next NewFrame. NavActivateId belongs
            // to the current frame and is replaced during NewFrame/NavUpdate.
            context->NavNextActivateId=directoryId;
            context->NavNextActivateFlags=ImGuiActivateFlags_PreferInput|ImGuiActivateFlags_TryToPreserveState;
            draw(ui);
            if(context->ActiveId!=directoryId) std::fprintf(stderr,
                "Backdrop input activation expected=%u actual=%u navWindow=%s browserActive=%d browserHidden=%d frame=%d\n",
                directoryId,context->ActiveId,context->NavWindow?context->NavWindow->Name:"none",
                int(browser->Active),int(browser->Hidden),ImGui::GetFrameCount());
            require(context->ActiveId==directoryId,"map-browser directory input activates before backdrop typing regression");
            shortcut(ui,SDL_SCANCODE_A,SDLK_a);
            const tString typed="Backdrop caf\xC3\xA9 \xF0\x9F\x98\x80";textEvent(typed);for(int i=0;i<3;++i) draw(ui);
            if(tString(ui.msMapBrowserPath)!=typed || !ui.IsVisible() || !ui.mbMapBrowserOpen ||
               !ui.IsChatHistoryInMenuBackdrop() || ui.GetChatFinalHistoryCount()!=0) std::fprintf(stderr,
                "Backdrop controls input expected=[%s] actual=[%s] activeID=%u expectedID=%u navWindow=%s visible=%d browser=%d backdrop=%d finalHistory=%llu frame=%d\n",
                typed.c_str(),ui.msMapBrowserPath,context->ActiveId,directoryId,
                context->NavWindow?context->NavWindow->Name:"none",int(ui.IsVisible()),int(ui.mbMapBrowserOpen),
                int(ui.IsChatHistoryInMenuBackdrop()),static_cast<unsigned long long>(ui.GetChatFinalHistoryCount()),ImGui::GetFrameCount());
            require(tString(ui.msMapBrowserPath)==typed && ui.IsVisible() && ui.mbMapBrowserOpen &&
                ui.IsChatHistoryInMenuBackdrop() && ui.GetChatFinalHistoryCount()==0,
                "the early history blur pass preserves native controls focus, shortcuts and queued Unicode text exactly once");
            ui.mbCloseMapBrowser=true;draw(ui);ui.Toggle();draw(ui);
            ui.msMapBrowserDirectory=savedDirectory;
            input.Update(1.0f/60);draw(ui);input.Update(1.0f/60);draw(ui);
        }
        for(auto& entry:session.chat) entry.age=9;draw(ui);
        require(ui.mlChatVisibleMessages==2 && ui.mfChatHistoryAlpha>0 && ui.mfChatHistoryAlpha<1,
            "history keeps its own timed fade while a native menu is open");
        for(auto& entry:session.chat) entry.age=11;draw(ui);
        require(!ui.mlChatVisibleMessages && !ui.mfChatHistoryAlpha && session.chat.size()==2,
            "expired history disappears from menus without deleting retained messages");
    }
    input.state=eLuxInputState_Game;updater->SetContainer("Default");
    for(auto& entry:session.chat) entry.age=0;
    const auto passiveVisible=[&]() {
        draw(ui);require(ui.mlChatVisibleMessages==2 && ui.mfChatHistoryAlpha==1 && !ui.IsChatCapturingInput(),
            "scripted/player pause gates editing independently from passive history rendering");
        chatKey(ui,SDL_SCANCODE_T,SDLK_t);require(!ui.IsChatOpen(),"T remains blocked while scripted or native pause owns input");
    };
    base.mpPlayer->active=false;passiveVisible();base.mpPlayer->active=true;
    base.mpPlayer->dead=true;passiveVisible();base.mpPlayer->dead=false;
    base.mpMessageHandler->paused=true;passiveVisible();base.mpMessageHandler->paused=false;
    base.mpEffectHandler->paused=true;passiveVisible();base.mpEffectHandler->paused=false;
    base.mpDebugHandler->active=true;passiveVisible();base.mpDebugHandler->active=false;
    base.mpEngine->SetPaused(true);passiveVisible();base.mpEngine->SetPaused(false);
    const auto hidden=[&]() {draw(ui);require(!ui.mlChatVisibleMessages && !ui.mfChatHistoryAlpha,
        "passive history still hides without a live map/session or during loading and the Steam overlay");};
    session.ready=false;hidden();session.ready=true;
    session.active=false;hidden();session.active=true;
    base.mpMapHandler->loaded=false;hidden();base.mpMapHandler->loaded=true;
    session.loading=true;hidden();session.loading=false;
    session.steamOverlayActive=true;hidden();session.steamOverlayActive=false;
    session.chat=savedHistory;input.state=savedState;updater->SetContainer(savedContainer);resizeOverlay(ui,800,600);
}
static void requireHistoryTail(cLuxMultiplayerUI& ui) {
    const ImFontGlyph* tail=ui.mpChatFont->FindGlyphNoFallback('L');
    require(tail!=NULL,"history tail marker has a visible font glyph");
    const ImDrawList* history=ImGui::GetBackgroundDrawList();bool visible=false;
    const float bottom=ui.mvChatHistoryPos.y+ui.mvChatHistorySize.y;
    for(int first=0;first+3<history->VtxBuffer.Size;first+=4) {
        const ImDrawVert* quad=history->VtxBuffer.Data+first;
        if(quad[0].col==IM_COL32(229,232,237,255) && sameUV(quad[0].uv.x,tail->U0) &&
            sameUV(quad[0].uv.y,tail->V0) && sameUV(quad[2].uv.x,tail->U1) && sameUV(quad[2].uv.y,tail->V1) &&
            quad[0].pos.y>=ui.mvChatHistoryPos.y && quad[2].pos.y<=bottom &&
            quad[2].pos.y>=bottom-ui.mfChatFontSize*2) visible=true;
    }
    require(visible,"the last glyph of an oversized newest message remains visible at the bottom of clipped history");
}
static void wheelHistory(cLuxMultiplayerUI& ui,int lines) {
    SDL_Event wheel={};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
    wheel.wheel.y=lines;wheel.wheel.direction=SDL_MOUSEWHEEL_NORMAL;
    require(SDL_PushEvent(&wheel)==1,"inject actual SDL history scrolling");
    base.mpEngine->GetInput()->Update(1.0f/60);for(int i=0;i<3;++i) draw(ui);
}
static void checkEmojiMapping(cLuxMultiplayerChatEmoji* emoji) {
    char* basePath=SDL_GetBasePath();require(basePath!=NULL,"resolve executable-adjacent emoji mapping");
    const tString path=tString(basePath)+"fonts/twemoji/mapping.txt";SDL_free(basePath);
    FILE* file=cPlatform::OpenFile(cString::UTF8ToWChar(path),_W("rb"));
    require(file!=NULL,"open packaged emoji mapping for complete sequence coverage");
    char line[1024];require(std::fgets(line,sizeof(line),file)!=NULL,"emoji mapping has an atlas header");
    const auto append=[](tString& value,uint32_t point) {
        if(point<0x80) value+=char(point);
        else if(point<0x800) {value+=char(0xc0|(point>>6));value+=char(0x80|(point&63));}
        else if(point<0x10000) {value+=char(0xe0|(point>>12));value+=char(0x80|((point>>6)&63));value+=char(0x80|(point&63));}
        else {value+=char(0xf0|(point>>18));value+=char(0x80|((point>>12)&63));value+=char(0x80|((point>>6)&63));value+=char(0x80|(point&63));}
    };
    size_t covered=0;
    while(std::fgets(line,sizeof(line),file)) {
        std::istringstream record(line);tString sequence;record>>sequence;
        tString original,stripped;size_t position=0;
        while(position<sequence.size()) {
            const size_t end=sequence.find('-',position);
            const uint32_t point=static_cast<uint32_t>(std::stoul(sequence.substr(position,end==tString::npos?end:end-position),NULL,16));
            append(original,point);if(point!=0xfe0f) append(stripped,point);
            if(end==tString::npos) break;position=end+1;
        }
        int glyph=-1;
        require(emoji->Match(original,0,glyph)==original.size() && glyph>=0,
            "every packaged emoji sequence matches through its final codepoint");
        require(emoji->Match(stripped,0,glyph)==stripped.size() && glyph>=0,
            "every packaged emoji sequence also accepts omitted VS16 selectors");
        ++covered;
    }
    std::fclose(file);
    require(covered==emoji->GetGlyphCount() && covered>=3000,"all packaged emoji glyphs were exercised");
}
static void checkEmojiCategories(cLuxMultiplayerChatEmoji* emoji) {
    static const char* keys[]={"people","nature","food","activity","travel","objects","symbols","flags"};
    // These expectations were checked against the pinned upstream snapshot,
    // independently of the generated metadata and the C++ loader.
    static const char* firstNames[]={"grinning","dog","green_apple","soccer","red_car","watch","pink_heart","flag_white"};
    static const unsigned expectedCounts[]={2261,215,130,434,133,238,328,270};
    static const char* flagPrefix[]={"flag_white","flag_black","pirate_flag","checkered_flag",
        "triangular_flag_on_post","rainbow_flag","transgender_flag","united_nations","flag_af","flag_ax","flag_al","flag_dz"};
    require(cLuxMultiplayerChatEmoji::CategoryCount==8,"picker has the eight Discord Unicode categories");
    const auto& entries=emoji->GetPickerEntries();
    require(entries.size()==4009 && entries.size()==emoji->GetGlyphCount(),
        "category metadata covers the complete pinned Twemoji atlas");
    std::vector<int> owners(emoji->GetGlyphCount(),-1);
    unsigned counts[8]={},baseCounts[8]={};int previousCategory=0;
    size_t first[8]={};
    for(size_t index=0;index<entries.size();++index) {
        const auto& entry=entries[index];int glyph=-1;
        require(entry.category>=0 && entry.category<8 && entry.category>=previousCategory,
            "metadata retains the source category block order rather than alphabetizing aliases");
        require(!entry.name.empty() && !entry.unicode.empty() &&
            emoji->Match(entry.unicode,0,glyph)==entry.unicode.size() && glyph==entry.glyph,
            "each categorized emoji resolves to the complete original atlas artwork");
        require(entry.glyph>=0 && size_t(entry.glyph)<owners.size() && owners[entry.glyph]==-1,
            "a canonical emoji appears once across categories even when it has many aliases");
        if(!counts[entry.category]) first[entry.category]=index;
        owners[entry.glyph]=static_cast<int>(index);++counts[entry.category];
        if(!entry.skinTone) ++baseCounts[entry.category];previousCategory=entry.category;
    }
    for(int category=0;category<8;++category) {
        require(tString(emoji->GetCategoryKey(category))==keys[category] && counts[category] && baseCounts[category],
            "all eight categories have their expected source key and usable default choices");
        require(counts[category]==expectedCounts[category] && entries[first[category]].name==firstNames[category],
            "category population and leading choices agree with the independently verified source snapshot");
        const auto alias=emoji->GetShortcodes().find(firstNames[category]);int expectedGlyph=-1;
        require(alias!=emoji->GetShortcodes().end() &&
            emoji->Match(alias->second,0,expectedGlyph)==alias->second.size() &&
            entries[first[category]].glyph==expectedGlyph,
            "leading source choices select the expected Unicode artwork from the independent shortcode dictionary");
    }
    for(size_t index=0;index<sizeof(flagPrefix)/sizeof(flagPrefix[0]);++index) {
        require(entries[first[7]+index].name==flagPrefix[index],
            "Flags retains Discord's white/black/pirate/special flags and Afghanistan/Åland/Albania/Algeria order");
        const auto alias=emoji->GetShortcodes().find(flagPrefix[index]);int expectedGlyph=-1;
        require(alias!=emoji->GetShortcodes().end() &&
            emoji->Match(alias->second,0,expectedGlyph)==alias->second.size() &&
            entries[first[7]+index].glyph==expectedGlyph,
            "ordered Flags choices resolve to the verified regional and special-flag Unicode artwork");
    }
    for(const auto& alias:emoji->GetShortcodes()) {
        int glyph=-1;
        require(emoji->Match(alias.second,0,glyph)==alias.second.size() && glyph>=0 && owners[glyph]>=0,
            "every existing Discord shortcode has a categorized canonical glyph");
        const auto& aliases=entries[owners[glyph]].aliases;
        require(std::find(aliases.begin(),aliases.end(),alias.first)!=aliases.end(),
            "category metadata preserves all existing aliases for global search, including generated tone names");
    }
    const tString firstTone="\xF0\x9F\x8F\xBB",mediumTone="\xF0\x9F\x8F\xBD",lastTone="\xF0\x9F\x8F\xBF";
    const tString mixedHeart=tString("\xF0\x9F\xA7\x91")+firstTone+
        "\xE2\x80\x8D\xE2\x9D\xA4\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\xA7\x91"+lastTone;
    const tString mixedHandshake=tString("\xF0\x9F\xAB\xB1")+firstTone+"\xE2\x80\x8D\xF0\x9F\xAB\xB2"+lastTone;
    for(const auto& family:std::vector<std::pair<tString,tString>>{
        {mixedHeart,"\xF0\x9F\x92\x91"},{mixedHandshake,"\xF0\x9F\xA4\x9D"}}) {
        int mixedGlyph=-1,baseGlyph=-1,mediumGlyph=-1;
        const tString uniform=family.second+mediumTone;
        require(emoji->Match(family.first,0,mixedGlyph)==family.first.size() &&
            emoji->Match(family.second,0,baseGlyph)==family.second.size() &&
            emoji->Match(uniform,0,mediumGlyph)==uniform.size(),
            "mixed and uniform legacy-family regression artwork exists in the real atlas");
        require(emoji->ResolvePickerTone(mixedGlyph,0)==baseGlyph && emoji->ResolvePickerTone(mixedGlyph,3)==mediumGlyph,
            "runtime reverse family metadata maps mixed couple/handshake artwork to default and selected uniform legacy glyphs");
    }
}
static tString toneSuffix(int tone);
static void checkEmojiCompletionModel(cLuxMultiplayerChatEmoji* emoji) {
    const auto token=[&](const tString& text,size_t cursor) {
        return luxchat::FindEmojiCompletionToken(text,cursor,cursor,cursor,emoji);
    };
    require(!token(":g",2).active,"one alias codepoint after a colon does not open completion");
    require(!token(":\xC3\xB1",3).active && token(":\xC3\xB1" "g",4).active,
        "completion threshold counts Unicode codepoints rather than UTF-8 bytes");
    const tString middle="before \xF0\x9F\x98\x80 :green after";
    const size_t colon=middle.find(':');const auto current=token(middle,colon+3);
    require(current.active && current.start==colon && current.end==colon+6 && current.query=="gr",
        "a Unicode draft exposes only the alias prefix before the caret while replacing the complete word");
    require(!luxchat::FindEmojiCompletionToken(middle,colon+3,colon+1,colon+3,emoji).active,
        "a selected range cannot be overwritten by unsolicited emoji completion");
    const auto adjacentKnown=token(":smile::gr",10),adjacentUnknown=token(":missing::gr",12);
    require(adjacentKnown.active && adjacentKnown.start==7 && adjacentKnown.end==10 && adjacentKnown.query=="gr" &&
        adjacentUnknown.active && adjacentUnknown.start==9 && adjacentUnknown.end==12 && adjacentUnknown.query=="gr",
        "an adjacent opening colon completes only the new alias after a known or unknown closed token");
    require(!token(":smile:gr",9).active && !token(":missing:gr",11).active && !token(":grinning:",3).active,
        "completed tokens own their closing colon even when the caret moves within their raw alias");
    const auto compound=token(":adult::sk",10);
    require(!token(":adult::s",9).active && compound.active && compound.start==0 && compound.end==10 && compound.query=="adult::sk",
        "registered compound aliases require two new suffix characters and replace the whole compound");
    const auto compoundMatches=emoji->FindCompletionSuggestions(compound.query,3,32);
    require(compoundMatches.size()>=5,"an explicit compound tone prefix retains every registered adult variant");
    for(int tone=1;tone<6;++tone) {
        const tString expected=tString("\xF0\x9F\xA7\x91")+toneSuffix(tone);int glyph=-1;
        require(emoji->Match(expected,0,glyph)==expected.size() && compoundMatches[tone-1].glyph==glyph &&
            emoji->ExpandShortcodes(compoundMatches[tone-1].replacement)==expected,
            "all five adult prefix variants precede older-adult substring matches and insert their displayed tone");
    }
    const auto knownCompound=token(":smile::adult::sk",17),unknownCompound=token(":missing::adult::sk",19);
    require(knownCompound.active && knownCompound.start==7 && knownCompound.end==17 && knownCompound.query=="adult::sk" &&
        unknownCompound.active && unknownCompound.start==9 && unknownCompound.end==19 && unknownCompound.query=="adult::sk",
        "compound completion preserves a preceding known or unknown adjacent alias and its owned closing colon");
    for(const tString& text:std::vector<tString>{"https://example.test", "https:gr", "12:30", "user:gr@example.test",
        "plain text", ":grinning:", ":grinning:gr"})
        require(!token(text,text.size()).active,"URLs, times, email-like text and completed aliases do not reuse a colon as an opener");
    const auto matches=emoji->FindCompletionSuggestions("gr",0,64);
    bool grapes=false,greenApple=false;
    for(const auto& match:matches) {
        const tString expanded=emoji->ExpandShortcodes(match.replacement);int glyph=-1;
        require(emoji->Match(expanded,0,glyph)==expanded.size() && glyph==match.glyph,
            "every completion replacement renders exactly the Unicode artwork presented to the user");
        if(expanded=="\xF0\x9F\x8D\x87") grapes=true;
        if(expanded=="\xF0\x9F\x8D\x8F") greenApple=true;
    }
    require(grapes && greenApple,"short prefixes include food aliases beyond the first smiley choices");
    const auto apples=emoji->FindCompletionSuggestions("apple",0,64);
    require(!apples.empty() && apples.front().alias=="apple","an exact registered alias ranks before substring matches");
    bool pineapple=false,green=false;
    for(const auto& match:apples) {
        const tString expanded=emoji->ExpandShortcodes(match.replacement);
        pineapple=pineapple || expanded=="\xF0\x9F\x8D\x8D";
        green=green || expanded=="\xF0\x9F\x8D\x8F";
    }
    require(pineapple && green,"completion also includes aliases containing the query within their name");
    require(!emoji->FindCompletionSuggestions("pi\xC3\xB1" "ata",0).empty() &&
        !emoji->FindCompletionSuggestions("+1",0).empty(),"Unicode and signed registered aliases are valid completion queries");
    require(emoji->FindCompletionSuggestions("codex_missing_emoji_completion",0).empty(),"unmatched aliases have no completion candidates");
}
static ImGuiWindow* pickerGrid() {
    ImGuiWindow* picker=ImGui::FindWindowByName("Emoji##MultiplayerChatPicker");
    require(picker && picker->Active,"category controls belong to the active picker");
    for(ImGuiWindow* child:picker->DC.ChildWindows)
        if(std::strstr(child->Name,"ChatEmojiGrid")) return child;
    require(false,"categorized emoji use an independently scrollable grid");return NULL;
}
static void requireCategoryControls(cLuxMultiplayerUI& ui) {
    const ImVec2 display=ImGui::GetIO().DisplaySize;
    const cVector2f panelEnd=ui.mvChatPickerPos+ui.mvChatPickerSize;
    for(int category=0;category<8;++category) {
        const cVector2f position=ui.mvChatPickerCategoryPos[category],size=ui.mvChatPickerCategorySize[category];
        require(size.x>0 && size.y>0 && position.x>=ui.mvChatPickerPos.x && position.y>=ui.mvChatPickerPos.y &&
            position.x+size.x<=panelEnd.x+1 && position.y+size.y<=panelEnd.y+1 &&
            position.x>=0 && position.y>=0 && position.x+size.x<=display.x+1 && position.y+size.y<=display.y+1,
            "every category button remains visible and clickable inside the resized picker");
        for(int earlier=0;earlier<category;++earlier) {
            const cVector2f other=ui.mvChatPickerCategoryPos[earlier],otherSize=ui.mvChatPickerCategorySize[earlier];
            require(position.x>=other.x+otherSize.x-0.1f || other.x>=position.x+size.x-0.1f ||
                position.y>=other.y+otherSize.y-0.1f || other.y>=position.y+size.y-0.1f,
                "resizing does not overlap category mouse targets");
        }
    }
}
static void searchPicker(cLuxMultiplayerUI& ui,const tString& query) {
    click(ui,ui.mvChatPickerSearchPos,ui.mvChatPickerSearchSize);
    shortcut(ui,SDL_SCANCODE_A,SDLK_a);
    event(SDL_KEYDOWN,SDL_SCANCODE_BACKSPACE,SDLK_BACKSPACE);draw(ui);
    event(SDL_KEYUP,SDL_SCANCODE_BACKSPACE,SDLK_BACKSPACE);for(int i=0;i<3;++i) draw(ui);
    if(!query.empty()) {textEvent(query);for(int i=0;i<3;++i) draw(ui);}
    require(tString(ui.msChatEmojiSearch)==query,"SDL editing changes only the picker query");
}
static void checkPickerCategories(cLuxMultiplayerUI& ui) {
    const tString draft=ui.msChatInput;const int cursor=ui.mlChatCursor;
    const int selectionStart=ui.mlChatSelectionStart,selectionEnd=ui.mlChatSelectionEnd;
    const auto& entries=ui.mpChatEmoji->GetPickerEntries();
    const auto select=[&](int category) {
        click(ui,ui.mvChatPickerCategoryPos[category],ui.mvChatPickerCategorySize[category]);
        require(ui.mlChatPickerCategory==category && !ui.mbChatPickerSearchResults && !ui.msChatEmojiSearch[0] &&
            ui.msChatPickerHeader==ui.mpChatEmoji->GetCategoryName(category) && !ui.mvChatPickerEntryIndices.empty(),
            "SDL clicking a category displays its named choices and clears global search");
        for(size_t index:ui.mvChatPickerEntryIndices)
            require(index<entries.size() && entries[index].category==category && !entries[index].skinTone,
                "default category browsing contains only that category without repeated tone variants");
        require(ui.mfChatPickerGridScroll<=0.1f,"switching categories returns to the beginning of their grid");
        require(tString(ui.msChatInput)==draft && ui.mlChatCursor==cursor &&
            ui.mlChatSelectionStart==selectionStart && ui.mlChatSelectionEnd==selectionEnd,
            "category navigation preserves the raw draft, caret and selection");
    };
    select(0);requireCategoryControls(ui);
    require(ui.msChatPickerFirstUnicode=="\xF0\x9F\x98\x80","People starts with grinning in Discord source order");
    screenshot("chat-categories-people.png");
    ImGuiWindow* grid=pickerGrid();
    moveMouse(ui,static_cast<int>(grid->Pos.x+grid->Size.x*0.5f),static_cast<int>(grid->Pos.y+grid->Size.y*0.5f));
    wheelHistory(ui,-8);
    require(ui.mfChatPickerGridScroll>0,"SDL wheel scrolls the actual category grid independently of history");
    for(int category=1;category<8;++category) select(category);
    screenshot("chat-categories-flags.png");
    resizeOverlay(ui,320,240);requireCategoryControls(ui);requireChatFont(ui);
    require(ui.mlChatPickerCategory==7 && ui.msChatPickerHeader=="Flags" && tString(ui.msChatInput)==draft,
        "a compact resize preserves selected category and editable draft");
    screenshot("chat-categories-flags-320x240.png");
    resizeOverlay(ui,800,600);select(6);
    searchPicker(ui,":FLAG_US:");
    require(ui.mbChatPickerSearchResults && ui.mlChatPickerCategory==6 && ui.mvChatPickerEntryIndices.size()==1 &&
        entries[ui.mvChatPickerEntryIndices[0]].category==7 &&
        ui.msChatPickerFirstUnicode=="\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8",
        "colon-wrapped uppercase aliases search globally outside the selected category");
    searchPicker(ui,"thumbsup_tone3");
    require(ui.mbChatPickerSearchResults && ui.mvChatPickerEntryIndices.size()==1 &&
        entries[ui.mvChatPickerEntryIndices[0]].skinTone &&
        ui.msChatPickerFirstUnicode=="\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD",
        "global search exposes a specific skin-tone variant without flooding normal browsing");
    searchPicker(ui,"ice cream");
    bool iceCream=false;
    for(size_t index:ui.mvChatPickerEntryIndices) if(entries[index].unicode=="\xF0\x9F\x8D\xA8") iceCream=true;
    require(ui.mbChatPickerSearchResults && iceCream,
        "readable search words find underscore-separated aliases without requiring shortcode syntax");
    searchPicker(ui,"\xF0\x9F\x98\x80");
    require(ui.mbChatPickerSearchResults && ui.mvChatPickerEntryIndices.size()==1 &&
        ui.msChatPickerFirstUnicode=="\xF0\x9F\x98\x80",
        "pasting a Unicode emoji searches its canonical artwork across categories");
    searchPicker(ui,"orca");
    require(ui.mbChatPickerSearchResults && ui.mvChatPickerEntryIndices.size()==1 &&
        entries[ui.mvChatPickerEntryIndices[0]].category==1 &&
        ui.msChatPickerFirstUnicode=="\xF0\x9F\xAB\x8D" &&
        ui.mpChatEmoji->GetShortcodes().find("orca")==ui.mpChatEmoji->GetShortcodes().end(),
        "Unicode 17 fallback labels remain searchable without inventing unregistered chat shortcodes");
    searchPicker(ui,"\xF0\x9F\xAB\x8D");
    require(ui.mbChatPickerSearchResults && ui.mvChatPickerEntryIndices.size()==1 &&
        ui.msChatPickerFirstUnicode=="\xF0\x9F\xAB\x8D",
        "Unicode search also finds recent artwork absent from the Discord alias snapshot");
    const int sends=base.mpMultiplayer->chatSends;
    event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);draw(ui);
    event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
    require(ui.mbChatEmojiPickerOpen && ui.IsChatOpen() && tString(ui.msChatInput)==draft &&
        base.mpMultiplayer->chatSends==sends,
        "Enter in picker search does not submit the saved chat draft or close the picker");
    searchPicker(ui,"codex_emoji_missing_category");
    require(ui.mbChatPickerSearchResults && ui.mvChatPickerEntryIndices.empty() &&
        ui.msChatPickerFirstUnicode.empty() && ui.mvChatPickerFirstEmojiSize.x==0,
        "an empty search result cannot retain a stale clickable emoji target");
    searchPicker(ui,"");
    require(!ui.mbChatPickerSearchResults && ui.mlChatPickerCategory==6 && ui.msChatPickerHeader=="Symbols" &&
        !ui.mvChatPickerEntryIndices.empty() && ui.mfChatPickerGridScroll<=0.1f,
        "clearing a global query restores the selected category at its beginning");
    searchPicker(ui,"grinning");select(7);searchPicker(ui,"grinning");
    require(ui.mbChatPickerSearchResults && ui.mlChatPickerCategory==7 && tString(ui.msChatInput)==draft &&
        ui.mlChatCursor==cursor && ui.mlChatSelectionStart==selectionStart && ui.mlChatSelectionEnd==selectionEnd,
        "category/search round trips retain the saved Unicode insertion point");
}
static tString toneSuffix(int tone) {
    static const char* values[]={"","\xF0\x9F\x8F\xBB","\xF0\x9F\x8F\xBC","\xF0\x9F\x8F\xBD","\xF0\x9F\x8F\xBE","\xF0\x9F\x8F\xBF"};
    require(tone>=0 && tone<6,"tone fixture has a valid preference");return values[tone];
}
static void requireToneOptions(cLuxMultiplayerUI& ui) {
    require(ui.mbChatPickerToneOpen,"tone choices are open after their rendered button was clicked");
    const ImVec2 display=ImGui::GetIO().DisplaySize;
    for(int tone=0;tone<6;++tone) {
        const cVector2f position=ui.mvChatPickerToneOptionPos[tone],size=ui.mvChatPickerToneOptionSize[tone];
        require(size.x>0 && size.y>0 && position.x>=0 && position.y>=0 &&
            position.x+size.x<=display.x+1 && position.y+size.y<=display.y+1,
            "all six tone options remain clickable within the resized viewport");
        for(int earlier=0;earlier<tone;++earlier) {
            const cVector2f other=ui.mvChatPickerToneOptionPos[earlier],otherSize=ui.mvChatPickerToneOptionSize[earlier];
            require(position.x>=other.x+otherSize.x-0.1f || other.x>=position.x+size.x-0.1f ||
                position.y>=other.y+otherSize.y-0.1f || other.y>=position.y+size.y-0.1f,
                "tone option mouse targets remain distinct after a resize");
        }
    }
}
static void openToneOptions(cLuxMultiplayerUI& ui) {
    click(ui,ui.mvChatPickerToneButtonPos,ui.mvChatPickerToneButtonSize);requireToneOptions(ui);
}
static void selectPickerTone(cLuxMultiplayerUI& ui,int tone) {
    const int category=ui.mlChatPickerCategory,cursor=ui.mlChatCursor;
    const int selectionStart=ui.mlChatSelectionStart,selectionEnd=ui.mlChatSelectionEnd;
    const tString query=ui.msChatEmojiSearch,draft=ui.msChatInput;
    const float scroll=ui.mfChatPickerGridScroll;
    openToneOptions(ui);click(ui,ui.mvChatPickerToneOptionPos[tone],ui.mvChatPickerToneOptionSize[tone]);
    require(!ui.mbChatPickerToneOpen && ui.mlChatPickerTone==tone && ui.mbChatEmojiPickerOpen && ui.IsChatOpen(),
        "SDL selecting a tone updates the full picker preference and closes only the dropdown");
    require(ui.mlChatPickerCategory==category && tString(ui.msChatEmojiSearch)==query &&
        tString(ui.msChatInput)==draft && ui.mlChatCursor==cursor &&
        ui.mlChatSelectionStart==selectionStart && ui.mlChatSelectionEnd==selectionEnd &&
        std::fabs(ui.mfChatPickerGridScroll-scroll)<=0.1f,
        "tone selection preserves the category, query, draft, Unicode selection and scroll position");
}
static void checkPickerTones(cLuxMultiplayerUI& ui) {
    const auto& entries=ui.mpChatEmoji->GetPickerEntries();
    const tString draft=ui.msChatInput;const int cursor=ui.mlChatCursor,sends=base.mpMultiplayer->chatSends;
    click(ui,ui.mvChatPickerCategoryPos[0],ui.mvChatPickerCategorySize[0]);
    ImGuiWindow* grid=pickerGrid();
    moveMouse(ui,static_cast<int>(grid->Pos.x+grid->Size.x*0.5f),static_cast<int>(grid->Pos.y+grid->Size.y*0.5f));
    wheelHistory(ui,-8);require(ui.mfChatPickerGridScroll>0,"tone preference fixture starts inside a scrolled category");
    int thumbBase=-1;
    require(ui.mpChatEmoji->Match("\xF0\x9F\x91\x8D",0,thumbBase)==4,"thumbs-up fixture resolves its unmodified artwork");
    for(int tone=0;tone<6;++tone) {
        selectPickerTone(ui,tone);
        const tString expected=tString("\xF0\x9F\x91\x8D")+toneSuffix(tone);int expectedGlyph=-1;
        require(ui.mpChatEmoji->Match(expected,0,expectedGlyph)==expected.size(),"preferred tone artwork really exists in the atlas");
        bool thumb=false,face=false;
        require(ui.mvChatPickerDisplayGlyphs.size()==ui.mvChatPickerEntryIndices.size(),"each category choice has an actual resolved display glyph");
        for(size_t index=0;index<ui.mvChatPickerEntryIndices.size();++index) {
            const auto& entry=entries[ui.mvChatPickerEntryIndices[index]];
            const int displayGlyph=ui.mvChatPickerDisplayGlyphs[index];
            require(displayGlyph>=0 && size_t(displayGlyph)<ui.mpChatEmoji->GetGlyphCount(),
                "global tone selection uses supported artwork without synthesizing nonexistent combinations");
            if(entry.glyph==thumbBase) {thumb=true;require(displayGlyph==expectedGlyph,"the selected tone changes thumbs-up in the whole category list");}
            if(entry.unicode=="\xF0\x9F\x98\x80") {face=true;require(displayGlyph==entry.glyph,"a tone preference leaves unsupported smiling faces unchanged");}
        }
        require(thumb && face,"the full category retains both supported hands and unchanged faces");
    }
    selectPickerTone(ui,0);
    searchPicker(ui,"thumbsup");
    for(int tone=0;tone<6;++tone) {
        selectPickerTone(ui,tone);
        require(ui.mvChatPickerMatches.size()==1 && ui.msChatPickerFirstUnicode==tString("\xF0\x9F\x91\x8D")+toneSuffix(tone),
            "generic aliases show one preferred-tone result instead of every variant or duplicate synonym");
    }
    selectPickerTone(ui,5);searchPicker(ui,"thumbsup_tone3");
    require(ui.mvChatPickerMatches.size()==1 && ui.msChatPickerFirstUnicode==tString("\xF0\x9F\x91\x8D")+toneSuffix(3),
        "an explicitly requested alias tone overrides the global preference");
    searchPicker(ui,"snowboarder");
    require(ui.mvChatPickerMatches.size()==1 && ui.msChatPickerFirstUnicode==tString("\xF0\x9F\x8F\x82")+toneSuffix(5),
        "the global tone also changes supported activity emoji found by generic search");
    searchPicker(ui,"couple_with_heart");
    int coupleBase=-1;ui.mpChatEmoji->Match("\xF0\x9F\x92\x91",0,coupleBase);
    for(int tone=1;tone<6;++tone) {
        selectPickerTone(ui,tone);bool couple=false;
        const tString expected=tString("\xF0\x9F\x92\x91")+toneSuffix(tone);int expectedGlyph=-1;
        require(ui.mpChatEmoji->Match(expected,0,expectedGlyph)==expected.size(),"uniform legacy couple artwork exists in the atlas");
        for(size_t index=0;index<ui.mvChatPickerEntryIndices.size();++index)
            if(entries[ui.mvChatPickerEntryIndices[index]].glyph==coupleBase) {
                couple=true;require(ui.mvChatPickerDisplayGlyphs[index]==expectedGlyph,
                    "multi-person couples use the selected uniform tone's real legacy glyph");
            }
        require(couple,"generic couple search retains its canonical legacy family");
    }
    const tString mixed=tString("\xF0\x9F\xA7\x91")+toneSuffix(1)+"\xE2\x80\x8D\xE2\x9D\xA4\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\xA7\x91"+toneSuffix(5);
    searchPicker(ui,mixed);selectPickerTone(ui,3);
    require(ui.mvChatPickerMatches.size()==1 && ui.msChatPickerFirstUnicode==mixed,
        "pasting an explicit mixed-tone couple preserves both requested tones");
    searchPicker(ui,"flag_us");
    require(ui.mvChatPickerMatches.size()==1 && ui.msChatPickerFirstUnicode=="\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8",
        "global tone selection leaves flags unchanged");
    searchPicker(ui,"thumbsup");
    const tString inserted=draft.substr(0,size_t(cursor))+tString("\xF0\x9F\x91\x8D")+toneSuffix(3)+draft.substr(size_t(cursor));
    click(ui,ui.mvChatPickerFirstEmojiPos,ui.mvChatPickerFirstEmojiSize);
    require(!ui.mbChatEmojiPickerOpen && ui.IsChatOpen() && tString(ui.msChatInput)==inserted &&
        base.mpMultiplayer->chatSends==sends,
        "clicking a preferred-tone result inserts the displayed Unicode variant at the saved caret without sending");
    shortcut(ui,SDL_SCANCODE_Z,SDLK_z);
    require(tString(ui.msChatInput)==draft && ui.mlChatCursor==cursor,"native Undo removes the complete chosen tone and restores the draft caret");
    openPicker(ui);
    require(ui.mlChatPickerTone==3 && !ui.msChatEmojiSearch[0],"reopening retains the chosen global tone while clearing the previous query");
    click(ui,ui.mvChatPickerCategoryPos[7],ui.mvChatPickerCategorySize[7]);searchPicker(ui,"grinning");
    openToneOptions(ui);
    for(const cVector2l& size:{cVector2l(320,240),cVector2l(997,613),cVector2l(338,1000),cVector2l(1920,540),cVector2l(3840,2160)}) {
        resizeOverlay(ui,size.x,size.y);requireToneOptions(ui);requireCategoryControls(ui);requireChatFont(ui);
        require(ui.mlChatPickerTone==3 && ui.mlChatPickerCategory==7 && tString(ui.msChatEmojiSearch)=="grinning" &&
            tString(ui.msChatInput)==draft && ui.mlChatCursor==cursor,
            "resizing an active tone dropdown preserves preference, category, global query and raw draft caret");
        const tString filename="chat-tone-dropdown-"+cString::ToString(size.x)+"x"+cString::ToString(size.y)+".png";
        screenshot(filename.c_str());
    }
    event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);draw(ui);
    event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);for(int i=0;i<3;++i) draw(ui);
    require(!ui.mbChatPickerToneOpen && ui.mbChatEmojiPickerOpen && ui.IsChatOpen() && ui.mlChatPickerTone==3 &&
        tString(ui.msChatInput)==draft && base.mpMultiplayer->chatSends==sends,
        "first Escape closes only the tone dropdown while retaining picker, preference and unsent draft");
    resizeOverlay(ui,800,600);
}
static void chatKey(cLuxMultiplayerUI& ui,SDL_Scancode scan,SDL_Keycode key) {
    event(SDL_KEYDOWN,scan,key);draw(ui);
    event(SDL_KEYUP,scan,key);for(int i=0;i<3;++i) draw(ui);
}
static void holdChatKeyThroughRepeat(cLuxMultiplayerUI& ui) {
    const ImGuiIO& io=ImGui::GetIO();
    const Uint32 duration=static_cast<Uint32>((io.KeyRepeatDelay+io.KeyRepeatRate*2)*1000)+20;
    const Uint32 started=SDL_GetTicks();
    // The SDL backend measures real time between frames; fast offscreen redraws
    // alone do not advance key DownDuration far enough to exercise repetition.
    while(SDL_GetTicks()-started<duration) {SDL_Delay(10);draw(ui);}
}
static void replaceChatDraft(cLuxMultiplayerUI& ui,const tString& value) {
    if(ui.mbChatCompletionOpen) chatKey(ui,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
    shortcut(ui,SDL_SCANCODE_A,SDLK_a);
    chatKey(ui,SDL_SCANCODE_BACKSPACE,SDLK_BACKSPACE);
    if(!value.empty()) {textEvent(value);for(int i=0;i<3;++i) draw(ui);}
    require(ui.IsChatOpen() && tString(ui.msChatInput)==value,"native selection and SDL typing prepare the intended completion draft");
}
static void requireChatCompletion(cLuxMultiplayerUI& ui,bool selectedVisible=true) {
    require(ui.mbChatCompletionOpen && !ui.mvChatCompletionMatches.empty() &&
        ui.mlChatCompletionSelected>=0 && size_t(ui.mlChatCompletionSelected)<ui.mvChatCompletionMatches.size(),
        "autocomplete has a valid selected registered alias");
    const ImVec2 screen=ImGui::GetIO().DisplaySize;
    const cVector2f position=ui.mvChatCompletionPos,size=ui.mvChatCompletionSize;
    require(position.x>=0 && position.y>=0 && size.x>0 && size.y>0 &&
        position.x+size.x<=screen.x+1 && position.y+size.y<=screen.y+1 &&
        position.y+size.y<=ui.mvChatEntryPos.y+1,
        "autocomplete fits the viewport above the entry without obscuring the editable draft");
    require(ui.mlChatCompletionVisibleRows>0 && ui.mlChatCompletionVisibleRows<=8 &&
        ui.mlChatCompletionFirstRow>=0 &&
        size_t(ui.mlChatCompletionFirstRow+ui.mlChatCompletionVisibleRows)<=ui.mvChatCompletionMatches.size(),
        "every visible autocomplete row identifies an existing suggestion");
    if(selectedVisible) require(ui.mlChatCompletionSelected>=ui.mlChatCompletionFirstRow &&
        ui.mlChatCompletionSelected<ui.mlChatCompletionFirstRow+ui.mlChatCompletionVisibleRows,
        "the keyboard-selected suggestion remains visible when the popup changes height");
    for(int row=0;row<ui.mlChatCompletionVisibleRows;++row) {
        const cVector2f a=ui.mvChatCompletionRowPos[row],extent=ui.mvChatCompletionRowSize[row];
        require(extent.x>0 && extent.y>0 && a.x>=position.x && a.y>=position.y &&
            a.x+extent.x<=position.x+size.x+1 && a.y+extent.y<=position.y+size.y+1,
            "autocomplete row artwork, text and mouse targets stay within its clipped panel");
        if(row) require(a.y>=ui.mvChatCompletionRowPos[row-1].y+ui.mvChatCompletionRowSize[row-1].y-0.1f,
            "autocomplete row mouse targets do not overlap");
    }
    ImGuiWindow* entry=ImGui::FindWindowByName("##MultiplayerChat");
    ImGuiWindow* completion=ImGui::FindWindowByName("Emoji suggestions##MultiplayerChatCompletion");
    require(entry && completion && completion->Active && completion->FontWindowScale==1.0f &&
        ImGui::GetCurrentContext()->ActiveId==entry->GetID("##Message"),
        "showing suggestions retains native text-editor focus and rasterized font pixels");
}
static tString completionResult(cLuxMultiplayerUI& ui,int index=-1) {
    if(index<0) index=ui.mlChatCompletionSelected;
    require(index>=0 && size_t(index)<ui.mvChatCompletionMatches.size(),"completion acceptance targets a real candidate");
    const tString draft=ui.msChatInput;const auto token=ui.mChatCompletionToken;
    return draft.substr(0,token.start)+ui.mvChatCompletionMatches[index].replacement+draft.substr(token.end);
}
static void checkChatAutocomplete(cLuxMultiplayerUI& ui) {
    const int sends=base.mpMultiplayer->chatSends;
    resizeOverlay(ui,800,600);chatKey(ui,SDL_SCANCODE_T,SDLK_t);
    require(ui.IsChatOpen() && !ui.msChatInput[0],"autocomplete fixtures begin in an empty native editor");
    replaceChatDraft(ui,":g");
    require(!ui.mbChatCompletionOpen,"typing one alias character leaves the entry unobstructed");
    textEvent("r");for(int i=0;i<3;++i) draw(ui);requireChatCompletion(ui);
    require(ui.mChatCompletionToken.query=="gr" && ui.mvChatCompletionMatches.size()>8,
        "the second typed character opens a useful prefix list with additional scrollable choices");
    const tString arrowDraft=ui.msChatInput;
    chatKey(ui,SDL_SCANCODE_DOWN,SDLK_DOWN);
    require(ui.mlChatCompletionSelected==1 && tString(ui.msChatInput)==arrowDraft,
        "Down selects the next suggestion instead of changing the raw draft");
    chatKey(ui,SDL_SCANCODE_UP,SDLK_UP);
    require(ui.mlChatCompletionSelected==0,"Up returns to the previous suggestion");
    chatKey(ui,SDL_SCANCODE_UP,SDLK_UP);
    require(ui.mlChatCompletionSelected==int(ui.mvChatCompletionMatches.size())-1,"Up wraps to the last registered suggestion");
    chatKey(ui,SDL_SCANCODE_DOWN,SDLK_DOWN);
    require(ui.mlChatCompletionSelected==0,"Down wraps back to the beginning");
    event(SDL_KEYDOWN,SDL_SCANCODE_DOWN,SDLK_DOWN);draw(ui);
    const int initialRepeat=ui.mlChatCompletionSelected;
    holdChatKeyThroughRepeat(ui);
    if(ui.mlChatCompletionSelected==initialRepeat)
        std::fprintf(stderr,"Held Down duration=%.3f repeatDelay=%.3f repeatRate=%.3f selected=%d initial=%d matches=%llu\n",
            ImGui::GetKeyData(ImGuiKey_DownArrow)->DownDuration,ImGui::GetIO().KeyRepeatDelay,ImGui::GetIO().KeyRepeatRate,
            ui.mlChatCompletionSelected,initialRepeat,static_cast<unsigned long long>(ui.mvChatCompletionMatches.size()));
    require(ui.mlChatCompletionSelected!=initialRepeat,"holding an arrow repeats suggestion navigation");
    event(SDL_KEYUP,SDL_SCANCODE_DOWN,SDLK_DOWN);for(int i=0;i<3;++i) draw(ui);requireChatCompletion(ui);
    const tString tabResult=completionResult(ui);
    chatKey(ui,SDL_SCANCODE_TAB,SDLK_TAB);
    require(!ui.mbChatCompletionOpen && ui.IsChatOpen() && tString(ui.msChatInput)==tabResult &&
        base.mpMultiplayer->chatSends==sends,"Tab accepts the highlighted alias without sending or moving focus away");
    shortcut(ui,SDL_SCANCODE_Z,SDLK_z);
    require(tString(ui.msChatInput)==arrowDraft,"one native Undo reverses the entire autocomplete replacement");
    shortcut(ui,SDL_SCANCODE_Y,SDLK_y);
    require(tString(ui.msChatInput)==tabResult,"one native Redo restores the entire autocomplete replacement");
    replaceChatDraft(ui,"Enter :gr");requireChatCompletion(ui);
    const tString enterResult=completionResult(ui);
    event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);draw(ui);
    holdChatKeyThroughRepeat(ui);
    event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN,1);draw(ui);
    if(!ui.IsChatOpen() || tString(ui.msChatInput)!=enterResult || base.mpMultiplayer->chatSends!=sends)
        std::fprintf(stderr,"Held Enter duration=%.3f repeatDelay=%.3f repeatRate=%.3f chat=%d sends=%d expectedSends=%d\n",
            ImGui::GetKeyData(ImGuiKey_Enter)->DownDuration,ImGui::GetIO().KeyRepeatDelay,ImGui::GetIO().KeyRepeatRate,
            int(ui.IsChatOpen()),base.mpMultiplayer->chatSends,sends);
    require(ui.IsChatOpen() && tString(ui.msChatInput)==enterResult && base.mpMultiplayer->chatSends==sends,
        "Enter accepts completion and its held/repeated key cannot submit the resulting message");
    event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
    replaceChatDraft(ui,"same :g");
    const auto sameCandidates=ui.mpChatEmoji->FindCompletionSuggestions("gr",ui.mlChatPickerTone,32);
    require(!sameCandidates.empty(),"same-frame acceptance has a registered candidate");
    textEvent("r");event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);draw(ui);
    event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
    require(ui.IsChatOpen() && tString(ui.msChatInput)=="same "+sameCandidates[0].replacement &&
        base.mpMultiplayer->chatSends==sends,"queued typing and Enter in one frame complete the alias before any send");
    replaceChatDraft(ui,":gr");requireChatCompletion(ui);
    chatKey(ui,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
    for(int i=0;i<10;++i) draw(ui);
    require(ui.IsChatOpen() && !ui.mbChatCompletionOpen && tString(ui.msChatInput)==":gr",
        "Escape dismisses suggestions while retaining the draft and suppressing unchanged-token reopening");
    chatKey(ui,SDL_SCANCODE_LEFT,SDLK_LEFT);
    require(!ui.mbChatCompletionOpen && ui.mlChatCursor==2 && tString(ui.msChatInput)==":gr",
        "moving away from a dismissed token preserves the draft without opening a one-character query");
    chatKey(ui,SDL_SCANCODE_RIGHT,SDLK_RIGHT);requireChatCompletion(ui);
    require(ui.mlChatCursor==3 && ui.mChatCompletionToken.query=="gr" && tString(ui.msChatInput)==":gr",
        "returning the caret to a previously dismissed token reopens its suggestions");
    chatKey(ui,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
    require(!ui.mbChatCompletionOpen,"the caret round-trip list can be dismissed again before editing");
    textEvent("e");for(int i=0;i<3;++i) draw(ui);requireChatCompletion(ui);
    for(const tString& draft:std::vector<tString>{"https://example.test", "https:gr", "12:30", "user:gr@example.test",
        ":codex_missing_emoji_completion", ":grinning:", ":grinning:gr", ":\xC3\xB1"}) {
        replaceChatDraft(ui,draft);
        require(!ui.mbChatCompletionOpen && tString(ui.msChatInput)==draft,
            "literal URLs, times, email-like text, unknown/completed aliases and one Unicode character stay unobstructed");
    }
    replaceChatDraft(ui,":pi\xC3\xB1" "ata");requireChatCompletion(ui);
    const tString pinataResult=completionResult(ui);
    const auto pinata=ui.mvChatCompletionMatches[ui.mlChatCompletionSelected];
    require(ui.mpChatEmoji->ExpandShortcodes(pinata.replacement)=="\xF0\x9F\xAA\x85",
        "a Unicode shortcode displays its correct pinata artwork");
    chatKey(ui,SDL_SCANCODE_TAB,SDLK_TAB);
    require(tString(ui.msChatInput)==pinataResult && luxnet::ValidChatText(ui.msChatInput),
        "accepting a Unicode alias preserves complete UTF-8 editing bytes");
    replaceChatDraft(ui,":+1");requireChatCompletion(ui);
    require(ui.mvChatCompletionMatches.size()==1,"a signed two-character alias satisfies the completion threshold");
    chatKey(ui,SDL_SCANCODE_TAB,SDLK_TAB);
    replaceChatDraft(ui,":adult::sk");requireChatCompletion(ui);
    const tString compoundResult=completionResult(ui);
    require(ui.mChatCompletionToken.start==0 && ui.mvChatCompletionMatches.size()>=5,
        "the editable field exposes registered compound tone aliases as complete replacements");
    for(int tone=1;tone<6;++tone)
        require(ui.mpChatEmoji->ExpandShortcodes(ui.mvChatCompletionMatches[tone-1].replacement)==
            tString("\xF0\x9F\xA7\x91")+toneSuffix(tone),
            "the field ranks all adult prefix variants before related substring matches without collapsing requested tones");
    chatKey(ui,SDL_SCANCODE_TAB,SDLK_TAB);
    require(tString(ui.msChatInput)==compoundResult && ui.mlChatEntryEmoji==1,
        "accepting a compound alias replaces its full raw prefix and renders one composed person glyph");
    const tString middle="pr\xC3\xA9 \xF0\x9F\x98\x80 :green suffix";
    replaceChatDraft(ui,middle);clickChatCaret(ui,middle.find(':')+3);requireChatCompletion(ui);
    const tString middleResult=completionResult(ui);
    chatKey(ui,SDL_SCANCODE_TAB,SDLK_TAB);
    require(tString(ui.msChatInput)==middleResult && middleResult.find("pr\xC3\xA9 \xF0\x9F\x98\x80 ")==0 &&
        middleResult.substr(middleResult.size()-7)==" suffix",
        "accepting at a Unicode caret replaces the whole alias word and preserves surrounding text");
    shortcut(ui,SDL_SCANCODE_Z,SDLK_z);
    require(tString(ui.msChatInput)==middle,"Undo restores the complete alias word and surrounding Unicode draft");
    const tString mouseDraft="mouse :gr suffix";
    replaceChatDraft(ui,mouseDraft);clickChatCaret(ui,9);requireChatCompletion(ui);
    const int mouseIndex=ui.mlChatCompletionFirstRow;
    const tString mouseResult=completionResult(ui,mouseIndex);
    const size_t mouseCaret=ui.mChatCompletionToken.start+ui.mvChatCompletionMatches[mouseIndex].replacement.size();
    const int mouseX=static_cast<int>(ui.mvChatCompletionRowPos[0].x+ui.mvChatCompletionRowSize[0].x*0.5f);
    const int mouseY=static_cast<int>(ui.mvChatCompletionRowPos[0].y+ui.mvChatCompletionRowSize[0].y*0.5f);
    queueMouse(true,mouseX,mouseY);textEvent("X");draw(ui);
    queueMouse(false,mouseX,mouseY);base.mpEngine->GetInput()->Update(1.0f/60);for(int i=0;i<3;++i) draw(ui);
    const tString mouseTyped=mouseResult.substr(0,mouseCaret)+"X"+mouseResult.substr(mouseCaret);
    require(ui.IsChatOpen() && !ui.mbChatCompletionOpen && tString(ui.msChatInput)==mouseTyped &&
        base.mpMultiplayer->chatSends==sends,"clicking a suggestion and queued typing retain native focus and insert text after the accepted alias");
    shortcut(ui,SDL_SCANCODE_Z,SDLK_z);require(tString(ui.msChatInput)==mouseResult,"Undo first removes queued typing without disturbing the accepted alias");
    shortcut(ui,SDL_SCANCODE_Z,SDLK_z);require(tString(ui.msChatInput)==mouseDraft,"the next Undo reverses the complete mouse acceptance atomically");
    shortcut(ui,SDL_SCANCODE_Y,SDLK_y);require(tString(ui.msChatInput)==mouseResult,"Redo restores the complete mouse acceptance atomically");
    replaceChatDraft(ui,mouseDraft);clickChatCaret(ui,9);requireChatCompletion(ui);
    const tString pasteResult=completionResult(ui,ui.mlChatCompletionFirstRow);
    const size_t pasteCaret=ui.mChatCompletionToken.start+ui.mvChatCompletionMatches[ui.mlChatCompletionFirstRow].replacement.size();
    char* previousClipboard=readFixtureClipboard();require(previousClipboard!=NULL,"save clipboard before same-frame completion paste");
    const tString previous=previousClipboard;SDL_free(previousClipboard);require(setFixtureClipboard("P"),"prepare native completion paste");
    const int pasteX=static_cast<int>(ui.mvChatCompletionRowPos[0].x+ui.mvChatCompletionRowSize[0].x*0.5f);
    const int pasteY=static_cast<int>(ui.mvChatCompletionRowPos[0].y+ui.mvChatCompletionRowSize[0].y*0.5f);
    queueMouse(true,pasteX,pasteY);event(SDL_KEYDOWN,SDL_SCANCODE_LCTRL,SDLK_LCTRL,0,KMOD_CTRL);
    event(SDL_KEYDOWN,SDL_SCANCODE_V,SDLK_v,0,KMOD_CTRL);draw(ui);
    event(SDL_KEYUP,SDL_SCANCODE_V,SDLK_v,0,KMOD_CTRL);event(SDL_KEYUP,SDL_SCANCODE_LCTRL,SDLK_LCTRL);
    queueMouse(false,pasteX,pasteY);base.mpEngine->GetInput()->Update(1.0f/60);for(int i=0;i<3;++i) draw(ui);
    require(setFixtureClipboard(previous.c_str()),"restore clipboard after native completion paste");
    require(tString(ui.msChatInput)==pasteResult.substr(0,pasteCaret)+"P"+pasteResult.substr(pasteCaret) &&
        base.mpMultiplayer->chatSends==sends,"mouse acceptance precedes queued native paste at the preserved raw draft caret");
    replaceChatDraft(ui,":thumbsup");requireChatCompletion(ui);
    require(ui.mpChatEmoji->ExpandShortcodes(ui.mvChatCompletionMatches[0].replacement)==tString("\xF0\x9F\x91\x8D")+toneSuffix(3),
        "generic autocomplete respects the selected global skin tone");
    openPicker(ui);
    require(!ui.mbChatCompletionOpen && ui.mbChatEmojiPickerOpen,"opening the emoji picker suspends autocomplete instead of stacking two choice windows");
    selectPickerTone(ui,5);chatKey(ui,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
    replaceChatDraft(ui,":thumbsup_tone3");requireChatCompletion(ui);
    require(ui.mpChatEmoji->ExpandShortcodes(ui.mvChatCompletionMatches[0].replacement)==tString("\xF0\x9F\x91\x8D")+toneSuffix(3),
        "an explicit completion tone remains authoritative over a different global preference");
    openPicker(ui);selectPickerTone(ui,3);chatKey(ui,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
    replaceChatDraft(ui,":gr");requireChatCompletion(ui);
    for(int i=0;i<7;++i) chatKey(ui,SDL_SCANCODE_DOWN,SDLK_DOWN);
    const int selected=ui.mlChatCompletionSelected,cursor=ui.mlChatCursor;
    const tString resizeDraft=ui.msChatInput,selectedAlias=ui.mvChatCompletionMatches[selected].alias;
    for(const cVector2l& screen:{cVector2l(320,240),cVector2l(997,613),cVector2l(338,1000),cVector2l(1920,540),cVector2l(3840,2160)}) {
        resizeOverlay(ui,screen.x,screen.y);requireChatCompletion(ui);requireChatFont(ui);
        require(tString(ui.msChatInput)==resizeDraft && ui.mlChatCursor==cursor && ui.mlChatCompletionSelected==selected &&
            ui.mvChatCompletionMatches[selected].alias==selectedAlias,
            "short, portrait, odd and 4K resizes preserve the draft, caret and visible keyboard-selected suggestion");
        const tString filename="chat-completion-"+cString::ToString(screen.x)+"x"+cString::ToString(screen.y)+".png";
        screenshot(filename.c_str());
    }
    checkChatFontDensities(ui);requireChatCompletion(ui);
    require(tString(ui.msChatInput)==resizeDraft && ui.mlChatCursor==cursor && ui.mlChatCompletionSelected==selected,
        "fractional and retina font rebuilds retain active autocomplete state and the raw editor caret");
    resizeOverlay(ui,800,600);
    replaceChatDraft(ui,tString(508,'X')+" :gr");requireChatCompletion(ui);
    const tString bounded=ui.msChatInput;chatKey(ui,SDL_SCANCODE_TAB,SDLK_TAB);
    require(ui.IsChatOpen() && tString(ui.msChatInput)==bounded && bounded.size()==512 &&
        luxnet::ValidChatText(ui.msChatInput) && base.mpMultiplayer->chatSends==sends,
        "completion at the byte limit retains the valid draft without truncating or sending it");
    replaceChatDraft(ui,tString(508,'X')+" :g");
    require(std::strlen(ui.msChatInput)==511 && !ui.mbChatCompletionOpen,"partial-capacity fixture starts one character short of completion");
    textEvent("rX");event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);draw(ui);
    event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
    require(ui.IsChatOpen() && tString(ui.msChatInput)==tString(508,'X')+" :gr" &&
        std::strlen(ui.msChatInput)==512 && base.mpMultiplayer->chatSends==sends,
        "same-frame partial-capacity typing reserves Enter after native editing forms a completion token");
    if(ui.mbChatCompletionOpen) chatKey(ui,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
    chatKey(ui,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
    require(!ui.IsChatOpen() && base.mpMultiplayer->chatSends==sends,"autocomplete regressions leave no open draft or accidental message");
}
static void checkDevILSaveByteCount() {
    const ILuint previousImage=static_cast<ILuint>(ilGetInteger(IL_CUR_IMAGE));
    ILuint image=0;ilGenImages(1,&image);ilBindImage(image);
    // DevIL RAW includes a 14-byte header: 121 two-channel pixels make
    // the complete file exactly 256 bytes, which an ILboolean truncates to 0.
    ILubyte pixels[121*2];
    for(unsigned x=0;x<121;++x) {
        pixels[x*2]=static_cast<ILubyte>(x*31);
        pixels[x*2+1]=static_cast<ILubyte>(255-x);
    }
    require(ilTexImage(121,1,1,2,IL_LUMINANCE_ALPHA,IL_UNSIGNED_BYTE,pixels)==IL_TRUE,
        "create deterministic luminance/alpha image for DevIL save byte-count regression");
    const tString path=outputDirectory+"/devil-save-count.raw";
    FILE* file=cPlatform::OpenFile(cString::UTF8ToWChar(path),_W("wb"));
    require(file!=NULL,"open isolated DevIL raw save artifact");
    const ILuint written=ilSaveF(IL_RAW,file);
    require(std::fflush(file)==0 && std::fseek(file,0,SEEK_END)==0,"flush deterministic DevIL raw save");
    const long bytes=std::ftell(file);
    const int closeResult=std::fclose(file);
    ilBindImage(previousImage);ilDeleteImages(1,&image);
    require(closeResult==0 && written==256 && bytes==256,
        "DevIL ilSaveF returns the full 256-byte count without ILboolean truncation and writes exactly 256 bytes");
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
    base.mpEngine->GetResources()->AddResourceDir(_W("shaders"),false);
    checkDevILSaveByteCount();
    char* desktopClipboard=readFixtureClipboard();require(desktopClipboard!=NULL,"capture desktop clipboard before chat regressions");
    savedDesktopClipboard=desktopClipboard;SDL_free(desktopClipboard);desktopClipboardSaved=true;
    std::atexit([](){restoreDesktopClipboard();});
    SDL_HideWindow(SDL_GL_GetCurrentWindow());
    cLuxInputHandler input; base.mpInputHandler=&input;
    cLuxMultiplayer session;
    cGuiSet* menuSet=base.mpEngine->GetGui()->CreateSet("TestMainMenu",NULL);
    base.mpEngine->GetGui()->SetFocus(menuSet);
    TestMapHandler map; TestPlayer player;
    TestMessageHandler messages;TestEffectHandler effects;TestDebugHandler debug;
    base.mpPlayer=&player;base.mpMessageHandler=&messages;base.mpEffectHandler=&effects;base.mpDebugHandler=&debug;
    TestMainMenu mainMenu; base.mpMainMenu=&mainMenu;
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
        char* previousClipboard=readFixtureClipboard();require(previousClipboard!=NULL,"save clipboard before lobby-code copy regression");
        ui.mlPendingAction=9; ui.Update(1.0f/60);
        char* clipboard=readFixtureClipboard("109775244398475112");
        require(clipboard && std::strcmp(clipboard,"109775244398475112")==0,"copy preserves full 64-bit lobby code");
        SDL_free(clipboard);
        require(setFixtureClipboard(previousClipboard ? previousClipboard : ""),"restore clipboard after lobby-code copy regression");
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
        ui.Toggle();session.active=true;session.host=true;session.ready=true;
        input.state=eLuxInputState_Game;
        base.mpEngine->GetUpdater()->SetContainer("Default");
        require(ui.mpChatFont && ui.mpChatFont!=ImGui::GetIO().Fonts->Fonts[0],
            "chat loads the executable-adjacent bundled TTF instead of the built-in fallback");
        require(ui.mpChatEmoji && ui.mpChatEmoji->IsReady() && ui.mpChatEmoji->GetGlyphCount()>=3000,
            "chat loads the bundled color emoji atlas and sequence mapping");
        checkEmojiMapping(ui.mpChatEmoji);
        checkEmojiCategories(ui.mpChatEmoji);
        checkEmojiCompletionModel(ui.mpChatEmoji);
        require(ui.mpChatEmoji->GetShortcodeCount()>=7500,"Discord alias mapping is packaged with the emoji atlas");
        for(const auto& shortcode:ui.mpChatEmoji->GetShortcodes())
            require(ui.mpChatEmoji->ExpandShortcodes(":"+shortcode.first+":")==shortcode.second,
                "every packaged Discord alias expands to its complete canonical Unicode sequence");
        require(ui.mpChatEmoji->ExpandShortcodes(":codex_missing_alias:heart: :incomplete")==":codex_missing_alias:heart: :incomplete",
            "unknown and unfinished aliases remain literal without reusing their closing colon");
        require(ui.mpChatEmoji->ExpandShortcodes("https://example.test :heart:")=="https://example.test \xE2\x9D\xA4\xEF\xB8\x8F",
            "a URL's ordinary colon does not consume a later emoji alias");
        require(ui.mpChatEmoji->ExpandShortcodes(":adult::skin-tone-1:")=="\xF0\x9F\xA7\x91\xF0\x9F\x8F\xBB",
            "compound Discord person and skin-tone aliases use their complete canonical mapping");
        require(ui.mpChatEmoji->ExpandShortcodes(":+1:")=="\xF0\x9F\x91\x8D" &&
            ui.mpChatEmoji->ExpandShortcodes(":pinata:")==ui.mpChatEmoji->ExpandShortcodes(":pi\xC3\xB1" "ata:"),
            "symbol aliases and accented/ASCII synonyms remain supported");
        const tString family="\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
            "\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";
        for(const tString& emoji:std::vector<tString>{"\xF0\x9F\x98\x80","\xE2\x9D\xA4\xEF\xB8\x8F",
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD","\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8",
            "\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD\xE2\x80\x8D\xF0\x9F\x92\xBB",family}) {
            int glyph=-1;
            require(ui.mpChatEmoji->Match("X"+emoji+"Y",1,glyph)==emoji.size() && glyph>=0,
                "Twemoji matches complete supplementary, VS16, skin, flag and family/occupation ZWJ sequences");
            const auto rich=luxchat::LayoutRichText("X"+emoji+"Y",ui.mpChatFont,18,80,ui.mpChatEmoji);
            require(rich.emojiCount==1,"rich layout draws a composed emoji as one image");
        }
        int textGlyph=-1;
        require(ui.mpChatEmoji->Match("\xE2\x9D\xA4\xEF\xB8\x8E",0,textGlyph)==0,
            "VS15 explicitly preserves text presentation");
        require(!ui.mpChatEmoji->ContainsEmoji("Plain multiplayer text"),"plain text never acquires emoji substitutions");
        const ImFontGlyph* combining=ui.mpChatFont->FindGlyphNoFallback(0x0301);
        require(combining && combining->Visible && std::fabs(combining->AdvanceX)<0.001f,
            "bundled font keeps the combining acute visible with zero advance");
        const auto cluster=luxchat::LayoutRichText("e\xCC\x81",ui.mpChatFont,18,30,ui.mpChatEmoji);
        require(cluster.glyphs.size()==2 && cluster.glyphs[1].width==0 && cluster.glyphs[1].y==cluster.glyphs[0].y,
            "combining acute remains on its base character's line without adding spacing");
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);textEvent("t");
        event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        const int gameUpdatesBeforeChat=input.gameUpdates;
        input.Update(1.0f/60);
        for(int i=0;i<3;++i) draw(ui);
        require(ui.IsChatOpen() && ui.IsChatCapturingInput() && !ui.msChatInput[0],
            "T opens focused chat and its SDL text event never inserts the opening character");
        require(input.gameUpdates==gameUpdatesBeforeChat,"chat capture blocks production gameplay input");
        const tString unicode="Hello caf\xC3\xA9 e\xCC\x81 \xCE\xA9 \xF0\x9F\x98\x80 "
            "\xE2\x9D\xA4\xEF\xB8\x8F \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD "
            "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 "
            "\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD\xE2\x80\x8D\xF0\x9F\x92\xBB";
        textEvent(unicode);for(int i=0;i<3;++i) draw(ui);
        require(tString(ui.msChatInput)==unicode,"real SDL input preserves Latin, Greek, supplementary emoji, variation selectors, skin tones, flags and ZWJ sequences");
        require(ui.mlChatEntryEmoji==5,"one editable draft field shows five composed color emoji");
        requireSingleEntry(ui);
        bool hasAtlasDraw=false;
        const ImTextureID atlas=static_cast<ImTextureID>(static_cast<uintptr_t>(ui.mpChatEmoji->GetTextureHandle()));
        ImDrawData* emojiDraw=ImGui::GetDrawData();
        for(int list=0;list<emojiDraw->CmdListsCount;++list) for(const ImDrawCmd& command:emojiDraw->CmdLists[list]->CmdBuffer)
            if(command.TextureId==atlas && command.ElemCount>0) hasAtlasDraw=true;
        require(hasAtlasDraw,"color emoji emit actual image draw commands referencing the uploaded atlas");
        moveMouse(ui,760,30);
        const unsigned restingEmojiElements=atlasDrawElements(ui);
        const unsigned restingGreyVertices=greyEmojiIconVertices(ui);
        require(restingGreyVertices>0,"resting emoji button has visible grey icon geometry");
        screenshot("chat-entry.png");
        moveMouse(ui,static_cast<int>(ui.mvChatEmojiButtonPos.x+ui.mvChatEmojiButtonSize.x*0.5f),
            static_cast<int>(ui.mvChatEmojiButtonPos.y+ui.mvChatEmojiButtonSize.y*0.5f));
        require(ui.mlChatEntryEmoji==5 && tString(ui.msChatInput)==unicode &&
            atlasDrawElements(ui)==restingEmojiElements && greyEmojiIconVertices(ui)==restingGreyVertices && session.chatSends==0,
            "hover keeps the emoji button grey without adding a color atlas icon or changing the draft");
        screenshot("chat-emoji-hover.png");
        moveMouse(ui,760,30);
        require(atlasDrawElements(ui)==restingEmojiElements,"moving away restores the emoji button's neutral appearance");
        event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);ui.Draw();
        require(session.chatSends==0,"chat rendering defers message send to the next update");
        ui.Update(1.0f/60);
        require(session.chatSends==1 && session.chat.back().text==unicode && !ui.IsChatOpen(),
            "Enter submits through coordinator and closes the editor");
        event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);
        for(int i=0;i<3;++i) draw(ui);
        require(ui.mlChatVisibleMessages==1 && ui.mfChatHistoryAlpha==1 && !ui.IsChatCapturingInput(),
            "recent passive history is readable without capturing gameplay");
        checkChatHistoryStyle(ui,session);
        session.chat.back().age=9;draw(ui);
        require(ui.mfChatHistoryAlpha>0 && ui.mfChatHistoryAlpha<1,"old chat fades through partial opacity");
        session.chat.back().age=11;draw(ui);
        require(ui.mlChatVisibleMessages==0 && ui.mfChatHistoryAlpha==0 && session.chat.size()==1,
            "expired chat disappears while retained session history remains available");
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        require(ui.IsChatOpen() && ui.mlChatVisibleMessages==1 && ui.mfChatHistoryAlpha==1,
            "typing reveals faded history again");
        tString oversized="Bounded ";for(int i=0;i<250;++i) oversized+="\xF0\x9F\x98\x80";
        textEvent(oversized);for(int i=0;i<3;++i) draw(ui);
        const tString bounded=ui.msChatInput;
        require(bounded.size()>400 && bounded.size()<=luxnet::MaxChatTextBytes && luxnet::ValidChatText(bounded),
            "overlong typing stops at the byte bound without splitting a supplementary character");
        luxnet::ChatMessage longHistory;longHistory.peer=1;longHistory.name="A trusted Steam friend";
        longHistory.nameColor=luxchat::ChooseNameColor({luxchat::DefaultNameColor});
        longHistory.text=bounded.substr(0,bounded.size()-4)+"TAIL";
        luxnet::AppendChatMessage(session.chat,longHistory);
        for(const cVector2l& size:{cVector2l(320,240),cVector2l(640,480),cVector2l(997,613),cVector2l(338,1000),cVector2l(1920,540),cVector2l(3840,2160)}) {
            resizeOverlay(ui,size.x,size.y);
            requireChatFont(ui);
            requireVisibleWindow("##MultiplayerChat",size.x,size.y);
            const cVector2f item=ui.mvChatTextInputPos,itemSize=ui.mvChatTextInputSize;
            require(itemSize.x>0 && itemSize.y>0 && item.x>=ui.mvChatEntryPos.x && item.y>=ui.mvChatEntryPos.y &&
                item.x+itemSize.x<=ui.mvChatEntryPos.x+ui.mvChatEntrySize.x+1 &&
                item.y+itemSize.y<=ui.mvChatEntryPos.y+ui.mvChatEntrySize.y+1 &&
                item.x+itemSize.x<=size.x+1 && item.y+itemSize.y<=size.y+1,
                "actual rendered input item fits the entry panel and viewport after resizing");
            require(ui.mlChatEntryEmoji>0,"resizing retains composed emoji inside the editable field");
            requireSingleEntry(ui);
            requireHistoryTail(ui);
            require(ui.IsChatOpen() && tString(ui.msChatInput)==bounded && ui.mlChatVisibleMessages>0,
                "resizing active entry preserves the draft and reveals retained history");
            const cVector2f position=ui.mvChatHistoryPos,extent=ui.mvChatHistorySize;
            require(position.x>=0 && position.y>=0 && position.x+extent.x<=size.x+1 &&
                position.y+extent.y<=ui.mvChatEntryPos.y+1,
                "wrapped chat history fits the resized viewport above the entry");
            const tString filename="chat-"+cString::ToString(size.x)+"x"+cString::ToString(size.y)+".png";
            screenshot(filename.c_str());
        }
        resizeOverlay(ui,640,480);
        const int historyCursor=ui.mlChatCursor;
        moveMouse(ui,static_cast<int>(ui.mvChatHistoryPos.x+ui.mvChatHistorySize.x*0.5f),
            static_cast<int>(ui.mvChatHistoryPos.y+ui.mvChatHistorySize.y*0.5f));
        wheelHistory(ui,3);
        require(ui.mfChatHistoryContentHeight>ui.mfChatHistoryViewportHeight && ui.mfChatHistoryScroll>0 &&
            ui.mfChatHistoryScroll<=ui.mfChatHistoryContentHeight-ui.mfChatHistoryViewportHeight &&
            ui.IsChatOpen() && tString(ui.msChatInput)==bounded && ui.mlChatCursor==historyCursor,
            "wheel over active chat history reveals older content without changing the draft caret or input ownership");
        wheelHistory(ui,1000);
        require(nearPixel(ui.mfChatHistoryScroll,ui.mfChatHistoryContentHeight-ui.mfChatHistoryViewportHeight),
            "history scrolling clamps at the oldest retained content");
        wheelHistory(ui,-1000);
        require(ui.mfChatHistoryScroll==0,"scrolling back down restores the newest message tail");
        requireHistoryTail(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        require(!ui.IsChatOpen() && ui.IsChatCapturingInput(),"Escape closes chat while capturing its event batch");
        input.Update(1.0f/60);
        require(input.gameUpdates==gameUpdatesBeforeChat,"Escape does not reach gameplay underneath chat");
        event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);draw(ui);
        require(session.chatSends==1,"Escape never sends the discarded bounded draft");
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);textEvent("Retry this message");for(int i=0;i<3;++i) draw(ui);
        session.chatSendAllowed=false;
        event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);ui.Draw();ui.Update(1.0f/60);
        require(ui.IsChatOpen() && tString(ui.msChatInput)=="Retry this message" && !ui.msChatError.empty() && session.chatSends==1,
            "a rejected send preserves the focused draft and reports a recoverable error");
        event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
        session.chatSendAllowed=true;
        event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);ui.Draw();ui.Update(1.0f/60);
        require(!ui.IsChatOpen() && session.chatSends==2 && session.chat.back().text=="Retry this message",
            "retry submits the preserved draft once when sending becomes available");
        event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        const tString aliases=":grinning: :heart: :thumbsup_tone3: :flag_us: :codex_missing_alias: :incomplete";
        textEvent(aliases);for(int i=0;i<3;++i) draw(ui);
        require(tString(ui.msChatInput)==aliases,"editing retains complete, unknown and incomplete alias text until sending");
        event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);ui.Draw();ui.Update(1.0f/60);
        const tString canonical="\xF0\x9F\x98\x80 \xE2\x9D\xA4\xEF\xB8\x8F "
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD \xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 :codex_missing_alias: :incomplete";
        require(!ui.IsChatOpen() && session.chatSends==3 && session.chat.back().text==canonical,
            "sending expands Discord aliases to canonical Unicode while preserving unknown and unfinished aliases");
        event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
        resizeOverlay(ui,800,600);
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);textEvent("hello");event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        if(!ui.IsChatOpen() || tString(ui.msChatInput)!="hello" || session.chatSends!=3)
            std::fprintf(stderr,"Fast opening actual=%s expected=hello open=%d sends=%d restore=%d focus=%d activeID=%u navID=%u cursor=%d selection=%d,%d queued=%d\n",
                ui.msChatInput,int(ui.IsChatOpen()),session.chatSends,int(ui.mbChatRestoreSelection),int(ui.mbChatFocusInput),
                ImGui::GetCurrentContext()->ActiveId,ImGui::GetCurrentContext()->NavId,ui.mlChatCursor,
                ui.mlChatSelectionStart,ui.mlChatSelectionEnd,ImGui::GetIO().InputQueueCharacters.Size);
        require(ui.IsChatOpen() && tString(ui.msChatInput)=="hello" && session.chatSends==3,
            "opening chat and typing before its first render preserves queued text in the newly focused field");
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        for(int i=0;i<3;++i) draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        const tString invisible=" \xE2\x80\x8B\xE2\x80\x8D\xE2\x81\xA0\xEF\xB8\x8F\xCD\x8F ";
        textEvent(invisible);for(int i=0;i<3;++i) draw(ui);
        require(tString(ui.msChatInput)==invisible,"format-only draft fixture enters through the real UTF-8 editor");
        event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);ui.Draw();ui.Update(1.0f/60);
        require(!ui.IsChatOpen() && session.chatSends==3 && ui.msChatError.empty(),
            "whitespace and invisible formatting alone cancel an empty draft without sending or leaving an error");
        event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        tString expanding;for(int i=0;i<170;++i) expanding+=":a:";
        require(expanding.size()<=luxnet::MaxChatTextBytes && ui.mpChatEmoji->ExpandShortcodes(expanding).size()>luxnet::MaxChatTextBytes,
            "shortcode fixture fits the editable bound but expands beyond the network text bound");
        textEvent(expanding);for(int i=0;i<3;++i) draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_RETURN,SDLK_RETURN);ui.Draw();ui.Update(1.0f/60);
        require(ui.IsChatOpen() && tString(ui.msChatInput)==expanding && ui.msChatError=="Message too long." && session.chatSends==3,
            "overlong expanded aliases preserve the focused raw draft and never submit a truncated or oversized message");
        event(SDL_KEYUP,SDL_SCANCODE_RETURN,SDLK_RETURN);for(int i=0;i<3;++i) draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        for(int i=0;i<3;++i) draw(ui);
        const tString occupation="\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD\xE2\x80\x8D\xF0\x9F\x92\xBB";
        const std::vector<std::pair<tString,bool>> compositions={{occupation,true},{":woman_technologist:",true},
            {":thumbsup:\xF0\x9F\x8F\xBD",true},{":woman:\xE2\x80\x8D\xF0\x9F\x92\xBB",true},
            {":woman:\xE2\x80\x8D:computer:",true},{":heart:\xEF\xB8\x8E",false}};
        for(const auto& composition:compositions) {
            const tString& token=composition.first;
            event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
            for(int i=0;i<3;++i) draw(ui);
            const tString raw="A"+token+"B";
            textEvent(raw);for(int i=0;i<3;++i) draw(ui);
            require(tString(ui.msChatInput)==raw && ui.mlChatEntryEmoji==(composition.second?1:0) &&
                ui.mChatEntryRich.displayText==ui.mpChatEmoji->ExpandShortcodes(raw),
                "mixed shortcode/Unicode modifiers, joiners and text selectors use canonical display composition while retaining raw draft bytes");
            requireSingleEntry(ui);
            const auto& glyphs=ui.mChatEntryRich.glyphs;
            if(composition.second) {
                int canonicalGlyph=-1;const tString canonicalToken=ui.mpChatEmoji->ExpandShortcodes(token);
                require(ui.mpChatEmoji->Match(canonicalToken,0,canonicalGlyph)==canonicalToken.size() &&
                    glyphs.size()==3 && glyphs[1].emoji==canonicalGlyph && glyphs[1].offset==1 && glyphs[1].length==token.size(),
                    "a mixed alias/raw emoji matches its canonical wire glyph and preserves the entire original byte span");
            }
            clickChatCaret(ui,1+token.size());
            textEvent("X");for(int i=0;i<3;++i) draw(ui);
            const tString atCaret="A"+token+"XB";
            if(tString(ui.msChatInput)!=atCaret)
                std::fprintf(stderr,"Rich caret click actual=%s expected=%s cursor=%d selection=%d,%d scroll=%.1f origin=%.1f,%.1f\n",
                    ui.msChatInput,atCaret.c_str(),ui.mlChatCursor,ui.mlChatSelectionStart,ui.mlChatSelectionEnd,
                    ui.mfChatEntryScroll,ui.mvChatEntryTextPos.x,ui.mvChatEntryTextPos.y);
            require(tString(ui.msChatInput)==atCaret && luxnet::ValidChatText(ui.msChatInput) && session.chatSends==3,
                "SDL clicking after a composed icon inserts at its raw byte boundary without splitting Unicode or sending");
            event(SDL_KEYDOWN,SDL_SCANCODE_LSHIFT,SDLK_LSHIFT,0,KMOD_SHIFT);draw(ui);
            event(SDL_KEYDOWN,SDL_SCANCODE_HOME,SDLK_HOME,0,KMOD_SHIFT);draw(ui);
            event(SDL_KEYUP,SDL_SCANCODE_HOME,SDLK_HOME,0,KMOD_SHIFT);draw(ui);
            event(SDL_KEYUP,SDL_SCANCODE_LSHIFT,SDLK_LSHIFT);for(int i=0;i<3;++i) draw(ui);
            require((std::min)(ui.mlChatSelectionStart,ui.mlChatSelectionEnd)==0 &&
                static_cast<size_t>((std::max)(ui.mlChatSelectionStart,ui.mlChatSelectionEnd))==2+token.size(),
                "Shift+Home selects the complete raw prefix through its composed emoji and inserted character");
            if(token==occupation) {
                ImFont* stableFont=ui.mpChatFont;
                const int cursor=ui.mlChatCursor,start=ui.mlChatSelectionStart,end=ui.mlChatSelectionEnd;
                for(const cVector2l& size:{cVector2l(997,613),cVector2l(338,1000),cVector2l(3840,2160),cVector2l(640,480)}) {
                    resizeOverlay(ui,size.x,size.y);requireChatFont(ui);requireSingleEntry(ui);
                    require(ui.mpChatFont==stableFont && ui.IsChatOpen() && tString(ui.msChatInput)==atCaret &&
                        ui.mlChatCursor==cursor && ui.mlChatSelectionStart==start && ui.mlChatSelectionEnd==end,
                        "font size rebuilds across odd, portrait and 4K windows retain the exact nonterminal caret and Unicode selection");
                }
                checkChatFontDensities(ui);
                require(tString(ui.msChatInput)==atCaret && ui.mlChatCursor==cursor &&
                    ui.mlChatSelectionStart==start && ui.mlChatSelectionEnd==end,
                    "fractional DPI atlas rebuilds preserve the live draft caret and selection");
            }
            textEvent("Z");for(int i=0;i<3;++i) draw(ui);
            require(tString(ui.msChatInput)=="ZB" && session.chatSends==3,
                "native selection replacement edits one rich field without leftover composed sequence bytes");
            shortcut(ui,SDL_SCANCODE_A,SDLK_a);
            char* originalClipboard=readFixtureClipboard();require(originalClipboard!=NULL,"read clipboard before reversible paste regression");
            const tString savedClipboard=originalClipboard;SDL_free(originalClipboard);
            require(setFixtureClipboard(raw.c_str()),"set Unicode/alias clipboard fixture");
            shortcut(ui,SDL_SCANCODE_V,SDLK_v);
            require(tString(ui.msChatInput)==raw,"native clipboard paste preserves composed Unicode and literal shortcode bytes");
            shortcut(ui,SDL_SCANCODE_A,SDLK_a);
            require(setFixtureClipboard("codex-rich-copy-sentinel"),"set a distinct sentinel before production native Copy");
            shortcut(ui,SDL_SCANCODE_C,SDLK_c);
            char* copied=readFixtureClipboard(raw.c_str());require(copied!=NULL,"read copied rich draft");
            const tString copiedDraft=copied;SDL_free(copied);
            require(setFixtureClipboard(savedClipboard.c_str()),"restore original clipboard after rich input regression");
            require(copiedDraft==raw && session.chatSends==3,"copying a selected rich draft returns the exact editable raw text without sending");
            shortcut(ui,SDL_SCANCODE_A,SDLK_a);textEvent("Punctuation: ");for(int i=0;i<3;++i) draw(ui);
            event(SDL_KEYDOWN,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE);textEvent("`~");
            event(SDL_KEYUP,SDL_SCANCODE_GRAVE,SDLK_BACKQUOTE);for(int i=0;i<3;++i) draw(ui);
            require(ui.IsChatOpen() && !ui.IsVisible() && tString(ui.msChatInput)=="Punctuation: `~",
                "grave/tilde keyboard input remains ordinary chat text and preserves the active draft");
            event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
            for(int i=0;i<3;++i) draw(ui);
        }
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        const tString simultaneous="A:smile:B";
        textEvent(simultaneous);for(int i=0;i<3;++i) draw(ui);
        SDL_Delay(static_cast<Uint32>(ImGui::GetIO().MouseDoubleClickTime*1000.0f)+20);draw(ui);
        const int batchX=static_cast<int>(ui.mvChatEntryTextPos.x+luxchat::RichCaretX(simultaneous,ui.mChatEntryRich,8)+1.0f);
        const int batchY=static_cast<int>(ui.mvChatTextInputPos.y+ui.mvChatTextInputSize.y*0.5f);
        const bool previousTrickling=ImGui::GetIO().ConfigInputTrickleEventQueue;
        ImGui::GetIO().ConfigInputTrickleEventQueue=false;
        queueMouse(true,batchX,batchY);textEvent("X");draw(ui);
        const bool sameFrameSingleClick=ImGui::GetIO().MouseClickedCount[0]==1;
        const bool sameFrameInserted=tString(ui.msChatInput)=="A:smile:XB";
        ImGui::GetIO().ConfigInputTrickleEventQueue=previousTrickling;
        queueMouse(false,batchX,batchY);base.mpEngine->GetInput()->Update(1.0f/60);for(int i=0;i<3;++i) draw(ui);
        if(tString(ui.msChatInput)!="A:smile:XB")
            std::fprintf(stderr,"Same-frame click/text actual=%s expected=A:smile:XB cursor=%d selection=%d,%d\n",
                ui.msChatInput,ui.mlChatCursor,ui.mlChatSelectionStart,ui.mlChatSelectionEnd);
        require(sameFrameSingleClick,"same-frame rich hit fixture is a single click rather than native double-click word selection");
        require(sameFrameInserted && tString(ui.msChatInput)=="A:smile:XB" && session.chatSends==3,
            "mouse hit mapping precedes text mutation when SDL click and typed text arrive in the same frame");
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        for(int i=0;i<3;++i) draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        const tString caretDraft="A\xF0\x9F\x98\x80" "B";
        textEvent(caretDraft);for(int i=0;i<3;++i) draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_LEFT,SDLK_LEFT);draw(ui);
        event(SDL_KEYUP,SDL_SCANCODE_LEFT,SDLK_LEFT);for(int i=0;i<3;++i) draw(ui);
        openPicker(ui);checkPickerCategories(ui);checkPickerTones(ui);
        require(tString(ui.msChatEmojiSearch)=="grinning" && !ui.mvChatPickerMatches.empty() &&
            !ui.msChatPickerFirstUnicode.empty(),"picker search finds Discord aliases through real SDL text input");
        const tString chosen=ui.msChatPickerFirstUnicode;
        require(chosen=="\xF0\x9F\x98\x80","grinning search exposes its canonical composed emoji");
        const int pickerCursor=ui.mlChatCursor,pickerSelectionStart=ui.mlChatSelectionStart,pickerSelectionEnd=ui.mlChatSelectionEnd;
        for(const cVector2l& size:{cVector2l(640,480),cVector2l(997,613),cVector2l(338,1000),cVector2l(1920,540),cVector2l(3840,2160)}) {
            resizeOverlay(ui,size.x,size.y);
            requireChatFont(ui);
            const cVector2f position=ui.mvChatPickerPos,extent=ui.mvChatPickerSize;
            require(ui.mbChatEmojiPickerOpen && ui.IsChatOpen() && tString(ui.msChatInput)==caretDraft &&
                tString(ui.msChatEmojiSearch)=="grinning" && ui.mlChatPickerCategory==7 &&
                ui.mlChatPickerTone==3 && ui.mbChatPickerSearchResults && ui.mlChatCursor==pickerCursor &&
                ui.mlChatSelectionStart==pickerSelectionStart && ui.mlChatSelectionEnd==pickerSelectionEnd &&
                position.x>=0 && position.y>=0 && extent.x>0 && extent.y>0 &&
                position.x+extent.x<=size.x+1 && position.y+extent.y<=size.y+1,
                "resizing an open searchable picker preserves the draft/query and keeps all controls in the viewport");
            requireCategoryControls(ui);
            const tString filename="chat-picker-"+cString::ToString(size.x)+"x"+cString::ToString(size.y)+".png";
            screenshot(filename.c_str());
            if(size==cVector2l(640,480)) screenshot("chat-picker.png");
        }
        checkChatFontDensities(ui);
        require(ui.mbChatEmojiPickerOpen && tString(ui.msChatEmojiSearch)=="grinning" &&
            ui.mlChatPickerCategory==7 && ui.mlChatPickerTone==3 && ui.mbChatPickerSearchResults &&
            tString(ui.msChatInput)==caretDraft && ui.mlChatCursor==pickerCursor &&
            ui.mlChatSelectionStart==pickerSelectionStart && ui.mlChatSelectionEnd==pickerSelectionEnd,
            "DPI font rebuilds preserve an open picker, search query and saved draft insertion point");
        click(ui,ui.mvChatPickerFirstEmojiPos,ui.mvChatPickerFirstEmojiSize);
        const tString inserted="A\xF0\x9F\x98\x80"+chosen+"B";
        if(tString(ui.msChatInput)!=inserted || ui.mbChatEmojiPickerOpen || !ui.IsChatOpen() || session.chatSends!=3) {
            const ImGuiInputTextState& state=ImGui::GetCurrentContext()->InputTextState;
            std::fprintf(stderr,"Picker insert actual[%llu]=%s expected[%llu]=%s; chat=%d sends=%d picker=%d restore=%d pending=%s stored cursor=%d selection=%d,%d activeID=%u activeCursor=%d first=%s rect=%.1f,%.1f %.1fx%.1f\n",
                static_cast<unsigned long long>(std::strlen(ui.msChatInput)),ui.msChatInput,
                static_cast<unsigned long long>(inserted.size()),inserted.c_str(),int(ui.IsChatOpen()),session.chatSends,int(ui.mbChatEmojiPickerOpen),int(ui.mbChatRestoreSelection),
                ui.msChatPendingInsert.c_str(),ui.mlChatCursor,ui.mlChatSelectionStart,ui.mlChatSelectionEnd,
                state.ID,state.GetCursorPos(),ui.msChatPickerFirstUnicode.c_str(),ui.mvChatPickerFirstEmojiPos.x,
                ui.mvChatPickerFirstEmojiPos.y,ui.mvChatPickerFirstEmojiSize.x,ui.mvChatPickerFirstEmojiSize.y);
        }
        require(!ui.mbChatEmojiPickerOpen && ui.IsChatOpen() && tString(ui.msChatInput)==inserted &&
            luxnet::ValidChatText(ui.msChatInput) && session.chatSends==3,
            "picker selection inserts at the Unicode caret between prefix/suffix without sending or splitting existing text");
        shortcut(ui,SDL_SCANCODE_Z,SDLK_z);
        require(tString(ui.msChatInput)==caretDraft && ui.IsChatOpen() && session.chatSends==3,
            "native Undo removes the complete picker insertion while retaining the surrounding Unicode draft");
        shortcut(ui,SDL_SCANCODE_Y,SDLK_y);
        require(tString(ui.msChatInput)==inserted && ui.IsChatOpen() && session.chatSends==3,
            "native Redo restores the complete picker insertion without sending or changing surrounding text");
        openPicker(ui);
        char* originalFastClipboard=readFixtureClipboard();require(originalFastClipboard!=NULL,"save clipboard before activation-frame paste");
        const tString fastClipboard=originalFastClipboard;SDL_free(originalFastClipboard);
        require(setFixtureClipboard("X"),"set queued paste fixture");
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        event(SDL_KEYDOWN,SDL_SCANCODE_LCTRL,SDLK_LCTRL,0,KMOD_CTRL);
        event(SDL_KEYDOWN,SDL_SCANCODE_V,SDLK_v,0,KMOD_CTRL);draw(ui);
        event(SDL_KEYUP,SDL_SCANCODE_V,SDLK_v,0,KMOD_CTRL);event(SDL_KEYUP,SDL_SCANCODE_LCTRL,SDLK_LCTRL);
        for(int i=0;i<3;++i) draw(ui);
        require(setFixtureClipboard(fastClipboard.c_str()),"restore clipboard after activation-frame paste");
        const tString pastedAtCaret="A\xF0\x9F\x98\x80"+chosen+"XB";
        if(tString(ui.msChatInput)!=pastedAtCaret)
            std::fprintf(stderr,"Picker Escape/refocus/paste actual=%s expected=%s cursor=%d selection=%d,%d\n",
                ui.msChatInput,pastedAtCaret.c_str(),ui.mlChatCursor,ui.mlChatSelectionStart,ui.mlChatSelectionEnd);
        require(!ui.mbChatEmojiPickerOpen && ui.IsChatOpen() && tString(ui.msChatInput)==pastedAtCaret && session.chatSends==3,
            "Escape and queued native paste restore the raw draft caret before the field's first refocus render");
        shortcut(ui,SDL_SCANCODE_Z,SDLK_z);
        require(tString(ui.msChatInput)==inserted,"undoing queued refocus paste retains the complete picker insertion");
        openPicker(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);draw(ui);
        require(!ui.mbChatEmojiPickerOpen && ui.IsChatOpen() && tString(ui.msChatInput)==inserted,
            "first Escape closes the emoji picker and retains the chat draft");
        event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);for(int i=0;i<3;++i) draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);draw(ui);
        require(!ui.IsChatOpen() && session.chatSends==3,"second Escape closes chat without sending picker selections");
        event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);for(int i=0;i<3;++i) draw(ui);
        resizeOverlay(ui,800,600);
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);textEvent(tString(512,'X'));for(int i=0;i<3;++i) draw(ui);
        openPicker(ui);const tString fullDraft=ui.msChatInput;
        click(ui,ui.mvChatPickerFirstEmojiPos,ui.mvChatPickerFirstEmojiSize);
        require(ui.IsChatOpen() && tString(ui.msChatInput)==fullDraft && fullDraft.size()==512 &&
            luxnet::ValidChatText(ui.msChatInput) && session.chatSends==3,
            "picker insertion at the full byte bound preserves the valid draft without overflow or sending");
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        for(int i=0;i<3;++i) draw(ui);
        if(ui.IsChatOpen()) {
            event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
            for(int i=0;i<3;++i) draw(ui);
        }
        checkChatAutocomplete(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);openPicker(ui);
        SDL_Event lost={};lost.type=SDL_WINDOWEVENT;lost.window.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
        lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;require(SDL_PushEvent(&lost)==1,"inject picker focus loss");
        base.mpEngine->GetInput()->Update(1.0f/60);draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen,"focus loss tears down both picker and editor");
        lost.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;require(SDL_PushEvent(&lost)==1,"return picker test focus");
        base.mpEngine->GetInput()->Update(1.0f/60);draw(ui);
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);openPicker(ui);
        iWidget* preservedNativeFocus=menuSet->GetFocusedWidget();input.state=eLuxInputState_Inventory;draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen && menuSet->GetFocusedWidget()==preservedNativeFocus,
            "native inventory transition removes picker/editor without disturbing HPL GUI focus");
        input.state=eLuxInputState_Game;
        checkPassiveChatMenus(ui,menuSet);
        const auto blocked=[&](const char* description) {
            event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
            for(int i=0;i<2;++i) draw(ui);
            require(!ui.IsChatOpen(),description);
        };
        session.active=false;blocked("chat cannot open while disconnected");session.active=true;
        session.ready=false;blocked("chat cannot open before session readiness");session.ready=true;
        session.loadPhase=eLuxMultiplayerLoadPhase_Preparing;blocked("chat cannot open during map preparation");
        session.loadPhase=eLuxMultiplayerLoadPhase_None;
        session.loading=true;blocked("chat cannot open during synchronous host reset/load with ready and load-phase-none");session.loading=false;
        session.mapPreparing=true;blocked("chat cannot open during host preparation with ready and load-phase-none");session.mapPreparing=false;
        session.pendingHostMap="02_entrance_hall.map";
        blocked("chat cannot open while a host map change is pending with ready and load-phase-none");session.pendingHostMap.clear();
        player.active=false;blocked("chat cannot open for an inactive player");player.active=true;
        player.dead=true;blocked("chat cannot open for a dead player");player.dead=false;
        messages.paused=true;blocked("chat cannot displace native pause messages");messages.paused=false;
        effects.paused=true;blocked("chat cannot displace paused player effects");effects.paused=false;
        debug.active=true;blocked("chat cannot displace the debug window");debug.active=false;
        base.mpEngine->SetPaused(true);blocked("chat cannot open while the engine is paused");base.mpEngine->SetPaused(false);
        for(const eLuxInputState state:{eLuxInputState_MainMenu,eLuxInputState_Inventory,eLuxInputState_Journal,eLuxInputState_LoadScreen}) {
            input.state=state;blocked("chat cannot steal native menu, inventory, journal or loading input");
        }
        input.state=eLuxInputState_Game;
        session.steamOverlayActive=true;blocked("Steam overlay owns T and cannot open chat");session.steamOverlayActive=false;
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);require(ui.IsChatOpen(),"chat opens after blocked states clear");
        openPicker(ui);
        session.steamOverlayActive=true;draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen,"opening Steam overlay cancels active picker/editor");session.steamOverlayActive=false;
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        openPicker(ui);
        openToneOptions(ui);
        session.loadPhase=eLuxMultiplayerLoadPhase_Loading;draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen && !ui.mbChatPickerToneOpen,
            "a map-loading transition closes active picker/editor and tone dropdown");session.loadPhase=eLuxMultiplayerLoadPhase_None;
        for(int stage=0;stage<3;++stage) {
            event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
            for(int i=0;i<3;++i) draw(ui);openPicker(ui);
            if(stage==0) session.loading=true;
            else if(stage==1) session.mapPreparing=true;
            else session.pendingHostMap="02_entrance_hall.map";
            require(session.IsReady() && session.GetLoadPhase()==eLuxMultiplayerLoadPhase_None,
                "host lifecycle fixture exercises synchronous map state beyond the visible load phase");
            ui.Update(1.0f/60);
            require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen,
                "host reset/load, preparation and pending map change each close active editor/picker before rendering");
            session.loading=false;session.mapPreparing=false;session.pendingHostMap.clear();
            for(int i=0;i<3;++i) draw(ui);
        }
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        openPicker(ui);
        openToneOptions(ui);
        player.dead=true;draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen && !ui.mbChatPickerToneOpen,
            "player death closes active picker/editor and tone dropdown");player.dead=false;
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        openPicker(ui);
        openToneOptions(ui);
        SDL_Event closeEvent={};closeEvent.type=SDL_WINDOWEVENT;
        closeEvent.window.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());closeEvent.window.event=SDL_WINDOWEVENT_CLOSE;
        require(SDL_PushEvent(&closeEvent)==1,"inject actual SDL window-close event");
        base.mpEngine->GetInput()->Update(1.0f/60);draw(ui);
        bool closeForwarded=false;
        auto* lowInput=static_cast<cLowLevelInputSDL*>(base.mpEngine->GetInput()->GetLowLevel());
        for(const auto& forwarded:lowInput->mlstEvents)
            if(forwarded.type==SDL_WINDOWEVENT && forwarded.window.event==SDL_WINDOWEVENT_CLOSE) closeForwarded=true;
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen && !ui.mbChatPickerToneOpen && closeForwarded && !base.mpEngine->GetGameIsDone(),
            "window close removes picker/editor and tone dropdown while forwarding the native event to the engine");
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);openPicker(ui);
        SDL_Event quitEvent={};quitEvent.type=SDL_QUIT;require(SDL_PushEvent(&quitEvent)==1,"inject SDL application quit event");
        base.mpEngine->GetInput()->Update(1.0f/60);draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen && lowInput->isQuitMessagePosted() && !base.mpEngine->GetGameIsDone(),
            "quit closes picker/editor and still posts the engine's application-quit request");
        lowInput->resetQuitMessagePosted(); // The production confirmation path is covered by live ChatOnly.
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        session.active=false;draw(ui);
        require(!ui.IsChatOpen(),"disconnect closes active entry");
    }
    base.mpEngine->GetGraphics()->GetLowLevel()->SwapBuffers();
    event(SDL_KEYDOWN,SDL_SCANCODE_W,SDLK_w);
    event(SDL_KEYUP,SDL_SCANCODE_W,SDLK_w);
    base.mpEngine->GetGui()->DestroySet(menuSet);
    require(restoreDesktopClipboard(),"restore original desktop clipboard before SDL teardown");
    DestroyHPLEngine(base.mpEngine);
    std::printf("PASS: real ImGui rendering; tilde/escape; global pre-swap/event hooks; key releases; Steam unavailable/hosting/searching/join/invitation; Steam overlay input capture; campaign defaults; current-map background guard; deferred map browser and start-position dropdown; stale/missing/invalid map handling; cache deletion while connected/disconnected; direct IP; chat SDL text/send/cancel, Unicode byte bounds, fade, category/source coverage, global alias/Unicode search, six skin-tone choices and insertion, autocomplete keyboard/mouse acceptance, queued typing/paste and atomic undo, picker/dropdown/completion resize, native/Steam/loading/death/window-close availability; teardown.\n");
    return 0;
}
int hplMain(const tString&) { return main(0,NULL); }
