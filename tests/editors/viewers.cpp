// Compile each production viewer, replacing only its application entry point.
#define hplMain ViewerApplicationMain
#include VIEWER_SOURCE
#undef hplMain
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#undef main
static void Require(bool ok,const char* message) {
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::fflush(NULL); std::_Exit(1); }
}
int hplMain(const tString&) { return 0; }
int main(int argc,char** argv) {
    Require(argc==2,"output directory required");
    const tWString output=cString::To16Char(argv[1]);
    SetLogFile(output+_W("/viewer.log"));
    cResources::SetForceCacheLoadingAndSkipSaving(true);
    cEngineInitVars vars;
    vars.mGraphics.mvScreenSize=cVector2l(1024,768);
    vars.mGraphics.mvWindowPosition=cVector2l(-10000,-10000);
    vars.mSound.mbUseHRTF=false; vars.mSound.mbUseThreading=false;
    gpEngine=CreateHPLEngine(eHplAPI_OpenGL,eHplSetup_All,&vars);
    SDL_Window* window=SDL_GL_GetCurrentWindow(); SDL_HideWindow(window);
    gpEngine->GetResources()->LoadResourceDirsFile("resources.cfg");
    gpEngine->GetResources()->AddResourceDir(_W("viewer"),true);
    auto* viewer=hplNew(cSimpleUpdate,());
    gpEngine->GetUpdater()->AddUpdate("Default",viewer);
    gpSimpleCamera=hplNew(cSimpleCamera,(viewer->GetName(),gpEngine,viewer->mpWorld,10,cVector3f(0,0,9),true));
    gpEngine->GetUpdater()->AddUpdate("Default",gpSimpleCamera);
    viewer->SetupView();
    auto* menu=viewer->mpOptionWindow;
    Require(menu->GetLocalPosition().x+menu->GetSize().x==1014 && menu->GetLocalPosition().y==10,"initial upper-right anchor");
    for(const auto size : {cVector2l(1280,900),cVector2l(901,701),cVector2l(1600,1000),cVector2l(1024,768)}) {
        SDL_SetWindowSize(window,size.x,size.y); SDL_PumpEvents();
        Require(gpEngine->GetGraphics()->GetLowLevel()->UpdateScreenSize(),"native resize received");
        gpEngine->GetUpdater()->BroadcastMessageToAll(eUpdateableMessage_OnScreenResize);
        Require(menu==viewer->mpOptionWindow,"resize preserves existing menu and controls");
        Require(menu->GetLocalPosition().x+menu->GetSize().x==size.x-10 && menu->GetLocalPosition().y==10,"menu stays ten pixels from top and right");
    }
    SDL_Event event={}; event.type=SDL_QUIT;
    Require(SDL_PushEvent(&event)==1,"post native close");
    gpEngine->GetInput()->Update(0.016f);
    Require(gpEngine->GetInput()->isQuitMessagePosted(),"native close reaches viewer input");
    gpEngine->GetUpdater()->RunMessage(eUpdateableMessage_OnQuit);
    gpEngine->GetInput()->resetQuitMessagePosted();
    Require(gpEngine->GetGameIsDone(),"native close invokes viewer exit");
    std::puts("PASS: viewer upper-right anchoring across native resizes and native close signal");
    std::fflush(NULL); std::_Exit(0);
}
