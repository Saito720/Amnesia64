#ifndef MULTIPLAYER_FPS_SETTINGS_REGRESSION_H
#define MULTIPLAYER_FPS_SETTINGS_REGRESSION_H

#include "LuxConfigHandler.h"
#define private public
#include "LuxMainMenu_Profile.h"
#undef private

// Runs against the actual options window in the isolated full-game test profile.
// The harness exposes Options/MainMenu internals, without replacing their code.
class cFPSSettingsRegression {
    unsigned phase=0;
    Uint32 started=0;
    bool screenshot=false, originalLimit=false, originalVsync=false;
    bool profilePrepared=false;
    cLuxMainMenu_Options* options=NULL;
    static int fail(tString& error,const char* message) {
        error=tString("FPS settings: ")+message;return -1;
    }
    void open() {
        gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_Options);
        options=static_cast<cLuxMainMenu_Options*>(gpBase->mpMainMenu->mvWindows[eLuxMainMenuWindow_Options]);
        options->mpTabGraphics->GetParentTabFrame()->SetTabOnTop(options->mpTabGraphics);
    }
    bool widgetsMatch(bool limit,bool vsync) const {
        return options->mpChBUncapFPS->IsChecked()==!limit && options->mpChBVSync->IsChecked()==vsync;
    }
    bool runtimeMatches(bool limit,bool vsync) const {
        return gpBase->mpEngine->GetLimitFPS()==limit && gpBase->mpConfigHandler->mbVSync==vsync;
    }
    bool savedMatches(bool limit,bool vsync) const {
        bool loadedDefault=true;
        cConfigFile* reloaded=gpBase->LoadConfigFile(gpBase->msDefaultMainConfigPath,
            gpBase->mpMainConfig->GetFileLocation(),false,&loadedDefault);
        if(!reloaded) return false;
        const bool matches=!loadedDefault && reloaded->GetBool("Engine","LimitFPS",!limit)==limit &&
            reloaded->GetBool("Screen","Vsync",!vsync)==vsync;
        hplDelete(reloaded);return matches;
    }
public:
    int Initial(tString& error) {
        if(phase==2) return 1;
        if(!profilePrepared) {
            // A fresh profile root starts at the Create Profile modal. Complete
            // that real flow before opening Options, so its attention cannot
            // block gamepad input and OK can persist actual user preferences.
            if(!gpBase->mpUserConfig) {
                gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_Profiles);
                auto* profile=static_cast<cLuxMainMenu_Profile*>(gpBase->mpMainMenu->mvWindows[eLuxMainMenuWindow_Profiles]);
                if(!profile->mpWindowEnterName->IsVisible()) return fail(error,"fresh profile creation window was not active");
                profile->mpTextEnterName->SetText(gpBase->msDefaultProfileName);
                profile->mpTextEnterName->ProcessMessage(eGuiMessage_TextBoxEnter,cGuiMessageData());
                profile->mpSelectButton->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
                if(!gpBase->mpUserConfig) return fail(error,"could not select the isolated test profile");
            }
            profilePrepared=true;
            return 0; // Selection schedules the normal options/GUI rebuild.
        }
        if(gpBase->mpMainMenu->mbRecreateGui) return 0;
        if(phase==0) {
            originalLimit=gpBase->mpEngine->GetLimitFPS();
            originalVsync=gpBase->mpConfigHandler->mbVSync;
            open();
            if(!widgetsMatch(originalLimit,originalVsync)) return fail(error,"opening the menu did not reflect active preferences");
            const auto* data=static_cast<cLuxOption_ExtData*>(options->mpChBUncapFPS->GetUserData());
            if(!data || data->mbNeedsRestart || data->msMessage.empty() || options->mpChBUncapFPS->GetText().empty())
                return fail(error,"uncap option is missing its label, tooltip, or live-apply metadata");
            const auto p=options->mpChBUncapFPS->GetGlobalPosition();
            const auto size=options->mpChBUncapFPS->GetSize();
            const auto gamma=options->mpSGamma->GetGlobalPosition();
            const auto footer=options->mpBToggleShowGfxOptions->GetGlobalPosition();
            if(p.x+size.x>gamma.x || p.y+size.y>footer.y)
                return fail(error,"uncap option overlaps the gamma controls or footer");

            auto* gui=gpBase->mpMainMenu->GetSet();
            if(options->mpCBTextureSizeLevel->GetFocusNavigation(eUIArrow_Down)!=options->mpChBUncapFPS)
                return fail(error,"Texture Quality is not connected to Uncap FPS");
            if(!options->mpChBUncapFPS->IsVisible() || !options->mpChBUncapFPS->IsEnabled())
                return fail(error,"the graphics tab did not expose Uncap FPS for input");
            gui->SetFocusedWidget(options->mpCBTextureSizeLevel);
            gui->SendMessage(eGuiMessage_UIArrowPress,cGuiMessageData(eUIArrow_Down));
            if(gui->GetFocusedWidget()!=options->mpChBUncapFPS) return fail(error,"gamepad cannot reach Uncap FPS");
            gui->SendMessage(eGuiMessage_UIArrowPress,cGuiMessageData(eUIArrow_Down));
            if(gui->GetFocusedWidget()!=options->mpSGamma) return fail(error,"gamepad cannot leave Uncap FPS");
            gui->SendMessage(eGuiMessage_UIArrowPress,cGuiMessageData(eUIArrow_Up));
            if(gui->GetFocusedWidget()!=options->mpChBUncapFPS) return fail(error,"gamepad cannot navigate back to Uncap FPS");
            started=SDL_GetTicks();phase=1;return 0;
        }
        if(SDL_GetTicks()-started>5000) return fail(error,"options window was never rendered");
        if(!screenshot) return 0;

        // Checkbox edits are staged until OK, like the existing V-sync option.
        options->mpChBUncapFPS->SetChecked(originalLimit,true);
        options->mpChBVSync->SetChecked(!originalVsync,true);
        if(!runtimeMatches(originalLimit,originalVsync)) return fail(error,"editing a checkbox applied it before OK");
        options->mpBCancel->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
        open();
        if(!runtimeMatches(originalLimit,originalVsync) || !widgetsMatch(originalLimit,originalVsync))
            return fail(error,"Cancel did not restore the original choices");

        options->mpChBUncapFPS->SetChecked(originalLimit,true);
        options->mpChBVSync->SetChecked(!originalVsync,true);
        options->mpBOK->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
        if(!runtimeMatches(!originalLimit,!originalVsync) || !savedMatches(!originalLimit,!originalVsync))
            return fail(error,"OK did not apply and persist both choices");
        if(gpBase->mpMainMenu->GetSet()->PopUpIsActive()) return fail(error,"changing FPS or V-sync incorrectly required a restart");
        open();
        if(!widgetsMatch(!originalLimit,!originalVsync)) return fail(error,"reopening the menu lost applied preferences");

        options->mpChBUncapFPS->SetChecked(!originalLimit,true);
        options->mpChBVSync->SetChecked(originalVsync,true);
        options->mpBOK->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData());
        if(!runtimeMatches(originalLimit,originalVsync) || !savedMatches(originalLimit,originalVsync))
            return fail(error,"restoring the initial preferences failed");
        phase=2;
        mark(role+"-fps-settings-passed.txt","PASS: native FPS/V-sync controls, gamepad navigation, readable layout, Cancel, live OK, saved-file reload and restoration.");
        return 1;
    }
    bool OnPostRender(tString& error) {
        if(phase!=1 || screenshot || SDL_GetTicks()-started<300) return true;
        cBitmap* bitmap=gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
        if(!bitmap) {fail(error,"could not read options screenshot");return false;}
        const bool saved=gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(bitmap,
            cString::To16Char(outputDir+"/"+role+"-fps-options.png"),0);
        hplDelete(bitmap);
        if(!saved) {fail(error,"could not save options screenshot");return false;}
        screenshot=true;return true;
    }
};
#endif
