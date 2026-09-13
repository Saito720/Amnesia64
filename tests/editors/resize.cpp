#include "hpl.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#undef main
// Expose only directory data to isolate the existing editor configuration loader.
#define protected public
#include "DirectoryHandler.h"
#undef protected
#include "EditorWindowViewport.h"
#include "EditorWindowMaterialEditor.h"
#include "EditorWorld.h"
#include EDITOR_HEADER
using namespace hpl;
static tWString output;
static void Require(bool ok, const char* message) {
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::fflush(NULL); std::_Exit(1); }
}
static void PostNativeQuit(cEngine* engine) {
    SDL_Event event={}; event.type=SDL_QUIT;
    Require(SDL_PushEvent(&event)==1,"post native close event");
    engine->GetInput()->Update(0.016f);
    Require(engine->GetInput()->isQuitMessagePosted(),"native close reaches engine input");
    engine->GetUpdater()->RunMessage(eUpdateableMessage_OnQuit);
    engine->GetInput()->resetQuitMessagePosted();
}
static void ClickDialog(cGuiSet* set,const tWString& text) {
    auto* window=set->GetAttentionWidget();
    Require(window!=NULL,"exit dialog has attention");
    for(auto* child : window->GetChildren()) {
        if(child->GetType()==eWidgetType_Button && child->GetText()==text) {
            child->ProcessMessage(eGuiMessage_ButtonPressed,cGuiMessageData(0));
            set->Update(0.016f);
            return;
        }
    }
    Require(false,"exit dialog button exists");
}
class cResizeEditor : public EDITOR_CLASS {
protected:
    void OnLoadConfig() override {
        mpDirHandler->msHomeDir=output+_W("/");
        mpDirHandler->msTempDir=output+_W("/");
        mpDirHandler->msThumbnailDir=output+_W("/");
        EDITOR_CLASS::OnLoadConfig();
    }
public:
#ifdef MATERIAL_EDITOR_TEST
    cResizeEditor() : EDITOR_CLASS("") {}
#endif
    void Check(const cVector2l& size) {
        Require(mvScreenSize==cVector2f((float)size.x,(float)size.y),"editor follows native window size");
        Require(mpBGFrame->GetSize()==mvScreenSize,"background fills window");
        if(mpMainMenu) Require(mpMainMenu->GetSize().x==size.x,"menu fills window width");
        for(auto* view : mvViewports) {
            const auto area=view->GetGuiViewportSize();
            Require(area.x>0 && area.y>0,"positive viewport size");
            Require(std::fabs(view->GetVCamera()->GetEngineCamera()->GetAspect()-area.x/area.y)<0.001f,"camera aspect follows viewport");
            auto* fb=view->GetFrameBuffer();
            Require(fb->GetColorBuffer(0)->ToTexture()->GetSizeInt2D()==fb->GetSize(),"color target matches framebuffer");
            Require(static_cast<iDepthStencilBuffer*>(fb->GetDepthBuffer())->GetSize()==fb->GetSize(),"depth target matches framebuffer");
            Require(fb->CompileAndValidate(),"framebuffer remains complete");
        }
        if(mvViewports.empty() && mpMaterialEditor) {
            const auto area=mpMaterialEditor->GetGuiViewportSize();
            Require(area.x>0 && area.y>0,"material preview stays positive");
            Require(mpMaterialEditor->GetFrameBuffer()->GetSize()==cVector2l((int)area.x,(int)area.y),"material render target follows preview");
            Require(mpMaterialEditor->GetFrameBuffer()->CompileAndValidate(),"material target remains complete");
        }
    }
};
int hplMain(const tString&) { return 0; }
int main(int argc,char** argv) {
    Require(argc==2,"output directory required"); output=cString::To16Char(argv[1]);
    SetLogFile(output+_W("/resize.log"));
    cResources::SetForceCacheLoadingAndSkipSaving(true);
    cEngineInitVars vars;
    vars.mGraphics.mvScreenSize=cVector2l(1024,768);
    vars.mGraphics.mvWindowPosition=cVector2l(-10000,-10000);
    vars.mSound.mbUseHRTF=false; vars.mSound.mbUseThreading=false;
    auto* engine=CreateHPLEngine(eHplAPI_OpenGL,eHplSetup_All,&vars);
    Require(engine!=NULL,"create engine");
    SDL_Window* window=SDL_GL_GetCurrentWindow(); SDL_HideWindow(window);
    engine->GetResources()->LoadResourceDirsFile("resources.cfg");
    auto* editor=hplNew(cResizeEditor,());
    editor->Init(engine,"ResizeTest","test",false);
    auto* world=editor->GetEditorWorld();
    auto* focus=editor->GetFocusedViewport();
    const auto views=editor->GetViewports();
    for(int pass=0;pass<2;++pass) {
        if(views.size()==4) editor->SetViewportEnlarged(pass==1);
        for(const auto size : {cVector2l(1280,900),cVector2l(901,701),cVector2l(1600,1000),cVector2l(1024,768)}) {
            SDL_SetWindowSize(window,size.x,size.y); SDL_PumpEvents();
            if(engine->GetGraphics()->GetLowLevel()->UpdateScreenSize())
                engine->GetUpdater()->BroadcastMessageToAll(eUpdateableMessage_OnScreenResize);
            editor->Check(size);
            Require(editor->GetEditorWorld()==world && editor->GetFocusedViewport()==focus && editor->GetViewports()==views,"resize preserves document and viewport instances");
            if(views.size()==4) Require(focus->IsEnlarged()==(pass==1),"enlarged viewport state survives resize");
            editor->Update(0.016f);
            engine->GetGui()->Update(0.016f);
            engine->GetUpdater()->RunMessage(eUpdateableMessage_OnDraw,0.016f);
            engine->GetScene()->Render(0.016f,tSceneRenderFlag_All);
            if(pass==0 && size==cVector2l(1280,900)) {
                auto* bitmap=engine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
                Require(bitmap!=NULL,"capture resized editor");
                engine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(bitmap,output+_W("/resized.png"),0);
                hplDelete(bitmap);
            }
            engine->GetGraphics()->GetLowLevel()->SwapBuffers();
        }
    }
    std::puts("PASS: repeated native resizes, render targets, camera aspect, document identity and enlarged viewports");
    if(world) world->UpdateSavedModifications();
    PostNativeQuit(engine);
    auto* dialog=editor->GetSet()->GetAttentionWidget();
    Require(dialog && dialog->GetText()==_W("Exiting") && !engine->GetGameIsDone(),"close asks before exiting");
    PostNativeQuit(engine);
    Require(editor->GetSet()->GetAttentionWidget()==dialog,"repeated close does not stack exit dialogs");
    ClickDialog(editor->GetSet(),_W("No"));
    Require(!engine->GetGameIsDone() && !editor->GetSet()->PopUpIsActive(),"cancel close returns to editor");
    if(world) {
        world->IncModifications();
        PostNativeQuit(engine);
        Require(editor->GetSet()->GetAttentionWidget()->GetText()==_W("Warning") && !engine->GetGameIsDone(),"dirty close uses unsaved-changes prompt");
        ClickDialog(editor->GetSet(),_W("No"));
        ClickDialog(editor->GetSet(),_W("No"));
        Require(world->IsModified() && !engine->GetGameIsDone(),"cancel preserves unsaved document");
        world->UpdateSavedModifications();
    }
    PostNativeQuit(engine);
    ClickDialog(editor->GetSet(),_W("Yes"));
    Require(engine->GetGameIsDone(),"confirm close invokes editor exit");
    std::puts("PASS: native close, confirmation, cancellation, duplicate close events and applicable unsaved changes");
    // Do not run editor shutdown, which persists preferences and recent files.
    std::fflush(NULL); std::_Exit(0);
}
