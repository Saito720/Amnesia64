/* Multiplayer session controls. Distributed under the GPLv3 or later. */
#include "LuxMultiplayerUI.h"
#include "LuxMultiplayer.h"
#include "LuxBase.h"
#include "LuxInputHandler.h"
#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerChatLayout.h"
#include "LuxMultiplayerChatEmoji.h"
#include "LuxPlayer.h"
#include "LuxMessageHandler.h"
#include "LuxEffectHandler.h"
#include "LuxDebugHandler.h"

#if USE_SDL2
#include "impl/LowLevelGraphicsSDL.h"
#include "impl/LowLevelInputSDL.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <set>

cLuxMultiplayerUI::cLuxMultiplayerUI(cLuxMultiplayer* apMultiplayer)
    : mpMultiplayer(apMultiplayer), mpContext(NULL), mpWindow(NULL),
      mbVisible(false), mbCampaign(false), mbFocusWindow(false),
      mbRestoreRelativeMouse(false), mbRestoreWindowGrab(false),
      mlRestoreCursor(0), mlPreviousInputState(0), mlPendingAction(0),
      mlPort(27015), mlMaxPlayers(4), mbAllowClientMapChanges(false),
      mbAllPlayersTriggerScripts(true), mbPlayerCollision(false), mbUseSteam(true), mbPublicLobby(false),
      mbSearchedSteamLobbies(false), mlSelectedSteamLobby(0), mbRestoreGuiMouse(false)
{
    msMap[0] = msStartPos[0] = msLobbyCode[0] = msMapBrowserPath[0] = '\0';
    std::strcpy(msAddress, "127.0.0.1:27015");
#if USE_SDL2
    static_cast<cLowLevelInputSDL*>(gpBase->mpEngine->GetInput()->GetLowLevel())
        ->SetEventCallback(EventCallback, this);
    static_cast<cLowLevelGraphicsSDL*>(gpBase->mpEngine->GetGraphics()->GetLowLevel())
        ->SetOverlayCallback(DrawCallback, this);
    Initialize();
#endif
}

cLuxMultiplayerUI::~cLuxMultiplayerUI()
{
#if USE_SDL2
    CloseChat();
    SetVisible(false);
    if(mpChatEmoji) {hplDelete(mpChatEmoji);mpChatEmoji=NULL;}
    static_cast<cLowLevelInputSDL*>(gpBase->mpEngine->GetInput()->GetLowLevel())
        ->SetEventCallback(NULL, NULL);
    static_cast<cLowLevelGraphicsSDL*>(gpBase->mpEngine->GetGraphics()->GetLowLevel())
        ->SetOverlayCallback(NULL, NULL);
    if(mpContext)
    {
        ImGui::SetCurrentContext(mpContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(mpContext);
    }
#endif
}

bool cLuxMultiplayerUI::Initialize()
{
#if USE_SDL2
    if(mpContext) return true;
    mpWindow = SDL_GL_GetCurrentWindow();
    if(!mpWindow || !SDL_GL_GetCurrentContext()) return false;
    IMGUI_CHECKVERSION();
    mpContext = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    // The overlay restores cursor state itself; hidden windows must not change it.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.0f;
    // Ship our chat typeface beside the executable. Keep the regular session
    // controls on ImGui's default face, and gracefully support older packages.
    io.Fonts->AddFontDefault();
    mpChatFont=NULL;
    static const ImWchar chatGlyphRanges[]={0x20,0xFFFD,0};
    char* basePath=SDL_GetBasePath();
    const tString baseDirectory=basePath?tString(basePath):tString("");
    const tString fontPath=baseDirectory+"fonts/NotoSans-Regular.ttf";
    if(basePath) SDL_free(basePath);
    ImFontConfig chatFontConfig;
    chatFontConfig.OversampleH=2;
    chatFontConfig.OversampleV=1;
    if(cPlatform::FileExists(cString::UTF8ToWChar(fontPath)))
        mpChatFont=io.Fonts->AddFontFromFileTTF(fontPath.c_str(),18.0f,&chatFontConfig,chatGlyphRanges);
    // Keep the fallback separate too: resizing chat must not resize the
    // existing multiplayer controls' default typeface.
    if(!mpChatFont) {chatFontConfig.SizePixels=18;mpChatFont=io.Fonts->AddFontDefault(&chatFontConfig);}
    if(!mpChatEmoji) mpChatEmoji=hplNew(cLuxMultiplayerChatEmoji,());
    if(!mpChatEmoji->IsReady()) mpChatEmoji->Load(baseDirectory+"fonts/twemoji/",
        gpBase->mpEngine->GetResources(),gpBase->mpEngine->GetGraphics()->GetLowLevel());
    if(!ImGui_ImplSDL2_InitForOpenGL(mpWindow, SDL_GL_GetCurrentContext()))
    {
        ImGui::DestroyContext(mpContext);
        mpContext = NULL;
        return false;
    }
    if(!ImGui_ImplOpenGL3_Init("#version 120"))
    {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(mpContext);
        mpContext = NULL;
        return false;
    }
    return true;
#else
    return false;
#endif
}

void cLuxMultiplayerUI::EnsureChatFont(float requestedSize,float framebufferDensity)
{
#if USE_SDL2
    ImGuiIO& io=ImGui::GetIO();
    if(!mpChatFont || io.Fonts->Locked) return;
    mfChatRequestedFontSize=requestedSize;
    const float size=std::round((std::max)(15.0f,(std::min)(23.0f,requestedSize)));
    const float density=std::isfinite(framebufferDensity) && framebufferDensity>0?framebufferDensity:1.0f;
    if(size==mfChatFontSize && std::fabs(density-mfChatFontDensity)<0.001f) return;
    // Rasterize at the actual rendered size instead of stretching an 18 px
    // bitmap. Density increases the texture resolution without changing any
    // logical input, wrapping or mouse coordinates. Retain font objects and
    // editor state across rebuilds, which happen only between rendered frames.
    for(ImFontConfig& source:io.Fonts->Sources) {
        source.RasterizerDensity=density;
        if(source.DstFont==mpChatFont) source.SizePixels=size;
    }
    if(!io.Fonts->Build()) return;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    mfChatFontSize=size;mfChatFontDensity=density;mfChatRasterSize=size*density;
    ++mlChatFontAtlasGeneration;
#endif
}

void cLuxMultiplayerUI::SetVisible(bool abVisible)
{
    if(abVisible == mbVisible) return;
#if USE_SDL2
    if(abVisible)
    {
        if(!Initialize()) return;
        CloseChat();
        mbRestoreRelativeMouse = gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetRelativeMouse();
        mbRestoreWindowGrab = gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetWindowGrab();
        mlRestoreCursor = SDL_ShowCursor(SDL_QUERY);
        mlPreviousInputState = gpBase->mpInputHandler->GetState();
        mbFocusWindow = true;
        CaptureGuiMouse();
    }
    else if(mpWindow)
    {
        if(mbMapBrowserOpen) mbCloseMapBrowser = true;
        RestoreGuiMouse();
        // Starting or joining may switch from a menu to the game while this is open.
        const bool bSameState = mlPreviousInputState == gpBase->mpInputHandler->GetState();
        const bool bGame = gpBase->mpInputHandler->GetState() == eLuxInputState_Game;
        gpBase->mpEngine->GetInput()->GetLowLevel()->RelativeMouse(bSameState ? mbRestoreRelativeMouse : bGame);
        gpBase->mpEngine->GetInput()->GetLowLevel()->LockInput(bSameState ? mbRestoreWindowGrab : bGame);
        SDL_ShowCursor(bSameState ? mlRestoreCursor : (bGame ? SDL_DISABLE : SDL_ENABLE));
        gpBase->mpEngine->GetInput()->ResetActionsToCurrentState();
        gpBase->mpInputHandler->ResetSmoothMousePos();
    }
    mbVisible = abVisible;
#endif
}

void cLuxMultiplayerUI::RestoreGuiMouse()
{
    if(msCapturedGuiSet.empty()) return;
    cGuiSet* pSet = gpBase->mpEngine->GetGui()->GetSetFromName(msCapturedGuiSet);
    if(pSet) pSet->SetDrawMouse(mbRestoreGuiMouse);
    msCapturedGuiSet.clear();
}

void cLuxMultiplayerUI::CaptureGuiMouse()
{
    cGuiSet* pSet = gpBase->mpEngine->GetGui()->GetFocusedSet();
    if(pSet && pSet->GetName() == msCapturedGuiSet) return;
    RestoreGuiMouse();
    if(pSet)
    {
        msCapturedGuiSet = pSet->GetName();
        mbRestoreGuiMouse = pSet->GetDrawMouse();
        pSet->SetDrawMouse(false);
    }
}

void cLuxMultiplayerUI::Show(bool abCampaign)
{
    mbCampaign = abCampaign;
    mbFocusWindow = true;
    mbCacheStatsDirty = true;
    mfCurrentMapRefresh = 0;
    mbStartPositionsDirty=true;
    mfStartPositionDelay=0;
    SetVisible(true);
}

void cLuxMultiplayerUI::Toggle()
{
    if(mbVisible) SetVisible(false);
    else Show(false);
}

bool cLuxMultiplayerUI::CanOpenChat() const
{
    if(mbVisible || !mpMultiplayer->IsActive() || !mpMultiplayer->IsReady() || mpMultiplayer->IsChangingMap() ||
       mpMultiplayer->IsSteamOverlayActive() || mpMultiplayer->GetLoadPhase()!=eLuxMultiplayerLoadPhase_None ||
       gpBase->mpInputHandler->GetState()!=eLuxInputState_Game ||
       gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!="Default" || gpBase->mpEngine->GetPaused() ||
       !gpBase->mpPlayer || !gpBase->mpPlayer->IsActive() || gpBase->mpPlayer->IsDead()) return false;
    if((gpBase->mpMessageHandler && gpBase->mpMessageHandler->IsPauseMessageActive()) ||
       (gpBase->mpEffectHandler && gpBase->mpEffectHandler->GetPlayerIsPaused()) ||
       (gpBase->mpDebugHandler && gpBase->mpDebugHandler->GetDebugWindowActive())) return false;
    cGuiSet* set=gpBase->mpEngine->GetGui()->GetFocusedSet();
    return !set || !set->PopUpIsActive();
}

void cLuxMultiplayerUI::OpenChat()
{
#if USE_SDL2
    if(mbChatOpen || !CanOpenChat() || !Initialize()) return;
    mbChatRestoreRelativeMouse=gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetRelativeMouse();
    mbChatRestoreWindowGrab=gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetWindowGrab();
    mlChatRestoreCursor=SDL_ShowCursor(SDL_QUERY);
    mlChatPreviousInputState=gpBase->mpInputHandler->GetState();
    mbChatRestoreTextInput=SDL_IsTextInputActive()==SDL_TRUE;
    ImGui::SetCurrentContext(mpContext);
    ImGui::SetWindowFocus(NULL);
    ImGuiIO& io=ImGui::GetIO();
    io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();
    msChatInput[0]='\0';msChatError.clear();mfChatErrorTime=0;
    mlChatCursor=mlChatSelectionStart=mlChatSelectionEnd=0;
    mbChatMouseSelecting=false;mfChatEntryScroll=0;mfChatHistoryScroll=0;mChatEntryRich=luxchat::RichTextLayout();
    msChatPendingInsert.clear();mbChatRestoreSelection=true;
    mbChatOpen=true;mbChatFocusInput=true;mbChatEventCaptured=true;
    mbSuppressChatOpeningText=true;mbChatSubmit=false;
    CaptureGuiMouse();
    gpBase->mpEngine->GetInput()->GetLowLevel()->RelativeMouse(false);
    gpBase->mpEngine->GetInput()->GetLowLevel()->LockInput(false);
    SDL_ShowCursor(SDL_ENABLE);
    SDL_StartTextInput();
#endif
}

void cLuxMultiplayerUI::CloseChat()
{
#if USE_SDL2
    if(!mbChatOpen) return;
    mbChatOpen=false;mbChatFocusInput=false;mbChatSubmit=false;
    mbChatEmojiPickerOpen=false;mbChatEmojiPickerFocus=false;mbChatRestoreSelection=false;
    mbChatMouseSelecting=false;mbChatMouseInput=false;mfChatEntryScroll=0;mfChatHistoryScroll=0;
    msChatPendingInsert.clear();msChatEmojiSearch[0]='\0';
    // The game still sees SDL key/button releases. Consume this event batch
    // as well so Escape cannot open the pause menu after cancelling chat.
    mbChatEventCaptured=true;mbSuppressChatOpeningText=false;
    msChatInput[0]='\0';msChatError.clear();mfChatErrorTime=0;
    RestoreGuiMouse();
    const bool sameState=mlChatPreviousInputState==gpBase->mpInputHandler->GetState();
    const bool game=gpBase->mpInputHandler->GetState()==eLuxInputState_Game;
    gpBase->mpEngine->GetInput()->GetLowLevel()->RelativeMouse(sameState?mbChatRestoreRelativeMouse:game);
    gpBase->mpEngine->GetInput()->GetLowLevel()->LockInput(sameState?mbChatRestoreWindowGrab:game);
    SDL_ShowCursor(sameState?mlChatRestoreCursor:(game?SDL_DISABLE:SDL_ENABLE));
    if(!mbChatRestoreTextInput) SDL_StopTextInput();
    if(mpContext) {
        ImGui::SetCurrentContext(mpContext);
        ImGui::SetWindowFocus(NULL);
        ImGuiIO& io=ImGui::GetIO();
        io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();
    }
    gpBase->mpEngine->GetInput()->ResetActionsToCurrentState();
    gpBase->mpInputHandler->ResetSmoothMousePos();
#endif
}

void cLuxMultiplayerUI::Update(float afTimeStep)
{
    // Global input runs before this module, so it has now consumed any chat
    // key that opened and closed the editor inside the same SDL event batch.
    mbChatEventCaptured=false;mbSuppressChatOpeningText=false;
    if(mbChatOpen && !CanOpenChat()) CloseChat();
    if(mbChatOpen && mbChatSubmit) {
        mbChatSubmit=false;
        const tString draft=mpChatEmoji?mpChatEmoji->ExpandShortcodes(msChatInput):tString(msChatInput);
        bool blank=true;size_t position=0;uint32_t point=0;
        while(position<draft.size()) if(!luxnet::ReadChatCodePoint(draft,position,point) ||
            (!luxnet::ChatCodePointIsSpace(point) && !luxnet::ChatCodePointIsFormat(point) &&
             point!='\t' && point!='\r' && point!='\n')) {blank=false;break;}
        if(draft.size()>luxnet::MaxChatTextBytes) {
            msChatError="Message too long.";mfChatErrorTime=3.0f;mbChatFocusInput=true;mbChatRestoreSelection=true;
        }
        else if(blank || mpMultiplayer->SendChatMessage(draft)) CloseChat();
        else {msChatError="Send failed. Try again.";mfChatErrorTime=3.0f;mbChatFocusInput=true;mbChatRestoreSelection=true;}
    }
    mfChatErrorTime=(std::max)(0.0f,mfChatErrorTime-afTimeStep);
    if(mfChatErrorTime==0) msChatError.clear();
    UpdateStartPositions(afTimeStep);
    const int lAction = mlPendingAction;
    mlPendingAction = 0;
    if(lAction == 1 || lAction == 12)
    {
        cLuxMultiplayerSettings settings;
        if(!mbCampaign)
        {
            settings.map = msMap;
            settings.startPos = msStartPos;
            settings.port = static_cast<unsigned short>(mlPort);
            settings.maxPlayers = static_cast<unsigned>(mlMaxPlayers);
            settings.allowClientMapChanges = mbAllowClientMapChanges;
            settings.allPlayersTriggerScripts = mbAllPlayersTriggerScripts;
            settings.playerCollision = mbPlayerCollision;
            settings.useSteam = mbUseSteam;
            settings.publicLobby = mbPublicLobby;
        }
        if(lAction == 12 && !mbCampaign) {
            mbCanHostCurrentMap=mpMultiplayer->GetCurrentMapForHosting(msCurrentMap,msCurrentMapReason);
            if(mbCanHostCurrentMap) mpMultiplayer->HostCurrentMap(settings);
            else mbHostCurrentMap=false;
        }
        else mpMultiplayer->Host(settings);
    }
    else if(lAction == 2) mpMultiplayer->Join(msAddress);
    else if(lAction == 3) mpMultiplayer->Stop("Session ended locally.");
    else if(lAction == 4) mpMultiplayer->JoinSteamLobby(msLobbyCode);
    else if(lAction == 5)
    {
        mbSearchedSteamLobbies = true;
        mlSelectedSteamLobby = 0;
        mpMultiplayer->RefreshSteamLobbies();
    }
    else if(lAction == 6) mpMultiplayer->InviteSteamFriends();
    else if(lAction == 7) mpMultiplayer->AcceptSteamInvite();
    else if(lAction == 8) mpMultiplayer->DismissSteamInvite();
    else if(lAction == 10) mpMultiplayer->RetrySteam();
    else if(lAction == 11) {
        mpMultiplayer->ClearMapCache();
        mbCacheStatsDirty = true;
    }
    else if(lAction == 13) OpenMapBrowser();
    else if(lAction == 14 && !msSelectedMap.empty()) {
        const tString selected=cString::To8Char(msSelectedMap);
        if(selected.size()<sizeof(msMap) && cPlatform::FileExists(msSelectedMap)) {
            std::strcpy(msMap,selected.c_str());mbHostCurrentMap=false;mbCloseMapBrowser=true;
            UpdateStartPositions(0);
        }
        else msMapBrowserError="The selected map is no longer available.";
    }

    if(!msPendingMapBrowserDirectory.empty()) {
        const tWString directory=msPendingMapBrowserDirectory;
        msPendingMapBrowserDirectory.clear();
        LoadMapBrowserDirectory(directory);
    }
    if(mbVisible && !mbCampaign && !mpMultiplayer->IsActive()) {
        mfCurrentMapRefresh-=afTimeStep;
        if(mfCurrentMapRefresh<=0) {
            mbCanHostCurrentMap=mpMultiplayer->GetCurrentMapForHosting(msCurrentMap,msCurrentMapReason);
            if(!mbCanHostCurrentMap) mbHostCurrentMap=false;
            mfCurrentMapRefresh=1.0f;
        }
    }

    // Disk enumeration belongs to Update, never rendering. An expanded section
    // notices completed downloads or other instances' changes within five
    // seconds; hidden/collapsed controls do not scan the cache.
    if(mbVisible && mbCacheSectionOpen) {
        mfCacheRefreshTime -= afTimeStep;
        if(mbCacheStatsDirty || mfCacheRefreshTime <= 0) {
            mCacheStats = LuxGetMultiplayerMapCacheStats();
            mbCacheStatsKnown = true;
            mbCacheStatsDirty = false;
            mfCacheRefreshTime = 5.0f;
        }
    }

#if USE_SDL2
    if(lAction == 9 && mpMultiplayer->GetSteamLobbyID())
        SDL_SetClipboardText(std::to_string(mpMultiplayer->GetSteamLobbyID()).c_str());
    if(mbVisible || mbChatOpen)
    {
        CaptureGuiMouse();
        gpBase->mpEngine->GetInput()->GetLowLevel()->RelativeMouse(false);
        gpBase->mpEngine->GetInput()->GetLowLevel()->LockInput(false);
        SDL_ShowCursor(SDL_ENABLE);
    }
#endif
}

void cLuxMultiplayerUI::EventCallback(void* apUserData, const SDL_Event& aEvent)
{
#if USE_SDL2
    cLuxMultiplayerUI* pUI = static_cast<cLuxMultiplayerUI*>(apUserData);
    // Steam owns these events until its overlay closes. In particular, Escape
    // and clicks must not operate the multiplayer window underneath it.
    if(pUI->mpMultiplayer->IsSteamOverlayActive()) {pUI->CloseChat();return;}
    if(aEvent.type==SDL_QUIT || (aEvent.type==SDL_WINDOWEVENT &&
       (aEvent.window.event==SDL_WINDOWEVENT_FOCUS_LOST || aEvent.window.event==SDL_WINDOWEVENT_CLOSE)))
        pUI->CloseChat();
    if(pUI->mbChatOpen && !pUI->CanOpenChat()) pUI->CloseChat();
    if(pUI->mbChatOpen && aEvent.type==SDL_KEYDOWN && aEvent.key.keysym.sym==SDLK_ESCAPE) {
        pUI->mbChatEventCaptured=true;
        if(!aEvent.key.repeat) {
            if(pUI->mbChatEmojiPickerOpen) pUI->CloseChatEmojiPicker();
            else pUI->CloseChat();
        }
        return;
    }
    // The physical grave key also works when Shift produces '~'. Never repeat.
    if(!pUI->mbChatOpen && aEvent.type == SDL_KEYDOWN && aEvent.key.keysym.scancode == SDL_SCANCODE_GRAVE && !aEvent.key.repeat)
    {
        pUI->Toggle();
        return;
    }
    if(!pUI->mbChatOpen && aEvent.type==SDL_KEYDOWN &&
       aEvent.key.keysym.scancode==SDL_SCANCODE_T && !aEvent.key.repeat &&
       !(aEvent.key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) && pUI->CanOpenChat()) {
        pUI->OpenChat();return;
    }
    if(pUI->mpContext)
    {
        ImGui::SetCurrentContext(pUI->mpContext);
        if(pUI->mbChatOpen) {
            pUI->mbChatEventCaptured=true;
            // SDL may already have queued the character generated by the
            // opening T. Suppress that character, never the player's next text.
            if(pUI->mbSuppressChatOpeningText && aEvent.type==SDL_TEXTINPUT &&
               (std::strcmp(aEvent.text.text,"t")==0 || std::strcmp(aEvent.text.text,"T")==0)) {
                pUI->mbSuppressChatOpeningText=false;return;
            }
        }
        if(!pUI->mbChatOpen && aEvent.type == SDL_TEXTINPUT &&
           (std::strcmp(aEvent.text.text, "~") == 0 || std::strcmp(aEvent.text.text, "`") == 0)) return;
        // Keep key/button releases flowing to both input systems, avoiding stuck keys.
        ImGui_ImplSDL2_ProcessEvent(&aEvent);
        if(pUI->mbVisible && aEvent.type == SDL_KEYDOWN && aEvent.key.keysym.sym == SDLK_ESCAPE) {
            if(pUI->mbMapBrowserOpen) pUI->mbCloseMapBrowser=true;
            else pUI->SetVisible(false);
        }
    }
#endif
}

void cLuxMultiplayerUI::DrawCallback(void* apUserData)
{
    static_cast<cLuxMultiplayerUI*>(apUserData)->Draw();
}

void cLuxMultiplayerUI::Draw()
{
#if USE_SDL2
    if(!Initialize()) return;
    ImGui::SetCurrentContext(mpContext);
    ImGui_ImplSDL2_NewFrame();
    const ImGuiIO& frameIO=ImGui::GetIO();
    const float requestedFontSize=luxchat::CalculateLayout(frameIO.DisplaySize.x,frameIO.DisplaySize.y,false).fontSize;
    EnsureChatFont(requestedFontSize,(std::max)(frameIO.DisplayFramebufferScale.x,frameIO.DisplayFramebufferScale.y));
    ImGui_ImplOpenGL3_NewFrame();
    if(mpMultiplayer->IsSteamOverlayActive())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    ImGui::NewFrame();
    const ImVec2 display=ImGui::GetIO().DisplaySize;
    const cVector2f displaySize(display.x,display.y);
    mbDisplaySizeChanged=displaySize!=mvLastDisplaySize;
    mvLastDisplaySize=displaySize;
    DrawChat();
    if(mbVisible) DrawControls();
    if(mbVisible || mbCloseMapBrowser) DrawMapBrowser();
    ImGui::Render();
    if(ImGui::GetDrawData()->CmdListsCount)
    {
        // HPL also uses fixed-function alpha testing, outside the GL3 backend's state.
        const GLboolean bAlphaTest = glIsEnabled(GL_ALPHA_TEST);
        glDisable(GL_ALPHA_TEST);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if(bAlphaTest) glEnable(GL_ALPHA_TEST);
    }
#endif
}

void cLuxMultiplayerUI::DrawChat()
{
#if USE_SDL2
    mvChatHistoryPos=0;mvChatHistorySize=0;mvChatEntryPos=0;mvChatEntrySize=0;
    mvChatTextInputPos=0;mvChatTextInputSize=0;
    mvChatEntryTextPos=0;mlChatEntryEmoji=0;
    mvChatEmojiButtonPos=0;mvChatEmojiButtonSize=0;mvChatPickerPos=0;mvChatPickerSize=0;
    mvChatPickerFirstEmojiPos=0;mvChatPickerFirstEmojiSize=0;msChatPickerFirstUnicode.clear();
    mfChatHistoryAlpha=0;mfChatHistoryContentHeight=mfChatHistoryViewportHeight=0;
    mlChatVisibleMessages=0;mlChatVisibleEmoji=0;mvChatHistoryStyles.clear();
    if(!CanOpenChat()) {if(mbChatOpen) CloseChat();return;}
    const ImVec2 screen=ImGui::GetIO().DisplaySize;
    if(screen.x<=0 || screen.y<=0) return;
    luxchat::Layout layout=luxchat::CalculateLayout(screen.x,screen.y,mbChatOpen,0,mfChatFontSize);
    const float extraHeight=msChatError.empty()?0:layout.fontSize+4.0f;
    if(mbChatOpen && extraHeight>0) layout=luxchat::CalculateLayout(screen.x,screen.y,true,extraHeight,mfChatFontSize);
    ImFont* font=mpChatFont?mpChatFont:ImGui::GetFont();
    const float padding=10.0f;
    const float textWidth=(std::max)(1.0f,layout.width-2*padding);
    struct Row {tString text;luxchat::RichTextLayout rich;float height,alpha;size_t nameBytes;uint32_t nameColor;};
    std::vector<Row> rows;
    float contentHeight=0;
    const auto& messages=mpMultiplayer->GetChatMessages();
    for(auto it=messages.rbegin();it!=messages.rend();++it) {
        const float alpha=luxchat::MessageAlpha(it->age,mbChatOpen);
        if(alpha<=0) continue;
        const tString name=it->name.empty()?"Player "+std::to_string(it->peer+1):it->name;
        const tString text=name+": "+it->text;
        luxchat::RichTextLayout rich=luxchat::LayoutRichText(text,font,layout.fontSize,textWidth,mpChatEmoji);
        const float height=rich.height+padding;
        rows.push_back({text,std::move(rich),height,alpha,name.size(),it->nameColor});contentHeight+=height;
    }
    if(!rows.empty()) {
        const float historyHeight=(std::min)(layout.historyHeight,contentHeight);
        mfChatHistoryContentHeight=contentHeight;mfChatHistoryViewportHeight=historyHeight;
        mvChatHistoryPos=cVector2f(layout.margin,layout.bottom-historyHeight);
        mvChatHistorySize=cVector2f(layout.width,historyHeight);
        ImDrawList* draw=ImGui::GetBackgroundDrawList();
        const ImVec2 start(mvChatHistoryPos.x,mvChatHistoryPos.y);
        const ImVec2 end(start.x+layout.width,layout.bottom);
        const float maximumScroll=(std::max)(0.0f,contentHeight-historyHeight);
        if(mbChatOpen && !mbChatEmojiPickerOpen && ImGui::IsMouseHoveringRect(start,end,false))
            mfChatHistoryScroll+=ImGui::GetIO().MouseWheel*(layout.fontSize+3.0f)*3.0f;
        mfChatHistoryScroll=mbChatOpen?(std::max)(0.0f,(std::min)(mfChatHistoryScroll,maximumScroll)):0;
        // Full row heights keep the end of an oversized newest message on
        // screen. While typing, wheel over the history to read its beginning
        // and earlier messages without moving the editor or taking focus.
        float bottom=layout.bottom+mfChatHistoryScroll;
        for(const Row& row:rows) {
            const float top=bottom-row.height;
            if(bottom<=start.y) break;
            if(top>=end.y) {bottom=top;continue;}
            const float clipTop=(std::max)(start.y,top+3),clipBottom=(std::min)(end.y,bottom-2);
            draw->PushClipRect(ImVec2(start.x+padding,clipTop),ImVec2(end.x-padding,clipBottom),true);
            const ImVec2 position(start.x+padding,top+padding*0.5f);
            const int alpha=static_cast<int>(255*row.alpha);
            const uint32_t nameColor=(row.nameColor&~IM_COL32_A_MASK)|(static_cast<uint32_t>(alpha)<<IM_COL32_A_SHIFT);
            const uint32_t textColor=IM_COL32(229,232,237,alpha);
            luxchat::DrawRichText(draw,row.text,row.rich,font,layout.fontSize,position.x,position.y,
                textColor,mpChatEmoji,true,row.nameBytes,nameColor);
            mvChatHistoryStyles.push_back({row.nameBytes,nameColor,textColor});
            ++mlChatVisibleMessages;mfChatHistoryAlpha=(std::max)(mfChatHistoryAlpha,row.alpha);
            for(const luxchat::RichGlyph& glyph:row.rich.glyphs)
                if(glyph.emoji>=0 && position.y+glyph.y<clipBottom &&
                    position.y+glyph.y+layout.fontSize>clipTop) ++mlChatVisibleEmoji;
            draw->PopClipRect();bottom=top;
        }
    }
    if(!mbChatOpen) return;
    mvChatEntryPos=cVector2f(layout.margin,screen.y-layout.margin-layout.entryHeight);
    mvChatEntrySize=cVector2f(layout.width,layout.entryHeight);
    ImGui::SetNextWindowPos(ImVec2(mvChatEntryPos.x,mvChatEntryPos.y),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(layout.width,layout.entryHeight),ImGuiCond_Always);
    if(mbChatFocusInput) ImGui::SetNextWindowFocus();
    ImGui::PushFont(font);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(padding,8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(7,4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(0.035f,0.045f,0.06f,0.88f));
    ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(0.7f,0.75f,0.82f,0.18f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(0.11f,0.13f,0.16f,0.8f));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.9f,0.92f,0.95f,1));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,ImVec4(0.6f,0.65f,0.72f,1));
    const ImGuiWindowFlags flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|
        ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoNav;
    if(ImGui::Begin("##MultiplayerChat",NULL,flags)) {
        ImGui::SetWindowFontScale(1.0f);
        const float buttonSize=layout.fontSize+8.0f;
        ImGui::SetNextItemWidth((std::max)(1.0f,ImGui::GetContentRegionAvail().x-buttonSize-ImGui::GetStyle().ItemSpacing.x));
        const ImGuiID inputId=ImGui::GetID("##Message");
        if(mbChatFocusInput && !mbChatEmojiPickerOpen) {
            // SetKeyboardFocusHere resolves on the following frame. Activate
            // this known field now so its first queued typing/paste is kept.
            ImGuiContext* context=ImGui::GetCurrentContext();
            context->NavActivateId=inputId;
            context->NavActivateFlags=ImGuiActivateFlags_PreferInput|ImGuiActivateFlags_TryToPreserveState;
            mbChatFocusInput=false;
        }
        // Keep ImGui's UTF-8 editor, undo and clipboard behavior. Its font
        // rendering/hit-test widths are replaced with the same rich layout
        // used by history, including complete joined emoji and shortcodes.
        const ImVec2 predictedMin=ImGui::GetCursorScreenPos();
        mvChatTextInputPos=cVector2f(predictedMin.x,predictedMin.y);
        mvChatTextInputSize=cVector2f(ImGui::CalcItemWidth(),ImGui::GetFrameHeight());
        mfChatEntryFontSize=layout.fontSize;
        mbChatMouseInput=ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            ImGui::IsMouseHoveringRect(predictedMin,ImVec2(predictedMin.x+mvChatTextInputSize.x,predictedMin.y+mvChatTextInputSize.y));
        if(!ImGui::IsMouseDown(0)) mbChatMouseSelecting=false;
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg,ImVec4(0,0,0,0));
        if(ImGui::InputText("##Message",msChatInput,sizeof(msChatInput),
            ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackAlways|ImGuiInputTextFlags_CallbackBeforeEdit,
            ChatInputCallback,this)) mbChatSubmit=true;
        ImGui::PopStyleColor(2);
        const ImVec2 inputMin=ImGui::GetItemRectMin(),inputMax=ImGui::GetItemRectMax();
        mvChatTextInputPos=cVector2f(inputMin.x,inputMin.y);
        mvChatTextInputSize=cVector2f(inputMax.x-inputMin.x,inputMax.y-inputMin.y);
        const bool inputActive=ImGui::IsItemActive();
        const tString draft=msChatInput;
        mChatEntryRich=luxchat::LayoutRichText(draft,font,layout.fontSize,FLT_MAX,mpChatEmoji,false,true);
        mlChatEntryEmoji=mChatEntryRich.emojiCount;
        const ImVec2 framePadding=ImGui::GetStyle().FramePadding;
        const ImVec2 clipMin(inputMin.x+framePadding.x,inputMin.y+framePadding.y);
        const ImVec2 clipMax(inputMax.x-framePadding.x,inputMax.y-framePadding.y);
        const float visibleWidth=(std::max)(1.0f,clipMax.x-clipMin.x);
        const float caretX=luxchat::RichCaretX(draft,mChatEntryRich,static_cast<size_t>((std::max)(0,mlChatCursor)));
        const float maximumScroll=(std::max)(0.0f,mChatEntryRich.width-visibleWidth+1.0f);
        mfChatEntryScroll=(std::max)(0.0f,(std::min)(mfChatEntryScroll,maximumScroll));
        if(inputActive && !mbChatEmojiPickerOpen) {
            if(caretX<mfChatEntryScroll) mfChatEntryScroll=caretX;
            if(caretX>mfChatEntryScroll+visibleWidth-1) mfChatEntryScroll=caretX-visibleWidth+1;
        }
        mvChatEntryTextPos=cVector2f(clipMin.x-mfChatEntryScroll,clipMin.y);
        ImDrawList* entryDraw=ImGui::GetWindowDrawList();
        entryDraw->PushClipRect(clipMin,clipMax,true);
        if(mlChatSelectionStart!=mlChatSelectionEnd) {
            const float a=luxchat::RichCaretX(draft,mChatEntryRich,static_cast<size_t>((std::max)(0,mlChatSelectionStart)));
            const float b=luxchat::RichCaretX(draft,mChatEntryRich,static_cast<size_t>((std::max)(0,mlChatSelectionEnd)));
            entryDraw->AddRectFilled(ImVec2(mvChatEntryTextPos.x+(std::min)(a,b),clipMin.y),
                ImVec2(mvChatEntryTextPos.x+(std::max)(a,b),clipMax.y),IM_COL32(91,126,175,120));
        }
        luxchat::DrawRichText(entryDraw,draft,mChatEntryRich,font,layout.fontSize,mvChatEntryTextPos.x,
            mvChatEntryTextPos.y,IM_COL32(229,235,242,255),mpChatEmoji,false);
        ImGuiInputTextState* inputState=ImGui::GetInputTextState(inputId);
        if(inputActive && !mbChatEmojiPickerOpen && inputState) {
            const float x=mvChatEntryTextPos.x+caretX;
            if(!ImGui::GetIO().ConfigInputTextCursorBlink || inputState->CursorAnim<=0 ||
                std::fmod(inputState->CursorAnim,1.2f)<=0.8f)
                entryDraw->AddLine(ImVec2(x,clipMin.y),ImVec2(x,clipMax.y),IM_COL32(229,235,242,255));
            // SDL's IME candidate window follows the visible rich caret too.
            ImGuiContext* context=ImGui::GetCurrentContext();
            context->PlatformImeData.InputPos=ImVec2((std::max)(clipMin.x,(std::min)(x,clipMax.x)),clipMin.y);
            context->PlatformImeData.InputLineHeight=layout.fontSize;
        }
        entryDraw->PopClipRect();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.23f,0.29f,0.37f,0.65f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0.3f,0.38f,0.49f,0.75f));
        if(ImGui::Button("##ChatEmojiButton",ImVec2(buttonSize,buttonSize))) {
            if(mbChatEmojiPickerOpen) CloseChatEmojiPicker();else OpenChatEmojiPicker();
        }
        ImGui::PopStyleColor(3);
        const ImVec2 buttonMin=ImGui::GetItemRectMin(),buttonMax=ImGui::GetItemRectMax();
        mvChatEmojiButtonPos=cVector2f(buttonMin.x,buttonMin.y);
        mvChatEmojiButtonSize=cVector2f(buttonMax.x-buttonMin.x,buttonMax.y-buttonMin.y);
        const ImVec2 center((buttonMin.x+buttonMax.x)*0.5f,(buttonMin.y+buttonMax.y)*0.5f);
        const float radius=layout.fontSize*0.42f;
        {
            const ImU32 iconColor=IM_COL32(164,174,187,255);
            entryDraw->AddCircle(center,radius,iconColor,24,1.5f);
            entryDraw->AddCircleFilled(ImVec2(center.x-radius*0.34f,center.y-radius*0.23f),1.2f,iconColor);
            entryDraw->AddCircleFilled(ImVec2(center.x+radius*0.34f,center.y-radius*0.23f),1.2f,iconColor);
            entryDraw->AddBezierCubic(ImVec2(center.x-radius*0.48f,center.y+radius*0.12f),
                ImVec2(center.x-radius*0.25f,center.y+radius*0.68f),ImVec2(center.x+radius*0.25f,center.y+radius*0.68f),
                ImVec2(center.x+radius*0.48f,center.y+radius*0.12f),iconColor,1.5f);
        }
        ImGui::SetItemTooltip("Emoji");
        // A button needs to retain its active ID until mouse-up. Reclaiming
        // the editor while the emoji button is held would cancel its click.
        if(!inputActive && !mbChatEmojiPickerOpen && !ImGui::IsMouseDown(0)) {
            mbChatFocusInput=true;mbChatRestoreSelection=true;
        }
        if(!msChatError.empty()) ImGui::TextDisabled("%s",msChatError.c_str());
        DrawChatEmojiPicker(layout.fontSize);
    }
    ImGui::End();
    ImGui::PopStyleColor(5);ImGui::PopStyleVar(4);ImGui::PopFont();
#endif
}

int cLuxMultiplayerUI::ChatInputCallback(ImGuiInputTextCallbackData* data)
{
#if USE_SDL2
    cLuxMultiplayerUI* ui=static_cast<cLuxMultiplayerUI*>(data->UserData);
    bool restored=false;
    if(data->EventFlag==ImGuiInputTextFlags_CallbackBeforeEdit &&
       (ui->mbChatRestoreSelection || !ui->msChatPendingInsert.empty())) {
        const auto bounded=[&](int offset) {return (std::max)(0,(std::min)(offset,data->BufTextLen));};
        const bool selected=ui->mlChatSelectionStart!=ui->mlChatSelectionEnd;
        const int start=bounded(selected?(std::min)(ui->mlChatSelectionStart,ui->mlChatSelectionEnd):ui->mlChatCursor);
        const int end=bounded(selected?(std::max)(ui->mlChatSelectionStart,ui->mlChatSelectionEnd):ui->mlChatCursor);
        data->CursorPos=bounded(ui->mlChatCursor);
        data->SelectionStart=bounded(ui->mlChatSelectionStart);data->SelectionEnd=bounded(ui->mlChatSelectionEnd);
        if(!ui->msChatPendingInsert.empty()) {
            if(static_cast<size_t>(data->BufTextLen-(end-start))+ui->msChatPendingInsert.size()<static_cast<size_t>(data->BufSize)) {
                data->DeleteChars(start,end-start);
                data->InsertChars(start,ui->msChatPendingInsert.c_str());
                data->CursorPos=start+static_cast<int>(ui->msChatPendingInsert.size());
                data->SelectionStart=data->SelectionEnd=data->CursorPos;
            } else {ui->msChatError="Message is full.";ui->mfChatErrorTime=3.0f;}
            ui->msChatPendingInsert.clear();
        }
        ui->mbChatRestoreSelection=false;restored=true;
        ui->mlChatCursor=data->CursorPos;
        ui->mlChatSelectionStart=data->SelectionStart;ui->mlChatSelectionEnd=data->SelectionEnd;
    }
    // Native keyboard editing works in UTF-8 byte offsets. Mouse editing uses
    // the visible rich cells rather than the hidden font's fallback widths.
    ImGuiIO& io=ImGui::GetIO();
    const bool clicked=ui->mbChatMouseInput && ImGui::IsMouseClicked(0);
    if(data->EventFlag==ImGuiInputTextFlags_CallbackBeforeEdit &&
        (clicked || (ui->mbChatMouseSelecting && ImGui::IsMouseDragging(0,0)))) {
        const tString text(data->Buf,static_cast<size_t>(data->BufTextLen));
        const luxchat::RichTextLayout rich=luxchat::LayoutRichText(text,ImGui::GetFont(),ui->mfChatEntryFontSize,
            FLT_MAX,ui->mpChatEmoji,false,true);
        const float left=ui->mvChatTextInputPos.x+ImGui::GetStyle().FramePadding.x;
        const float right=ui->mvChatTextInputPos.x+ui->mvChatTextInputSize.x-ImGui::GetStyle().FramePadding.x;
        if(!clicked) {
            const float overflow=io.MousePos.x<left?io.MousePos.x-left:io.MousePos.x>right?io.MousePos.x-right:0;
            ui->mfChatEntryScroll=(std::max)(0.0f,(std::min)(ui->mfChatEntryScroll+overflow*io.DeltaTime*8,
                (std::max)(0.0f,rich.width-(right-left)+1)));
        }
        const float hitX=io.MousePos.x-left+ui->mfChatEntryScroll;
        const int hit=static_cast<int>(luxchat::HitRichText(rich,hitX,text.size()));
        if(clicked) {
            ui->mbChatMouseSelecting=true;
            ui->mlChatMouseAnchor=io.KeyShift?(ui->mlChatSelectionStart!=ui->mlChatSelectionEnd?
                ui->mlChatSelectionStart:ui->mlChatCursor):hit;
            const int clicks=ImGui::GetMouseClickedCount(0);
            if(clicks>=3 || (io.KeyCtrl && !io.KeyShift)) {
                data->CursorPos=data->SelectionEnd=data->BufTextLen;data->SelectionStart=0;
                ui->mlChatMouseAnchor=0;
            } else if(clicks==2 && !io.KeyShift) {
                size_t first=0,last=text.size();bool found=false;
                for(size_t i=0;i<rich.glyphs.size();++i) {
                    const luxchat::RichGlyph& glyph=rich.glyphs[i];
                    if(hitX<glyph.x || hitX>=glyph.x+glyph.width) continue;
                    first=glyph.offset;last=glyph.offset+glyph.length;found=true;
                    if(glyph.emoji<0) {
                        const auto space=[](uint32_t point) {return luxnet::ChatCodePointIsSpace(point) || point=='\t';};
                        const bool spaces=space(glyph.codePoint);
                        for(size_t j=i;j>0 && rich.glyphs[j-1].emoji<0 && space(rich.glyphs[j-1].codePoint)==spaces;--j)
                            first=rich.glyphs[j-1].offset;
                        for(size_t j=i+1;j<rich.glyphs.size() && rich.glyphs[j].emoji<0 && space(rich.glyphs[j].codePoint)==spaces;++j)
                            last=rich.glyphs[j].offset+rich.glyphs[j].length;
                    }
                    break;
                }
                data->SelectionStart=found?static_cast<int>(first):hit;
                data->CursorPos=data->SelectionEnd=found?static_cast<int>(last):hit;
                ui->mlChatMouseAnchor=data->SelectionStart;
            } else {
                data->CursorPos=data->SelectionEnd=hit;data->SelectionStart=ui->mlChatMouseAnchor;
            }
        } else {
            data->CursorPos=data->SelectionEnd=hit;data->SelectionStart=ui->mlChatMouseAnchor;
        }
        ui->mlChatCursor=data->CursorPos;
        ui->mlChatSelectionStart=data->SelectionStart;ui->mlChatSelectionEnd=data->SelectionEnd;
        return 1;
    }
    ui->mlChatCursor=data->CursorPos;
    ui->mlChatSelectionStart=data->SelectionStart;ui->mlChatSelectionEnd=data->SelectionEnd;
    return restored?1:0;
#endif
    return 0;
}

void cLuxMultiplayerUI::OpenChatEmojiPicker()
{
#if USE_SDL2
    if(!mbChatOpen || !CanOpenChat()) return;
    mbChatEmojiPickerOpen=true;mbChatEmojiPickerFocus=true;mbChatFocusInput=false;
    msChatEmojiSearch[0]='\0';msChatPickerLastSearch="\xff";
#endif
}

void cLuxMultiplayerUI::CloseChatEmojiPicker()
{
#if USE_SDL2
    if(!mbChatEmojiPickerOpen) return;
    mbChatEmojiPickerOpen=false;mbChatEmojiPickerFocus=false;
    mbChatFocusInput=true;mbChatRestoreSelection=true;mbChatEventCaptured=true;
    ImGui::SetWindowFocus("##MultiplayerChat");
#endif
}

void cLuxMultiplayerUI::DrawChatEmojiPicker(float fontSize)
{
#if USE_SDL2
    if(!mbChatEmojiPickerOpen) return;
    const ImVec2 screen=ImGui::GetIO().DisplaySize;
    const float margin=(std::min)(12.0f,(std::min)(screen.x,screen.y)*0.04f);
    const float width=(std::max)(1.0f,(std::min)(420.0f,screen.x-2*margin));
    const float available=mvChatEntryPos.y-margin-8.0f;
    const float height=(std::max)(1.0f,(std::min)(340.0f,available>=120.0f?available:screen.y-2*margin));
    const float x=(std::max)(margin,(std::min)(mvChatEntryPos.x,screen.x-width-margin));
    const float y=(std::max)(margin,(std::min)(mvChatEntryPos.y-height-8.0f,screen.y-height-margin));
    ImGui::SetNextWindowPos(ImVec2(x,y),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width,height),ImGuiCond_Always);
    const bool focus=mbChatEmojiPickerFocus;
    if(focus) {ImGui::SetNextWindowFocus();mbChatEmojiPickerFocus=false;}
    bool open=true;
    const ImGuiWindowFlags flags=ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(0.035f,0.045f,0.06f,0.98f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,ImVec4(0.045f,0.06f,0.075f,1));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,ImVec4(0.075f,0.095f,0.12f,1));
    if(ImGui::Begin("Emoji##MultiplayerChatPicker",&open,flags)) {
        ImGui::SetWindowFontScale(1.0f);
        const ImVec2 position=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
        mvChatPickerPos=cVector2f(position.x,position.y);mvChatPickerSize=cVector2f(size.x,size.y);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if(focus) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##ChatEmojiSearch","Search emoji",msChatEmojiSearch,sizeof(msChatEmojiSearch));
        if(msChatPickerLastSearch!=msChatEmojiSearch) {
            msChatPickerLastSearch=msChatEmojiSearch;mvChatPickerMatches.clear();
            tString search=msChatEmojiSearch;
            for(char& character:search) if(character>='A' && character<='Z') character=character-'A'+'a';
            if(search.size()>1 && search.front()==':' && search.back()==':') search=search.substr(1,search.size()-2);
            std::set<int> seen;
            if(mpChatEmoji && mpChatEmoji->IsReady()) {
                for(const auto& alias:mpChatEmoji->GetShortcodes()) {
                    if(!search.empty() && alias.first.find(search)==tString::npos) continue;
                    int glyph=-1;
                    if(mpChatEmoji->Match(alias.second,0,glyph) && seen.insert(glyph).second)
                        mvChatPickerMatches.emplace_back(alias.first,alias.second);
                }
                // New Twemoji sequences may precede the shortcode dictionary.
                // Every packaged emoji remains available in the unfiltered grid.
                if(search.empty()) for(size_t glyph=0;glyph<mpChatEmoji->GetGlyphCount();++glyph)
                    if(seen.insert(static_cast<int>(glyph)).second)
                        mvChatPickerMatches.emplace_back("",mpChatEmoji->GetGlyphUnicode(static_cast<int>(glyph)));
            }
        }
        ImGui::BeginChild("##ChatEmojiGrid",ImVec2(0,0),false);
        if(mvChatPickerMatches.empty()) ImGui::TextDisabled("No emoji found.");
        const float emojiSize=fontSize+10.0f,cellSize=emojiSize+12.0f;
        const float spacing=ImGui::GetStyle().ItemSpacing.x;
        const int columns=(std::max)(1,static_cast<int>((ImGui::GetContentRegionAvail().x+spacing)/(cellSize+spacing)));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>((mvChatPickerMatches.size()+columns-1)/columns),cellSize+ImGui::GetStyle().ItemSpacing.y);
        bool first=true;
        while(clipper.Step()) for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
            for(int column=0;column<columns;++column) {
                const size_t index=static_cast<size_t>(row)*columns+column;
                if(index>=mvChatPickerMatches.size()) break;
                if(column) ImGui::SameLine();
                ImGui::PushID(static_cast<int>(index));
                const bool selected=ImGui::InvisibleButton("##Emoji",ImVec2(cellSize,cellSize));
                const ImVec2 itemMin=ImGui::GetItemRectMin(),itemMax=ImGui::GetItemRectMax();
                const auto& entry=mvChatPickerMatches[index];
                if(first) {
                    mvChatPickerFirstEmojiPos=cVector2f(itemMin.x,itemMin.y);
                    mvChatPickerFirstEmojiSize=cVector2f(itemMax.x-itemMin.x,itemMax.y-itemMin.y);
                    msChatPickerFirstUnicode=entry.second;first=false;
                }
                if(ImGui::IsItemHovered()) {
                    ImGui::GetWindowDrawList()->AddRectFilled(itemMin,itemMax,IM_COL32(110,128,158,65),5.0f);
                    if(!entry.first.empty()) ImGui::SetTooltip(":%s:",entry.first.c_str());
                }
                int glyph=-1;
                if(mpChatEmoji->Match(entry.second,0,glyph)) mpChatEmoji->DrawGlyph(ImGui::GetWindowDrawList(),glyph,
                    itemMin.x+6,itemMin.y+6,emojiSize,255);
                if(selected) {msChatPendingInsert=entry.second;CloseChatEmojiPicker();}
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    if(!open) CloseChatEmojiPicker();
#endif
}

void cLuxMultiplayerUI::UpdateStartPositions(float afTimeStep)
{
    if(!mbVisible || mbCampaign || mpMultiplayer->IsActive()) return;
    if(msStartPositionMap!=msMap) {
        msStartPositionMap=msMap;msStartPos[0]='\0';mvStartPositions.clear();msStartPositionError.clear();
        mbStartPositionsDirty=true;mfStartPositionDelay=0.25f;
    }
    if(mbHostCurrentMap || !mbStartPositionsDirty) return;
    mfStartPositionDelay-=afTimeStep;
    if(mfStartPositionDelay>0) return;
    mbStartPositionsDirty=false;mvStartPositions.clear();msStartPositionError.clear();
    tString map=msStartPositionMap.empty()?gpBase->msStartMapFile:msStartPositionMap;
    tString folder=msStartPositionMap.empty()?gpBase->msStartMapFolder:cString::GetFilePath(map);
    if(!msStartPositionMap.empty()) {
        map=cString::GetFileName(map);
        if(folder.empty()) folder=gpBase->msStartMapFolder;
    }
    tWString path=cString::To16Char(folder+map);
    if(!cPlatform::FileExists(path)) path=gpBase->mpEngine->GetResources()->GetFileSearcher()->GetFilePath(folder+map);
    std::vector<uint8_t> bytes;
    if(!LuxReadMultiplayerMap(path,bytes)) msStartPositionError="Choose an available XML map to list its start positions.";
    else LuxCollectMultiplayerStartPositions(bytes,mvStartPositions,msStartPositionError);
    if(msStartPos[0] && std::find(mvStartPositions.begin(),mvStartPositions.end(),msStartPos)==mvStartPositions.end())
        msStartPos[0]='\0';
}

void cLuxMultiplayerUI::DrawStartPositions()
{
#if USE_SDL2
    const char* preview=msStartPos[0]?msStartPos:"Map default";
    if(ImGui::BeginCombo("Start position",preview)) {
        if(ImGui::Selectable("Map default",msStartPos[0]=='\0')) msStartPos[0]='\0';
        for(const tString& start:mvStartPositions)
            if(ImGui::Selectable(start.c_str(),start==msStartPos)) std::strcpy(msStartPos,start.c_str());
        ImGui::EndCombo();
    }
    if(mbStartPositionsDirty) ImGui::TextDisabled("Reading start positions...");
    else if(!msStartPositionError.empty()) ImGui::TextWrapped("%s",msStartPositionError.c_str());
    else if(mvStartPositions.empty()) ImGui::TextDisabled("This map has no named player starts.");
    if(ImGui::SmallButton("Refresh start positions")) {mbStartPositionsDirty=true;mfStartPositionDelay=0;}
#endif
}

void cLuxMultiplayerUI::OpenMapBrowser()
{
    tWString directory=msMapBrowserDirectory;
    if(directory.empty()) {
        directory=cString::GetFilePathW(cString::To16Char(msMap));
        if(directory.empty() && !msCurrentMap.empty())
            directory=cString::GetFilePathW(cString::To16Char(msCurrentMap));
        if(directory.empty()) {
            directory=cPlatform::GetWorkingDir();
            if(cPlatform::FolderExists(directory+_W("/maps"))) directory+=_W("/maps");
        }
    }
    LoadMapBrowserDirectory(directory);
    mbMapBrowserOpen=true;mbFocusMapBrowser=true;mbCloseMapBrowser=false;
}

void cLuxMultiplayerUI::LoadMapBrowserDirectory(const tWString& directory)
{
    // Directory I/O is performed only in Update, never from the GL overlay.
    const tWString full=cPlatform::FolderExists(directory)?cPlatform::GetFullFilePath(directory):_W("");
    if(full.empty()) {msMapBrowserError="That folder could not be opened.";return;}
    const tString display=cString::To8Char(full);
    if(display.size()>=sizeof(msMapBrowserPath)) {msMapBrowserError="That folder path is too long.";return;}
    msMapBrowserDirectory=cString::ReplaceCharToW(full,_W("\\"),_W("/"));
    if(msMapBrowserDirectory.back()!=_W('/')) msMapBrowserDirectory+=_W('/');
    std::strcpy(msMapBrowserPath,display.c_str());
    msSelectedMap.clear();msMapBrowserError.clear();
    mlstMapBrowserFolders.clear();mlstMapBrowserFiles.clear();
    cPlatform::FindFoldersInDir(mlstMapBrowserFolders,msMapBrowserDirectory,false,false);
    tWStringList files;
    cPlatform::FindFilesInDir(files,msMapBrowserDirectory,_W("*"),false);
    for(const tWString& file:files)
        if(cString::ToLowerCaseW(cString::GetFileExtW(file))==_W("map")) mlstMapBrowserFiles.push_back(file);
    mlstMapBrowserFolders.sort();mlstMapBrowserFiles.sort();
}

void cLuxMultiplayerUI::QueueMapBrowserParent()
{
    tWString path=msMapBrowserDirectory;
    if(path.size()>1 && path.back()==_W('/') && !(path.size()==3 && path[1]==_W(':'))) path.pop_back();
    // GetFilePathW treats extensionless input as a directory and returns it
    // unchanged, so parent navigation must split the actual separator.
    const size_t separator=path.find_last_of(_W('/'));
    if(separator!=tWString::npos) msPendingMapBrowserDirectory=path.substr(0,separator+1);
}

void cLuxMultiplayerUI::DrawMapBrowser()
{
#if USE_SDL2
    if(!mbMapBrowserOpen && !mbCloseMapBrowser) return;
    const char* title="Select a map";
    if(mbFocusMapBrowser) {ImGui::OpenPopup(title);mbFocusMapBrowser=false;}
    const ImVec2 screen=ImGui::GetIO().DisplaySize;
    const ImVec2 limit((std::max)(1.0f,screen.x-24),(std::max)(1.0f,screen.y-24));
    const ImGuiCond layout=mbDisplaySizeChanged?ImGuiCond_Always:ImGuiCond_Appearing;
    ImGui::SetNextWindowPos(ImVec2(screen.x*0.5f,screen.y*0.5f),layout,ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2((std::min)(300.0f,limit.x),(std::min)(120.0f,limit.y)),limit);
    ImGui::SetNextWindowSize(ImVec2((std::min)(680.0f,limit.x),(std::min)(470.0f,limit.y)),layout);
    if(ImGui::BeginPopupModal(title,&mbMapBrowserOpen,ImGuiWindowFlags_NoCollapse)) {
        if(mbCloseMapBrowser) {ImGui::CloseCurrentPopup();mbMapBrowserOpen=false;}
        else {
            ImGui::TextUnformatted("Choose an XML map (.map)");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-50);
            if(ImGui::InputText("##MapDirectory",msMapBrowserPath,sizeof(msMapBrowserPath),ImGuiInputTextFlags_EnterReturnsTrue))
                msPendingMapBrowserDirectory=cString::To16Char(msMapBrowserPath);
            ImGui::SameLine();
            if(ImGui::Button("Go")) msPendingMapBrowserDirectory=cString::To16Char(msMapBrowserPath);
            if(ImGui::Button("Up")) QueueMapBrowserParent();
            ImGui::SameLine();
            if(ImGui::Button("Refresh")) msPendingMapBrowserDirectory=msMapBrowserDirectory;
            ImGui::SameLine();ImGui::TextDisabled("Double-click a folder to open it");
            const float reserve=110.0f;
            ImGui::BeginChild("MapFiles",ImVec2(0,(std::max)(80.0f,ImGui::GetContentRegionAvail().y-reserve)),true);
            for(const tWString& folder:mlstMapBrowserFolders) {
                const tString name="[Folder] "+cString::To8Char(folder);
                if(ImGui::Selectable(name.c_str(),false,ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
                    msPendingMapBrowserDirectory=msMapBrowserDirectory+folder;
            }
            for(const tWString& file:mlstMapBrowserFiles) {
                const tWString path=msMapBrowserDirectory+file;
                const tString name=cString::To8Char(file);
                if(ImGui::Selectable(name.c_str(),msSelectedMap==path,ImGuiSelectableFlags_AllowDoubleClick)) {
                    if(cString::To8Char(path).size()>=sizeof(msMap)) {
                        msSelectedMap.clear();msMapBrowserError="That map path is too long to host.";
                    }
                    else {
                        msSelectedMap=path;msMapBrowserError.clear();
                        if(ImGui::IsMouseDoubleClicked(0)) mlPendingAction=14;
                    }
                }
            }
            if(mlstMapBrowserFiles.empty() && mlstMapBrowserFolders.empty()) ImGui::TextDisabled("This folder contains no maps or subfolders.");
            ImGui::EndChild();
            ImGui::TextWrapped("Selected: %s",msSelectedMap.empty()?"None":cString::To8Char(cString::GetFileNameW(msSelectedMap)).c_str());
            if(!msMapBrowserError.empty()) ImGui::TextWrapped("%s",msMapBrowserError.c_str());
            ImGui::BeginDisabled(msSelectedMap.empty());
            if(ImGui::Button("Use selected map")) mlPendingAction=14;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) {mbMapBrowserOpen=false;ImGui::CloseCurrentPopup();}
        }
        ImGui::EndPopup();
    }
    mbCloseMapBrowser=false;
#endif
}

void cLuxMultiplayerUI::DrawSteamJoinControls()
{
#if USE_SDL2
    const bool bAvailable = mpMultiplayer->IsSteamAvailable();
    ImGui::InputTextWithHint("Lobby code", "Paste the host's numeric lobby code", msLobbyCode, sizeof(msLobbyCode));
    ImGui::BeginDisabled(!bAvailable || msLobbyCode[0] == '\0');
    if(ImGui::Button("Join by code")) mlPendingAction = 4;
    ImGui::EndDisabled();
    ImGui::TextWrapped("Join friends through a Steam invitation or their lobby code. Public sessions appear below.");
    ImGui::Spacing();
    const bool bSearching = mpMultiplayer->IsSteamLobbySearchPending();
    ImGui::BeginDisabled(!bAvailable || bSearching);
    if(ImGui::Button("Refresh sessions")) mlPendingAction = 5;
    ImGui::EndDisabled();
    if(bSearching)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Searching Steam...");
    }
    const std::vector<hpl::cSteamLobbyInfo>& lobbies = mpMultiplayer->GetSteamLobbies();
    if(!lobbies.empty())
    {
        if(ImGui::BeginTable("SteamSessions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, 142)))
        {
            ImGui::TableSetupColumn("Session", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for(size_t i = 0; i < lobbies.size(); ++i)
            {
                const hpl::cSteamLobbyInfo& lobby = lobbies[i];
                const tString code = std::to_string(lobby.id);
                ImGui::PushID(code.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const tString label = (lobby.name.empty() ? "Unnamed session" : lobby.name) + "##session";
                if(ImGui::Selectable(label.c_str(), mlSelectedSteamLobby == lobby.id, ImGuiSelectableFlags_SpanAllColumns))
                    mlSelectedSteamLobby = lobby.id;
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(lobby.map.empty() ? "Campaign" : lobby.map.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u / %u", lobby.players, lobby.maxPlayers);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        bool bCanJoinSelected = false;
        for(size_t i = 0; i < lobbies.size(); ++i)
            if(lobbies[i].id == mlSelectedSteamLobby && lobbies[i].players < lobbies[i].maxPlayers)
                bCanJoinSelected = true;
        ImGui::BeginDisabled(!bAvailable || bSearching || !bCanJoinSelected);
        if(ImGui::Button("Join selected session"))
        {
            const tString code = std::to_string(mlSelectedSteamLobby);
            std::strncpy(msLobbyCode, code.c_str(), sizeof(msLobbyCode) - 1);
            msLobbyCode[sizeof(msLobbyCode) - 1] = '\0';
            mlPendingAction = 4;
        }
        ImGui::EndDisabled();
    }
    else if(!bSearching)
        ImGui::TextDisabled(mbSearchedSteamLobbies ? "No public sessions found." : "Refresh to find public sessions.");
#endif
}

void cLuxMultiplayerUI::DrawControls()
{
#if USE_SDL2
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        mbDisplaySizeChanged?ImGuiCond_Always:ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const float fWidth=(std::max)(1.0f,(std::min)(550.0f,io.DisplaySize.x-24));
    const float fHeight=(std::max)(1.0f,io.DisplaySize.y-24);
    ImGui::SetNextWindowSizeConstraints(ImVec2((std::min)(300.0f,fWidth),(std::min)(120.0f,fHeight)),ImVec2(fWidth,fHeight));
    ImGui::SetNextWindowSize(ImVec2(fWidth, 0), ImGuiCond_Always);
    if(mbFocusWindow) { ImGui::SetNextWindowFocus(); mbFocusWindow = false; }
    bool bOpen = true;
    const bool bExpanded = ImGui::Begin("Multiplayer", &bOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    if(bExpanded)
    {
        ImGui::TextWrapped("%s", mpMultiplayer->GetStatus().c_str());
        ImGui::Separator();
        if(mpMultiplayer->GetPendingSteamInvite())
        {
            ImGui::TextWrapped("Steam invitation: lobby %s", std::to_string(mpMultiplayer->GetPendingSteamInvite()).c_str());
            if(mpMultiplayer->IsActive())
                ImGui::TextWrapped("Joining this invitation leaves your current session.");
            ImGui::BeginDisabled(!mpMultiplayer->IsSteamAvailable());
            if(ImGui::Button("Join invited session")) mlPendingAction = 7;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("Dismiss invitation")) mlPendingAction = 8;
            ImGui::Separator();
        }
        if(mpMultiplayer->IsActive())
        {
            if(mpMultiplayer->IsSteamSession())
            {
                ImGui::TextWrapped("%s", mpMultiplayer->GetSteamStatus().c_str());
                if(!mpMultiplayer->IsSteamAvailable() && ImGui::Button("Retry Steam")) mlPendingAction = 10;
                if(mpMultiplayer->GetSteamLobbyID())
                {
                    ImGui::Text("Lobby code: %s", std::to_string(mpMultiplayer->GetSteamLobbyID()).c_str());
                    if(ImGui::Button("Copy code")) mlPendingAction = 9;
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!mpMultiplayer->IsSteamAvailable());
                    if(ImGui::Button("Invite friends")) mlPendingAction = 6;
                    ImGui::EndDisabled();
                }
            }
            ImGui::TextWrapped("The session keeps running while menus and this window are open.");
            if(ImGui::Button(mpMultiplayer->IsHost() ? "End session" : "Disconnect")) mlPendingAction = 3;
        }
        else
        {
            if(mbCampaign)
                ImGui::TextWrapped("Play the Amnesia campaign together through Steam. Host starts a fresh campaign in a friends-only session.");
            else
            {
                if(ImGui::RadioButton("Steam", mbUseSteam)) mbUseSteam = true;
                ImGui::SameLine();
                if(ImGui::RadioButton("Direct IP", !mbUseSteam)) mbUseSteam = false;
            }
            const bool bSteam = mbCampaign || mbUseSteam;
            if(bSteam)
            {
                ImGui::TextWrapped("%s", mpMultiplayer->GetSteamStatus().c_str());
                if(!mpMultiplayer->IsSteamAvailable() && ImGui::Button("Retry Steam")) mlPendingAction = 10;
            }
            if(ImGui::BeginTabBar("SessionMode"))
            {
                if(ImGui::BeginTabItem("Host"))
                {
                    if(!mbCampaign)
                    {
                        if(mbCanHostCurrentMap) ImGui::Checkbox("Host currently loaded map", &mbHostCurrentMap);
                        else if(!msCurrentMap.empty() && !msCurrentMapReason.empty())
                            ImGui::TextWrapped("%s",msCurrentMapReason.c_str());
                        if(mbHostCurrentMap) {
                            if(mbCanHostCurrentMap) {
                                ImGui::TextWrapped("Current map: %s", cString::GetFileName(msCurrentMap).c_str());
                                ImGui::TextWrapped("Keeps your current world and player position. Earlier scripted scenes and spawned objects are not replayed for joining players.");
                            }
                            else ImGui::TextWrapped("%s",msCurrentMapReason.c_str());
                        }
                        else {
                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-80);
                            ImGui::InputTextWithHint("##HostMap", "Blank starts the campaign", msMap, sizeof(msMap));
                            ImGui::SameLine();
                            if(ImGui::Button("Browse...")) mlPendingAction=13;
                            DrawStartPositions();
                        }
                        if(bSteam)
                        {
                            ImGui::Checkbox("Public session", &mbPublicLobby);
                            ImGui::TextWrapped(mbPublicLobby ?
                                "Anyone with this game can find and join the session." :
                                "Friends can join through Steam invitations or a lobby code.");
                        }
                        else
                        {
                            ImGui::InputInt("UDP port", &mlPort);
                            if(mlPort < 1) mlPort = 1;
                            if(mlPort > 65535) mlPort = 65535;
                        }
                        ImGui::InputInt("Players (including host)", &mlMaxPlayers);
                        if(mlMaxPlayers < 2) mlMaxPlayers = 2;
                        if(mlMaxPlayers > 16) mlMaxPlayers = 16;
                        ImGui::Checkbox("Allow clients to trigger map changes", &mbAllowClientMapChanges);
                        ImGui::Checkbox("Players collide with each other", &mbPlayerCollision);
                        ImGui::Checkbox("Every player can trigger Player scripts", &mbAllPlayersTriggerScripts);
                        ImGui::TextWrapped(mbAllPlayersTriggerScripts ?
                            "Player triggers accept any connected player." :
                            "Only the host activates Player-specific script triggers.");
                    }
                    ImGui::BeginDisabled((bSteam && !mpMultiplayer->IsSteamAvailable()) ||
                        (!mbCampaign && mbHostCurrentMap && !mbCanHostCurrentMap));
                    if(ImGui::Button(mbCampaign ? "Host campaign" : "Host session"))
                        mlPendingAction = !mbCampaign && mbHostCurrentMap ? 12 : 1;
                    ImGui::EndDisabled();
                    if(bSteam)
                        ImGui::TextWrapped("Invite friends through Steam or share your lobby code after hosting.");
                    else
                        ImGui::TextWrapped("Share your reachable IP address and UDP port. Internet hosting may require port forwarding. Default port: 27015.");
                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Join"))
                {
                    if(bSteam) DrawSteamJoinControls();
                    else
                    {
                        ImGui::InputText("Host address", msAddress, sizeof(msAddress));
                        ImGui::TextWrapped("Enter an IP address with optional port, for example 192.168.1.20:27015 or [::1]:27015.");
                        ImGui::BeginDisabled(msAddress[0] == '\0');
                        if(ImGui::Button("Join session")) mlPendingAction = 2;
                        ImGui::EndDisabled();
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::Separator();
        const bool bCacheSectionOpen = ImGui::CollapsingHeader("Downloaded maps");
        if(bCacheSectionOpen && !mbCacheSectionOpen) mbCacheStatsDirty = true;
        mbCacheSectionOpen = bCacheSectionOpen;
        if(bCacheSectionOpen)
        {
            if(mbCacheStatsKnown)
                ImGui::Text("Stored maps: %.2f MiB (%llu %s)",static_cast<double>(mCacheStats.bytes)/(1024.0*1024.0),
                    static_cast<unsigned long long>(mCacheStats.maps),mCacheStats.maps==1?"map":"maps");
            else ImGui::TextDisabled("Stored maps: checking...");
            ImGui::TextWrapped("Downloaded maps are saved for faster joins. Deleted maps can be downloaded again. Maps currently in use are kept.");
            if(ImGui::Button("Delete downloaded maps")) mlPendingAction = 11;
        }
        ImGui::Separator();
        if(ImGui::Button("Close")) bOpen = false;
        ImGui::SameLine();
        ImGui::TextDisabled("~ or Escape closes this window");
    }
    ImGui::End();
    if(!bOpen) SetVisible(false);
#endif
}
