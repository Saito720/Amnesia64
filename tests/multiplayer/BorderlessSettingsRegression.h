#ifndef MULTIPLAYER_BORDERLESS_SETTINGS_REGRESSION_H
#define MULTIPLAYER_BORDERLESS_SETTINGS_REGRESSION_H

#include "LuxConfigHandler.h"

// The focused settings run uses the real menu callbacks and saved configuration.
// Window-mode changes must remain pending until restart, including when the
// original borderless choice came from the legacy native-resolution heuristic.
class cBorderlessSettingsRegression {
    unsigned phase=0;
    Uint32 started=0, originalFlags=0;
    bool originalFullscreen=false, originalBorderless=false;
    cVector2l originalSize=0;
    cLuxMainMenu_Options* options=NULL;

    static int fail(tString& error,const char* message) {
        error=tString("Borderless settings: ")+message;return -1;
    }
    void open() {
        gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_Options);
        options=static_cast<cLuxMainMenu_Options*>(gpBase->mpMainMenu->mvWindows[eLuxMainMenuWindow_Options]);
        options->mpTabGraphics->GetParentTabFrame()->SetTabOnTop(options->mpTabGraphics);
    }
    bool widgetsMatch(bool fullscreen,bool borderless) const {
        return options->mpChBFullScreen->IsChecked()==fullscreen &&
            options->mpChBBorderless->IsChecked()==borderless;
    }
    bool configMatches(bool fullscreen,bool borderless) const {
        return gpBase->mpConfigHandler->mbFullscreen==fullscreen &&
            gpBase->mpConfigHandler->mbBorderless==borderless;
    }
    bool savedMatches(bool fullscreen,bool borderless) const {
        cConfigFile saved(gpBase->mpMainConfig->GetFileLocation());
        if(!saved.Load()) return false;
        return saved.GetBool("Screen","FullScreen",!fullscreen)==fullscreen &&
            saved.GetBool("Screen","Borderless",!borderless)==borderless &&
            gpBase->mpConfigHandler->mbBorderlessSpecified;
    }
    bool windowUnchanged() const {
        const Uint32 modeMask=SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_BORDERLESS|SDL_WINDOW_RESIZABLE;
        int width=0,height=0;SDL_GetWindowSize(SDL_GL_GetCurrentWindow(),&width,&height);
        return (SDL_GetWindowFlags(SDL_GL_GetCurrentWindow())&modeMask)==originalFlags &&
            cVector2l(width,height)==originalSize;
    }
    bool acknowledgeRestart(tString& error) {
        auto* gui=gpBase->mpMainMenu->GetSet();
        if(!gui->PopUpIsActive()) {fail(error,"saving a changed window mode did not show the restart notice");return false;}
        // The native message box owns attention and accepts this primary action.
        gui->SendMessage(eGuiMessage_UIButtonPress,cGuiMessageData(eUIButton_Primary));
        started=SDL_GetTicks();return true;
    }
public:
    bool CheckStartup(tString& error) {
        const auto* config=gpBase->mpConfigHandler;
        const tString value=gpBase->mpMainConfig->GetString("Screen","Borderless","");
        const bool specified=!value.empty();
        SDL_Rect display;
        if(SDL_GetDisplayBounds(config->mlDisplay,&display)!=0) {
            fail(error,"could not resolve the configured display bounds");return false;
        }
        const bool expected=!config->mbFullscreen && (specified ?
            gpBase->mpMainConfig->GetBool("Screen","Borderless",false) :
            config->mvScreenSize==cVector2l(display.w,display.h));
        const bool actual=(SDL_GetWindowFlags(SDL_GL_GetCurrentWindow())&SDL_WINDOW_BORDERLESS)!=0;
        if(config->mbBorderlessSpecified!=specified || config->mbBorderless!=expected || actual!=expected) {
            fail(error,"loaded preference, legacy inference, and actual window border disagree");return false;
        }
        // A routine save must preserve legacy Auto until the user accepts an
        // explicit mode in Options. In particular, a fallback window size must
        // not silently disable native-resolution borderless on the next launch.
        const bool storedBorderless=gpBase->mpMainConfig->GetBool("Screen","Borderless",false);
        gpBase->mpConfigHandler->SaveMainConfig();
        if(config->mbBorderlessSpecified!=specified || config->mbBorderless!=expected ||
            (!specified && !gpBase->mpMainConfig->GetString("Screen","Borderless","").empty()) ||
            (specified && gpBase->mpMainConfig->GetBool("Screen","Borderless",!storedBorderless)!=storedBorderless)) {
            fail(error,"routine config save changed the missing/explicit borderless preference");return false;
        }
        return true;
    }
    int Update(tString& error) {
        auto* gui=gpBase->mpMainMenu->GetSet();
        if(phase==4) return 1;
        if(phase!=0 && !windowUnchanged()) return fail(error,"editing or saving restarted the window immediately");
        if(phase==1 || phase==2 || phase==3) {
            if(gui->PopUpIsActive()) {
                if(SDL_GetTicks()-started>5000) return fail(error,"restart notice could not be dismissed through native UI input");
                return 0; // Popups are destroyed at the normal GUI update boundary.
            }
            if(phase==3) {
                if(!configMatches(originalFullscreen,originalBorderless) || !savedMatches(originalFullscreen,originalBorderless))
                    return fail(error,"could not restore the original window-mode preference");
                phase=4;
                mark(role+"-borderless-settings-passed.txt","PASS: legacy/explicit startup choice, native Borderless label/layout/controller navigation, mutually exclusive modes, staged edits, Cancel, restart notice, saved-file reload, and unchanged live window.");
                return 1;
            }
            open();
            if(phase==1) {
                if(!widgetsMatch(false,true)) return fail(error,"reopening Options lost the saved borderless preference");
                options->mpChBFullScreen->SetChecked(true,true);
                if(!widgetsMatch(true,false)) return fail(error,"enabling fullscreen left borderless enabled");
                options->mpBOK->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
                if(!configMatches(true,false) || !savedMatches(true,false)) return fail(error,"fullscreen did not persist its exclusive choice");
                if(!acknowledgeRestart(error)) return -1;
                phase=2;return 0;
            }
            if(!widgetsMatch(true,false)) return fail(error,"reopening Options lost the saved fullscreen preference");
            options->mpChBFullScreen->SetChecked(originalFullscreen,true);
            options->mpChBBorderless->SetChecked(originalBorderless,true);
            options->mpBOK->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
            if(!acknowledgeRestart(error)) return -1;
            phase=3;return 0;
        }

        originalFullscreen=gpBase->mpConfigHandler->mbFullscreen;
        originalBorderless=gpBase->mpConfigHandler->mbBorderless;
        originalFlags=SDL_GetWindowFlags(SDL_GL_GetCurrentWindow()) &
            (SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_BORDERLESS|SDL_WINDOW_RESIZABLE);
        SDL_GetWindowSize(SDL_GL_GetCurrentWindow(),&originalSize.x,&originalSize.y);
        open();
        if(!widgetsMatch(originalFullscreen,originalBorderless)) return fail(error,"menu did not reflect the current window-mode preference");
        const auto* data=static_cast<cLuxOption_ExtData*>(options->mpChBBorderless->GetUserData());
        if(!data || !data->mbNeedsRestart || data->msMessage.empty() || options->mpChBBorderless->GetText().empty() ||
            !options->mpChBBorderless->IsVisible() || !options->mpChBBorderless->IsEnabled())
            return fail(error,"Borderless is missing its label, tooltip, restart metadata, or input visibility");
        const auto fullPos=options->mpChBFullScreen->GetGlobalPosition();
        const auto borderPos=options->mpChBBorderless->GetGlobalPosition();
        const auto borderSize=options->mpChBBorderless->GetSize();
        auto* group=options->mpChBBorderless->GetParent();
        if(borderPos.x<fullPos.x+options->mpChBFullScreen->GetSize().x ||
            std::abs(borderPos.y-fullPos.y)>0.1f ||
            borderPos.x+borderSize.x>group->GetGlobalPosition().x+group->GetSize().x ||
            borderPos.y+borderSize.y>options->mpChBVSync->GetGlobalPosition().y)
            return fail(error,"Borderless overlaps its adjacent controls or leaves the graphics group");
        gui->SetFocusedWidget(options->mpChBFullScreen);
        gui->SendMessage(eGuiMessage_UIArrowPress,cGuiMessageData(eUIArrow_Right));
        if(gui->GetFocusedWidget()!=options->mpChBBorderless) return fail(error,"controller cannot reach Borderless from Fullscreen");
        gui->SendMessage(eGuiMessage_UIArrowPress,cGuiMessageData(eUIArrow_Left));
        if(gui->GetFocusedWidget()!=options->mpChBFullScreen) return fail(error,"controller cannot return to Fullscreen");
        gui->SetFocusedWidget(options->mpChBBorderless);
        gui->SendMessage(eGuiMessage_UIArrowPress,cGuiMessageData(eUIArrow_Down));
        if(gui->GetFocusedWidget()!=options->mpChBVSync) return fail(error,"controller cannot leave Borderless for V-sync");

        // Both directions of exclusivity are staged, and Cancel must restore the
        // original preference without causing a later restart warning.
        options->mpChBFullScreen->SetChecked(true,true);
        if(!widgetsMatch(true,false)) return fail(error,"fullscreen checkbox did not clear borderless");
        options->mpChBBorderless->SetChecked(true,true);
        if(!widgetsMatch(false,true) || !configMatches(originalFullscreen,originalBorderless) || !windowUnchanged())
            return fail(error,"borderless checkbox failed exclusivity or applied before OK");
        options->mpBCancel->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
        open();
        if(!widgetsMatch(originalFullscreen,originalBorderless)) return fail(error,"Cancel did not restore both mode checkboxes");
        options->mpBOK->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
        if(gui->PopUpIsActive()) return fail(error,"cancelled mode edits caused a spurious restart notice");
        open();
        options->mpChBBorderless->SetChecked(!originalBorderless,true);
        options->mpChBFullScreen->SetChecked(originalFullscreen,true);
        options->mpChBBorderless->SetChecked(originalBorderless,true);
        options->mpBOK->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
        if(gui->PopUpIsActive()) return fail(error,"toggling back to the original mode required a restart");

        // Save a different mode first so every following save is a real change,
        // regardless of whether this process started with borderless enabled.
        open();
        options->mpChBFullScreen->SetChecked(false,true);
        options->mpChBBorderless->SetChecked(false,true);
        if(originalBorderless) {
            options->mpChBFullScreen->SetChecked(true,true);
            options->mpBOK->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
            if(!configMatches(true,false) || !savedMatches(true,false)) return fail(error,"initial exclusive fullscreen save failed");
            if(!acknowledgeRestart(error)) return -1;
            phase=2;return 0;
        }
        options->mpChBBorderless->SetChecked(true,true);
        options->mpBOK->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
        if(!configMatches(false,true) || !savedMatches(false,true)) return fail(error,"Borderless OK did not persist an explicit choice");
        if(!acknowledgeRestart(error)) return -1;
        phase=1;return 0;
    }
};
#endif
