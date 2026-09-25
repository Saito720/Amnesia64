#ifndef MULTIPLAYER_QUIT_BACKGROUND_REGRESSION_H
#define MULTIPLAYER_QUIT_BACKGROUND_REGRESSION_H

#include "impl/LowLevelGraphicsSDL.h"
#include "graphics/Texture.h"

// Run while the hosting fixture is still an offline game: active multiplayer
// uses a live background and cannot expose a stale captured overlay.
static bool CheckQuitBackground(tString& error)
{
    auto* menu = gpBase->mpMainMenu;
    auto* session = gpBase->mpMultiplayer;
    auto* low = gpBase->mpEngine->GetGraphics()->GetLowLevel();
    auto* updater = gpBase->mpEngine->GetUpdater();
    cLuxMap* map = gpBase->mpMapHandler->GetCurrentMap();
    for(const char* container : {"Default", "Inventory", "Journal"})
    {
        updater->SetContainer(container);
        session->ShowWindow();
        // Make stale screen content unmistakable, then draw the actual overlay.
        low->SetCurrentFrameBuffer(NULL);
        low->SetClearColor(cColor(1, 0, 1, 1));
        low->ClearFrameBuffer(eClearFrameBufferFlag_Color | eClearFrameBufferFlag_Depth);
        session->mpUI->Draw();
        gpBase->mpInputHandler->OnQuit();
        gpBase->mpInputHandler->Update(1.0f / 60);
        if(session->IsWindowVisible() || !menu->mpScreenTexture || !menu->mpQuitConfirmation)
        {
            error = "offline close did not hide the overlay and create a captured quit menu";
            return false;
        }
        iTexture* texture = menu->mpScreenTexture;
        std::vector<unsigned char> pixels(texture->GetWidth() * texture->GetHeight() * 4);
        low->SetTexture(0, texture);
        glGetTexImage(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        low->SetTexture(0, NULL);
        size_t stale = 0;
        for(size_t i = 0; i < pixels.size(); i += 4)
            if(pixels[i] > 245 && pixels[i+1] < 10 && pixels[i+2] > 245) ++stale;
        if(stale > pixels.size() / 4 / 4)
        {
            error = "quit background retained stale screen/overlay pixels";
            return false;
        }
        // Cancel through the production popup's No button.
        std::vector<iWidget*> widgets(1, menu->GetSet()->GetAttentionWidget());
        bool answered = false;
        while(!widgets.empty())
        {
            iWidget* widget = widgets.back(); widgets.pop_back();
            if(!widget) continue;
            if(widget->GetType() == eWidgetType_Button && widget->GetText() == kTranslate("MainMenu", "No"))
            {
                answered = widget->ProcessMessage(eGuiMessage_ButtonPressed, cGuiMessageData());
                break;
            }
            for(auto* child : widget->GetChildren()) widgets.push_back(child);
        }
        menu->GetSet()->Update(0);
        if(!answered || menu->mpQuitConfirmation || menu->mbExiting ||
           gpBase->mpMapHandler->GetCurrentMap() != map || gpBase->mpEngine->GetGameIsDone())
        {
            error = "cancelling offline close changed the map or left a quit pending";
            return false;
        }
    }
    updater->SetContainer("Default");
    printStatus("PASS: offline quit captures a fresh background from game, inventory and journal");
    return true;
}
#endif
