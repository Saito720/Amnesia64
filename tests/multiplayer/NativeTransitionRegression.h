#ifndef MULTIPLAYER_NATIVE_TRANSITION_REGRESSION_H
#define MULTIPLAYER_NATIVE_TRANSITION_REGRESSION_H
#include "LuxSavedGame.h"

static unsigned nativeTransitionCallbacks=0;
static void __stdcall CodexNativeTransitionCallback(std::string& name,std::string& event) {
    if(name=="codex_pending_map_pickup" && event=="OnPickup") ++nativeTransitionCallbacks;
}

// Withhold an approved pickup's result until both normal and debug transition
// entry points have been exercised. The real host decoder then commits it, and
// the normal save/load path must exclude its deferred destruction from revisits.
static int RunNativeTransitionRegression(tString& error) {
    auto* mp=gpBase->mpMultiplayer;
    if(role!="host") {
        // The cancellation is a real reliable network message. Wait for the
        // client to process it before either peer starts the pause-menu phase.
        if(!exists("host-native-transition-passed.txt") ||
           mp->GetStatus().find("codex_missing_pending_pickup_")==tString::npos ||
           !mp->IsReady() || mp->GetLoadPhase()!=eLuxMultiplayerLoadPhase_None) return 0;
        mark("client-native-transition-passed.txt","confirmed pickup transition cancellation processed before menu tests");
        return 1;
    }
    if(exists("host-native-transition-passed.txt")) return exists("client-native-transition-passed.txt")?1:0;
    auto* maps=gpBase->mpMapHandler;auto* map=maps->GetCurrentMap();
    auto* entities=mp->GetEntities();
    const tString name="codex_pending_map_pickup";
    const tString missing="codex_missing_pending_pickup_"+std::filesystem::path(outputDir).filename().string()+".map";
    const auto fail=[&](const char* message) {error=message;return -1;};
    if(!map || mp->mPeers.empty() || entities->HasPendingInteractions() ||
       maps->mMapChangeData.mbActive || !mp->msPendingHostMap.empty())
        return fail("native transition fixture needs an idle ready host");
    if(!gpBase->mpEngine->GetSystem()->GetLowLevel()->AddScriptFunc(
        "void CodexNativeTransitionCallback(string &in name, string &in event)",(void*)CodexNativeTransitionCallback))
        return fail("native transition callback registration failed");
    map->CreateEntity(name,"entities/item/key_tower/key_tower.ent",cMatrixf::Identity,1);
    auto* item=static_cast<cLuxProp_Item*>(map->GetEntityByName(name,eLuxEntityType_Prop,eLuxPropType_Item));
    if(!item || item->GetItemType()!=eLuxItemType_Puzzle || !item->GetBodyNum())
        return fail("native transition puzzle pickup could not be loaded");
    item->SetCallbackFunc("CodexNativeTransitionCallback");
    const uint32_t id=item->GetID(),peer=mp->mPeers.begin()->first,token=UINT32_MAX-7;
    const uint32_t epoch=mp->GetMapEpoch();
    entities->mClaims[id]={peer,token,0,item->GetRuntimeID(),false,false};

    maps->ChangeMap(missing,"PlayerStartArea_1","","");
    gpBase->mpEffectHandler->GetFade()->SetDirectAlpha(1);
    maps->Update(0);
    if(!maps->mMapChangeData.mbActive || maps->GetCurrentMap()!=map || mp->GetMapEpoch()!=epoch ||
       !entities->HasPendingInteractions() || item->GetDestroyMe() || nativeTransitionCallbacks ||
       mp->mRemoteItems[peer].count(name))
        return fail("normal transition saved or reset an unconfirmed pickup");
    const auto held=entities->mClaims[id];entities->mClaims.erase(id);
    const bool acceptedWhilePreparing=entities->BeginInteraction(item);
    entities->mClaims[id]=held;
    if(acceptedWhilePreparing) return fail("preparing host accepted a new native interaction");
    if(!mp->HostChangeMap(missing,"PlayerStartArea_1")) return fail("pending debug transition was refused");
    mp->Update(0);
    if(mp->msPendingHostMap!=missing || mp->GetMapEpoch()!=epoch || maps->GetCurrentMap()!=map ||
       !entities->HasPendingInteractions()) return fail("debug transition abandoned a pending pickup");
    // Keep the normal transition queued; release only the fixture's debug request.
    mp->msPendingHostMap.clear();

    luxnet::Writer result(luxnet::NativeResult);result.U32(epoch);result.String(name);result.U32(id);
    result.U32(token);result.U8(1);result.U32(0);
    if(!entities->HandleMessage(peer,result.data)) return fail("delayed pickup result was rejected");
    if(!mp->IsActive() || entities->HasPendingInteractions() || !item->GetDestroyMe() ||
       nativeTransitionCallbacks!=1 || !mp->mRemoteItems[peer].count(name))
        return fail("delayed pickup result did not commit exactly once before map leave");
    // The intentionally missing destination cancels after the real old-map save.
    maps->Update(0);
    if(!mp->IsActive() || maps->mMapChangeData.mbActive || mp->mbMapPreparing ||
       maps->GetCurrentMap()!=map || mp->GetMapEpoch()!=epoch || map->GetEntityByID(id))
        return fail("settled transition did not flush the collected pickup before saving");
    auto* saved=maps->mpSavedGame->GetSavedMap(map->GetName(),false);
    if(!saved) return fail("settled transition did not save its old map");
    auto active=saved->mlstActiveItems.GetIterator();
    while(active.HasNext()) if(active.Next()==int(id)) return fail("saved revisit resurrects a confirmed pickup");
    auto full=saved->mlstFullEntities.GetIterator();
    while(full.HasNext()) if(full.Next()->mlID==int(id)) return fail("full entity save retained a confirmed pickup");
    entities->mRemovedItems.erase(name);mp->mRemoteItems[peer].erase(name);
    gpBase->mpEffectHandler->GetFade()->SetDirectAlpha(0);
    mark("host-native-transition-passed.txt","PASS: normal/debug loads wait for a pickup result; callback and recovery ownership commit once; old-map save excludes deferred pickup destruction.");
    printStatus("PASS: pending pickups settle before normal/debug map changes and revisit saves");
    return 0;
}
#endif
