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
class cLuxMultiplayerUI : public iViewportCallback
{
public:
    explicit cLuxMultiplayerUI(cLuxMultiplayer* apMultiplayer);
    ~cLuxMultiplayerUI();

    void Update(float afTimeStep);
    void Draw();
    void DrawMenuBackdrop();
    void OnPreWorldDraw() override {}
    void OnPostWorldDraw() override { DrawMenuBackdrop(); }
    bool IsChatHistoryInMenuBackdrop() const { return mbChatHistoryInMenuBackdrop; }
    int GetChatMenuBlurPasses() const { return mlChatMenuBlurPasses; }
    float GetChatMenuBlurAmount() const { return mfChatMenuBlurAmount; }
    cVector2l GetChatMenuBackdropSize() const { return mvChatMenuBackdropSize; }
    cVector2l GetChatMenuBlurSize() const { return mvChatMenuBlurSize; }
    size_t GetChatFinalHistoryCount() const { return mlChatFinalHistoryCount; }
    void Show(bool abCampaign = false);
    void Toggle();
    bool IsVisible() const { return mbVisible; }
    bool IsCapturingInput() const { return mbVisible || IsChatCapturingInput(); }
    bool IsChatOpen() const { return mbChatOpen; }
    bool IsChatCapturingInput() const { return mbChatOpen || mbChatEventCaptured; }
    void CloseChat();

private:
    bool Initialize();
    bool BeginDrawFrame();
    bool IsNativeChatMenu() const;
    float GetNativeChatMenuBlurAmount() const;
    bool EnsureMenuBackdrop();
    void DestroyMenuBackdrop();
    void BlurMenuBackdrop(float amount);
    void EnsureChatFont(float requestedSize,float framebufferDensity);
    void SetVisible(bool abVisible);
    void CaptureGuiMouse();
    void RestoreGuiMouse();
    void DrawControls();
    bool CanShowChatHistory() const;
    bool CanOpenChat() const;
    void OpenChat();
    void DrawChat(bool drawHistory=true);
    void OpenChatEmojiPicker();
    void CloseChatEmojiPicker();
    void UpdateChatPickerMatches();
    void CloseChatPickerToneMenu();
    void DrawChatPickerToneMenu(float fontSize);
    void DrawChatEmojiPicker(float fontSize);
    void UpdateChatCompletion(const tString& text,int cursor,int selectionStart,int selectionEnd);
    void ClearChatCompletionDismissalIfChanged(const tString& text,int cursor,int selectionStart,int selectionEnd);
    const std::vector<cLuxMultiplayerChatEmoji::CompletionSuggestion>& GetChatCompletionMatches(const tString& query);
    void CloseChatCompletion(bool dismiss=false);
    bool AcceptChatCompletion(ImGuiInputTextCallbackData* data,int index);
    void DrawChatCompletion(float fontSize);
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
    bool mbDrawFrameStarted=false, mbChatHistoryInMenuBackdrop=false;
    int mlChatMenuBlurPasses=0;
    int mlChatMenuBackdropRenderFrame=-1;
    float mfChatMenuBlurAmount=0;
    size_t mlChatFinalHistoryCount=0;
    cVector2l mvChatMenuBackdropSize=0;
    cVector2l mvChatMenuBlurSize=0;
    iTexture* mpChatMenuSource=NULL;
    iTexture* mpChatMenuBlurTexture[2]={};
    iFrameBuffer* mpChatMenuBlurBuffer[2]={};
    iGpuProgram* mpChatMenuBlurProgram[2]={};
    unsigned mlChatMenuBlurProgramHandle[2]={};
    std::vector<cViewport*> mvChatMenuViewports;
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
    std::vector<size_t> mvChatPickerEntryIndices;
    std::vector<int> mvChatPickerDisplayGlyphs;
    int mlChatPickerCategory=0, mlChatPickerBuiltCategory=-1;
    int mlChatPickerTone=0, mlChatPickerBuiltTone=-1;
    unsigned mlChatPickerTonePopupID=0;
    bool mbChatPickerToneOpen=false;
    int mlChatPickerHoveredEntry=-1, mlChatPickerSelectedEntry=-1, mlChatPickerFirstEntry=-1;
    bool mbChatPickerSearchResults=false, mbChatPickerResetScroll=false;
    tString msChatPickerHeader;
    float mfChatPickerGridScroll=0;
    bool mbChatCompletionOpen=false, mbChatCompletionAcceptKey=false;
    bool mbChatCompletionEnterHeld=false, mbChatCompletionTabHeld=false;
    int mlChatCompletionSelected=0, mlChatCompletionMove=0, mlChatCompletionMouse=-1;
    luxchat::EmojiCompletionToken mChatCompletionToken;
    std::vector<cLuxMultiplayerChatEmoji::CompletionSuggestion> mvChatCompletionMatches;
    std::vector<cLuxMultiplayerChatEmoji::CompletionSuggestion> mvChatCompletionCachedMatches;
    tString msChatCompletionMatchQuery;
    int mlChatCompletionMatchTone=-1;
    tString msChatCompletionText, msChatCompletionDismissText;
    int mlChatCompletionDismissCursor=-1, mlChatCompletionDismissStart=-1, mlChatCompletionDismissEnd=-1;
    float mfChatCompletionScroll=0;
    int mlChatCompletionFirstRow=0, mlChatCompletionVisibleRows=0, mlChatCompletionLastVisibleRows=0;
    bool mbChatCompletionFollowSelection=false;
    cVector2f mvChatCompletionPos=0, mvChatCompletionSize=0;
    cVector2f mvChatCompletionRowPos[8]={}, mvChatCompletionRowSize[8]={};
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
    cVector2f mvChatPickerSearchPos=0, mvChatPickerSearchSize=0;
    cVector2f mvChatPickerToneButtonPos=0, mvChatPickerToneButtonSize=0;
    cVector2f mvChatPickerTonePopupPos=0, mvChatPickerTonePopupSize=0;
    cVector2f mvChatPickerToneOptionPos[6]={}, mvChatPickerToneOptionSize[6]={};
    cVector2f mvChatPickerCategoryPos[8]={}, mvChatPickerCategorySize[8]={};
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
