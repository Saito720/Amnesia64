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
    int globalUpdates = 0, gameUpdates = 0, menuUpdates = 0;
    int resets = 0;
    eLuxInputState GetState() { return state; }
    void ResetSmoothMousePos() { ++resets; }
    void Update(float);
    void OnQuit();
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
struct TestMainMenu {
    bool RequestQuit() { return true; }
};
struct cLuxBase {
    cEngine* mpEngine;
    cLuxInputHandler* mpInputHandler;
    TestMapHandler* mpMapHandler;
    cLuxMultiplayer* mpMultiplayer;
    TestMainMenu* mpMainMenu;
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
static bool restoreDesktopClipboard() {
    if(!desktopClipboardSaved) return true;
    if(!SDL_WasInit(SDL_INIT_VIDEO)) return false;
    if(SDL_SetClipboardText(savedDesktopClipboard.c_str())!=0) return false;
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
    checkDevILSaveByteCount();
    char* desktopClipboard=SDL_GetClipboardText();require(desktopClipboard!=NULL,"capture desktop clipboard before chat regressions");
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
        ui.Toggle();session.active=true;session.host=true;session.ready=true;
        input.state=eLuxInputState_Game;
        base.mpEngine->GetUpdater()->SetContainer("Default");
        require(ui.mpChatFont && ui.mpChatFont!=ImGui::GetIO().Fonts->Fonts[0],
            "chat loads the executable-adjacent bundled TTF instead of the built-in fallback");
        require(ui.mpChatEmoji && ui.mpChatEmoji->IsReady() && ui.mpChatEmoji->GetGlyphCount()>=3000,
            "chat loads the bundled color emoji atlas and sequence mapping");
        checkEmojiMapping(ui.mpChatEmoji);
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
            char* originalClipboard=SDL_GetClipboardText();require(originalClipboard!=NULL,"read clipboard before reversible paste regression");
            const tString savedClipboard=originalClipboard;SDL_free(originalClipboard);
            require(SDL_SetClipboardText(raw.c_str())==0,"set Unicode/alias clipboard fixture");
            shortcut(ui,SDL_SCANCODE_V,SDLK_v);
            require(tString(ui.msChatInput)==raw,"native clipboard paste preserves composed Unicode and literal shortcode bytes");
            shortcut(ui,SDL_SCANCODE_A,SDLK_a);shortcut(ui,SDL_SCANCODE_C,SDLK_c);
            char* copied=SDL_GetClipboardText();require(copied!=NULL,"read copied rich draft");
            const tString copiedDraft=copied;SDL_free(copied);
            require(SDL_SetClipboardText(savedClipboard.c_str())==0,"restore original clipboard after rich input regression");
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
        openPicker(ui);textEvent("grinning");for(int i=0;i<3;++i) draw(ui);
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
                tString(ui.msChatEmojiSearch)=="grinning" && ui.mlChatCursor==pickerCursor &&
                ui.mlChatSelectionStart==pickerSelectionStart && ui.mlChatSelectionEnd==pickerSelectionEnd &&
                position.x>=0 && position.y>=0 && extent.x>0 && extent.y>0 &&
                position.x+extent.x<=size.x+1 && position.y+extent.y<=size.y+1,
                "resizing an open searchable picker preserves the draft/query and keeps all controls in the viewport");
            const tString filename="chat-picker-"+cString::ToString(size.x)+"x"+cString::ToString(size.y)+".png";
            screenshot(filename.c_str());
            if(size==cVector2l(640,480)) screenshot("chat-picker.png");
        }
        checkChatFontDensities(ui);
        require(ui.mbChatEmojiPickerOpen && tString(ui.msChatEmojiSearch)=="grinning" &&
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
        char* originalFastClipboard=SDL_GetClipboardText();require(originalFastClipboard!=NULL,"save clipboard before activation-frame paste");
        const tString fastClipboard=originalFastClipboard;SDL_free(originalFastClipboard);
        require(SDL_SetClipboardText("X")==0,"set queued paste fixture");
        event(SDL_KEYDOWN,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);event(SDL_KEYUP,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE);
        event(SDL_KEYDOWN,SDL_SCANCODE_LCTRL,SDLK_LCTRL,0,KMOD_CTRL);
        event(SDL_KEYDOWN,SDL_SCANCODE_V,SDLK_v,0,KMOD_CTRL);draw(ui);
        event(SDL_KEYUP,SDL_SCANCODE_V,SDLK_v,0,KMOD_CTRL);event(SDL_KEYUP,SDL_SCANCODE_LCTRL,SDLK_LCTRL);
        for(int i=0;i<3;++i) draw(ui);
        require(SDL_SetClipboardText(fastClipboard.c_str())==0,"restore clipboard after activation-frame paste");
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
        session.loadPhase=eLuxMultiplayerLoadPhase_Loading;draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen,"a map-loading transition closes active picker/editor");session.loadPhase=eLuxMultiplayerLoadPhase_None;
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
        player.dead=true;draw(ui);
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen,"player death closes active picker/editor");player.dead=false;
        event(SDL_KEYDOWN,SDL_SCANCODE_T,SDLK_t);event(SDL_KEYUP,SDL_SCANCODE_T,SDLK_t);
        for(int i=0;i<3;++i) draw(ui);
        openPicker(ui);
        SDL_Event closeEvent={};closeEvent.type=SDL_WINDOWEVENT;
        closeEvent.window.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());closeEvent.window.event=SDL_WINDOWEVENT_CLOSE;
        require(SDL_PushEvent(&closeEvent)==1,"inject actual SDL window-close event");
        base.mpEngine->GetInput()->Update(1.0f/60);draw(ui);
        bool closeForwarded=false;
        auto* lowInput=static_cast<cLowLevelInputSDL*>(base.mpEngine->GetInput()->GetLowLevel());
        for(const auto& forwarded:lowInput->mlstEvents)
            if(forwarded.type==SDL_WINDOWEVENT && forwarded.window.event==SDL_WINDOWEVENT_CLOSE) closeForwarded=true;
        require(!ui.IsChatOpen() && !ui.mbChatEmojiPickerOpen && closeForwarded && !base.mpEngine->GetGameIsDone(),
            "window close removes picker/editor while forwarding the native event to the engine");
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
    std::printf("PASS: real ImGui rendering; tilde/escape; global pre-swap/event hooks; key releases; Steam unavailable/hosting/searching/join/invitation; Steam overlay input capture; campaign defaults; current-map background guard; deferred map browser and start-position dropdown; stale/missing/invalid map handling; cache deletion while connected/disconnected; direct IP; chat SDL text/send/cancel, Unicode byte bounds, fade, resize and native/Steam/loading availability; teardown.\n");
    return 0;
}
int hplMain(const tString&) { return main(0,NULL); }
