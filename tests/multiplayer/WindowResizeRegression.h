#ifndef MULTIPLAYER_WINDOW_RESIZE_REGRESSION_H
#define MULTIPLAYER_WINDOW_RESIZE_REGRESSION_H
#include "../../HPL2/tests/WindowResize/PopupResizeRegression.h"
#include "gui/GuiPopUpMessageBox.h"
#include "graphics/RendererDeferred.h"
#include <thread>
#include <chrono>
#if defined(_WIN32)
#include <SDL2/SDL_syswm.h>
#endif

// Optional combined-game coverage; all geometry changes run through the real
// outer engine loop, with no direct invocation of resize callbacks.
class cWindowResizeRegression {
    unsigned initialPhase=0,sessionPhase=0,sessionFrames=0;
    Uint32 started=0;
    uint32_t sequence=0;
    bool sawCapture=false;
    cGuiPopUpMessageBox* popup=NULL;
    iWidget* attention=NULL;
    std::thread cancel;
    static bool sizeIs(int width,int height) {
        return gpBase->mpEngine->GetGraphics()->GetLowLevel()->GetScreenSizeInt()==cVector2l(width,height);
    }
    static void resize(int width,int height) { SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),width,height); }
    static int fail(tString& error,const tString& message) { error="window resize: "+message;return -1; }
public:
    ~cWindowResizeRegression() { if(cancel.joinable()) cancel.join(); }
    int Initial(tString& error) {
        auto* gui=gpBase->mpMainMenu->GetSet();
        if(initialPhase==0) {
            if(!RunPopupResizeRegression(gpBase->mpEngine->GetGui(),gui->GetSkin(),error)) return -1;
            resize(1920,1080);started=SDL_GetTicks();initialPhase=1;return 0;
        }
        if(SDL_GetTicks()-started>15000) return fail(error,"initial popup resize did not finish");
        if(initialPhase==1) {
            if(!sizeIs(1920,1080)) return 0;
            popup=gui->CreatePopUpMessageBox(_W("Resize regression"),_W("Keep this confirmation accessible"),_W("OK"),_W("Cancel"),NULL,NULL);
            attention=gui->GetAttentionWidget();initialPhase=2;started=SDL_GetTicks();return 0;
        }
        if(initialPhase==2) {
            if(SDL_GetTicks()-started<150) return 0;
            resize(640,480);initialPhase=3;return 0;
        }
        if(initialPhase==3) {
            if(!sizeIs(640,480)) return 0;
            const auto position=attention->GetGlobalPosition();const auto extent=attention->GetSize();const auto view=gui->GetVirtualSize();
            if(gui->GetAttentionWidget()!=attention || !gui->PopUpIsActive() || position.x<0 || position.y<0 ||
                position.x+extent.x>view.x+1 || position.y+extent.y>view.y+1)
                return fail(error,"native menu confirmation became inaccessible or lost modal focus after shrinking");
            gui->DestroyPopUp(popup);popup=NULL;attention=NULL;
            resize(800,600);initialPhase=4;return 0;
        }
        if(!sizeIs(800,600)) return 0;
        mark(role+"-resize-popup-passed.txt","PASS: generic modal layout and actual main-menu resize retain visible controls and modal focus.");
        return 1;
    }
    int Session(tString& error) {
#if defined(_WIN32)
        auto* session=gpBase->mpMultiplayer;
        auto* world=session->GetWorld();
        if(sessionPhase==0) {
            // Test one native capture at a time; the other real peer must keep
            // receiving poses and ticking while this process resizes.
            if(role=="client" && !exists("host-resize-native-passed.txt")) return 0;
            SDL_SysWMinfo info;SDL_VERSION(&info.version);
            if(!SDL_GetWindowWMInfo(SDL_GL_GetCurrentWindow(),&info)) return fail(error,"could not get test window handle");
            const HWND handle=info.info.win.window;
            sequence=world->mlSequence;sessionFrames=0;started=SDL_GetTicks();sessionPhase=1;
            cancel=std::thread([handle] {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                PostMessage(handle,WM_CANCELMODE,0,0);
            });
            if(!PostMessage(handle,WM_SYSCOMMAND,SC_SIZE|WMSZ_RIGHT,0)) return fail(error,"could not start native sizing");
            return 0;
        }
        if(sessionPhase==1) {
            ++sessionFrames;
            SDL_SysWMinfo info;SDL_VERSION(&info.version);
            if(SDL_GetWindowWMInfo(SDL_GL_GetCurrentWindow(),&info) && GetCapture()==info.info.win.window) sawCapture=true;
            if(SDL_GetTicks()-started<2200) return 0;
            cancel.join();
            if(!sawCapture || sessionFrames<30 || world->mlSequence<=sequence+10 || !session->IsReady() || world->GetRemotePlayers().empty())
                return fail(error,"native sizing blocked gameplay or session updates");
            for(const auto& peer:world->GetRemotePlayers()) if(peer.second.age>0.5f)
                return fail(error,"remote poses stopped arriving during native sizing");
            mark(role+"-resize-native-passed.txt","PASS: real game/session updates and reciprocal poses continue through a two-second native sizing operation.");
            sessionPhase=2;
        }
        if(!exists("host-resize-native-passed.txt") || !exists("client-resize-native-passed.txt")) return 0;
#endif
        return 1;
    }
    static bool ValidateTargets(tString& error) {
        auto* graphics=gpBase->mpEngine->GetGraphics();
        auto* renderer=static_cast<cRendererDeferred*>(graphics->GetRenderer(eRenderer_Main));
        auto* effects=gpBase->mpEffectRenderer;
        const auto size=graphics->GetLowLevel()->GetScreenSizeInt();
        if(effects->mpFrameBufferColor->GetSize()!=size || effects->mpOutlineColorTexture->GetSizeInt2D()!=size ||
            effects->mpFrameBufferColor->GetDepthBuffer()!=renderer->GetDepthStencilBuffer() ||
            effects->mpDeferredAccumBuffer!=renderer->GetAccumBuffer()) {
            error="resize left stale game outline/depth attachments";return false;
        }
        if(!effects->mpFrameBufferColor->CompileAndValidate()) {error="resized game outline framebuffer is incomplete";return false;}
        graphics->GetLowLevel()->SetCurrentFrameBuffer(NULL);
        return true;
    }
};
#endif
