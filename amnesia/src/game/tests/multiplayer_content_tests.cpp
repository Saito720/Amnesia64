#include "hpl.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#undef main
using namespace hpl;
#define LUX_BASE_H
struct cLuxBase { cEngine* mpEngine; };
cLuxBase* gpBase = nullptr;
// Compile the production validator with only its application singleton stubbed.
#include "../LuxMultiplayerContent.cpp"
#include "../LuxMultiplayerCache.cpp"

static void Require(bool value, const char* message)
{
    if(!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static std::vector<uint8_t> Bytes(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }
#include "MultiplayerCacheMetricsTests.h"
#include "MultiplayerCachePathTests.h"
static void TestMapCache(const tWString& logPath) {
    const auto parent=std::filesystem::path(logPath).parent_path();
    tWString root;
    for(unsigned attempt=0;attempt<100;++attempt) {
        const tWString candidate=(parent/(_W("cache-storage_")+cString::To16Char(cString::ToString(cPlatform::GetApplicationTime()))+
            _W('_')+cString::To16Char(cString::ToString(attempt)))).wstring();
        if(cPlatform::CreateFolder(candidate)) {root=candidate;break;}
    }
    Require(!root.empty(),"reserve exclusive workspace cache-test directory");
    const auto first=Bytes("<Level>map cache test one</Level>"), second=Bytes("<Level>map cache test two</Level>");
    const tString firstHash=luxnet::MapHash(first),secondHash=luxnet::MapHash(second);
    const tWString objects=root+_W("/objects"),firstPath=objects+_W('/')+cString::To16Char(firstHash)+_W(".map");
    std::vector<uint8_t> loaded={1};
    Require(!LuxReadCachedMultiplayerMap(firstHash,static_cast<uint32_t>(first.size()),loaded,root) && loaded.empty(),"missing cache entry clears result");
    Require(!LuxStoreCachedMultiplayerMap("../../escape",first,root),"cache key cannot escape objects directory");
    Require(!LuxStoreCachedMultiplayerMap(firstHash,second,root),"cache store rejects mismatched payload hash");
    Require(LuxStoreCachedMultiplayerMap(firstHash,first,root),"cache store creates missing objects directory");
    Require(LuxReadCachedMultiplayerMap(firstHash,static_cast<uint32_t>(first.size()),loaded,root) && loaded==first,"cache hit verifies exact bytes");
    Require(!LuxReadCachedMultiplayerMap(firstHash,static_cast<uint32_t>(first.size()+1),loaded,root) && loaded.empty(),"cache size mismatch is a miss");
    const auto savedTime=std::filesystem::file_time_type::clock::now()-std::chrono::hours(24);
    std::filesystem::last_write_time(std::filesystem::path(firstPath),savedTime);
    const auto timestamp=std::filesystem::last_write_time(std::filesystem::path(firstPath));
    Require(LuxStoreCachedMultiplayerMap(firstHash,first,root),"existing valid cache entry remains usable");
    Require(std::filesystem::last_write_time(std::filesystem::path(firstPath))==timestamp,"existing valid entry is not rewritten");
    auto write=[](const tWString& path,const std::vector<uint8_t>& bytes) {
        FILE* file=cPlatform::OpenFile(path,_W("wb"));Require(file!=nullptr,"create isolated cache fixture file");
        const bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
        const bool closed=std::fclose(file)==0;Require(written && closed,"write isolated cache fixture file");
    };
    const tWString blockedRoot=root+_W("/not-a-directory");write(blockedRoot,first);
    Require(!LuxStoreCachedMultiplayerMap(firstHash,first,blockedRoot) && LuxClearMultiplayerMapCache(blockedRoot)==0,
        "cache operations cannot replace an unrelated file used as a root");
    Require(LuxReadMultiplayerMap(blockedRoot,loaded) && loaded==first,"unrelated root file remains unchanged");
    cPlatform::RemoveFile(blockedRoot);
    auto corrupt=first;corrupt[8]^=1;write(firstPath,corrupt);
    Require(!LuxReadCachedMultiplayerMap(firstHash,static_cast<uint32_t>(first.size()),loaded,root) && loaded.empty(),"same-sized corruption cannot masquerade as a cache hit");
    Require(LuxStoreCachedMultiplayerMap(firstHash,first,root),"verified download repairs corrupted recognized cache object");
    Require(LuxReadCachedMultiplayerMap(firstHash,static_cast<uint32_t>(first.size()),loaded,root) && loaded==first,"repaired entry verifies again");
    Require(LuxStoreCachedMultiplayerMap(secondHash,second,root),"store independent cached map");
    const tWString active=root+_W("/123_1"),unrelated=objects+_W("/leave-me.txt"),unknownMap=objects+_W("/not-a-hash.map");
    Require(cPlatform::CreateFolder(active),"create active-copy directory beside objects");
    write(active+_W("/active.map"),first);write(unrelated,second);write(unknownMap,first);
    Require(LuxClearMultiplayerMapCache(root)==2,"clear removes only two recognized stored objects");
    Require(cPlatform::FileExists(active+_W("/active.map")) && cPlatform::FileExists(unrelated) && cPlatform::FileExists(unknownMap),"clear preserves active copies and unrelated files");
    Require(LuxClearMultiplayerMapCache(root)==0,"repeated cache clear is idempotent");
    tWStringList temporaryFolders;cPlatform::FindFoldersInDir(temporaryFolders,objects,true,false);
    Require(temporaryFolders.empty(),"atomic store leaves no temporary directories");
    cPlatform::RemoveFile(active+_W("/active.map"));cPlatform::RemoveFile(unrelated);cPlatform::RemoveFile(unknownMap);
    Require(cPlatform::RemoveFolder(active,false,false) && cPlatform::RemoveFolder(objects,false,false) &&
        cPlatform::RemoveFolder(root,false,false),"remove only exact generated fixture files and empty directories");
    std::puts("PASS: persistent cache verification, corruption repair, atomic writes, existing-entry preservation and scoped clearing");
}
int hplMain(const tString&) { return 0; }
int main(int argc, char** argv)
{
    Require(argc == 2 || argc==3, "provide the absolute test log path and optional --cache-only");
    SetLogFile(cString::To16Char(argv[1]));
    TestMapCache(cString::To16Char(argv[1]));
    TestMapCacheMetrics(cString::To16Char(argv[1]));
    TestMapCachePaths(cString::To16Char(argv[1]));
    if(argc==3) {Require(tString(argv[2])=="--cache-only","known focused test mode");return 0;}
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
    Require(LuxValidateMultiplayerAsset("react_scare6", "gui_sound", error, resources), "Archives PlayGuiSound extensionless raw sample");
    Require(LuxValidateMultiplayerAsset("react_scare6.ogg", "gui_sound", error, resources), "PlayGuiSound explicit raw sample");
    Require(LuxValidateMultiplayerAsset("react_scare", "gui_sound", error, resources), "PlayGuiSound extensionless sound entity and numbered sample dependencies");
    Require(LuxValidateMultiplayerAsset("react_scare.snt", "gui_sound", error, resources), "PlayGuiSound explicit sound entity");
    Require(!LuxValidateMultiplayerAsset("react_scare6.snt", "gui_sound", error, resources), "explicit missing sound entity cannot fall back to an unrelated raw sample");
    Require(!LuxValidateMultiplayerAsset("absent-network-test-sound", "gui_sound", error, resources) && error.find("absent-network-test-sound") != tString::npos, "genuinely missing GUI sound rejected by name");
    // These retail PreloadSound hints name raw samples, not sound entities.
    // A failed optional warm-up must not loosen actual playback validation.
    for(const char* sample : {"water_lurker_eat_rev2", "guardian_idle6", "26_zimmerman_part1.ogg",
        "insanity_monster_roar02", "insanity_monster_roar03", "12_event_blood", "11_event_tree"}) {
        Require(LuxValidateMultiplayerAsset(sample, "audio", error, resources), "reported retail sound resolves for raw playback");
        Require(LuxValidateMultiplayerAsset(sample, "gui_sound", error, resources), "reported retail sound resolves for GUI playback");
        Require(!LuxValidateMultiplayerAsset(sample, "snt", error, resources), "raw sample does not masquerade as a sound entity");
    }
    for(const char* hint : {"8_done02.snt", "ater_lurker_hunt", "waterlurker_run_splash"})
        Require(!LuxValidateMultiplayerAsset(hint, "snt", error, resources), "invalid retail preload hint stays invalid as required sound entity");
    Require(LuxValidateMultiplayerAsset("28_done02.snt", "snt", error, resources), "Inner Sanctum actual playback uses correct sound entity");
    Require(LuxValidateMultiplayerAsset("water_lurker_hunt", "snt", error, resources), "Archives Cellar actual playback uses correct sound entity");
    Require(LuxValidateMultiplayerAsset("waterlurker_run_splash", "ps", error, resources), "retail sound preload typo names a particle effect");
    Require(LuxValidateMultiplayerAsset("CH01L00_DanielsMind01_01.ogg", "audio", error, resources), "AddEffectVoice localized voice");
    Require(LuxValidateMultiplayerAsset("CH01L00_DanielsMind01_01", "audio", error, resources), "extensionless localized voice");
    Require(LuxValidateMultiplayerAsset("", "audio", error, resources), "empty optional voice effect file");
    Require(LuxValidateMultiplayerAsset("menu_loading_screen", "texture", error, resources), "extensionless loading image");
    Require(LuxValidateMultiplayerAsset("oil_cubemap.dds", "cubemap", error, resources), "DDS cubemap");
    Require(!LuxValidateMultiplayerAsset("missing-cubemap-test", "cubemap", error, resources) && error.find("_pos_x") != tString::npos, "missing cubemap identifies missing faces");
    Require(LuxValidateMultiplayerAsset("../../../../../", "cubemap", error, resources), "scripted cubemap with no filename remains unset");
    for(const char* emptyTexture : {"", "../../../../../", "..\\..\\..\\..\\..\\", "textures/", ".", ".."})
        Require(LuxValidateMultiplayerMap(Bytes(std::string("<Level><MapData SkyBoxTexture='") + emptyTexture + "'><MapContents/></MapData></Level>"), error, resources), "optional skybox directory-only path is empty");
    Require(!LuxValidateMultiplayerMap(Bytes("<Level><MapData SkyBoxTexture='../../../../../absent-network-test-sky.dds'><MapContents/></MapData></Level>"), error, resources) && error.find("absent-network-test-sky.dds") != tString::npos, "relative skybox path with actual missing filename rejected");
    Require(!LuxValidateMultiplayerMap(Bytes("<Level><MapData SkyBoxTexture='../../../../../caf\xc3\xa9.dds'><MapContents/></MapData></Level>"), error, resources), "UTF-8 skybox filename is not mistaken for an empty path");
    Require(!LuxValidateMultiplayerMap(Bytes("<Level><MapData><MapContents><Entities><ParticleSystem ID='1' File='../../../../../'/></Entities></MapContents></MapData></Level>"), error, resources), "directory-only required particle resource still rejected");
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
    std::puts("PASS: map structure, bounds, nesting, GUI sound resolution, optional texture paths, missing dependencies and retail maps");
}
