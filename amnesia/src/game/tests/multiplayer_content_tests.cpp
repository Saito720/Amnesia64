#include "hpl.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#undef main
using namespace hpl;
#define LUX_BASE_H
struct cLuxBase { cEngine* mpEngine; };
cLuxBase* gpBase = nullptr;
// Compile the production validator with only its application singleton stubbed.
#include "../LuxMultiplayerContent.cpp"

static void Require(bool value, const char* message)
{
    if(!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static std::vector<uint8_t> Bytes(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }
int hplMain(const tString&) { return 0; }
int main(int argc, char** argv)
{
    Require(argc == 2, "provide the absolute test log path");
    SetLogFile(cString::To16Char(argv[1]));
    cResources::SetForceCacheLoadingAndSkipSaving(true);
    cEngineInitVars vars;
    vars.mGraphics.mvScreenSize = cVector2l(320, 240);
    vars.mGraphics.mvWindowPosition = cVector2l(-10000, -10000);
    vars.mSound.mbUseHRTF = false; vars.mSound.mbUseThreading = false;
    cEngine* engine = CreateHPLEngine(eHplAPI_OpenGL, eHplSetup_Screen, &vars);
    Require(engine != nullptr, "create engine");
    SDL_HideWindow(SDL_GL_GetCurrentWindow());
    cResources* resources = engine->GetResources();
    Require(resources->LoadResourceDirsFile("resources.cfg"), "load retail resource directories");
    resources->AddResourceDir(_W("lang/eng/voices"), true);
    tString error;
    Require(LuxValidateMultiplayerAsset("fb_sfx_00_daniel.ogg", "audio", error, resources), "PlayGuiSound explicit .ogg remains raw audio");
    Require(!LuxValidateMultiplayerAsset("fb_sfx_00_daniel.ogg", "snt", error, resources), "sound entity category must not silently accept raw audio");
    Require(LuxValidateMultiplayerAsset("CH01L00_DanielsMind01_01.ogg", "audio", error, resources), "AddEffectVoice localized voice");
    Require(LuxValidateMultiplayerAsset("CH01L00_DanielsMind01_01", "audio", error, resources), "extensionless localized voice");
    Require(LuxValidateMultiplayerAsset("", "audio", error, resources), "empty optional voice effect file");
    Require(LuxValidateMultiplayerAsset("menu_loading_screen", "texture", error, resources), "extensionless loading image");
    Require(LuxValidateMultiplayerAsset("oil_cubemap.dds", "cubemap", error, resources), "DDS cubemap");
    Require(!LuxValidateMultiplayerAsset("missing-cubemap-test", "cubemap", error, resources) && error.find("_pos_x") != tString::npos, "missing cubemap identifies missing faces");
    Require(LuxValidateMultiplayerMap(Bytes("<Level><MapData><MapContents/></MapData></Level>"), error, resources), "minimal valid map");
    Require(!LuxValidateMultiplayerMap(Bytes(""), error, resources), "empty XML rejected before parser");
    Require(!LuxValidateMultiplayerMap(Bytes("<!-- nothing -->"), error, resources), "comment-only XML rejected before parser");
    Require(!LuxValidateMultiplayerMap(Bytes("<!DOCTYPE Level><Level/>"), error, resources), "document declarations rejected");
    Require(!LuxValidateMultiplayerMap(Bytes("<Level><MapData><MapContents><FileIndex_Entities NumOfFiles='1'><File Id='3' Path='x.ent'/></FileIndex_Entities></MapContents></MapData></Level>"), error, resources), "out-of-bounds file index rejected");
    Require(!LuxValidateMultiplayerMap(Bytes("<Level><MapData><MapContents><Entities><Area ID='1'/><Area ID='1'/></Entities></MapContents></MapData></Level>"), error, resources), "duplicate entity IDs rejected");
    Require(!LuxValidateMultiplayerMap(Bytes("<Level><MapData><MapContents><Entities><ParticleSystem ID='1' File='absent-network-test-particle.ps'/></Entities></MapContents></MapData></Level>"), error, resources), "missing particle dependency rejected");
    Require(error.find("absent-network-test-particle.ps") != tString::npos, "missing asset names shown");
    std::string deep;
    for(unsigned i = 0; i < 110; ++i) deep += "<x>";
    for(unsigned i = 0; i < 110; ++i) deep += "</x>";
    Require(!LuxValidateMultiplayerMap(Bytes(deep), error, resources), "deep XML rejected before recursive parser");
    unsigned passed = 0, failed = 0;
    for(const char* chapter : {"ch01", "ch02", "ch03"})
    {
        const tWString directory = _W("maps/main/") + cString::To16Char(chapter);
        tWStringList files;
        cPlatform::FindFilesInDir(files, directory, _W("*.map"));
        for(const auto& file : files)
        {
            std::vector<uint8_t> bytes;
            Require(LuxReadMultiplayerMap(directory + _W("/") + file, bytes), "read retail map");
            if(LuxValidateMultiplayerMap(bytes, error, resources)) ++passed;
            else { ++failed; std::fprintf(stderr, "RETAIL %s: %s\n", cString::To8Char(file).c_str(), error.c_str()); }
        }
    }
    Require(passed + failed > 20, "test retail campaign maps");
    std::printf("Retail maps validated: %u passed, %u failed\n", passed, failed);
    DestroyHPLEngine(engine);
    Require(failed == 0, "retail campaign validation");
    std::puts("PASS: map structure, bounds, nesting, missing dependencies and retail maps");
}
