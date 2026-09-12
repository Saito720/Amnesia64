// Attach networking to a progressed offline retail map without replacing it.
#ifndef MULTIPLAYER_HOSTING_REGRESSION_H
#define MULTIPLAYER_HOSTING_REGRESSION_H
#include "LuxProp_Item.h"
#include "LuxProp_Object.h"
#include "LuxMultiplayerContent.h"

static bool StartCurrentMapHost(const cLuxMultiplayerSettings& settings,tString& error)
{
    cLuxMultiplayer* session=gpBase->mpMultiplayer;
    if(!gpBase->mpUserConfig) {
        gpBase->CreateProfile(gpBase->msDefaultProfileName);
        gpBase->SetProfile(gpBase->msDefaultProfileName);
        gpBase->InitUserConfig();
    }
    gpBase->mpEngine->GetUpdater()->SetContainer("Default");
    gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
    const tString map=settings.map.empty()?gpBase->msStartMapFile:cString::GetFileName(settings.map);
    const tString folder=settings.map.empty()?gpBase->msStartMapFolder:cString::GetFilePath(settings.map);
    // An isolated copy has no compiled map cache, so the real offline loader
    // builds a certifiable XML world without changing retail cache behavior.
    const auto sourceFolder=std::filesystem::path(outputDir)/"current-map-source";
    std::filesystem::create_directory(sourceFolder);
    std::vector<uint8_t> source;
    tWString resolvedSourceFolder;
    auto copySource=[&](const tString& name,bool required) {
        std::vector<uint8_t> bytes;
        tWString path=required?cString::To16Char(folder+name):resolvedSourceFolder+cString::To16Char(name);
        // The retail startup folder is maps/main/, while its XML lives in
        // ch01/. Match native map loading through the resource search index.
        if(!cPlatform::FileExists(path))
            path=gpBase->mpEngine->GetResources()->GetFileSearcher()->GetFilePath(folder+name);
        if(path.empty() && !required) return true;
        if(!LuxReadMultiplayerMap(path,bytes)) {
            error="cannot read offline fixture source: "+folder+name+" (resolved: "+cString::To8Char(path)+")";return false;
        }
        if(required) resolvedSourceFolder=cString::GetFilePathW(path);
        const tWString destination=(sourceFolder/name).wstring();
        FILE* file=cPlatform::OpenFile(destination,_W("wb"));
        if(!file) {error="cannot open offline fixture destination: "+cString::To8Char(destination);return false;}
        const bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
        const bool closed=std::fclose(file)==0;
        if(required) source=bytes;
        if(!written || !closed) error="cannot complete offline fixture write: "+cString::To8Char(destination);
        return written && closed;
    };
    if(!copySource(map,true) || !copySource(cString::SetFileExt(map,"hps"),false)) {
        return false;
    }
    gpBase->mpEngine->GetResources()->AddResourceDir(sourceFolder.wstring(),false);
    if(!gpBase->StartGame(map,sourceFolder.generic_string()+"/",gpBase->msStartMapPos)) {error="could not load offline hosting fixture";return false;}
    cLuxMap* current=gpBase->mpMapHandler->GetCurrentMap();
    if(!LuxValidateMultiplayerCurrentMapSource(current->GetWorld(),source,error)) return false;
    // Edit only a copied byte buffer. A semantic edit must not match the live
    // world, while harmless formatting still describes its original source.
    tString changed(source.begin(),source.end());
    const size_t name=changed.find("tinderbox_3");
    if(name==tString::npos) {error="map-source mutation target missing";return false;}
    changed.replace(name,11,"tinderbox_4");
    if(LuxValidateMultiplayerCurrentMapSource(current->GetWorld(),std::vector<uint8_t>(changed.begin(),changed.end()),error)) {
        error="changed entity XML was accepted as the loaded world's source";return false;
    }
    source.insert(source.begin(),'\n');
    if(!LuxValidateMultiplayerCurrentMapSource(current->GetWorld(),source,error)) return false;
    cLuxProp_Item* item=static_cast<cLuxProp_Item*>(current->GetEntityByName("tinderbox_3",eLuxEntityType_Prop,eLuxPropType_Item));
    if(!item || !item->GetBodyNum()) {error="offline hosting pickup fixture missing";return false;}
    const int tinderboxes=gpBase->mpPlayer->GetTinderboxes();
    item->OnInteract(item->GetBody(0),item->GetBody(0)->GetWorldPosition());
    if(!item->GetDestroyMe() || gpBase->mpPlayer->GetTinderboxes()!=tinderboxes+1) {
        error="offline hosting fixture did not complete the native item pickup";return false;
    }
    cLuxInventory_Item* inventory=gpBase->mpInventory->AddItem("codex_host_current_sentinel",eLuxItemType_Puzzle,"KeyTower","key_tower.tga",1,"","");
    if(!inventory) {error="offline hosting inventory sentinel failed";return false;}
    const cVector3f position=gpBase->mpPlayer->GetCharacterBody()->GetPosition()+cVector3f(0.1f,0,0.1f);
    gpBase->mpPlayer->GetCharacterBody()->SetPosition(position);
    const float speed=gpBase->mpPlayer->GetScriptMoveSpeedMul();
    gpBase->mpPlayer->SetScriptMoveSpeedMul(0.72f);gpBase->mbHardMode=true;
    // Hosting from an offline pause menu must resume the existing world.
    gpBase->mpEngine->GetUpdater()->SetContainer("MainMenu");
    gpBase->mpInputHandler->ChangeState(eLuxInputState_MainMenu);
    auto* chair=static_cast<cLuxProp_Object*>(current->GetEntityByName("chair_wood02_1",eLuxEntityType_Prop,eLuxPropType_Object));
    if(!chair || !chair->GetBodyNum()) {error="offline hosting grab fixture missing";return false;}
    iPhysicsBody* held=chair->GetMainBody();
    const float heldMass=held->GetMass();const bool heldGravity=held->GetGravity();
    chair->OnInteract(held,held->GetWorldPosition());
    if(gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_InteractGrab || held->GetGravity()) {
        error="offline hosting fixture did not enter its native grab state";return false;
    }
    cLuxMultiplayerSettings invalid=settings;invalid.maxPlayers=1;
    if(session->HostCurrentMap(invalid) || session->IsActive() || gpBase->mpMapHandler->GetCurrentMap()!=current ||
       gpBase->mpPlayer->GetCharacterBody()->GetPosition()!=position || gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_InteractGrab) {
        error="invalid current-map settings changed the offline game";return false;
    }
    if(!session->HostCurrentMap(settings)) {error=session->GetStatus();return false;}
    if(gpBase->mpMapHandler->GetCurrentMap()!=current || gpBase->mpPlayer->GetCharacterBody()->GetPosition()!=position ||
       gpBase->mpInventory->GetItem("codex_host_current_sentinel")!=inventory ||
       gpBase->mpPlayer->GetTinderboxes()!=tinderboxes+1 || gpBase->mpPlayer->GetScriptMoveSpeedMul()!=0.72f || !gpBase->mbHardMode ||
       gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!="Default" ||
       gpBase->mpInputHandler->GetState()!=eLuxInputState_Game || !current->GetWorld()->IsActive() ||
       gpBase->mpPlayer->GetCurrentState()!=eLuxPlayerState_Normal || held->GetGravity()!=heldGravity || held->GetMass()!=heldMass) {
        error="hosting the current map reset the live world, player, inventory or active game mode";return false;
    }
    gpBase->mpPlayer->SetScriptMoveSpeedMul(speed);
    mark("host-current-map-passed.txt","PASS: offline pickup, inventory, world identity, position and modifiers survive hosting from a paused menu; held-object PID and mass/gravity restored before network attachment.");
    printStatus("PASS: current-map hosting preserves the progressed live world and resumes a paused menu");
    return true;
}

static int VerifyCurrentMapHostPickup(tString& error)
{
    static Uint32 waiting=0;
    if(!exists(role+"-current-map-hardmode.txt")) {
        if(!gpBase->mbHardMode) {error="current-map difficulty did not reach both players before joining";return -1;}
        mark(role+"-current-map-hardmode.txt","PASS: current-map Hard Mode preserved on host and applied before client load.");
    }
    if(!exists("host-current-map-hardmode.txt") || !exists("client-current-map-hardmode.txt")) return 0;
    gpBase->mbHardMode=false;
    iLuxEntity* item=gpBase->mpMapHandler->GetCurrentMap()->GetEntityByName("tinderbox_3");
    if(item && !item->GetDestroyMe()) {
        if(!waiting) waiting=SDL_GetTicks();
        if(SDL_GetTicks()-waiting>10000) {error="joining the current map resurrected an offline-collected item";return -1;}
        return 0;
    }
    mark(role+"-current-map-pickup-passed.txt","PASS: an item collected before hosting remains removed in the client baseline.");
    return 1;
}
#endif
