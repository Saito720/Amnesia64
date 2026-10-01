/* Multiplayer session controls. Distributed under the GPLv3 or later. */
#ifndef LUX_MULTIPLAYER_UI_H
#define LUX_MULTIPLAYER_UI_H

#include "LuxTypes.h"
#include "LuxMultiplayerCache.h"
#include "LuxMultiplayerChatEmoji.h"
#include <cstdint>
#include <utility>
#include <vector>

class cLuxMultiplayer;
class cLuxMultiplayerChatEmoji;
struct ImGuiContext;
struct ImFont;
struct ImGuiInputTextCallbackData;
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
    bool IsCapturingInput() const { return mbVisible || IsChatCapturingInput(); }
    bool IsChatOpen() const { return mbChatOpen; }
    bool IsChatCapturingInput() const { return mbChatOpen || mbChatEventCaptured; }
    void CloseChat();

private:
    bool Initialize();
    void EnsureChatFont(float requestedSize,float framebufferDensity);
    void SetVisible(bool abVisible);
    void CaptureGuiMouse();
    void RestoreGuiMouse();
    void DrawControls();
    bool CanOpenChat() const;
    void OpenChat();
    void DrawChat();
    void OpenChatEmojiPicker();
    void CloseChatEmojiPicker();
    void DrawChatEmojiPicker(float fontSize);
    static int ChatInputCallback(ImGuiInputTextCallbackData* data);
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
    bool mbChatOpen=false;
    bool mbChatFocusInput=false;
    bool mbChatEventCaptured=false;
    bool mbSuppressChatOpeningText=false;
    bool mbChatSubmit=false;
    bool mbChatRestoreRelativeMouse=false;
    bool mbChatRestoreWindowGrab=false;
    bool mbChatRestoreTextInput=false;
    int mlChatRestoreCursor=0;
    int mlChatPreviousInputState=0;
    ImFont* mpChatFont=NULL;
    float mfChatRequestedFontSize=18, mfChatFontSize=0, mfChatFontDensity=0, mfChatRasterSize=0;
    unsigned mlChatFontAtlasGeneration=0;
    cLuxMultiplayerChatEmoji* mpChatEmoji=NULL;
    bool mbChatEmojiPickerOpen=false, mbChatEmojiPickerFocus=false;
    bool mbChatRestoreSelection=false;
    int mlChatCursor=0, mlChatSelectionStart=0, mlChatSelectionEnd=0;
    bool mbChatMouseSelecting=false, mbChatMouseInput=false;
    int mlChatMouseAnchor=0;
    float mfChatEntryScroll=0, mfChatEntryFontSize=18;
    luxchat::RichTextLayout mChatEntryRich;
    tString msChatPendingInsert, msChatPickerLastSearch;
    char msChatEmojiSearch[128]={};
    std::vector<std::pair<tString,tString>> mvChatPickerMatches;
    char msChatInput[513]={};
    tString msChatError;
    float mfChatErrorTime=0;
    // Last rendered geometry, also used by the live resize regressions.
    cVector2f mvChatHistoryPos=0, mvChatHistorySize=0;
    cVector2f mvChatEntryPos=0, mvChatEntrySize=0;
    cVector2f mvChatTextInputPos=0, mvChatTextInputSize=0;
    cVector2f mvChatEntryTextPos=0;
    cVector2f mvChatEmojiButtonPos=0, mvChatEmojiButtonSize=0;
    cVector2f mvChatPickerPos=0, mvChatPickerSize=0;
    cVector2f mvChatPickerFirstEmojiPos=0, mvChatPickerFirstEmojiSize=0;
    tString msChatPickerFirstUnicode;
    float mfChatHistoryAlpha=0;
    float mfChatHistoryScroll=0, mfChatHistoryContentHeight=0, mfChatHistoryViewportHeight=0;
    struct ChatHistoryStyle {size_t nameBytes;uint32_t nameColor,textColor;};
    std::vector<ChatHistoryStyle> mvChatHistoryStyles;
    size_t mlChatVisibleMessages=0;
    size_t mlChatVisibleEmoji=0, mlChatEntryEmoji=0;
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
