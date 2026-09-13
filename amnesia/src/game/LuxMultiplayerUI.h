/* Multiplayer session controls. Distributed under the GPLv3 or later. */
#ifndef LUX_MULTIPLAYER_UI_H
#define LUX_MULTIPLAYER_UI_H

#include "LuxTypes.h"
#include "LuxMultiplayerCache.h"
#include <cstdint>
#include <vector>

class cLuxMultiplayer;
struct ImGuiContext;
struct SDL_Window;
union SDL_Event;

// Owns the overlay independently of the currently selected game container.
// Network and map operations are deferred from rendering to Update().
class cLuxMultiplayerUI
{
public:
    explicit cLuxMultiplayerUI(cLuxMultiplayer* apMultiplayer);
    ~cLuxMultiplayerUI();

    void Update(float afTimeStep);
    void Draw();
    void Show(bool abCampaign = false);
    void Toggle();
    bool IsVisible() const { return mbVisible; }
    bool IsCapturingInput() const { return mbVisible; }

private:
    bool Initialize();
    void SetVisible(bool abVisible);
    void CaptureGuiMouse();
    void RestoreGuiMouse();
    void DrawControls();
    void DrawSteamJoinControls();
    void OpenMapBrowser();
    void LoadMapBrowserDirectory(const tWString& directory);
    void QueueMapBrowserParent();
    void DrawMapBrowser();
    void UpdateStartPositions(float afTimeStep);
    void DrawStartPositions();
    static void DrawCallback(void* apUserData);
    static void EventCallback(void* apUserData, const SDL_Event& aEvent);

    cLuxMultiplayer* mpMultiplayer;
    ImGuiContext* mpContext;
    SDL_Window* mpWindow;
    bool mbVisible;
    bool mbCampaign;
    bool mbFocusWindow;
    cVector2f mvLastDisplaySize=0;
    bool mbDisplaySizeChanged=false;
    bool mbRestoreRelativeMouse;
    bool mbRestoreWindowGrab;
    int mlRestoreCursor;
    int mlPreviousInputState;
    int mlPendingAction;
    int mlPort;
    int mlMaxPlayers;
    bool mbAllowClientMapChanges;
    bool mbAllPlayersTriggerScripts;
    bool mbPlayerCollision;
    bool mbUseSteam;
    bool mbPublicLobby;
    bool mbSearchedSteamLobbies;
    uint64_t mlSelectedSteamLobby;
    bool mbRestoreGuiMouse;
    bool mbHostCurrentMap=false;
    bool mbCanHostCurrentMap=false;
    float mfCurrentMapRefresh=0;
    tString msCurrentMap, msCurrentMapReason;
    bool mbMapBrowserOpen=false;
    bool mbFocusMapBrowser=false;
    bool mbCloseMapBrowser=false;
    tWString msMapBrowserDirectory, msPendingMapBrowserDirectory, msSelectedMap;
    tString msMapBrowserError;
    tWStringList mlstMapBrowserFolders, mlstMapBrowserFiles;
    char msMapBrowserPath[1024];
    cLuxMultiplayerMapCacheStats mCacheStats;
    bool mbCacheSectionOpen=false;
    bool mbCacheStatsKnown=false;
    bool mbCacheStatsDirty=true;
    float mfCacheRefreshTime=0;
    tString msCapturedGuiSet;
    char msMap[512];
    char msStartPos[128];
    tString msStartPositionMap, msStartPositionError;
    std::vector<tString> mvStartPositions;
    float mfStartPositionDelay=0;
    bool mbStartPositionsDirty=true;
    char msAddress[256];
    char msLobbyCode[32];
};

#endif
