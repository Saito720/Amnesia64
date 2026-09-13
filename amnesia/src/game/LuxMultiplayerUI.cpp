/* Multiplayer session controls. Distributed under the GPLv3 or later. */
#include "LuxMultiplayerUI.h"
#include "LuxMultiplayer.h"
#include "LuxBase.h"
#include "LuxInputHandler.h"
#include "LuxMultiplayerContent.h"

#if USE_SDL2
#include "impl/LowLevelGraphicsSDL.h"
#include "impl/LowLevelInputSDL.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#endif

#include <algorithm>
#include <cstring>

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

void cLuxMultiplayerUI::Update(float afTimeStep)
{
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
    const ImVec2 display=ImGui::GetIO().DisplaySize;
    const cVector2f displaySize(display.x,display.y);
    mbDisplaySizeChanged=displaySize!=mvLastDisplaySize;
    mvLastDisplaySize=displaySize;
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
