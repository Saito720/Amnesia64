#ifndef MULTIPLAYER_CHAT_REGRESSION_H
#define MULTIPLAYER_CHAT_REGRESSION_H

#include <cstring>
#include "imgui_internal.h"
#include "gui/GuiPopUpMessageBox.h"
#include "gui/WidgetImage.h"
#include "LuxMultiplayerChatEmoji.h"
#include "LuxMultiplayerChatLayout.h"
#include "LuxMessageHandler.h"
#include "impl/LowLevelGraphicsSDL.h"

// Every key and text edit enters through SDL's queue and the normal engine
// input/render loop. Both retail processes remain connected until all checks
// finish, so a local echo cannot masquerade as successful network delivery.
class cChatRegression {
    unsigned phase=0,initialPhase=0,resizeIndex=0,menuIndex=0;
    Uint32 entered=0,initialEntered=0;
    bool capturePending=false,captured=false,mapTransitionStarted=false,initialResumeRequested=false,finalCaptureInstalled=false;
    bool originalRelative=false,originalGrab=false,lanternActive=false;
    int originalCursor=SDL_DISABLE;
    size_t initialMessages=0;
    uint32_t epoch=0,sequence=0;
    cLuxMap* map=NULL;
    ImFont* originalChatFont=NULL;
    tString longDraft;
    cGuiSet* nativeSet=NULL;
    iWidget* nativeAttention=NULL;
    iWidget* nativeFocus=NULL;
    cGuiPopUpMessageBox* nativePopup=NULL;
    cWidgetImage* nativeHistoryProbe=NULL;
    cGuiGfxElement* nativeHistoryProbeGfx=NULL;
    cVector2l nativeHistoryProbePixel=0;
    tString finalCaptureError;
    bool savedEffectPause=false,savedPlayerActive=true,savedFadeActive=false;
    float savedFadeAlpha=0,savedFadeGoal=0,savedFadeSpeed=0;
    const bool focused=std::getenv("CODEX_MP_CHAT")!=NULL;
    Uint32 transitionNotice=0;
    static const cVector2l* sizes() {
        static const cVector2l values[]={cVector2l(640,480),cVector2l(997,613),
            cVector2l(338,1000),cVector2l(1920,540),cVector2l(3840,2160)};
        return values;
    }
    static const cVector2l* fadeSizes() {
        static const cVector2l values[]={cVector2l(320,240),cVector2l(640,480),cVector2l(997,613),
            cVector2l(338,1000),cVector2l(1920,540),cVector2l(3840,2160)};
        return values;
    }
    static tString message(const tString& sender) {
        return "Chat "+sender+": caf\xC3\xA9 e\xCC\x81 \xCE\xA9 \xF0\x9F\x98\x80 "
            "\xE2\x9D\xA4\xEF\xB8\x8F \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD "
            "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 "
            "\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD\xE2\x80\x8D\xF0\x9F\x92\xBB";
    }
    static tString aliasDraft(const tString& sender) {
        return "Aliases "+sender+": :grinning: :heart: :thumbsup_tone3: :flag_us: :codex_missing_alias: :incomplete";
    }
    static tString aliasMessage(const tString& sender) {
        return "Aliases "+sender+": \xF0\x9F\x98\x80 \xE2\x9D\xA4\xEF\xB8\x8F "
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD \xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 :codex_missing_alias: :incomplete";
    }
    static void key(SDL_Keycode value,bool down,Uint8 repeat=0) {
        SDL_Event event={};event.type=down?SDL_KEYDOWN:SDL_KEYUP;
        event.key.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
        event.key.state=down?SDL_PRESSED:SDL_RELEASED;event.key.repeat=repeat;
        event.key.keysym.sym=value;event.key.keysym.scancode=SDL_GetScancodeFromKey(value);
        SDL_PushEvent(&event);
    }
    static void press(SDL_Keycode value) {key(value,true);key(value,false);}
    static void text(const tString& value) {
        size_t position=0;
        while(position<value.size()) {
            size_t end=(std::min)(position+size_t(SDL_TEXTINPUTEVENT_TEXT_SIZE-1),value.size());
            while(end<value.size() && (static_cast<unsigned char>(value[end])&0xc0)==0x80) --end;
            SDL_Event event={};event.type=SDL_TEXTINPUT;
            event.text.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
            std::memcpy(event.text.text,value.data()+position,end-position);
            event.text.text[end-position]='\0';SDL_PushEvent(&event);position=end;
        }
    }
    static void open() {key(SDLK_t,true);text("t");key(SDLK_t,false);}
    static void focus(Uint8 kind) {
        SDL_Event event={};event.type=SDL_WINDOWEVENT;
        event.window.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());event.window.event=kind;
        SDL_PushEvent(&event);
    }
    static void mouse(bool down,const cVector2f& position,const cVector2f& size) {
        const int x=static_cast<int>(position.x+size.x*0.5f),y=static_cast<int>(position.y+size.y*0.5f);
        SDL_Event motion={};motion.type=SDL_MOUSEMOTION;motion.motion.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
        motion.motion.x=x;motion.motion.y=y;SDL_PushEvent(&motion);
        SDL_Event button={};button.type=down?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
        button.button.windowID=motion.motion.windowID;button.button.button=SDL_BUTTON_LEFT;
        button.button.state=down?SDL_PRESSED:SDL_RELEASED;button.button.x=x;button.button.y=y;SDL_PushEvent(&button);
    }
    static iWidget* find(iWidget* widget,eWidgetType type,const tWString& text) {
        if(!widget) return NULL;
        if(widget->GetType()==type && widget->GetText()==text) return widget;
        for(auto* child:widget->GetChildren()) if(auto* found=find(child,type,text)) return found;
        return NULL;
    }
    void next(unsigned value) {phase=value;entered=SDL_GetTicks();}
    static void FinalOverlay(void* data) {
        auto* fixture=static_cast<cChatRegression*>(data);
        cBitmap* menuBefore=NULL;
        if(fixture->capturePending && (fixture->phase==10 || fixture->phase==21 || fixture->phase==22)) {
            menuBefore=gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
            if(!menuBefore) fixture->finalCaptureError="native menu final-overlay baseline readback failed";
        }
        // Exercise the actual pre-swap callback exactly once. Read back only
        // after normal world/postfx/native GUI rendering and the production UI.
        gpBase->mpMultiplayer->mpUI->Draw();
        if(fixture->finalCaptureError.empty()) fixture->CaptureFinalFrame(fixture->finalCaptureError,menuBefore);
        if(menuBefore) hplDelete(menuBefore);
    }
    int fail(tString& error,const tString& message) const {
        error="chat phase "+cString::ToString(int(phase))+": "+message;return -1;
    }
    bool colorsValid(tString& error) const {
        const uint32_t hostColor=luxchat::DefaultNameColor;
        const uint32_t clientColor=luxchat::ChooseNameColor({hostColor});
        for(const auto& entry:gpBase->mpMultiplayer->GetChatMessages()) {
            const uint32_t expected=entry.peer==0?hostColor:clientColor;
            if(!luxchat::ValidNameColor(entry.nameColor) || entry.nameColor!=expected || hostColor==clientColor) {
                error="reliable chat changed its host-assigned sender color for peer "+cString::ToString(int(entry.peer));return false;
            }
        }
        return true;
    }
    bool delivered(tString& error) const {
        const auto* session=gpBase->mpMultiplayer;
        const auto& messages=session->GetChatMessages();
        unsigned hostCount=0,clientCount=0;
        const uint32_t clientPeer=role=="client"?session->GetLocalPeerId():session->mPeers.begin()->first;
        for(const auto& entry:messages) {
            if(entry.text==message("host")) {
                ++hostCount;
                if(entry.peer!=0 || entry.name!=luxnet::ChatFallbackName(0)) {
                    error="host chat used an untrusted sender or label";return false;
                }
            }
            if(entry.text==message("client")) {
                ++clientCount;
                if(entry.peer!=clientPeer || entry.name!=luxnet::ChatFallbackName(clientPeer)) {
                    error="client chat used an untrusted sender or label";return false;
                }
            }
            if(!luxnet::ValidChatText(entry.text) || entry.name.empty()) {
                error="history contains malformed or unbounded text";return false;
            }
        }
        if(!colorsValid(error)) return false;
        if(hostCount>1 || clientCount>1) {error="chat was echoed or delivered more than once";return false;}
        return hostCount==1 && clientCount==1;
    }
    bool unchanged() const {
        return gpBase->mpMultiplayer->IsReady() && gpBase->mpMultiplayer->GetMapEpoch()==epoch &&
            gpBase->mpMapHandler->GetCurrentMap()==map;
    }
    bool gameplay() const {
        return gpBase->mpInputHandler->GetState()==eLuxInputState_Game &&
            gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()=="Default";
    }
    tString gateState() const {
        auto* gui=gpBase->mpEngine->GetGui()->GetFocusedSet();
        return "state="+cString::ToString(int(gpBase->mpInputHandler->GetState()))+
            " container="+gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()+
            " active="+cString::ToString(int(gpBase->mpPlayer->IsActive()))+
            " dead="+cString::ToString(int(gpBase->mpPlayer->IsDead()))+
            " enginePaused="+cString::ToString(int(gpBase->mpEngine->GetPaused()))+
            " popup="+cString::ToString(int(gui && gui->PopUpIsActive()))+
            " effectPaused="+cString::ToString(int(gpBase->mpEffectHandler->GetPlayerIsPaused()))+
            " messagePaused="+cString::ToString(int(gpBase->mpMessageHandler->IsPauseMessageActive()));
    }
    void tracePicker(const char* moment) const {
        auto* session=gpBase->mpMultiplayer;auto* ui=session->mpUI;
        ImGui::SetCurrentContext(ui->mpContext);
        const ImGuiContext* context=ImGui::GetCurrentContext();const ImGuiIO& io=ImGui::GetIO();
        SDL_Window* window=SDL_GL_GetCurrentWindow();
        const ImGuiWindow* entry=ImGui::FindWindowByName("##MultiplayerChat");
        std::fprintf(stderr,"%s chat picker %s phase=%u chat=%d picker=%d capture=%d ready=%d load=%d controls=%d overlay=%d gates=[%s] focusRequest=%d restore=%d activeID=%u hoveredID=%u hoveredWindow=%s navWindow=%s entryActive=%d entryHidden=%d mouse=%.1f,%.1f down=%d lastClicks=%u appFocusLost=%d keyboardFocus=%d mouseFocus=%d windowFlags=%u display=%.1fx%.1f button=%.1f,%.1f %.1fx%.1f\n",
            role.c_str(),moment,phase,int(ui->IsChatOpen()),int(ui->mbChatEmojiPickerOpen),int(ui->IsChatCapturingInput()),
            int(session->IsReady()),int(session->GetLoadPhase()),int(ui->IsVisible()),int(session->IsSteamOverlayActive()),
            gateState().c_str(),int(ui->mbChatFocusInput),int(ui->mbChatRestoreSelection),context->ActiveId,context->HoveredId,
            context->HoveredWindow?context->HoveredWindow->Name:"none",context->NavWindow?context->NavWindow->Name:"none",
            int(entry && entry->Active),int(entry && entry->Hidden),io.MousePos.x,io.MousePos.y,int(io.MouseDown[0]),
            unsigned(io.MouseClickedLastCount[0]),int(io.AppFocusLost),int(SDL_GetKeyboardFocus()==window),
            int(SDL_GetMouseFocus()==window),unsigned(SDL_GetWindowFlags(window)),io.DisplaySize.x,io.DisplaySize.y,
            ui->mvChatEmojiButtonPos.x,ui->mvChatEmojiButtonPos.y,ui->mvChatEmojiButtonSize.x,ui->mvChatEmojiButtonSize.y);
        std::fflush(stderr);
    }
    bool restored() const {
        auto* graphics=gpBase->mpEngine->GetGraphics()->GetLowLevel();
        return graphics->GetRelativeMouse()==originalRelative && graphics->GetWindowGrab()==originalGrab &&
            SDL_ShowCursor(SDL_QUERY)==originalCursor;
    }
public:
    void OpenForDisconnect() {open();}
    int Initial(tString& error) {
        if(initialPhase==2) return 1;
        if(!initialPhase) {open();initialEntered=SDL_GetTicks();initialPhase=1;return 0;}
        if(SDL_GetTicks()-initialEntered<150) return 0;
        if(gpBase->mpMultiplayer->mpUI->IsChatOpen() || gpBase->mpMultiplayer->IsChatCapturingInput()) {
            error="chat opened without an active session in the initial main menu";return -1;
        }
        initialPhase=2;return 1;
    }
    int Update(tString& error) {
        if(!finalCaptureError.empty()) return fail(error,finalCaptureError);
        auto* session=gpBase->mpMultiplayer;auto* ui=session->mpUI;
        auto* updater=gpBase->mpEngine->GetUpdater();
        const Uint32 age=SDL_GetTicks()-entered;
        const Uint32 timeout=phase==54?20000:phase>=59 && phase<=61?60000:15000;
        if(phase && age>timeout) {
            if(phase==14 || phase==15 || phase==143 || phase==144) {
                const tString expected=phase==14 || phase==15?longDraft:aliasMessage(role);
                std::fprintf(stderr,"%s chat delivery timeout phase=%u localPeer=%u expected=%s history=%llu\n",
                    role.c_str(),phase,session->GetLocalPeerId(),expected.c_str(),
                    static_cast<unsigned long long>(session->GetChatMessages().size()));
                for(const auto& entry:session->GetChatMessages())
                    std::fprintf(stderr,"  peer=%u name=%s bytes=%llu text=%s\n",entry.peer,entry.name.c_str(),
                        static_cast<unsigned long long>(entry.text.size()),entry.text.c_str());
            }
            return fail(error,"operation did not finish: "+session->GetStatus());
        }
        if(phase && !(phase>=59 && phase<=61) && !unchanged()) return fail(error,"chat changed the session or loaded map");
        if(phase==0) {
            if(!session->IsReady()) return 0;
            if(session->IsWindowVisible()) session->ToggleWindow();
            if(!gameplay()) {
                if(!initialResumeRequested) {
                    initialResumeRequested=true;entered=SDL_GetTicks();
                    const eLuxInputState state=gpBase->mpInputHandler->GetState();
                    if(state==eLuxInputState_MainMenu) {
                        updater->SetContainer("MainMenu");gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_LastEnum);
                        gpBase->mpMainMenu->ExitMenu(eLuxMainMenuExit_ReturnToGame);
                    } else if(state==eLuxInputState_Inventory) gpBase->mpInventory->ExitPressed();
                    else if(state==eLuxInputState_Journal) gpBase->mpJournal->ExitPressed(true);
                    else return fail(error,"initial chat fixture is not in a resumable gameplay/menu state: "+gateState());
                }
                if(SDL_GetTicks()-entered>15000) return fail(error,"initial native menu did not resume gameplay: "+gateState());
                return 0;
            }
            gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_LastEnum);
            map=gpBase->mpMapHandler->GetCurrentMap();epoch=session->GetMapEpoch();
            initialMessages=session->GetChatMessages().size();sequence=session->GetWorld()->mlSequence;
            auto* graphics=gpBase->mpEngine->GetGraphics()->GetLowLevel();
            originalRelative=graphics->GetRelativeMouse();originalGrab=graphics->GetWindowGrab();
            originalCursor=SDL_ShowCursor(SDL_QUERY);next(1);return 0;
        }
        if(phase==1) {if(age<250) return 0;open();next(2);return 0;}
        if(phase==2) {
            if(age<200) return 0;
            if(!ui->IsChatOpen() || !session->IsChatCapturingInput() || !gameplay() || ui->msChatInput[0])
                return fail(error,"T did not focus an empty entry or its opening character leaked into the draft");
            ImGui::SetCurrentContext(ui->mpContext);
            if(!ui->mpChatFont || ui->mpChatFont==ImGui::GetIO().Fonts->Fonts[0])
                return fail(error,"bundled TTF was not loaded for chat");
            originalChatFont=ui->mpChatFont;
            if(!ui->mpChatEmoji || !ui->mpChatEmoji->IsReady() || ui->mpChatEmoji->GetGlyphCount()<3000)
                return fail(error,"bundled color emoji atlas was not loaded for chat");
            text(message(role));captured=false;next(3);return 0;
        }
        if(phase==3) {
            if(age<200) return 0;
            if(tString(ui->msChatInput)!=message(role)) return fail(error,"SDL text input lost Unicode or changed the draft");
            if(ui->mlChatEntryEmoji!=5) return fail(error,"single editable field did not compose skin-tone, flag or ZWJ sequences");
            if(!captured) {capturePending=true;return 0;}
            press(SDLK_RETURN);next(4);return 0;
        }
        if(phase==4) {
            if(age<300) return 0;
            if(ui->IsChatOpen() || !gameplay() || !restored()) return fail(error,"Enter failed to close entry and restore gameplay mouse state");
            mark(role+"-chat-sent.txt",message(role));next(5);return 0;
        }
        if(phase==5) {
            if(!exists("host-chat-sent.txt") || !exists("client-chat-sent.txt")) return 0;
            tString deliveryError;
            if(!delivered(deliveryError)) {if(!deliveryError.empty()) return fail(error,deliveryError);return 0;}
            if(session->GetChatMessages().size()!=initialMessages+2) return fail(error,"a send inserted extra history entries");
            if(age<1000) return 0; // Leave time for an unwanted reliable/local echo.
            open();next(6);return 0;
        }
        if(phase==6) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"entry could not reopen after sending");
            lanternActive=gpBase->mpPlayer->GetHelperLantern()->IsActive();
            key(SDLK_w,true);key(SDLK_TAB,true);key(SDLK_j,true);key(SDLK_f,true);key(SDLK_SPACE,true);
            SDL_Event mouse={};mouse.type=SDL_MOUSEBUTTONDOWN;mouse.button.button=SDL_BUTTON_LEFT;
            mouse.button.state=SDL_PRESSED;SDL_PushEvent(&mouse);
            next(7);return 0;
        }
        if(phase==7) {
            if(age<350) return 0;
            if(!ui->IsChatOpen() || !gameplay() || gpBase->mpPlayer->GetPressedMove() ||
               gpBase->mpPlayer->GetHelperLantern()->IsActive()!=lanternActive)
                return fail(error,"typing movement, inventory, journal or lantern keys leaked into gameplay");
            key(SDLK_w,false);key(SDLK_TAB,false);key(SDLK_j,false);key(SDLK_f,false);key(SDLK_SPACE,false);
            SDL_Event mouse={};mouse.type=SDL_MOUSEBUTTONUP;
            mouse.button.button=SDL_BUTTON_LEFT;mouse.button.state=SDL_RELEASED;SDL_PushEvent(&mouse);
            key(SDLK_ESCAPE,true);next(8);return 0;
        }
        if(phase==8) {
            if(age<200) return 0;
            if(ui->IsChatOpen() || !gameplay() || !restored()) return fail(error,"Escape cancelled chat and also opened pause or retained mouse capture");
            if(session->GetChatMessages().size()!=initialMessages+2) return fail(error,"cancelled draft was sent");
            key(SDLK_ESCAPE,false);next(81);return 0;
        }
        if(phase==81) {
            if(age<200) return 0;
            key(SDLK_ESCAPE,true);next(9);return 0;
        }
        if(phase==9) {
            if(updater->GetCurrentContainerName()!="MainMenu") return 0;
            key(SDLK_ESCAPE,false);
            if(ui->IsChatOpen()) return fail(error,"ordinary pause menu left a chat entry open");
            nativeSet=gpBase->mpMainMenu->GetSet();
            nativePopup=nativeSet->CreatePopUpMessageBox(_W("Chat modal regression"),
                _W("Keep this native question focused"),_W("OK"),_W("Cancel"),NULL,NULL);
            nativeAttention=nativeSet->GetAttentionWidget();
            if(!nativePopup || !nativeSet->PopUpIsActive() || !nativeAttention)
                return fail(error,"native modal fixture did not acquire attention");
            for(auto& entry:session->mChatMessages) entry.age=0;
            captured=false;capturePending=false;
            nativeFocus=nativeSet->GetFocusedWidget();open();next(10);return 0;
        }
        if(phase==10) {
            if(age<200) return 0;
            if(ui->IsChatOpen() || gpBase->mpInputHandler->GetState()!=eLuxInputState_MainMenu ||
               nativeSet->GetAttentionWidget()!=nativeAttention || nativeSet->GetFocusedWidget()!=nativeFocus)
                return fail(error,"T displaced native pause-menu input or focus");
            if(!captured) {capturePending=true;return 0;}
            nativeSet->DestroyPopUp(nativePopup);nativePopup=NULL;
            next(101);return 0;
        }
        if(phase==101) {
            // Native popup deletion is deferred to the GUI update. Let it
            // release attention while this GUI set still receives updates.
            if(age<150 || nativeSet->PopUpIsActive()) return 0;
            gpBase->mpMainMenu->ExitMenu(eLuxMainMenuExit_ReturnToGame);next(102);return 0;
        }
        if(phase==102) {
            if(age<150 || !gameplay()) return 0;
            open();next(11);return 0;
        }
        if(phase==11) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"entry failed to reopen after pause: "+gateState());
            tString oversized="Bounded ";for(unsigned i=0;i<250;++i) oversized+="\xF0\x9F\x98\x80";
            text(oversized);next(12);return 0;
        }
        if(phase==12) {
            if(age<250) return 0;
            longDraft=ui->msChatInput;
            if(longDraft.size()>luxnet::MaxChatTextBytes || longDraft.size()<400 || !luxnet::ValidChatText(longDraft))
                return fail(error,"overlong Unicode typing exceeded the entry bound or split a UTF-8 character");
            resizeIndex=0;captured=false;capturePending=false;
            SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),sizes()[0].x,sizes()[0].y);next(13);return 0;
        }
        if(phase==13) {
            if(gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt()!=sizes()[resizeIndex] || age<150) return 0;
            if(!ui->IsChatOpen() || tString(ui->msChatInput)!=longDraft || !session->IsChatCapturingInput())
                return fail(error,"window resize lost draft text or chat focus");
            if(!captured) {capturePending=true;return 0;}
            if(++resizeIndex<5) {
                captured=false;SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),sizes()[resizeIndex].x,sizes()[resizeIndex].y);
                next(13);return 0;
            }
            SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),800,600);press(SDLK_RETURN);next(14);return 0;
        }
        if(phase==14) {
            if(age<400) return 0;
            if(ui->IsChatOpen()) return fail(error,"bounded message did not submit after resizing");
            const uint32_t local=session->GetLocalPeerId();unsigned count=0;
            for(const auto& entry:session->GetChatMessages()) if(entry.peer==local && entry.text==longDraft) {
                ++count;
                if(entry.name!=luxnet::ChatFallbackName(local)) {
                    std::fprintf(stderr,"Bounded chat label mismatch peer=%u name=%s text=%s\n",entry.peer,entry.name.c_str(),entry.text.c_str());
                    return fail(error,"bounded Unicode message used an untrusted sender label");
                }
            }
            if(count>1) return fail(error,"bounded Unicode message was echoed more than once: "+cString::ToString(int(count)));
            if(!count) return 0;
            mark(role+"-chat-resize-passed.txt","PASS: active entry and wrapped history fit 640x480, odd, portrait, wide and 4K windows; native font size and Unicode draft/capture preserved.");
            next(15);return 0;
        }
        if(phase==15) {
            if(!exists("host-chat-resize-passed.txt") || !exists("client-chat-resize-passed.txt") || age<500) return 0;
            if(session->GetChatMessages().size()<initialMessages+4) return 0;
            if(session->GetChatMessages().size()!=initialMessages+4) return fail(error,"bounded host/client messages did not arrive exactly once");
            tString colorError;if(!colorsValid(colorError)) return fail(error,colorError);
            open();next(141);return 0;
        }
        if(phase==141) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"entry did not reopen for Discord aliases");
            text(aliasDraft(role));next(142);return 0;
        }
        if(phase==142) {
            if(age<200) return 0;
            if(tString(ui->msChatInput)!=aliasDraft(role)) return fail(error,"typing aliases changed raw draft text");
            press(SDLK_RETURN);next(143);return 0;
        }
        if(phase==143) {
            if(age<400) return 0;
            if(ui->IsChatOpen()) return fail(error,"Discord alias message failed to submit");
            unsigned count=0;
            for(const auto& entry:session->GetChatMessages()) if(entry.peer==session->GetLocalPeerId() && entry.text==aliasMessage(role)) {
                ++count;
                if(entry.name!=luxnet::ChatFallbackName(entry.peer)) {
                    std::fprintf(stderr,"Canonical alias label mismatch peer=%u name=%s text=%s\n",entry.peer,entry.name.c_str(),entry.text.c_str());
                    return fail(error,"canonical alias message used an untrusted sender label");
                }
            }
            if(count>1) return fail(error,"canonical alias message was delivered more than once: "+cString::ToString(int(count)));
            if(!count) return 0;
            mark(role+"-chat-aliases-passed.txt",aliasMessage(role));next(144);return 0;
        }
        if(phase==144) {
            if(!exists("host-chat-aliases-passed.txt") || !exists("client-chat-aliases-passed.txt") || age<500) return 0;
            if(session->GetChatMessages().size()<initialMessages+6) return 0;
            if(session->GetChatMessages().size()!=initialMessages+6) return fail(error,"canonical alias messages did not arrive exactly once on both peers");
            tString colorError;if(!colorsValid(colorError)) return fail(error,colorError);
            unsigned hostAliases=0,clientAliases=0;
            for(const auto& entry:session->GetChatMessages()) {
                if(entry.peer==0 && entry.text==aliasMessage("host")) ++hostAliases;
                if(entry.peer!=0 && entry.text==aliasMessage("client")) ++clientAliases;
            }
            if(hostAliases!=1 || clientAliases!=1) return fail(error,"remote chat did not receive canonical emoji alias text");
            resizeIndex=0;captured=false;capturePending=false;
            SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),sizes()[0].x,sizes()[0].y);next(18);return 0;
        }
        if(phase==18) {
            if(gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt()!=sizes()[resizeIndex] || age<150) return 0;
            if(ui->IsChatOpen() || session->IsChatCapturingInput()) return fail(error,"passive resized history captured gameplay");
            if(!captured) {capturePending=true;return 0;}
            if(++resizeIndex<5) {
                captured=false;SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),sizes()[resizeIndex].x,sizes()[resizeIndex].y);
                next(18);return 0;
            }
            auto* fade=gpBase->mpEffectHandler->GetFade();
            savedEffectPause=gpBase->mpEffectHandler->GetPlayerIsPaused();savedPlayerActive=gpBase->mpPlayer->IsActive();
            savedFadeActive=fade->IsActive();savedFadeAlpha=fade->mfAlpha;savedFadeGoal=fade->mfGoalAlpha;savedFadeSpeed=fade->mfFadeSpeed;
            gpBase->mpEffectHandler->SetPlayerIsPaused(true);fade->FadeOut(0);
            for(auto& entry:session->mChatMessages) entry.age=0;
            resizeIndex=0;captured=false;SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),fadeSizes()[0].x,fadeSizes()[0].y);
            next(181);return 0;
        }
        if(phase==181) {
            if(gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt()!=fadeSizes()[resizeIndex] || age<150) return 0;
            if(ui->IsChatOpen() || session->IsChatCapturingInput() || gpBase->mpPlayer->IsActive() ||
               !gpBase->mpEffectHandler->GetPlayerIsPaused() || gpBase->mpEffectHandler->GetFade()->mfAlpha!=1)
                return fail(error,"opaque scripted fade fixture did not keep chat passive while player input is paused");
            if(!captured) {capturePending=true;return 0;}
            if(++resizeIndex<6) {
                captured=false;SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),fadeSizes()[resizeIndex].x,fadeSizes()[resizeIndex].y);
                next(181);return 0;
            }
            auto* fade=gpBase->mpEffectHandler->GetFade();
            fade->mfAlpha=savedFadeAlpha;fade->mfGoalAlpha=savedFadeGoal;fade->mfFadeSpeed=savedFadeSpeed;fade->SetActive(savedFadeActive);
            gpBase->mpEffectHandler->SetPlayerIsPaused(savedEffectPause);gpBase->mpPlayer->SetActive(savedPlayerActive);
            mark(role+"-chat-overlay-fade-passed.txt","PASS: final presented pixels keep passive chat text/emoji above a fully opaque native fade at all resized viewports while scripted pause disables player input.");
            SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),800,600);
            // Set only presentation age, preserving the real delivered contents.
            for(auto& entry:session->mChatMessages) entry.age=9;
            capturePending=true;captured=false;next(16);return 0;
        }
        if(phase==16) {
            if(!captured) return 0;
            if(ui->mfChatHistoryAlpha<=0 || ui->mfChatHistoryAlpha>=1 || !ui->mlChatVisibleMessages)
                return fail(error,"old history did not fade smoothly");
            for(auto& entry:session->mChatMessages) entry.age=11;
            capturePending=true;captured=false;next(17);return 0;
        }
        if(phase==17) {
            if(!captured) return 0;
            if(ui->mfChatHistoryAlpha!=0 || ui->mlChatVisibleMessages || session->GetChatMessages().size()!=initialMessages+6)
                return fail(error,"expired history remained visible or fading deleted session history");
            open();next(20);return 0;
        }
        if(phase==20) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"chat could not open for a native GUI transition");
            static const char* containers[]={"MainMenu","Inventory","Journal"};
            if(menuIndex==2) gpBase->mpJournal->SetOpenedFromInventory(false);
            for(auto& entry:session->mChatMessages) entry.age=0;
            captured=false;capturePending=false;
            updater->SetContainer(containers[menuIndex]);next(21);return 0;
        }
        if(phase==21) {
            if(age<250) return 0;
            static const eLuxInputState states[]={eLuxInputState_MainMenu,eLuxInputState_Inventory,eLuxInputState_Journal};
            nativeSet=menuIndex==0?gpBase->mpMainMenu->GetSet():menuIndex==1?gpBase->mpInventory->GetSet():gpBase->mpJournal->GetSet();
            if(ui->IsChatOpen() || ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen || ui->mbChatCompletionOpen ||
               session->IsChatCapturingInput() || gpBase->mpInputHandler->GetState()!=states[menuIndex] || !nativeSet->IsActive())
                return fail(error,"native pause/inventory/journal transition retained chat capture");
            if(!nativeHistoryProbe) {
                const cVector2l screen=gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt();
                const cVector2f extent=nativeSet->GetVirtualSize(),offset=nativeSet->GetVirtualSizeOffset();
                const cVector2f pixel(static_cast<float>(static_cast<int>(ui->mvChatHistoryPos.x)+16),
                    static_cast<float>(static_cast<int>(ui->mvChatHistoryPos.y+ui->mvChatHistorySize.y)-28));
                const cVector2f position(pixel.x*extent.x/screen.x-offset.x,pixel.y*extent.y/screen.y-offset.y);
                const cVector2f size(32*extent.x/screen.x,20*extent.y/screen.y);
                nativeHistoryProbeGfx=gpBase->mpEngine->GetGui()->CreateGfxFilledRect(cColor(0.9f,0.1f,0.6f,1),eGuiMaterial_Diffuse,false);
                nativeHistoryProbe=nativeSet->CreateWidgetImage("",cVector3f(position.x,position.y,100),size,eGuiMaterial_Diffuse);
                if(!nativeHistoryProbeGfx || !nativeHistoryProbe) return fail(error,"native GUI occlusion probe creation failed");
                nativeHistoryProbe->SetImage(nativeHistoryProbeGfx);
                nativeHistoryProbePixel=cVector2l(static_cast<int>(pixel.x)+16,static_cast<int>(pixel.y)+10);
                next(21);return 0;
            }
            if(!captured) {capturePending=true;return 0;}
            nativeAttention=nativeSet->GetAttentionWidget();nativeFocus=nativeSet->GetFocusedWidget();
            nativeHistoryProbe->SetImage(NULL);nativeSet->DestroyWidget(nativeHistoryProbe);nativeHistoryProbe=NULL;
            hplDelete(nativeHistoryProbeGfx);nativeHistoryProbeGfx=NULL;
            captured=false;capturePending=false;
            open();next(22);return 0;
        }
        if(phase==22) {
            if(age<200) return 0;
            if(ui->IsChatOpen() || !ui->mlChatVisibleMessages || ui->mfChatHistoryAlpha!=1 ||
               nativeSet->GetAttentionWidget()!=nativeAttention || nativeSet->GetFocusedWidget()!=nativeFocus)
                return fail(error,"T opened over or stole focus from pause/inventory/journal");
            if(!captured) {capturePending=true;return 0;}
            for(auto& entry:session->mChatMessages) entry.age=9;
            next(222);return 0;
        }
        if(phase==222) {
            if(age<100) return 0;
            if(!ui->IsChatHistoryInMenuBackdrop() || ui->GetChatFinalHistoryCount()!=0 || !ui->mlChatVisibleMessages ||
               ui->mfChatHistoryAlpha<=0 || ui->mfChatHistoryAlpha>=1)
                return fail(error,"blurred native-menu history did not continue its own timed fade");
            for(auto& entry:session->mChatMessages) entry.age=11;
            next(223);return 0;
        }
        if(phase==223) {
            if(age<100) return 0;
            if(!ui->IsChatHistoryInMenuBackdrop() || ui->GetChatFinalHistoryCount()!=0 || ui->mlChatVisibleMessages ||
               ui->mfChatHistoryAlpha!=0 || session->GetChatMessages().size()!=initialMessages+6)
                return fail(error,"expired blurred-menu history left stale visible messages or erased retained history");
            for(auto& entry:session->mChatMessages) entry.age=0;
            if(menuIndex==0) gpBase->mpMainMenu->ExitMenu(eLuxMainMenuExit_ReturnToGame);
            else if(menuIndex==1) gpBase->mpInventory->ExitPressed();
            else gpBase->mpJournal->ExitPressed(true);
            next(221);return 0;
        }
        if(phase==221) {
            if(age<150 || !gameplay()) return 0;
            if(ui->IsChatHistoryInMenuBackdrop() || !ui->GetChatFinalHistoryCount() || ui->IsChatOpen())
                return fail(error,"leaving a native menu did not restore recent history to the sharp passive gameplay overlay");
            if(++menuIndex<3) {open();next(20);return 0;}
            mark(role+"-chat-overlay-menus-passed.txt","PASS: native pause, inventory and journal blur passive history with the game backdrop beneath native GUI, with no sharp final-overlay duplicate and unchanged input/focus ownership.");
            open();next(30);return 0;
        }
        if(phase==30) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"entry did not open for focus-loss test");
            focus(SDL_WINDOWEVENT_FOCUS_LOST);next(31);return 0;
        }
        if(phase==31) {
            if(age<200) return 0;
            if(ui->IsChatOpen() || session->IsChatCapturingInput()) return fail(error,"focus loss retained a draft or input capture");
            focus(SDL_WINDOWEVENT_FOCUS_GAINED);open();next(32);return 0;
        }
        if(phase==32) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"chat failed to reopen after focus returned");
            press(SDLK_BACKQUOTE);text("`~");next(33);return 0;
        }
        if(phase==33) {
            if(age<200) return 0;
            if(!ui->IsChatOpen() || session->IsWindowVisible() || tString(ui->msChatInput)!="`~")
                return fail(error,"typing grave/tilde toggled multiplayer controls or changed the literal draft");
            session->ShowWindow(false);next(331);return 0;
        }
        if(phase==331) {
            if(age<200) return 0;
            if(ui->IsChatOpen() || !session->IsWindowVisible()) return fail(error,"opening multiplayer controls retained chat capture");
            press(SDLK_ESCAPE);next(34);return 0;
        }
        if(phase==34) {
            if(age<200) return 0;
            if(ui->IsChatOpen() || session->IsWindowVisible() || !gameplay()) return fail(error,"closing multiplayer controls disrupted gameplay or reopened chat");
            if(session->GetWorld()->mlSequence<=sequence+20) return fail(error,"chat capture blocked network/world progression");
            const tString summary="PASS: real SDL T/text/Enter; reciprocal reliable delivery exactly once; trusted labels; Unicode bounds; Discord aliases; active-entry/passive-history resize; final-frame opaque native fade while player paused; history in native pause/inventory/journal/modal screens; independent timed fade; Escape; gameplay keys; focus, literal grave/tilde and explicit multiplayer-control transitions.";
            mark(role+"-chat-input-passed.txt",summary);
            if(!focused) mark(role+"-chat-passed.txt",summary);
            next(focused?50:40);return 0;
        }
        if(phase==50) {
            if(!exists("host-chat-input-passed.txt") || !exists("client-chat-input-passed.txt")) return 0;
            open();next(51);return 0;
        }
        if(phase==51) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"death fixture could not open the live editor");
            text("Death completion :gr");next(52);return 0;
        }
        if(phase==52) {
            if(age<200) return 0;
            if(!ui->mbChatCompletionOpen || ui->mvChatCompletionMatches.empty() ||
               ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen)
                return fail(error,"death fixture did not expose real SDL emoji completion choices");
            gpBase->mpPlayer->GetHelperDeath()->SetShowHint(false);gpBase->mpPlayer->SetHealth(0);
            next(54);return 0;
        }
        if(phase==54) {
            if(age<150) return 0;
            if(ui->IsChatOpen() || ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen || ui->mbChatCompletionOpen || !gameplay())
                return fail(error,"actual player death left editor or emoji completion capture active");
            gpBase->mpPlayer->GetHelperDeath()->OnPressButton();
            if(age<2000 || gpBase->mpPlayer->IsDead() || gpBase->mpPlayer->GetHelperDeath()->GetFadeAlpha()>0) return 0;
            mark(role+"-chat-death-passed.txt","PASS: actual native multiplayer death while SDL emoji completion is open closes editor/suggestion capture and recovers in the same session/map.");
            next(55);return 0;
        }
        if(phase==55) {
            if(!exists("host-chat-death-passed.txt") || !exists("client-chat-death-passed.txt")) return 0;
            open();next(56);return 0;
        }
        if(phase==56) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"map-transition fixture could not reopen chat after native death recovery");
            if(ui->msChatInput[0] || ui->msChatEmojiSearch[0] || ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen || ui->mbChatCompletionOpen)
                return fail(error,"reopening chat after death restored a stale draft, emoji query or picker focus");
            text("Map completion :gr");next(57);return 0;
        }
        if(phase==57) {
            if(age<200) return 0;
            if(!ui->mbChatCompletionOpen || ui->mvChatCompletionMatches.empty())
                return fail(error,"map-transition fixture could not open both peers' SDL completion choices");
            mark(role+"-chat-transition-ready.txt","live chat editor and emoji completion active");
            transitionNotice=0;next(59);return 0;
        }
        if(phase==59) {
            mark(role+"-chat-transition-armed.txt","map-transition phase owns the next real map load");
            if(role=="host" && !mapTransitionStarted) {
                if(!exists("host-chat-transition-armed.txt") || !exists("client-chat-transition-armed.txt")) return 0;
                mapTransitionStarted=true;
                if(!session->HostChangeMap("maps/main/ch01/02_entrance_hall.map","PlayerStartArea_1"))
                    return fail(error,"real host map transition was refused");
                if(ui->IsChatOpen() || ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen || ui->mbChatCompletionOpen) {
                    tracePicker("host retained editor after synchronous map change");
                    return fail(error,"host map-change call returned with its previous editor/picker still open");
                }
            }
            if(!session->IsActive()) return fail(error,"real map transition disconnected the chat session");
            if(session->GetLoadPhase()!=eLuxMultiplayerLoadPhase_None || session->GetMapEpoch()>epoch) {
                if(!transitionNotice) transitionNotice=SDL_GetTicks();
                if(SDL_GetTicks()-transitionNotice>100 && (ui->IsChatOpen() || ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen || ui->mbChatCompletionOpen))
                    return fail(error,"actual map loading retained editor/picker capture");
            }
            if(!session->IsReady() || session->GetMapEpoch()<=epoch || session->msMapName!="02_entrance_hall.map") return 0;
            if(ui->IsChatOpen() || ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen || ui->mbChatCompletionOpen) {
                tracePicker("completed map transition retained editor");
                return fail(error,"completed real map load retained editor or picker on the new map");
            }
            if(session->GetChatMessages().size()!=initialMessages+6) return fail(error,"real map transition erased session chat history");
            tString colorError;if(!colorsValid(colorError)) return fail(error,colorError);
            map=gpBase->mpMapHandler->GetCurrentMap();epoch=session->GetMapEpoch();next(60);return 0;
        }
        if(phase==60) {
            if(age<150 || !session->IsReady() || !gpBase->mpPlayer->IsActive() || gpBase->mpPlayer->IsDead() ||
               gpBase->mpEffectHandler->GetPlayerIsPaused() || gpBase->mpMessageHandler->IsPauseMessageActive()) return 0;
            if(ui->IsChatOpen() || ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen || ui->mbChatCompletionOpen || session->IsChatCapturingInput())
                return fail(error,"completed map transition retained editor/picker input capture after normal gameplay updates");
            mark(role+"-chat-map-transition-passed.txt","PASS: real retail map load with editor/emoji completion active closes capture on both peers, retains reliable session history and resumes gameplay.");
            next(61);return 0;
        }
        if(phase==61) {
            if(!exists("host-chat-map-transition-passed.txt") || !exists("client-chat-map-transition-passed.txt")) return 0;
            open();next(62);return 0;
        }
        if(phase==62) {
            if(age<200) return 0;
            if(!ui->IsChatOpen()) return fail(error,"window-close fixture could not open chat on the transitioned map");
            text("Quit completion :gr");next(63);return 0;
        }
        if(phase==63) {
            if(age<200) return 0;
            if(!ui->mbChatCompletionOpen || ui->mvChatCompletionMatches.empty())
                return fail(error,"window-close fixture could not open the SDL emoji completion choices");
            SDL_Event close={};close.type=SDL_WINDOWEVENT;close.window.windowID=SDL_GetWindowID(SDL_GL_GetCurrentWindow());
            close.window.event=SDL_WINDOWEVENT_CLOSE;SDL_PushEvent(&close);
            SDL_Event quit={};quit.type=SDL_QUIT;SDL_PushEvent(&quit);next(65);return 0;
        }
        if(phase==65) {
            auto* gui=gpBase->mpMainMenu->GetSet();
            if(updater->GetCurrentContainerName()!="MainMenu" || !gui->PopUpIsActive()) return 0;
            if(ui->IsChatOpen() || ui->mbChatEmojiPickerOpen || ui->mbChatPickerToneOpen || ui->mbChatCompletionOpen || gpBase->mpEngine->GetGameIsDone())
                return fail(error,"native window close retained chat or bypassed quit confirmation");
            auto* cancel=find(gui->GetAttentionWidget(),eWidgetType_Button,kTranslate("MainMenu","No"));
            if(!cancel || !cancel->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData()))
                return fail(error,"native quit confirmation did not receive window-close input or allow cancellation");
            next(66);return 0;
        }
        if(phase==66) {
            if(age<200) return 0;
            if(gpBase->mpMainMenu->GetSet()->PopUpIsActive() || gpBase->mpEngine->GetGameIsDone())
                return fail(error,"cancelling the real window-close confirmation did not preserve the live session");
            gpBase->mpMainMenu->ExitMenu(eLuxMainMenuExit_ReturnToGame);next(67);return 0;
        }
        if(phase==67) {
            if(age<150 || !gameplay()) return 0;
            mark(role+"-chat-window-close-passed.txt","PASS: SDL window-close/quit reaches the production native quit confirmation after closing editor/emoji completion, and No preserves the session.");
            mark(role+"-chat-passed.txt","PASS: chat delivery, Unicode/Discord aliases, resize/fade/native GUI isolation, actual death recovery, real map transition and native window-close confirmation.");
            next(40);return 0;
        }
        if(phase==40) {
            if(!exists("host-chat-passed.txt") || !exists("client-chat-passed.txt")) return 0;
            return 1;
        }
        return 0;
    }
    bool OnPostRender(tString& error) {
        if(!finalCaptureInstalled) {
            static_cast<cLowLevelGraphicsSDL*>(gpBase->mpEngine->GetGraphics()->GetLowLevel())
                ->SetOverlayCallback(FinalOverlay,this);
            finalCaptureInstalled=true;
        }
        if(!finalCaptureError.empty()) {error=finalCaptureError;return false;}
        return true;
    }
private:
    bool CaptureFinalFrame(tString& error,cBitmap* menuBefore=NULL) {
        if(!capturePending) return true;
        capturePending=false;auto* ui=gpBase->mpMultiplayer->mpUI;
        ImGui::SetCurrentContext(ui->mpContext);
        const ImGuiIO& io=ImGui::GetIO();
        const float requested=luxchat::CalculateLayout(io.DisplaySize.x,io.DisplaySize.y,false).fontSize;
        const float density=(std::max)(io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y);
        if(ui->mpChatFont!=originalChatFont || ui->mfChatFontSize!=std::round(requested) ||
           ui->mpChatFont->FontSize!=ui->mfChatFontSize || std::fabs(ui->mfChatRequestedFontSize-requested)>0.001f ||
           std::fabs(ui->mfChatFontDensity-density)>0.001f ||
           std::fabs(ui->mfChatRasterSize-ui->mfChatFontSize*density)>0.001f) {
            error="chat resize: font identity, native glyph size or framebuffer raster density changed incorrectly";return false;
        }
        if(ui->IsChatOpen()) {
            const ImGuiWindow* entry=ImGui::FindWindowByName("##MultiplayerChat");
            if(!entry || entry->FontWindowScale!=1.0f || ui->mfChatEntryFontSize!=ui->mpChatFont->FontSize) {
                error="chat resize: entry stretched the font instead of using its native raster size";return false;
            }
        }
        const cVector2l screen=gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt();
        const auto fits=[&screen](const cVector2f& position,const cVector2f& size) {
            return position.x>=0 && position.y>=0 && size.x>0 && size.y>0 &&
                position.x+size.x<=screen.x+1 && position.y+size.y<=screen.y+1;
        };
        if(phase==13 && (!fits(ui->mvChatEntryPos,ui->mvChatEntrySize) ||
           !fits(ui->mvChatTextInputPos,ui->mvChatTextInputSize) ||
           !fits(ui->mvChatHistoryPos,ui->mvChatHistorySize) || !ui->mlChatVisibleMessages ||
           ui->mvChatHistoryPos.y+ui->mvChatHistorySize.y>ui->mvChatEntryPos.y+1 ||
           ui->mvChatTextInputPos.x<ui->mvChatEntryPos.x || ui->mvChatTextInputPos.y<ui->mvChatEntryPos.y ||
           ui->mvChatTextInputPos.x+ui->mvChatTextInputSize.x>ui->mvChatEntryPos.x+ui->mvChatEntrySize.x+1 ||
           ui->mvChatTextInputPos.y+ui->mvChatTextInputSize.y>ui->mvChatEntryPos.y+ui->mvChatEntrySize.y+1)) {
            error="chat resize: entry/history were clipped, overlapped or disappeared at "+
                cString::ToString(screen.x)+"x"+cString::ToString(screen.y);return false;
        }
        if((phase==3 || phase==13) && (!ui->mlChatEntryEmoji ||
            ui->mvChatEntrySize.y>ui->mvChatTextInputSize.y+24)) {
            error="chat entry: composed emoji disappeared or a duplicate draft row increased the panel height";return false;
        }
        if((phase==18 || phase==181 || phase==21 || phase==22 || phase==10) &&
           (!fits(ui->mvChatHistoryPos,ui->mvChatHistorySize) || !ui->mlChatVisibleMessages)) {
            error="chat resize: passive long-message history escaped the viewport or disappeared";return false;
        }
        if((phase==181 || phase==21 || phase==22 || phase==10) && (ui->IsChatOpen() || ui->IsChatCapturingInput() ||
           ui->mvChatEntrySize!=cVector2f(0) || ui->mfChatHistoryAlpha!=1)) {
            error="chat overlay: native fade/menu hid recent history or left interactive chat visible";return false;
        }
        if((phase==21 || phase==22 || phase==10) && (!ui->IsChatHistoryInMenuBackdrop() || ui->GetChatMenuBlurPasses()==0 ||
           ui->GetChatFinalHistoryCount()!=0)) {
            error="native menu chat did not enter the blur backdrop or was also drawn sharply in the final overlay";return false;
        }
        if(phase==181 && (ui->IsChatHistoryInMenuBackdrop() || ui->GetChatFinalHistoryCount()==0)) {
            error="gameplay opaque-fade chat did not return to the sharp final overlay";return false;
        }
        if(phase==3 || phase==13 || phase==18 || phase==181 || phase==21 || phase==22 || phase==10) {
            cBitmap* bitmap=gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
            if(!bitmap) {error="chat resize screenshot readback failed";return false;}
            if(phase==21 || phase==22 || phase==10) {
                const cBitmapData* before=menuBefore?menuBefore->GetData(0,0):NULL;
                const cBitmapData* after=bitmap->GetData(0,0);
                if(!before || !after || !before->mpData || !after->mpData || before->mlSize!=after->mlSize ||
                   menuBefore->GetSize()!=bitmap->GetSize() || menuBefore->GetPixelFormat()!=bitmap->GetPixelFormat() ||
                   std::memcmp(before->mpData,after->mpData,after->mlSize)!=0) {
                    hplDelete(bitmap);error="native menu pixels changed during final UI overlay: sharp history was drawn over native blur/GUI";return false;
                }
                if(ui->GetChatMenuBackdropSize()!=screen || ui->GetChatMenuBlurSize()!=cVector2l((screen.x+1)/2,(screen.y+1)/2)) {
                    hplDelete(bitmap);error="native menu backdrop/blur targets did not match the current framebuffer dimensions";return false;
                }
                if(phase==21) {
                    unsigned char pixel[4]={};bitmap->GetPixel(0,0,cVector3l(nativeHistoryProbePixel.x,
                        bitmap->GetHeight()-1-nativeHistoryProbePixel.y,0),pixel);
                    if(std::abs(int(pixel[0])-230)>3 || std::abs(int(pixel[1])-26)>3 || std::abs(int(pixel[2])-153)>3) {
                        hplDelete(bitmap);error="native GUI occlusion probe was blurred or overdrawn by sharp chat";return false;
                    }
                }
            }
            if(phase==181) {
                const cBitmapData* data=bitmap->GetData(0,0);
                const int width=bitmap->GetWidth(),height=bitmap->GetHeight(),bytes=bitmap->GetBytesPerPixel();
                if(!data || !data->mpData || width!=screen.x || height!=screen.y || bytes<3 || data->mlSize<width*height*bytes) {
                    hplDelete(bitmap);error="chat opaque-fade pixel readback has invalid dimensions or format";return false;
                }
                const auto bright=[&](int x,int y,int threshold) {
                    // OpenGL frame readbacks store the bottom row first.
                    const unsigned char* pixel=data->mpData+((height-1-y)*width+x)*bytes;
                    return pixel[0]>threshold || pixel[1]>threshold || pixel[2]>threshold;
                };
                unsigned textPixels=0,backgroundPixels=0;
                const int left=static_cast<int>(ui->mvChatHistoryPos.x),top=static_cast<int>(ui->mvChatHistoryPos.y);
                const int right=(std::min)(width,static_cast<int>(std::ceil(ui->mvChatHistoryPos.x+ui->mvChatHistorySize.x)));
                const int bottom=(std::min)(height,static_cast<int>(std::ceil(ui->mvChatHistoryPos.y+ui->mvChatHistorySize.y)));
                for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) if(bright(x,y,100)) ++textPixels;
                for(int y=height/4-5;y<height/4+5;++y) for(int x=width/2-5;x<width/2+5;++x)
                    if(bright(x,y,4)) ++backgroundPixels;
                if(textPixels<100 || backgroundPixels) {
                    hplDelete(bitmap);error="chat final opaque fade pixels: visible text="+cString::ToString(int(textPixels))+
                        ", uncovered background="+cString::ToString(int(backgroundPixels));return false;
                }
            }
            static const char* menuNames[]={"MainMenu","Inventory","Journal"};
            const tString filename=phase==3?role+"-chat-entry.png":
                phase==10?role+"-chat-passive-modal.png":
                phase==21?role+"-chat-menu-priority-"+menuNames[menuIndex]+".png":
                phase==22?role+"-chat-passive-"+menuNames[menuIndex]+".png":
                role+(phase==181?"-chat-opaque-fade-":phase==18?"-chat-history-":"-chat-")+
                    cString::ToString(screen.x)+"x"+cString::ToString(screen.y)+".png";
            const bool saved=gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(bitmap,
                cString::To16Char(outputDir+"/"+filename),0);
            hplDelete(bitmap);if(!saved) {error="chat resize screenshot save failed";return false;}
        }
        captured=true;return true;
    }
};

#endif
