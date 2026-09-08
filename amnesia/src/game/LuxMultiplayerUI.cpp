/* Multiplayer session controls. Distributed under the GPLv3 or later. */
#include "LuxMultiplayerUI.h"
#include "LuxMultiplayer.h"
#include "LuxBase.h"
#include "LuxInputHandler.h"

#if USE_SDL2
#include "impl/LowLevelGraphicsSDL.h"
#include "impl/LowLevelInputSDL.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#endif

#include <cstring>

cLuxMultiplayerUI::cLuxMultiplayerUI(cLuxMultiplayer* apMultiplayer)
    : mpMultiplayer(apMultiplayer), mpContext(NULL), mpWindow(NULL),
      mbVisible(false), mbCampaign(false), mbFocusWindow(false),
      mbRestoreRelativeMouse(false), mbRestoreWindowGrab(false),
      mlRestoreCursor(0), mlPreviousInputState(0), mlPendingAction(0),
      mlPort(27015), mlMaxPlayers(4), mbAllowClientMapChanges(false),
      mbAllPlayersTriggerScripts(true), mbUseSteam(true), mbPublicLobby(false),
      mbSearchedSteamLobbies(false), mlSelectedSteamLobby(0), mbRestoreGuiMouse(false)
{
    msMap[0] = msStartPos[0] = msLobbyCode[0] = '\0';
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
    SetVisible(false);
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

void cLuxMultiplayerUI::SetVisible(bool abVisible)
{
    if(abVisible == mbVisible) return;
#if USE_SDL2
    if(abVisible)
    {
        if(!Initialize()) return;
        mbRestoreRelativeMouse = SDL_GetRelativeMouseMode() == SDL_TRUE;
        mbRestoreWindowGrab = SDL_GetWindowGrab(mpWindow) == SDL_TRUE;
        mlRestoreCursor = SDL_ShowCursor(SDL_QUERY);
        mlPreviousInputState = gpBase->mpInputHandler->GetState();
        mbFocusWindow = true;
        CaptureGuiMouse();
    }
    else if(mpWindow)
    {
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
    SetVisible(true);
}

void cLuxMultiplayerUI::Toggle()
{
    if(mbVisible) SetVisible(false);
    else Show(false);
}

void cLuxMultiplayerUI::Update(float afTimeStep)
{
    const int lAction = mlPendingAction;
    mlPendingAction = 0;
    if(lAction == 1)
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
            settings.useSteam = mbUseSteam;
            settings.publicLobby = mbPublicLobby;
        }
        mpMultiplayer->Host(settings);
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

#if USE_SDL2
    if(lAction == 9 && mpMultiplayer->GetSteamLobbyID())
        SDL_SetClipboardText(std::to_string(mpMultiplayer->GetSteamLobbyID()).c_str());
    if(mbVisible)
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
    if(pUI->mpMultiplayer->IsSteamOverlayActive()) return;
    // The physical grave key also works when Shift produces '~'. Never repeat.
    if(aEvent.type == SDL_KEYDOWN && aEvent.key.keysym.scancode == SDL_SCANCODE_GRAVE && !aEvent.key.repeat)
    {
        pUI->Toggle();
        return;
    }
    if(pUI->mpContext)
    {
        ImGui::SetCurrentContext(pUI->mpContext);
        if(aEvent.type == SDL_TEXTINPUT &&
           (std::strcmp(aEvent.text.text, "~") == 0 || std::strcmp(aEvent.text.text, "`") == 0)) return;
        // Keep key/button releases flowing to both input systems, avoiding stuck keys.
        ImGui_ImplSDL2_ProcessEvent(&aEvent);
        if(pUI->mbVisible && aEvent.type == SDL_KEYDOWN && aEvent.key.keysym.sym == SDLK_ESCAPE)
            pUI->SetVisible(false);
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
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    if(mpMultiplayer->IsSteamOverlayActive())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    ImGui::NewFrame();
    if(mbVisible) DrawControls();
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
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const float fWidth = io.DisplaySize.x < 574 ? io.DisplaySize.x - 24 : 550;
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 120), ImVec2(fWidth, io.DisplaySize.y - 24));
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
                        ImGui::InputTextWithHint("Map", "Blank starts the campaign", msMap, sizeof(msMap));
                        ImGui::InputTextWithHint("Start position", "Map default", msStartPos, sizeof(msStartPos));
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
                        ImGui::Checkbox("Every player can trigger Player scripts", &mbAllPlayersTriggerScripts);
                        ImGui::TextWrapped(mbAllPlayersTriggerScripts ?
                            "Player triggers accept any connected player." :
                            "Only the host activates Player-specific script triggers.");
                    }
                    ImGui::BeginDisabled(bSteam && !mpMultiplayer->IsSteamAvailable());
                    if(ImGui::Button(mbCampaign ? "Host campaign" : "Host session")) mlPendingAction = 1;
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
        if(ImGui::Button("Close")) bOpen = false;
        ImGui::SameLine();
        ImGui::TextDisabled("~ or Escape closes this window");
    }
    ImGui::End();
    if(!bOpen) SetVisible(false);
#endif
}
