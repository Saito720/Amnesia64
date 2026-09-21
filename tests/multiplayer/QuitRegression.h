#ifndef MULTIPLAYER_QUIT_REGRESSION_H
#define MULTIPLAYER_QUIT_REGRESSION_H

// Inject native SDL close/keyboard events into the real outer engine loop.
// Answers go through the production message-box buttons, not menu callbacks.
class cQuitRegression {
    unsigned phase=0,trial=0;
    Uint32 entered=0;
    cLuxMap* map=NULL;
    uint32_t epoch=0;
    iWidget* attention=NULL;
    bool finalAccepted=false;
    bool confirmationReplaced=false;
    const bool directly=std::getenv("CODEX_MP_QUIT_DIRECTLY")!=NULL;
    void next(unsigned value) {phase=value;entered=SDL_GetTicks();}
    static void closeWindow() {SDL_Event event={};event.type=SDL_QUIT;SDL_PushEvent(&event);}
    static void escape(bool down) {
        SDL_Event event={};event.type=down?SDL_KEYDOWN:SDL_KEYUP;
        event.key.state=down?SDL_PRESSED:SDL_RELEASED;
        event.key.keysym.sym=SDLK_ESCAPE;event.key.keysym.scancode=SDL_SCANCODE_ESCAPE;
        SDL_PushEvent(&event);
    }
    static iWidget* find(iWidget* widget,eWidgetType type,const tWString& text) {
        if(!widget) return NULL;
        if(widget->GetType()==type && widget->GetText()==text) return widget;
        for(auto* child:widget->GetChildren()) if(auto* found=find(child,type,text)) return found;
        return NULL;
    }
    static bool answer(const tWString& text) {
        auto* button=find(gpBase->mpMainMenu->GetSet()->GetAttentionWidget(),eWidgetType_Button,text);
        return button && button->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
    }
    bool prompt() const {
        auto* gui=gpBase->mpMainMenu->GetSet();
        return gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()=="MainMenu" && gui->IsActive() &&
            gui->PopUpIsActive() && find(gui->GetAttentionWidget(),eWidgetType_Label,
                kTranslate("MainMenu","Sure you want to quit?"));
    }
    bool unchanged() const {
        return gpBase->mpMultiplayer->IsReady() && gpBase->mpMultiplayer->GetMapEpoch()==epoch &&
            gpBase->mpMapHandler->GetCurrentMap()==map &&
            gpBase->mpInventory->GetItem("codex_quit_sentinel") && !gpBase->mpEngine->GetGameIsDone();
    }
    int fail(tString& error,const tString& message) const {
        error="native quit phase "+cString::ToString(int(phase))+", trial "+cString::ToString(int(trial))+": "+message;
        return -1;
    }
public:
    bool FinalAccepted() const {return finalAccepted;}
    int Update(tString& error) {
        auto* session=gpBase->mpMultiplayer;
        auto* menu=gpBase->mpMainMenu;
        auto* gui=menu->GetSet();
        auto* updater=gpBase->mpEngine->GetUpdater();
        const Uint32 age=SDL_GetTicks()-entered;
        if(phase && age>15000) return fail(error,"operation did not finish: "+session->GetStatus());
        if(phase<20 && phase && !unchanged()) return fail(error,"unconfirmed close changed the map, inventory, or session");
        if(phase==0) {
            map=gpBase->mpMapHandler->GetCurrentMap();epoch=session->GetMapEpoch();
            if(!map || !session->IsReady() || !gpBase->mpInventory->AddItem("codex_quit_sentinel",
                    eLuxItemType_Puzzle,"KeyTower","key_tower.tga",1,"",""))
                return fail(error,"could not initialize the live session fixture");
            next(1);return 0;
        }
        if(phase==1) {
            confirmationReplaced=false;
            if(session->IsWindowVisible()) session->ToggleWindow();
            gpBase->mbExitMenuDirectly=(trial%2)==1;
            updater->SetContainer("Default");
            menu->SetWindowActive(eLuxMainMenuWindow_LastEnum);
            if(trial==1 || trial>=4) updater->SetContainer("MainMenu");
            if(trial==2) updater->SetContainer("Inventory");
            if(trial==3) {
                gpBase->mpJournal->SetOpenedFromInventory(false);
                updater->SetContainer("Journal");
            }
            if(trial==4) menu->SetWindowActive(eLuxMainMenuWindow_Options);
            if(trial==5) session->ShowWindow();
            next(2);return 0;
        }
        if(phase==2) {
            if(age<250) return 0;
            if(trial==9) {
                // Start the genuine resume fade from a fully visible menu,
                // then post X while that non-quit exit is still in progress.
                if(menu->mfMenuFadeAlpha>0) return 0;
                iWidget* resume=NULL;
                for(auto* label:menu->mvTopMenuLabels)
                    if(label->GetText()==kTranslate("MainMenu","Back To Game")) {resume=label;break;}
                if(!resume || !resume->ProcessMessage(eGuiMessage_MouseDown,cGuiMessageData(eGuiMouseButton_Left)) ||
                    !menu->mbExiting || menu->mExitMessage!=eLuxMainMenuExit_ReturnToGame)
                    return fail(error,"could not enter the real return-to-game fade");
                closeWindow();next(4);return 0;
            }
            if(trial==6) {
                gui->CreatePopUpMessageBox(_W("Existing question"),_W("Keep the current question until it is answered"),
                    _W("Finish existing question"),_W(""),NULL,NULL);
                attention=gui->GetAttentionWidget();
            }
            if(trial==7 || trial==8) {
                const tWString caption=kTranslate("MainMenu",gpBase->mbExitMenuDirectly?"Exit":"ExitToMainMenu");
                iWidget* exit=NULL;
                for(auto* label:menu->mvTopMenuLabels) if(label->GetText()==caption) {exit=label;break;}
                if(!exit || !exit->ProcessMessage(eGuiMessage_MouseDown,cGuiMessageData(eGuiMouseButton_Left)))
                    return fail(error,"pause-menu Exit action could not be pressed");
            } else closeWindow();
            next(trial==6?3:4);return 0;
        }
        if(phase==3) {
            if(age<250) return 0;
            if(!gui->PopUpIsActive() || gui->GetAttentionWidget()!=attention || prompt())
                return fail(error,"window close displaced an existing modal question");
            if(!answer(_W("Finish existing question"))) return fail(error,"existing modal could not be answered");
            next(4);return 0;
        }
        if(phase==4) {
            if(!prompt()) return 0;
            if(session->IsWindowVisible()) return fail(error,"multiplayer overlay covers the quit confirmation");
            if((trial==10 || trial==11) && !confirmationReplaced) {
                confirmationReplaced=true;
                if(trial==10) menu->RecreateGui();
                else {
                    updater->SetContainer("Inventory");
                    if(updater->GetCurrentContainerName()!="Inventory" || gui->IsActive())
                        return fail(error,"fixture did not replace the open confirmation's container");
                }
                next(8);return 0;
            }
            attention=gui->GetAttentionWidget();closeWindow();next(5);return 0;
        }
        if(phase==8) {
            if(age<150 || menu->mbRecreateGui || !prompt()) return 0;
            if(!menu->mpQuitConfirmation || gpBase->mpInputHandler->GetState()!=eLuxInputState_MainMenu)
                return fail(error,"restored confirmation lost ownership or main-menu input");
            next(4);return 0;
        }
        if(phase==5) {
            if(age<150) return 0;
            if(!prompt() || gui->GetAttentionWidget()!=attention) return fail(error,"repeated close replaced or duplicated the confirmation");
            escape(true);next(6);return 0;
        }
        if(phase==6) {
            if(age<150) return 0;
            escape(false);
            if(!prompt() || menu->mbExiting || gpBase->mpInputHandler->GetState()!=eLuxInputState_MainMenu)
                return fail(error,"Escape resumed gameplay underneath the confirmation");
            if(!answer(kTranslate("MainMenu","No"))) return fail(error,"No button could not be pressed");
            next(7);return 0;
        }
        if(phase==7) {
            if(age<250) return 0;
            if(gui->PopUpIsActive() || menu->mbExiting) return fail(error,"No left a duplicate or pending confirmation");
            if(++trial<12) {next(1);return 0;}
            mark(role+"-quit-cancel-passed.txt","PASS: native X from gameplay, pause, inventory, journal, options and multiplayer overlay; both existing Exit choices, repeated X, Escape, No, existing modal deferral, resume fade, menu recreation and container replacement preserve the session.");
            printStatus("PASS: native close confirmation, modal deferral and cancellation preserve gameplay/session");
            next(20);return 0;
        }
        if(phase==20) {
            if(!exists("host-quit-cancel-passed.txt") || !exists("client-quit-cancel-passed.txt")) return 0;
            // The client leaves first so the host still has a live map/session
            // when checking its own affirmative decision.
            if(role=="host" && !exists(directly?"client-passed.txt":"client-quit-menu-passed.txt")) return 0;
            gpBase->mbExitMenuDirectly=directly;
            if(session->IsWindowVisible()) session->ToggleWindow();
            updater->SetContainer("Default");closeWindow();next(21);return 0;
        }
        if(phase==21) {
            if(!prompt()) return 0;
            if(!unchanged()) return fail(error,"final confirmation changed the session before Yes");
            if(!answer(kTranslate("MainMenu","Yes"))) return fail(error,"Yes button could not be pressed");
            if(!menu->mbQuitAccepted || !menu->mbExiting)
                return fail(error,"Yes did not record an accepted exit before its fade");
            // A death or map transition may replace the menu during its fade.
            // Preserve the already accepted destination without asking again.
            updater->SetContainer("Inventory");
            if(updater->GetCurrentContainerName()!="Inventory" || gui->IsActive() || !menu->mbQuitAccepted)
                return fail(error,"accepted exit was lost while replacing the menu container");
            if(directly) {
                finalAccepted=true;next(40);return 0;
            }
            next(22);return 0;
        }
        if(phase==22) {
            if(prompt()) return fail(error,"accepted return to menu asked for confirmation again");
            if(gpBase->mpMapHandler->GetCurrentMap() || session->IsActive() || menu->mbExiting) return 0;
            if(updater->GetCurrentContainerName()!="MainMenu" || !gui->IsActive() || gpBase->mpEngine->GetGameIsDone())
                return fail(error,"ExitMenuDirectly=false did not leave the application at its main menu");
            mark(role+"-quit-menu-passed.txt","PASS: confirmed native close with ExitMenuDirectly=false disconnected and returned to the main menu.");
            printStatus("PASS: confirmed X with ExitMenuDirectly=false returns to the main menu");
            next(23);return 0;
        }
        if(phase==23) {
            if(!exists("host-quit-menu-passed.txt") || !exists("client-quit-menu-passed.txt")) return 0;
            gpBase->mbExitMenuDirectly=false;
            if(session->IsWindowVisible()) session->ToggleWindow();
            closeWindow();next(24);return 0;
        }
        if(phase==24) {
            if(!prompt()) return 0;
            if(!answer(kTranslate("MainMenu","Yes"))) return fail(error,"title-screen Yes button could not be pressed");
            finalAccepted=true;next(40);return 0;
        }
        if(phase==40 && prompt()) return fail(error,"accepted application exit asked for confirmation again");
        // The executable's outer main function records success only after the
        // production quit path actually returns from the engine's Run loop.
        return 0;
    }
};
#endif
