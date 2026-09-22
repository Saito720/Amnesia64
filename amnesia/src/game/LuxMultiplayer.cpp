#include "LuxMultiplayer.h"
#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerCache.h"
#include "LuxMultiplayerMapHash.h"
#include "LuxMultiplayerProtocol.h"
#include "LuxMultiplayerIdentityProtocol.h"
#include "LuxMultiplayerUI.h"
#include "LuxMultiplayerWorld.h"
#include "LuxMultiplayerTriggerGeometry.h"
#include "LuxMultiplayerEntities.h"
#include "LuxMultiplayerEntityDefinition.h"
#include "LuxMultiplayerEffects.h"
#include "LuxMultiplayerEnemies.h"
#include "LuxMultiplayerScript.h"
#include "LuxMultiplayerInventoryPolicy.h"
#include "LuxSteamLaunch.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxEntity.h"
#include "LuxProp_LevelDoor.h"
#include "LuxProp_Object.h"
#include "LuxProp_Item.h"
#include "LuxInputHandler.h"
#include "LuxMainMenu.h"
#include "LuxDebugHandler.h"
#include "LuxEffectHandler.h"
#include "LuxEffectRenderer.h"
#include "LuxMapHelper.h"
#include "LuxInsanityHandler.h"
#include "LuxMusicHandler.h"
#include "LuxMessageHandler.h"
#include "LuxGlobalDataHandler.h"
#include "LuxHintHandler.h"
#include "LuxPostEffects.h"
#include "LuxCompletionCountHandler.h"
#include "LuxLoadScreenHandler.h"
#include "LuxHelpFuncs.h"
#include "LuxInventory.h"
#include "LuxArea.h"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>

using namespace luxnet;

namespace {
void MultiplayerLogText(char* message,bool warning=false) {
    for(char* p=message;*p;++p) if(static_cast<unsigned char>(*p)<32) *p=' ';
    const auto now=std::chrono::system_clock::now();
    const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t seconds=static_cast<std::time_t>(millis/1000);
    std::tm utc={};
#ifdef _WIN32
    gmtime_s(&utc,&seconds);
#else
    gmtime_r(&seconds,&utc);
#endif
    char timestamp[32];std::strftime(timestamp,sizeof(timestamp),"%Y-%m-%dT%H:%M:%S",&utc);
    // The engine Warning formatter has a 2048-byte buffer; bound the whole
    // line, including diagnostic context and the timestamp, before calling it.
    char line[1900];
    std::snprintf(line,sizeof(line),"[%s.%03uZ; app=%lu ms] Multiplayer %s\n",timestamp,
        static_cast<unsigned>(millis%1000),cPlatform::GetApplicationTime(),message);
    line[sizeof(line)-2]='\n';line[sizeof(line)-1]='\0';
    if(warning) Warning("%s",line);else Log("%s",line);
}
void MultiplayerLog(const char* format,...) {
    char message[2048];va_list args;va_start(args,format);
    std::vsnprintf(message,sizeof(message),format,args);va_end(args);
    MultiplayerLogText(message);
}
const char* LoadPhaseName(eLuxMultiplayerLoadPhase phase) {
    switch(phase) {
    case eLuxMultiplayerLoadPhase_None:return "none";
    case eLuxMultiplayerLoadPhase_Connecting:return "connecting";
    case eLuxMultiplayerLoadPhase_Preparing:return "waiting-for-host-map";
    case eLuxMultiplayerLoadPhase_Checking:return "checking-map";
    case eLuxMultiplayerLoadPhase_Downloading:return "downloading-map";
    case eLuxMultiplayerLoadPhase_Loading:return "loading-map";
    default:return "unknown";
    }
}
const char* TransportName(bool steam) {
#ifdef HPL_USE_STEAMWORKS
    return steam?"Steamworks relay":"Steamworks direct IP";
#else
    (void)steam;return "standalone direct IP";
#endif
}
bool WithinInteractionReach(iPhysicsBody* body,const cLuxMultiplayerRemotePlayer& player,float reach) {
    const cVector3f eyes=player.position+cVector3f(player.gameplay.eyeOffset[0],player.gameplay.eyeOffset[1],player.gameplay.eyeOffset[2]);
    const cVector3f min=body->GetBoundingVolume()->GetMin(),max=body->GetBoundingVolume()->GetMax();
    const cVector3f nearest(cMath::Clamp(eyes.x,min.x,max.x),cMath::Clamp(eyes.y,min.y,max.y),cMath::Clamp(eyes.z,min.z,max.z));
    return (eyes-nearest).Length()<=reach+0.5f;
}
bool EmptyDirectory(const tWString& path) {
    tWStringList files,folders;
    cPlatform::FindFilesInDir(files,path,_W("*"),true);
    cPlatform::FindFoldersInDir(folders,path,true,false);
    return files.empty() && folders.empty();
}
void RemoveEmptyDirectory(const tWString& path) {
    if(cPlatform::FolderExists(path) && EmptyDirectory(path)) cPlatform::RemoveFolder(path,false,false);
}
bool EnsureCacheDirectory(tWString path) {
    path=cString::ReplaceCharToW(path,_W("\\"),_W("/"));
    if(path.empty()) return false;
    if(cPlatform::FolderExists(path)) return true;
    while(path.size()>1 && path.back()==_W('/')) path.pop_back();
    const size_t separator=path.find_last_of(_W('/'));
    const tWString parent=separator==tWString::npos?_W(""):path.substr(0,separator+1);
    if(parent.empty() || parent==path || !EnsureCacheDirectory(parent)) return false;
    return cPlatform::CreateFolder(path) || cPlatform::FolderExists(path);
}
bool CreateMapCacheDirectory(uint32_t epoch,tWString& directory) {
    const tWString root=LuxMultiplayerCacheRoot();
    if(root.empty()) return false;
    if(!EnsureCacheDirectory(root)) return false;
    const tWString prefix=cString::To16Char(cString::ToString(cPlatform::GetApplicationTime())+"_"+
        cString::ToString(epoch)+"_");
    // CreateFolder succeeds only for a new directory, reserving each download
    // atomically even when several game instances share the same cache root.
    for(unsigned attempt=0;attempt<100;++attempt) {
        const tWString candidate=root+prefix+cString::To16Char(cString::ToString(attempt));
        if(cPlatform::CreateFolder(candidate)) {directory=candidate+_W("/");return true;}
    }
    return false;
}
}

cLuxMultiplayer::cLuxMultiplayer():iLuxUpdateable("LuxMultiplayer"),mpUI(NULL),mpWorld(NULL),
    mlScriptHistoryBytes(0),mlLocalPeer(0),mlMapEpoch(0),mlMapChecksum(0),mlExpectedMapBytes(0),
    mlPendingSteamInvite(0),msStatus("Offline"),mbLoading(false),mbReceiving(false),mbReady(false),mbReturnToMenu(false),
    mbApplyingScriptEffect(false),mbRemoteScriptTrigger(false),mbHistoryComplete(true),mbRestoreFocusWait(false),mbHasClientMap(false),mbPreserveHostPosition(false),mbSessionWorld(false),mfJoinAge(0) {
    mpWorld=hplNew(cLuxMultiplayerWorld,(this));
    mpEntities=hplNew(cLuxMultiplayerEntities,(this));
    mpEffects=hplNew(cLuxMultiplayerEffects,(this));
    mpEnemies=hplNew(cLuxMultiplayerEnemies,(this));
    mpUI=hplNew(cLuxMultiplayerUI,(this));
}
cLuxMultiplayer::~cLuxMultiplayer() {
    mpEnemies->Reset();mpEffects->Reset();mTransport.Stop(); hplDelete(mpUI); hplDelete(mpWorld); hplDelete(mpEntities);hplDelete(mpEffects);hplDelete(mpEnemies);
}
void cLuxMultiplayer::EnsureProfile() {
    if(gpBase->mpUserConfig) return;
    gpBase->CreateProfile(gpBase->msDefaultProfileName);
    gpBase->SetProfile(gpBase->msDefaultProfileName);
    gpBase->InitUserConfig();
}
bool cLuxMultiplayer::Host(const cLuxMultiplayerSettings& settings) {
    if(IsActive()) {msStatus="Disconnect the current session before hosting another.";return false;}
    if((!settings.useSteam && settings.port==0) || settings.maxPlayers<2 || settings.maxPlayers>16) {msStatus="Use 2 to 16 players and, for direct IP, a port from 1 to 65535.";return false;}
    if(settings.useSteam && !hpl::cNetworkTransport::InitializeSteam(msStatus)) {
        LogDiagnostic("session","Steam initialization failed: %s",msStatus.c_str());return false;
    }
    EnsureProfile();
    mSettings=settings; gpBase->SetCustomStory(NULL); gpBase->mbHardMode=false;
    tString map=settings.map.empty()?gpBase->msStartMapFile:settings.map;
    tString folder=settings.map.empty()?gpBase->msStartMapFolder:"";
    if(!settings.map.empty()) {
        folder=cString::GetFilePath(map);map=cString::GetFileName(map);
        if(folder.empty()) folder=gpBase->msStartMapFolder;
    }
    tString start=settings.startPos.empty()?gpBase->msStartMapPos:settings.startPos;
    tWString source=cString::To16Char(folder+map);
    if(!cPlatform::FileExists(source)) source=gpBase->mpEngine->GetResources()->GetFileSearcher()->GetFilePath(folder+map);
    std::vector<uint8_t> preflight;
    msStatus="Loading hosted map...";gpBase->mpLoadScreenHandler->DrawMultiplayerScreen();
    if(!LuxReadMultiplayerMap(source,preflight) || !LuxValidateMultiplayerMap(preflight,msStatus)) {
        if(preflight.empty()) msStatus="Cannot read the selected XML .map file (maximum 16 MiB): "+folder+map;
        LogDiagnostic("session","Hosted map validation failed: %s",msStatus.c_str());
        return false;
    }
    folder=cString::To8Char(cString::GetFilePathW(source));
    gpBase->mpEngine->GetResources()->AddResourceDir(cString::GetFilePathW(source),false);
    gpBase->mpDebugHandler->SetFastForward(false);
    const bool listening=settings.useSteam ?
        mTransport.HostSteam(settings.maxPlayers-1,settings.publicLobby,map,msStatus) :
        mTransport.Host(settings.port,settings.maxPlayers-1,msStatus);
    if(!listening) {LogDiagnostic("session","Could not start host transport: %s",msStatus.c_str());return false;}
    LogDiagnostic("session","Starting map='%s' protocol=%u transport=%s players=%u public=%d client_map_changes=%d player_scripts=%d player_collision=%d.",
        map.c_str(),ProtocolVersion,TransportName(settings.useSteam),settings.maxPlayers,settings.publicLobby,
        settings.allowClientMapChanges,settings.allPlayersTriggerScripts,settings.playerCollision);
    if(!++mlSessionSerial) ++mlSessionSerial;
    mbSessionWorld=true;
    mbRestoreFocusWait=gpBase->mpEngine->GetWaitIfAppOutOfFocus();
    gpBase->mpEngine->SetWaitIfAppOutOfFocus(false);
    mbLoading=true; mlLocalPeer=0; mlMapEpoch=0;mbReturnToMenu=false;
    gpBase->mpEngine->GetUpdater()->SetContainer("Default");
    gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
    // Explicit campaign values avoid developer-configured startup map overrides.
    bool ok=gpBase->StartGame(map,folder,start);
    mbLoading=false;
    if(!ok) {Stop("Unable to load the hosted map.");return false;}
    mbReady=true;
    msStatus=settings.useSteam ? "Creating Steam lobby..." : "Hosting on UDP port "+cString::ToString(settings.port)+". Waiting for players.";
    return true;
}
bool cLuxMultiplayer::GetCurrentMapForHosting(tString& map,tString& reason) const {
    map.clear();reason.clear();
    if(IsActive()) {reason="Disconnect the current session before hosting another.";return false;}
    cLuxMap* current=gpBase->mpMapHandler->GetCurrentMap();
    if(!current || !current->GetWorld() || !gpBase->mpPlayer->GetCharacterBody()) {
        reason="Load a game or map first to host it without restarting.";return false;
    }
	// A menu background is a separate scene, not a playable saved game. A real
	// paused game retains its gameplay viewport and player physics world.
	if(!gpBase->mpMapHandler->GetViewport() ||
	   gpBase->mpMapHandler->GetViewport()->GetWorld()!=current->GetWorld() ||
	   gpBase->mpPlayer->GetCharacterBody()->GetCurrentBody()->GetWorld()!=current->GetPhysicsWorld()) {
	    reason="Load a playable game or map first; menu backgrounds cannot be hosted.";return false;
	}
    const tWString& path=current->GetWorld()->GetFilePath();
    if(cString::ToLowerCaseW(cString::GetFileExtW(path))!=_W("map")) {
        reason="The current world has no XML .map file to share.";return false;
    }
    if(!cPlatform::FileExists(path)) {
        reason="The current map's XML file is no longer available on disk.";return false;
    }
    const unsigned long size=cPlatform::GetFileSize(path);
    if(!size || size>16*1024*1024) {
        reason="The current XML map must be between 1 byte and 16 MiB.";return false;
    }
    map=cString::To8Char(path);
    if(!current->GetWorld()->HasVerifiedMapSource()) {
        reason="This world was loaded with cached or partial geometry. Select its XML map with Browse to start a verified multiplayer session.";
        return false;
    }
    return true;
}
bool cLuxMultiplayer::HostCurrentMap(const cLuxMultiplayerSettings& settings) {
    tString path;
    if(!GetCurrentMapForHosting(path,msStatus)) return false;
    if((!settings.useSteam && settings.port==0) || settings.maxPlayers<2 || settings.maxPlayers>16) {
        msStatus="Use 2 to 16 players and, for direct IP, a port from 1 to 65535.";return false;
    }
    std::vector<uint8_t> preflight;
    if(!LuxReadMultiplayerMap(cString::To16Char(path),preflight) || !LuxValidateMultiplayerMap(preflight,msStatus)) {
        if(preflight.empty()) msStatus="Cannot read the current XML .map file (maximum 16 MiB): "+path;
        LogDiagnostic("session","Current-map validation failed: %s",msStatus.c_str());
        return false;
    }
    if(!LuxValidateMultiplayerCurrentMapSource(gpBase->mpMapHandler->GetCurrentMap()->GetWorld(),preflight,msStatus)) return false;
    mpEntities->Reset();
    if(!mpEntities->SeedCurrentMapItems(preflight,msStatus)) return false;
    if(settings.useSteam && !hpl::cNetworkTransport::InitializeSteam(msStatus)) return false;
    // Everything that can reject this choice runs before changing the live game.
    const bool listening=settings.useSteam ?
        mTransport.HostSteam(settings.maxPlayers-1,settings.publicLobby,cString::GetFileName(path),msStatus) :
        mTransport.Host(settings.port,settings.maxPlayers-1,msStatus);
    if(!listening) {LogDiagnostic("session","Could not host current map: %s",msStatus.c_str());return false;}
    LogDiagnostic("session","Hosting current map='%s' protocol=%u transport=%s players=%u public=%d client_map_changes=%d player_scripts=%d player_collision=%d.",
        cString::GetFileName(path).c_str(),ProtocolVersion,TransportName(settings.useSteam),settings.maxPlayers,settings.publicLobby,
        settings.allowClientMapChanges,settings.allPlayersTriggerScripts,settings.playerCollision);
    if(!++mlSessionSerial) ++mlSessionSerial;
    mSettings=settings;mSettings.map=path;mSettings.startPos.clear();
    mlLocalPeer=0;mlMapEpoch=0;mbReturnToMenu=false;
    cLuxMap* current=gpBase->mpMapHandler->GetCurrentMap();
    if(!CaptureMap(current,"",&preflight)) {mTransport.Stop();msStatus="Could not read the current map for transfer.";return false;}
    // The overlay normally releases a held object, but direct callers can
    // attach while an offline PID is still running without a network lease.
    if(cLuxMultiplayerWorld::IsInteractionState(gpBase->mpPlayer->GetCurrentState()))
        gpBase->mpPlayer->ChangeState(eLuxPlayerState_Normal);
    mpWorld->OnMapLoaded(current);
    mpEntities->CaptureMapBaseline();
    mpEnemies->OnMapLoaded(current);
    mpEffects->OnMapLoaded(current);
    mbSessionWorld=true;mbReady=true;
    mbRestoreFocusWait=gpBase->mpEngine->GetWaitIfAppOutOfFocus();
    gpBase->mpEngine->SetWaitIfAppOutOfFocus(false);
    gpBase->mpDebugHandler->SetFastForward(false);
    gpBase->mpDebugHandler->SetDebugWindowActive(false);
    // A paused offline menu must return to a running world without StartGame,
    // resets, player relocation, or changes to the active custom story.
    gpBase->mpEngine->GetUpdater()->SetContainer("Default");
    gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
    msStatus=settings.useSteam ? "Creating Steam lobby for the current map..." :
        "Hosting the current map on UDP port "+cString::ToString(settings.port)+". Waiting for players.";
    return true;
}
bool cLuxMultiplayer::Join(const tString& address) {
    if(IsActive()) {msStatus="Disconnect the current session before joining another.";return false;}
    if(address.empty() || address.size()>255) {msStatus="Enter a host IP address, optionally followed by :port.";return false;}
    SetLoadPhase(eLuxMultiplayerLoadPhase_Connecting,"Connecting to "+address+"...",true);
    EnsureProfile(); gpBase->mpDebugHandler->SetFastForward(false);
    gpBase->mpDebugHandler->SetDebugWindowActive(false);
    if(!mTransport.Join(address,27015,msStatus)) {
        LogDiagnostic("session","Could not start direct connection: %s",msStatus.c_str());mLoadPhase=eLuxMultiplayerLoadPhase_None;return false;
    }
    mSettings.useSteam=false;
    msStatus="Connecting to "+address+"...";
    BeginClientSession();
    return true;
}
bool cLuxMultiplayer::JoinSteamLobby(const tString& code) {
    if(IsActive()) {msStatus="Disconnect the current session before joining another.";return false;}
    uint64_t lobby=0;
    if(!luxsteam::ParseLobbyCode(code,lobby)) {msStatus="Enter the numeric Steam lobby code shared by the host.";return false;}
    SetLoadPhase(eLuxMultiplayerLoadPhase_Connecting,"Joining Steam lobby...",true);
    if(!hpl::cNetworkTransport::InitializeSteam(msStatus)) {mLoadPhase=eLuxMultiplayerLoadPhase_None;return false;}
    EnsureProfile();
    if(!mTransport.JoinSteamLobby(lobby,msStatus)) {
        LogDiagnostic("session","Could not start Steam lobby join: %s",msStatus.c_str());mLoadPhase=eLuxMultiplayerLoadPhase_None;return false;
    }
    mSettings.useSteam=true;
    gpBase->mpDebugHandler->SetFastForward(false);
    gpBase->mpDebugHandler->SetDebugWindowActive(false);
    msStatus="Joining Steam lobby...";
    BeginClientSession();
    return true;
}
void cLuxMultiplayer::BeginClientSession() {
    mbSessionWorld=true;
    mbRestoreFocusWait=gpBase->mpEngine->GetWaitIfAppOutOfFocus();
    gpBase->mpEngine->SetWaitIfAppOutOfFocus(false);
    mbReady=false;mbReceiving=false;mbReturnToMenu=false;mbHasClientMap=false;mfJoinAge=0;mlMapEpoch=0;mlLocalPeer=0;
    mlLastJoinLogTime=mlLoadPhaseLogStart=cPlatform::GetApplicationTime();
    mlLastPacketType=mlLastPacketBytes=0;mlLastPacketTime=0;
    MultiplayerLog("client beginning join (transport=%s, protocol=%u).",TransportName(mSettings.useSteam),ProtocolVersion);
    EnterClientLoading();
    SetLoadPhase(eLuxMultiplayerLoadPhase_Connecting,msStatus,true);
}
void cLuxMultiplayer::EnterClientLoading() {
    // Leaving an interaction restores its PID/body settings and releases its
    // lease before the old world stops ticking (also safe on a cancelled load).
    if(gpBase->mpMapHandler->GetCurrentMap()) {
        if(cLuxMultiplayerWorld::IsInteractionState(gpBase->mpPlayer->GetCurrentState()))
            gpBase->mpPlayer->ChangeState(eLuxPlayerState_Normal);
        mpWorld->ReleaseInteraction();
    }
    gpBase->mpEngine->GetUpdater()->SetContainer("MultiplayerLoading");
    // From a live menu, MapHandler does not receive another OnLeaveContainer.
    // Hide its retained gameplay viewport as well as stopping the old world.
    gpBase->mpMapHandler->GetViewport()->SetActive(false);
    gpBase->mpMapHandler->GetViewport()->SetVisible(false);
    if(gpBase->mpMapHandler->GetCurrentMap()) gpBase->mpMapHandler->GetCurrentMap()->GetWorld()->SetActive(false);
}
void cLuxMultiplayer::SetLoadPhase(eLuxMultiplayerLoadPhase phase,const tString& status,bool present) {
    if(mLoadPhase!=phase || msLoadScreenStatus!=status) {
        mlLastJoinLogTime=mlLoadPhaseLogStart=cPlatform::GetApplicationTime();
        MultiplayerLog("%s load phase=%s on map '%s' (epoch %u): %s",
            IsHost()?"host":"client",LoadPhaseName(phase),msMapName.c_str(),mlMapEpoch,status.c_str());
    }
    mLoadPhase=phase;msLoadScreenStatus=status;msStatus=status;
    if(present) gpBase->mpLoadScreenHandler->DrawMultiplayerScreen();
}
void cLuxMultiplayer::RefreshSteamLobbies() {
    if(!hpl::cNetworkTransport::InitializeSteam(msStatus)) return;
    if(mTransport.RequestSteamLobbies(msStatus)) msStatus="Searching Steam sessions...";
}
void cLuxMultiplayer::RetrySteam() {
    if(hpl::cNetworkTransport::InitializeSteam(msStatus)) msStatus=hpl::cNetworkTransport::SteamStatus();
}
void cLuxMultiplayer::InviteSteamFriends() {
    tString error;
    if(!mTransport.InviteSteamFriends(error)) msStatus=error;
}
void cLuxMultiplayer::QueueSteamInvite(uint64_t lobby) {
    if(!lobby || (IsSteamSession() && lobby==GetSteamLobbyID())) return;
    mlPendingSteamInvite=lobby;
    ShowWindow(true);
}
void cLuxMultiplayer::AcceptSteamInvite() {
    const uint64_t lobby=mlPendingSteamInvite;
    if(!lobby) return;
    if(!hpl::cNetworkTransport::InitializeSteam(msStatus)) return;
    if(IsActive()) Stop("Joining invited Steam session.");
    if(JoinSteamLobby(std::to_string(lobby))) mlPendingSteamInvite=0;
}
void cLuxMultiplayer::Stop(const tString& reason) {
    std::vector<hpl::cNetworkEvent> diagnostics;mTransport.DrainDiagnostics(diagnostics);
    for(const auto& event:diagnostics) MultiplayerLog("transport %s",event.reason.c_str());
    if(IsActive()) {
        MultiplayerLog("%s stopping on map '%s' (epoch %u, load phase=%s): %s",
            IsHost()?"host":"client",msMapName.c_str(),mlMapEpoch,LoadPhaseName(mLoadPhase),reason.empty()?"Disconnected":reason.c_str());
        if(IsHost()) {
            for(const auto& peer:mPeers) if(!peer.second.ready) LogPeerJoinState(peer.first,"session stopping");
        } else if(IsClient()) LogDiagnostic("receive-context","Last packet type=%u bytes=%u age=%.1fs.",mlLastPacketType,mlLastPacketBytes,
            mlLastPacketType?(cPlatform::GetApplicationTime()-mlLastPacketTime)/1000.0:-1.0);
    }
    mLoggedUnregisteredPeers.clear();
    mPendingRecoveredItems.clear();mAutoCombineItems.clear();mbCombiningInventory=mbGroupInventory=mbAutoCombiningInventory=false;
    mSharedScriptItems.clear();
    mRemoteItems.clear();
    bool client=IsClient();
    if(IsActive()) gpBase->mpEngine->SetWaitIfAppOutOfFocus(mbRestoreFocusWait);
    mpEnemies->Reset();mpEffects->Reset();mpWorld->Shutdown();mpEntities->Reset();
    FlushDiagnosticSummary();
    mTransport.Stop();mPeers.clear();mSteamPeerIdentities.clear();
    mvMapBytes.clear();mvScriptHistory.clear();mlScriptHistoryBytes=0;
    msPendingHostMap.clear();
    mbMapPreparing=false;mbResumeReady=false;mlMapTransition=0;
    mvPreparingPackets.clear();mlPreparingPacketBytes=0;msPreparingMap.clear();
    mLoadPhase=eLuxMultiplayerLoadPhase_None;mResumeLoadPhase=eLuxMultiplayerLoadPhase_None;
    msLoadScreenStatus.clear();msResumeLoadStatus.clear();
    mbReady=false;mbReceiving=false;mbHasClientMap=false;
    msStatus=reason.empty()?"Disconnected":reason;
    if(client && !mbLoading) mbReturnToMenu=true;
    RemoveReceivedMapFiles();
    msExistingMapPath.clear();msLoadedMapPath.clear();mbReusingMap=false;
}
void cLuxMultiplayer::ClearMapCache() {
    const unsigned removed=LuxClearMultiplayerMapCache();
    msStatus="Deleted "+cString::ToString(removed)+" downloaded map(s). Active sessions are unchanged.";
}
void cLuxMultiplayer::RemoveReceivedMapFiles() {
    if(msReceivedMapPath.empty()) return;
    // Only names derived from this instance's downloaded map, followed by an
    // empty-directory removal. Never recursively delete a shared cache root.
    tWString path=cString::To16Char(msReceivedMapPath);
    cPlatform::RemoveFile(path);
    cPlatform::RemoveFile(cString::SetFileExtW(path,_W("map_cache")));
    cPlatform::RemoveFile(cString::SetFileExtW(path,_W("map_cache_fastload")));
    cPlatform::RemoveFile(cString::SetFileExtW(path,_W("cmap")));
    RemoveEmptyDirectory(cString::GetFilePathW(path));
    msReceivedMapPath.clear();
}
void cLuxMultiplayer::Reset() {
    mPendingRecoveredItems.clear();mAutoCombineItems.clear();mbCombiningInventory=mbGroupInventory=mbAutoCombiningInventory=false;
    mSharedScriptItems.clear();
    mRemoteItems.clear();
    mpEnemies->Reset();mpEffects->Reset();mpWorld->Reset();mpEntities->Reset();
    if(!mbLoading && IsActive()) Stop("Session ended.");
    else if(!mbLoading) mbSessionWorld=false;
}
bool cLuxMultiplayer::ShouldSuppressOfflineSaves() const {
    return IsActive() || (mbSessionWorld && gpBase->mpMapHandler->GetCurrentMap()!=NULL);
}
void cLuxMultiplayer::OnMapLeave(cLuxMap*) {
    mpEnemies->Reset();mpEffects->Reset();mpWorld->Reset();mpEntities->Reset();
    FlushDiagnosticSummary();
}
void cLuxMultiplayer::ShowWindow(bool campaign) {mpUI->Show(campaign);}
void cLuxMultiplayer::ToggleWindow() {mpUI->Toggle();}
bool cLuxMultiplayer::IsWindowVisible() const {return mpUI->IsVisible();}
bool cLuxMultiplayer::IsSteamOverlayActive() const {return hpl::cNetworkTransport::SteamOverlayActive();}
const hpl::cSteamAvatarImage* cLuxMultiplayer::GetPlayerSteamAvatar(uint32_t peer) {
    if(!IsSteamSession()) return NULL;
    uint64_t identity=0;
    if(IsHost()) identity=mTransport.GetSteamPeerID(peer);
    else {
        const auto found=mSteamPeerIdentities.find(peer);
        if(found!=mSteamPeerIdentities.end()) identity=found->second;
    }
    return identity?mTransport.GetSteamAvatar(identity):NULL;
}
void cLuxMultiplayer::BroadcastPlayerIdentities() {
    if(!IsHost() || !IsSteamSession()) return;
    PeerSteamIdentities identities;
    const uint64_t host=mTransport.GetSteamPeerID(0);
    if(!host) return;
    identities[0]=host;
    for(const auto& peer:mPeers) if(peer.second.greeted) {
        const uint64_t identity=mTransport.GetSteamPeerID(peer.first);
        if(identity) identities[peer.first]=identity;
    }
    const auto packet=WritePlayerIdentities(identities);
    // Send to greeted peers even while their map is loading. This roster comes
    // only from identities authenticated by the host's Steam connections.
    for(auto& peer:mPeers) if(peer.second.greeted && !Send(peer.first,packet,true)) peer.second.reliableSendFailed=true;
}
bool cLuxMultiplayer::Send(uint32_t peer,const std::vector<uint8_t>& data,bool reliable) {return mTransport.Send(peer,data,reliable);}
void cLuxMultiplayer::LogDiagnostic(const char* category,const char* format,...) const {
    va_list args;va_start(args,format);LogDiagnosticV(category,format,args,false,false);va_end(args);
}
void cLuxMultiplayer::LogDiagnosticLimited(const char* category,const char* format,...) const {
    va_list args;va_start(args,format);LogDiagnosticV(category,format,args,true,false);va_end(args);
}
void cLuxMultiplayer::LogDiagnosticWarning(const char* category,const char* format,...) const {
    va_list args;va_start(args,format);LogDiagnosticV(category,format,args,false,true);va_end(args);
}
void cLuxMultiplayer::LogDiagnosticWarningLimited(const char* category,const char* format,...) const {
    va_list args;va_start(args,format);LogDiagnosticV(category,format,args,true,true);va_end(args);
}
void cLuxMultiplayer::LogDiagnosticV(const char* category,const char* format,va_list args,bool limited,bool warning) const {
    if(limited) {
        uint32_t& count=mDiagnosticCounts[category];if(count<UINT32_MAX) ++count;
        if(count>8) {
            if(count==9) LogDiagnostic(category,"Further details suppressed for this map; totals will be logged when leaving it.");
            return;
        }
    }
    char detail[1536];std::vsnprintf(detail,sizeof(detail),format,args);
    char message[2048];std::snprintf(message,sizeof(message),"%s %s on map '%s' (epoch %u): %s",
        IsHost()?"host":IsClient()?"client":"offline",category,msMapName.c_str(),mlMapEpoch,detail);
    MultiplayerLogText(message,warning);
}
void cLuxMultiplayer::FlushDiagnosticSummary() {
    for(const auto& count:mDiagnosticCounts) if(count.second>8)
        LogDiagnostic(count.first.c_str(),"%u occurrences in this map, %u additional details suppressed.",count.second,count.second-8);
    mDiagnosticCounts.clear();
}
void cLuxMultiplayer::Broadcast(const std::vector<uint8_t>& data,bool reliable) {
    if(!IsHost()) return;
    for(auto& peer:mPeers) if(peer.second.ready && !Send(peer.first,data,reliable) && reliable)
        peer.second.reliableSendFailed=true;
}
bool cLuxMultiplayer::CaptureMap(cLuxMap* map,const tString& start,const std::vector<uint8_t>* verifiedSource) {
    FlushDiagnosticSummary();
    // Current-map attachment transfers the exact bytes checked against the
    // loaded world, even if an editor saves the disk file during setup.
    if(verifiedSource) mvMapBytes=*verifiedSource;
    else if(!LuxReadMultiplayerMap(map->GetWorld()->GetFilePath(),mvMapBytes)) return false;
    mbMapResetsGame=mbLoading;
    mbMapHardMode=gpBase->mbHardMode;
    ++mlMapEpoch;if(!mlMapEpoch) ++mlMapEpoch;
    msMapName=cString::GetFileName(map->GetFileName());msStartPos=start;
    mTransport.SetSteamMapName(msMapName);
    mlMapChecksum=Checksum(mvMapBytes);
    msMapHash=MapHash(mvMapBytes);
    mvScriptHistory.clear();mlScriptHistoryBytes=0;mbHistoryComplete=true;
    mbMapPreparing=false;msPreparingMap.clear();
    for(auto& peer:mPeers) {peer.second.ready=false;peer.second.beginSent=false;peer.second.endSent=false;peer.second.transferRequested=false;peer.second.offset=0;peer.second.age=0;}
    for(auto& peer:mPeers) {peer.second.lastJoinLogTime=cPlatform::GetApplicationTime();peer.second.mapSendFailureLogged=false;}
    MultiplayerLog("host captured map '%s' (epoch %u, bytes=%u, peers=%u).",msMapName.c_str(),mlMapEpoch,
        static_cast<unsigned>(mvMapBytes.size()),static_cast<unsigned>(mPeers.size()));
    return true;
}
void cLuxMultiplayer::OnMapLoaded(cLuxMap* map,const tString& start) {
    if(!IsActive() || !map) return;
    mpWorld->OnMapLoaded(map);
    if(IsHost()) {
        if(!CaptureMap(map,start)) Stop("Could not read the new map for transfer; session stopped.");
        else mpEntities->CaptureMapBaseline();
    }
    if(IsActive()) mpEnemies->OnMapLoaded(map);
    if(IsActive()) mpEffects->OnMapLoaded(map);
}
void cLuxMultiplayer::RejectPeer(uint32_t peer,const tString& reason) {
    if(IsHost()) LogPeerJoinState(peer,"rejecting peer");
    LogDiagnosticWarning("session","rejecting peer %u: %s",peer,reason.c_str());
    if(IsHost()) {mTransport.Disconnect(peer,reason);mpWorld->OnPeerDisconnected(peer);mpEntities->OnPeerDisconnected(peer);mpEffects->OnPeerDisconnected(peer);mpEnemies->OnPeerDisconnected(peer);mPeers.erase(peer);BroadcastPlayerIdentities();}
    else {Stop(reason);ShowWindow();}
}
void cLuxMultiplayer::HandleEvent(const hpl::cNetworkEvent& event) {
    if(event.type==hpl::eNetworkEventType::Diagnostic) {
        MultiplayerLog("transport %s",event.reason.c_str());
    } else if(event.type==hpl::eNetworkEventType::SessionFailed) {
        Stop(event.reason);ShowWindow();
    } else if(event.type==hpl::eNetworkEventType::SessionReady) {
        MultiplayerLog("%s session ready (epoch %u).",IsHost()?"host":"client",mlMapEpoch);
        msStatus=IsHost() ? "Hosting "+msMapName+" on Steam. Invite friends or share the lobby code." : "Steam lobby joined. Connecting to host...";
        if(IsClient()) SetLoadPhase(eLuxMultiplayerLoadPhase_Connecting,msStatus);
    } else if(event.type==hpl::eNetworkEventType::Connected) {
        MultiplayerLog("%s connected peer %u on map '%s' (epoch %u).",
            IsHost()?"host":"client",event.peer,msMapName.c_str(),mlMapEpoch);
        mLoggedUnregisteredPeers.erase(event.peer);
        if(IsHost()) {mPeers[event.peer]=Peer();mPeers[event.peer].lastJoinLogTime=cPlatform::GetApplicationTime();}
        else {
            Writer w(Hello);w.U32(ProtocolVersion);const bool sent=Send(0,w.data,true);
            MultiplayerLog("client Hello send (peer 0, version=%u, accepted=%d).",ProtocolVersion,sent);
            if(!sent) {RejectPeer(0,"Could not send the multiplayer handshake.");return;}
            SetLoadPhase(eLuxMultiplayerLoadPhase_Preparing,"Connected. Waiting for the host's map...");
        }
    } else if(event.type==hpl::eNetworkEventType::Disconnected) {
        MultiplayerLog("%s disconnected peer %u on map '%s' (epoch %u): %s",
            IsHost()?"host":"client",event.peer,msMapName.c_str(),mlMapEpoch,event.reason.c_str());
        if(IsHost()) LogPeerJoinState(event.peer,"peer disconnected");
        mLoggedUnregisteredPeers.erase(event.peer);
        if(IsHost()) {mPeers.erase(event.peer);mpWorld->OnPeerDisconnected(event.peer);mpEntities->OnPeerDisconnected(event.peer);mpEffects->OnPeerDisconnected(event.peer);mpEnemies->OnPeerDisconnected(event.peer);BroadcastPlayerIdentities();}
        else {Stop("Disconnected: "+event.reason);ShowWindow();}
    } else if(event.type==hpl::eNetworkEventType::Message) HandlePacket(event.peer,event.data);
}
void cLuxMultiplayer::LogPeerJoinState(uint32_t peer,const char* context) const {
    const auto found=mPeers.find(peer);if(found==mPeers.end()) return;
    const Peer& state=found->second;
    MultiplayerLog("host peer %u %s on map '%s' (epoch %u): age=%.1fs greeted=%d manifest=%d request=%d end=%d ready=%d offset=%u/%u preparing=%d send_failed=%d last_rx_type=%u bytes=%u rx_age=%.1fs.",
        peer,context,msMapName.c_str(),mlMapEpoch,state.age,state.greeted,state.beginSent,state.transferRequested,
        state.endSent,state.ready,state.offset,static_cast<unsigned>(mvMapBytes.size()),mbMapPreparing,state.reliableSendFailed,
        state.lastPacketType,state.lastPacketBytes,state.lastPacketType?(cPlatform::GetApplicationTime()-state.lastPacketTime)/1000.0:-1.0);
}
void cLuxMultiplayer::SendMap(uint32_t peer,Peer& state) {
    if(mbMapPreparing || !state.greeted || state.ready || mvMapBytes.empty()) return;
    if(!state.beginSent) {
        Writer w(MapBegin);w.U32(ProtocolVersion);w.U32(peer);w.U32(mlMapEpoch);
        w.U32(static_cast<uint32_t>(mvMapBytes.size()));w.U32(mlMapChecksum);
        w.String(msMapName);w.String(msStartPos);w.U8(mSettings.allowClientMapChanges);w.U8(mSettings.allPlayersTriggerScripts);
        w.U8(mSettings.playerCollision);w.U8(mbMapResetsGame);w.U8(mbMapHardMode);
        w.String(msMapHash);
        if(!Send(peer,w.data,true)) {
            if(!state.mapSendFailureLogged) {MultiplayerLog("host MapBegin send failed (peer %u, epoch %u); normal retry remains pending.",peer,mlMapEpoch);state.mapSendFailureLogged=true;}
            return;
        }
        state.beginSent=true;
        MultiplayerLog("host queued MapBegin (peer %u, epoch %u, map='%s', bytes=%u).",peer,mlMapEpoch,msMapName.c_str(),static_cast<unsigned>(mvMapBytes.size()));
    }
    // The client checks installed XML and its saved cache before requesting
    // bytes. A matching file needs only this manifest and the end marker.
    if(!state.transferRequested) return;
    for(int i=0;i<2 && state.offset<mvMapBytes.size();++i) {
        size_t size=std::min(size_t(MapChunkBytes),mvMapBytes.size()-state.offset);
        Writer w(MapChunk);w.U32(mlMapEpoch);w.U32(state.offset);w.Bytes(mvMapBytes.data()+state.offset,size);
        if(!Send(peer,w.data,true)) {
            if(!state.mapSendFailureLogged) {MultiplayerLog("host MapChunk send failed (peer %u, epoch %u, offset=%u); normal retry remains pending.",peer,mlMapEpoch,state.offset);state.mapSendFailureLogged=true;}
            return;
        }
        state.offset+=static_cast<uint32_t>(size);
    }
    if(state.offset==mvMapBytes.size() && !state.endSent) {
        Writer w(MapEnd);w.U32(mlMapEpoch);
        state.endSent=Send(peer,w.data,true);
        if(state.endSent) MultiplayerLog("host queued MapEnd (peer %u, epoch %u).",peer,mlMapEpoch);
        else if(!state.mapSendFailureLogged) {MultiplayerLog("host MapEnd send failed (peer %u, epoch %u); normal retry remains pending.",peer,mlMapEpoch);state.mapSendFailureLogged=true;}
    }
}
void cLuxMultiplayer::HandlePacket(uint32_t peer,const std::vector<uint8_t>& data) {
    if(IsHost()) {
        auto found=mPeers.find(peer);if(found!=mPeers.end()) {
            found->second.lastPacketType=data.empty()?0:data[0];found->second.lastPacketBytes=static_cast<uint32_t>(data.size());
            found->second.lastPacketTime=cPlatform::GetApplicationTime();
        }
    } else if(IsClient() && peer==0) {
        mlLastPacketType=data.empty()?0:data[0];mlLastPacketBytes=static_cast<uint32_t>(data.size());mlLastPacketTime=cPlatform::GetApplicationTime();
    }
    if(data.empty() || data.size()>hpl::cNetworkTransport::MaxMessageBytes) {RejectPeer(peer,"Invalid packet size.");return;}
    Reader r(data);uint8_t type=data[0];
    if(IsHost()) {
        auto it=mPeers.find(peer);if(it==mPeers.end()) {
            if(mLoggedUnregisteredPeers.size()<16 && mLoggedUnregisteredPeers.insert(peer).second)
                LogDiagnosticLimited("unregistered-peer","Discarded packet without a registered game peer (peer %u, type=%u, bytes=%u, epoch %u).",
                    peer,static_cast<unsigned>(type),static_cast<unsigned>(data.size()),mlMapEpoch);
            return;
        }
        Peer& state=it->second;
        if(type==Hello) {
            uint32_t version=r.U32();
            MultiplayerLog("host received Hello (peer %u, version=%u, already_greeted=%d, epoch %u).",peer,version,state.greeted,mlMapEpoch);
            if(!r.Done() || state.greeted || version!=ProtocolVersion) {RejectPeer(peer,"Multiplayer protocol mismatch. Use the same Amnesia build as the host.");return;}
            if(!mbHistoryComplete) {RejectPeer(peer,"This session has exceeded the late-join script history limit. Join after the next map change.");return;}
            state.greeted=true;
            BroadcastPlayerIdentities();
            if(mbMapPreparing) SendMapPreparation(peer,state);
            return;
        }
        if(!state.greeted) {RejectPeer(peer,"Expected multiplayer handshake.");return;}
        if(type==MapRequest) {
            const uint32_t epoch=r.U32();const uint8_t reuse=r.U8();const tString hash=r.String(64);
            if(!r.Done() || reuse>1 || !ValidMapHash(hash)) {RejectPeer(peer,"Malformed map request.");return;}
            if(epoch!=mlMapEpoch) {
                LogDiagnosticLimited("stale-handshake","Ignored MapRequest (peer %u, packet epoch %u, host epoch %u).",peer,epoch,mlMapEpoch);
                return;
            }
            if(!state.beginSent || state.transferRequested || state.ready || hash!=msMapHash) {
                RejectPeer(peer,"Unexpected map request.");return;
            }
            state.transferRequested=true;
            if(reuse) state.offset=static_cast<uint32_t>(mvMapBytes.size());
            MultiplayerLog("host accepted MapRequest (peer %u, epoch %u, reuse=%u).",peer,epoch,static_cast<unsigned>(reuse));
            return;
        }
        if(type==Ready) {
            uint32_t epoch=r.U32();if(!r.Done()) {RejectPeer(peer,"Malformed map acknowledgement.");return;}
            if(epoch!=mlMapEpoch) {
                LogDiagnosticLimited("stale-handshake","Ignored Ready (peer %u, packet epoch %u, host epoch %u).",peer,epoch,mlMapEpoch);
                return;
            }
            MultiplayerLog("host received Ready (peer %u, packet epoch %u, host epoch %u).",peer,epoch,mlMapEpoch);
            if(!state.endSent || state.ready) {RejectPeer(peer,"Unexpected map acknowledgement.");return;}
            if(!mbHistoryComplete) {RejectPeer(peer,"The session's initialization history exceeded its limit while joining.");return;}
            state.ready=true;state.age=0;
            if(!mpEntities->SendMapBaseline(peer)) {RejectPeer(peer,"Could not initialize restored map entities: "+mpEntities->GetLastError());return;}
            if(!SyncItemCallbacks(peer)) {RejectPeer(peer,"Could not initialize item callbacks.");return;}
            for(const auto& effect:mvScriptHistory) if(!Send(peer,effect,true)) {RejectPeer(peer,"Script state exceeded the connection queue.");return;}
            if(!mpEntities->SendInitialState(peer)) {RejectPeer(peer,"Could not synchronize map entities: "+mpEntities->GetLastError());return;}
            if(!mpWorld->SendInitialState(peer)) {RejectPeer(peer,"World is too large for initial synchronization (32 MiB limit).");return;}
            if(!mpEnemies->SendInitialState(peer)) {RejectPeer(peer,"Could not initialize enemy state.");return;}
            if(!mpEffects->SendInitialState(peer)) {RejectPeer(peer,"Could not initialize active world effects.");return;}
            MultiplayerLog("host queued initial world state (peer %u, epoch %u, script_records=%u).",peer,mlMapEpoch,static_cast<unsigned>(mvScriptHistory.size()));
            msStatus="Hosting "+msMapName+". Connected clients: "+cString::ToString(static_cast<int>(mPeers.size()));return;
        }
        if(!state.ready) return;
        if(type==EnemyStimulus) {
            if(!mpWorld->HandleEnemyStimulus(peer,data)) RejectPeer(peer,"Invalid enemy hearing stimulus.");return;
        }
        if(type==WorldEffect) {
            tString error;if(!mpEffects->HandleMessage(peer,data,error)) RejectPeer(peer,error.empty()?"Invalid player effect update.":error);return;
        }
        if(type==NativeRequest || type==NativeResult || type==JointBreakRequest) {
            if(!mpEntities->HandleMessage(peer,data)) RejectPeer(peer,"Malformed native interaction request.");return;
        }
        if(type==MapChangeRequest) {
            uint32_t epoch=r.U32();tString map=r.String(512),start=r.String(128),a=r.String(256),b=r.String(256);
            if(!r.Done() || !SafeRelativePath(map) || map!=AuthoredMapFilename(map)) {RejectPeer(peer,"Invalid map change request.");return;}
            if(epoch!=mlMapEpoch || !mSettings.allowClientMapChanges || state.requestCooldown>0) {
                LogDiagnosticLimited("request-denied","Map change from peer %u ignored: packet_epoch=%u permitted=%d cooldown=%.1fs.",peer,epoch,mSettings.allowClientMapChanges,state.requestCooldown);return;
            }
            // Resolve and validate an unlocked level door in the host's authoritative map.
            cLuxMap* current=gpBase->mpMapHandler->GetCurrentMap();
            const auto& poses=mpWorld->GetRemotePlayers();auto pose=poses.find(peer);
            if(!current || pose==poses.end() || pose->second.age>2 || !(pose->second.gameplay.flags&LuxWorldWire::PlayerAlive)) return;
            cLuxProp_LevelDoor* door=NULL;
            cLuxEntityIterator entities=current->GetEntityIterator();
            while(entities.HasNext()) {
                iLuxEntity* ent=entities.Next();
                if(ent->GetEntityType()!=eLuxEntityType_Prop || static_cast<iLuxProp*>(ent)->GetPropType()!=eLuxPropType_LevelDoor) continue;
                cLuxProp_LevelDoor* candidate=static_cast<cLuxProp_LevelDoor*>(ent);
                if(!candidate->IsActive() || candidate->GetDestroyMe() || candidate->GetInteractionDisabled() || candidate->GetLocked() ||
                   AuthoredMapFilename(candidate->GetMapFile())!=map || candidate->GetStartPos()!=start) continue;
                for(int i=0;i<candidate->GetBodyNum();++i)
                    if(candidate->CanInteract(candidate->GetBody(i)) && WithinInteractionReach(candidate->GetBody(i),pose->second,candidate->GetMaxFocusDistance())) door=candidate;
            }
            if(!door) {LogDiagnosticLimited("request-denied","Map change from peer %u has no matching unlocked level door in reach.",peer);return;}
            LogDiagnostic("map-transition","Accepted peer %u level-door request to '%s'.",peer,map.c_str());
            state.requestCooldown=2.0f;
            cLuxMultiplayerRemoteTriggerScope remoteTrigger(true,peer);
            door->OnInteract(door->GetBody(0),pose->second.position);return;
        }
        if(type==ItemCombineRequest) {
            const uint32_t epoch=r.U32();const tString a=r.String(256),b=r.String(256);
            if(!r.Done() || a.empty() || b.empty()) {RejectPeer(peer,"Invalid item combination.");return;}
            if(epoch!=mlMapEpoch || !mSettings.allPlayersTriggerScripts || state.interactionTokens<1) {
                LogDiagnosticLimited("request-denied","Item combination from peer %u ignored: packet_epoch=%u scripts=%d tokens=%.1f.",peer,epoch,mSettings.allPlayersTriggerScripts,state.interactionTokens);return;
            }
            state.interactionTokens-=1;
            cLuxMultiplayerRemoteTriggerScope trigger(true,peer);
            if(!HasRemoteItem(a) || !HasRemoteItem(b)) {LogDiagnosticLimited("request-denied","Item combination from peer %u lacks an owned ingredient.",peer);return;}
            auto* callback=gpBase->mpInventory->GetCombineCallback(a,b);if(!callback) {LogDiagnosticLimited("request-denied","Item combination from peer %u has no registered callback.",peer);return;}
            CombineInventoryItems(peer,a,b);
            return;
        }
        if(type==ItemUseRequest) {
            const uint32_t epoch=r.U32();const tString item=r.String(256),name=r.String(256);
            if(!r.Done() || item.empty() || name.empty()) {RejectPeer(peer,"Invalid item use request.");return;}
            if(epoch!=mlMapEpoch || !mSettings.allPlayersTriggerScripts || state.interactionTokens<1) {
                LogDiagnosticLimited("request-denied","Item use from peer %u ignored: packet_epoch=%u scripts=%d tokens=%.1f.",peer,epoch,mSettings.allPlayersTriggerScripts,state.interactionTokens);return;
            }
            state.interactionTokens-=1;
            cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
            iLuxEntity* ent=map?map->GetEntityByName(name):NULL;
            auto pose=mpWorld->GetRemotePlayers().find(peer);
            if(!ent || !ent->IsActive() || ent->GetDestroyMe() || ent->GetInteractionDisabled() ||
               pose==mpWorld->GetRemotePlayers().end() || pose->second.age>2 ||
               !(pose->second.gameplay.flags&LuxWorldWire::PlayerAlive)) return;
            cLuxMultiplayerRemoteTriggerScope trigger(true,peer);
            if(!HasRemoteItem(item)) return;
            bool near=false;
            const float reach=cMath::Max(ent->GetMaxFocusDistance(),gpBase->mpGameCfg->GetFloat("Player_Interaction","MinUseItemDistance",0));
            for(int i=0;i<ent->GetBodyNum();++i) {
                if(WithinInteractionReach(ent->GetBody(i),pose->second,reach)) near=true;
            }
            cLuxUseItemCallback* callback=map->GetUseItemCallback(item,name);
            if(!near || !callback) {LogDiagnosticLimited("request-denied","Item use from peer %u ignored: in_reach=%d callback=%d.",peer,near,callback!=NULL);return;}
            const bool remove=callback->mbAutoDestroy;const tString callbackName=callback->msName;
            map->RunScript(callback->msFunction+"(\""+callback->msItem+"\", \""+callback->msEntity+"\")");
            if(remove) map->RemoveUseItemCallback(callback,callbackName);
            return;
        }
        if(type==EntityInteract) {
            uint32_t epoch=r.U32();tString name=r.String(256);uint32_t index=r.U32();
            if(!r.Done()) {RejectPeer(peer,"Invalid interaction request.");return;}
            if(epoch!=mlMapEpoch || !mSettings.allPlayersTriggerScripts || state.interactionTokens<1) {
                LogDiagnosticLimited("request-denied","Entity interaction from peer %u ignored: packet_epoch=%u scripts=%d tokens=%.1f.",peer,epoch,mSettings.allPlayersTriggerScripts,state.interactionTokens);return;
            }
            state.interactionTokens-=1;
            cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
            iLuxEntity* ent=map?map->GetEntityByName(name):NULL;
            const auto& poses=mpWorld->GetRemotePlayers();auto pose=poses.find(peer);
            if(!ent || !ent->IsActive() || index>=uint32_t(ent->GetBodyNum()) || pose==poses.end()) {
                LogDiagnosticLimited("request-denied","Entity interaction from peer %u ignored: target='%s' body=%u target_exists=%d player_exists=%d.",peer,name.c_str(),index,ent!=NULL,pose!=poses.end());return;
            }
            iPhysicsBody* body=ent->GetBody(index);
            if(pose->second.age>2 || ent->GetDestroyMe() || ent->GetInteractionDisabled() ||
               !(pose->second.gameplay.flags&LuxWorldWire::PlayerAlive) ||
               mpWorld->IsInteractionOwnedByOther(body,peer) ||
               !WithinInteractionReach(body,pose->second,ent->GetMaxFocusDistance()) || !ent->CanInteract(body)) {
                LogDiagnosticLimited("request-denied","Entity interaction from peer %u failed live-player, ownership, reach or target-state checks (target='%s', pose_age=%.2fs).",peer,name.c_str(),pose->second.age);return;
            }
            // Interactive physics controllers are handled by leases, never through the host's player state.
            cLuxMultiplayerRemoteTriggerScope remoteTrigger(true,peer);
            ent->RunInteractCallbackFunc();return;
        }
        if(type>=64) {if(!mpWorld->HandleMessage(peer,data)) RejectPeer(peer,"Malformed world message.");return;}
        RejectPeer(peer,"Unexpected client packet.");return;
    }
    if(!IsClient() || peer!=0) return;
    if(type==PlayerIdentities) {
        PeerSteamIdentities identities;
        if(!IsSteamSession() || !ReadPlayerIdentities(r,identities) || identities[0]!=mTransport.GetSteamPeerID(0)) {
            RejectPeer(0,"Invalid Steam player identities from host.");return;
        }
        mSteamPeerIdentities.swap(identities);return;
    }
    if(type==MapPreparing) {
        const uint32_t epoch=r.U32(),transition=r.U32();const tString name=r.String(256);
        if(!r.Done() || !epoch || !transition || !SafeRelativePath(name) || cString::GetFileName(name)!=name) {
            RejectPeer(0,"Host sent an invalid map preparation notice.");return;
        }
        if(mlMapEpoch && epoch!=mlMapEpoch) return;
        if(mbMapPreparing && transition==mlMapTransition) return;
        if(!mbMapPreparing) {
            mbResumeReady=mbReady;mResumeLoadPhase=mLoadPhase;msResumeLoadStatus=msLoadScreenStatus;
        }
        mbMapPreparing=true;mlMapTransition=transition;mfJoinAge=0;
        EnterClientLoading();mbReady=false;
        SetLoadPhase(eLuxMultiplayerLoadPhase_Preparing,"Waiting for the host to prepare "+name+"...",true);
        return;
    }
    if(type==MapCancelled) {
        const uint32_t epoch=r.U32(),transition=r.U32();const tString reason=r.String(512);
        if(!r.Done() || !epoch || !transition) {RejectPeer(0,"Host sent an invalid map cancellation.");return;}
        if(!mbMapPreparing || transition!=mlMapTransition || (mlMapEpoch && epoch!=mlMapEpoch)) return;
        mbMapPreparing=false;mbReady=mbResumeReady;mfJoinAge=0;
        if(mbReady) {
            gpBase->mpEngine->GetUpdater()->SetContainer("Default");
            gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
            SetLoadPhase(eLuxMultiplayerLoadPhase_None,"Map change cancelled: "+reason);
        } else SetLoadPhase(mResumeLoadPhase,msResumeLoadStatus,true);
        std::vector<std::vector<uint8_t> > pending;pending.swap(mvPreparingPackets);mlPreparingPacketBytes=0;
        for(const auto& packet:pending) {if(!IsClient()) break;HandlePacket(0,packet);}
        return;
    }
    if(type==MapBegin) {
        uint32_t version=r.U32(),id=r.U32(),epoch=r.U32(),size=r.U32(),crc=r.U32();
        tString name=r.String(256),start=r.String(128);uint8_t changes=r.U8(),triggers=r.U8(),collision=r.U8(),resetGame=r.U8(),hardMode=r.U8();
        tString hash=r.String(64);
        MultiplayerLog("client received MapBegin (peer %u, assigned peer=%u, epoch %u, bytes=%u).",peer,id,epoch,size);
        if(!r.Done() || version!=ProtocolVersion || id==0 || epoch==0 || size==0 || size>MaxMapBytes ||
            !SafeRelativePath(name) || cString::GetFileName(name)!=name || cString::ToLowerCase(cString::GetFileExt(name))!="map" || changes>1 || triggers>1 || collision>1 || resetGame>1 || hardMode>1 || !ValidMapHash(hash)) {
            RejectPeer(0,"Host sent an invalid map manifest.");return;
        }
        if(mbHasClientMap && !resetGame) {
            // The host's OnLeave script runs after MapPreparing. Its final
            // player/effect cleanup must reach the old client world before
            // ordinary map leave callbacks preserve the remaining state.
            // Debug restarts instead discard that state through StartGame.
            std::vector<std::vector<uint8_t> > pending;
            pending.swap(mvPreparingPackets);mlPreparingPacketBytes=0;
            for(const auto& packet:pending) {
                if(!packet.empty() && (packet.front()==InventoryGrant || packet.front()==InventoryRemove)) {
                    if(!ApplyInventoryPacket(packet)) {RejectPeer(0,"Invalid inventory update before map change.");return;}
                    continue;
                }
                if(packet.empty() || packet.front()!=ScriptEffect) continue;
                Reader effect(packet);
                if(effect.U32()!=mlMapEpoch) continue;
                tString error;
                mbApplyingScriptEffect=true;
                const bool ok=LuxApplyMultiplayerScriptEffect(effect,error);
                mbApplyingScriptEffect=false;
                if(!ok) {RejectPeer(0,error.empty()?"Invalid script effect before map change.":error);return;}
                if(!IsClient()) return;
            }
        }
        FlushDiagnosticSummary();
        EnterClientLoading();mbReady=false;
        SetLoadPhase(eLuxMultiplayerLoadPhase_Checking,"Checking installed and downloaded copies of "+name+"...",true);
        mbMapPreparing=false;mvPreparingPackets.clear();mlPreparingPacketBytes=0;
        mpEnemies->Reset();mpEffects->Reset();mpWorld->Reset();mpEntities->Reset();mlLocalPeer=id;mlMapEpoch=epoch;mlExpectedMapBytes=size;mlMapChecksum=crc;
        msMapName=name;msStartPos=start;mSettings.allowClientMapChanges=changes!=0;mSettings.allPlayersTriggerScripts=triggers!=0;
        msMapHash=hash;msExistingMapPath.clear();mbReusingMap=false;mlDownloadedMapBytes=0;
        mSettings.playerCollision=collision!=0;
        mbMapResetsGame=resetGame!=0;
        mbMapHardMode=hardMode!=0;
        LogDiagnostic("session","Host settings: client_map_changes=%d player_scripts=%d player_collision=%d reset_game=%d hard_mode=%d.",
            mSettings.allowClientMapChanges,mSettings.allPlayersTriggerScripts,mSettings.playerCollision,mbMapResetsGame,mbMapHardMode);
        mvMapBytes.clear();mvMapBytes.reserve(size);mbReceiving=true;mbReady=false;mfJoinAge=0;
        mbReusingMap=FindMatchingMap();
        SetLoadPhase(mbReusingMap?eLuxMultiplayerLoadPhase_Loading:eLuxMultiplayerLoadPhase_Downloading,
            mbReusingMap?"Using matching local map: "+name:"Downloading "+name+" from the host...",true);
        Writer request(MapRequest);request.U32(mlMapEpoch);request.U8(mbReusingMap);request.String(msMapHash);
        const bool sent=Send(0,request.data,true);
        MultiplayerLog("client MapRequest send (epoch %u, reuse=%d, accepted=%d).",mlMapEpoch,mbReusingMap,sent);
        if(!sent) RejectPeer(0,"Could not request the host map.");
        return;
    }
    if(mbMapPreparing) {
        // The host still ticks the old world during its fade. Preserve these
        // events for cancellation; a successful manifest supersedes them.
        if(mlPreparingPacketBytes+data.size()>32*1024*1024 || mvPreparingPackets.size()>=65536) {
            RejectPeer(0,"Too much world state arrived while waiting for the host map.");return;
        }
        mlPreparingPacketBytes+=data.size();mvPreparingPackets.push_back(data);return;
    }
    if(type==MapChunk) {
        uint32_t epoch=r.U32(),offset=r.U32();
        if(epoch!=mlMapEpoch) return;
        if(!r.valid || !mbReceiving || mbReusingMap || offset!=mvMapBytes.size() || data.size()-r.pos>MapChunkBytes ||
            data.size()-r.pos>mlExpectedMapBytes-mvMapBytes.size()) {RejectPeer(0,"Invalid or out-of-order map chunk.");return;}
        mlDownloadedMapBytes+=static_cast<uint32_t>(data.size()-r.pos);
        mvMapBytes.insert(mvMapBytes.end(),data.begin()+r.pos,data.end());return;
    }
    if(type==MapEnd) {
        uint32_t epoch=r.U32();
        MultiplayerLog("client received MapEnd (packet epoch %u, client epoch %u, bytes=%u/%u).",epoch,mlMapEpoch,
            static_cast<unsigned>(mvMapBytes.size()),mlExpectedMapBytes);
        if(epoch!=mlMapEpoch) return;
        if(r.Done() && mbReceiving)
            SetLoadPhase(eLuxMultiplayerLoadPhase_Checking,"Verifying "+msMapName+"...",true);
        if(!r.Done() || !mbReceiving || mvMapBytes.size()!=mlExpectedMapBytes || Checksum(mvMapBytes)!=mlMapChecksum ||
            MapHash(mvMapBytes)!=msMapHash) {RejectPeer(0,"Host map failed its size or content hash check.");return;}
        mbReceiving=false;
        if(!LoadReceivedMap()) {RejectPeer(0,msStatus);return;}
        Writer w(Ready);w.U32(mlMapEpoch);
        const bool sent=Send(0,w.data,true);
        MultiplayerLog("client Ready send after map load (epoch %u, accepted=%d).",mlMapEpoch,sent);
        if(!sent) {RejectPeer(0,"Could not acknowledge the loaded host map.");return;}
        mbReady=true;
        SetLoadPhase(eLuxMultiplayerLoadPhase_None,"Joined "+msMapName+". Session remains live while menus are open.");return;
    }
    if((type==EnemyState || type==EnemyRemoved) && mbReady) {
        if(!mpEnemies->HandleMessage(peer,data)) RejectPeer(0,"Invalid enemy update from host.");return;
    }
    if((type==EnemyDamage || type==EnemyTerror) && mbReady) {
        if(!mpWorld->HandlePlayerEvent(peer,data)) RejectPeer(0,"Invalid player combat event from host.");return;
    }
    if(type==WorldEffect && mbReady) {
        tString error;if(!mpEffects->HandleMessage(0,data,error)) RejectPeer(0,error.empty()?"Invalid world effect update.":error);return;
    }
    if(type==EntityDefinition && mbReady) {
        if(!mpEntities->ApplyDefinition(data)) RejectPeer(0,"Could not restore map entity: "+mpEntities->GetLastError());return;
    }
    if(type==EntityRemoved && mbReady) {
        if(!mpEntities->ApplyRemoval(data)) RejectPeer(0,"Invalid removed-entity update from host.");return;
    }
    if(type==SameMapTeleport && mbReady) {
        const uint32_t epoch=r.U32();const tString mapName=r.String(256),start=r.String(128),a=r.String(256),b=r.String(256);
        if(!r.Done() || !SafeRelativePath(mapName)) {RejectPeer(0,"Invalid same-map teleport.");return;}
        auto* current=gpBase->mpMapHandler->GetCurrentMap();
        if(epoch!=mlMapEpoch || !current) return;
        if(!IsSameMapDestination(mapName,current->GetName()) || (!current->GetPlayerStart(start) && !current->GetFirstPlayerStart())) {
            RejectPeer(0,"Same-map teleport has no matching destination.");return;
        }
        mbApplyingScriptEffect=true;
        gpBase->mpMapHandler->ChangeMap(mapName,start,a,b);
        mbApplyingScriptEffect=false;
        return;
    }
    if((type==NativeGrant || type==NativeDiaryResult || type==EntityState || type==ItemRemoved || type==RopeState) && mbReady) {
        if(!mpEntities->HandleMessage(0,data)) RejectPeer(0,"Invalid entity update from host.");return;
    }
    if((type==InventoryGrant || type==InventoryRemove) && mbReady) {
        if(!ApplyInventoryPacket(data)) RejectPeer(0,"Invalid inventory update from host.");
        return;
    }
    if(type==ItemCallbacks && mbReady) {
        const uint32_t epoch=r.U32(),count=r.U32();
        if(count>4096) {RejectPeer(0,"Too many item callbacks.");return;}
        struct Callback {tString name,item,entity;bool remove;};std::vector<Callback> callbacks;
        for(uint32_t i=0;i<count && r.valid;++i) {
            Callback c;c.name=r.String(4096);c.item=r.String(4096);c.entity=r.String(4096);
            const uint8_t remove=r.U8();if(remove>1) r.valid=false;c.remove=remove!=0;callbacks.push_back(c);
        }
        const uint32_t combineCount=r.U32();std::vector<Callback> combinations;
        if(combineCount>4096) {RejectPeer(0,"Too many item combinations.");return;}
        for(uint32_t i=0;i<combineCount && r.valid;++i) {
            Callback c;c.name=r.String(4096);c.item=r.String(4096);c.entity=r.String(4096);
            const uint8_t remove=r.U8();if(remove>1) r.valid=false;c.remove=remove!=0;combinations.push_back(c);
        }
        if(!r.Done()) {RejectPeer(0,"Invalid item callbacks.");return;}
        cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
        if(epoch!=mlMapEpoch || !map) return;
        map->ClearUseItemCallbacks();
        for(const auto& c:callbacks) map->AddUseItemCallback(c.name,c.item,c.entity,"",c.remove);
        gpBase->mpInventory->ClearCombineCallbacks();
        for(const auto& c:combinations) gpBase->mpInventory->AddCombineCallback(c.name,c.item,c.entity,"",c.remove);
        return;
    }
    if(type==ScriptEffect && mbReady) {
        uint32_t epoch=r.U32();if(epoch!=mlMapEpoch) return;
        tString error;
        mbApplyingScriptEffect=true;bool ok=LuxApplyMultiplayerScriptEffect(r,error);mbApplyingScriptEffect=false;
        if(!ok) RejectPeer(0,error.empty()?"Invalid script effect from host.":error);return;
    }
    if(type==ObjectBreak && mbReady) {
        uint32_t epoch=r.U32();tString name=r.String(256);
        if(!r.Done()) {RejectPeer(0,"Malformed object break event.");return;}
        if(epoch!=mlMapEpoch) return;
        cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
        cLuxProp_Object* object=map?static_cast<cLuxProp_Object*>(map->GetEntityByName(name,eLuxEntityType_Prop,eLuxPropType_Object)):NULL;
        if(object) {mbApplyingScriptEffect=true;object->Break();mbApplyingScriptEffect=false;}
        return;
    }
    // Unreliable old-map packets may arrive after the reliable next-map manifest.
    if(type>=64) {if(mbReady && !mpWorld->HandleMessage(0,data)) RejectPeer(0,"Invalid world update from host.");return;}
    RejectPeer(0,"Unexpected host packet.");
}
bool cLuxMultiplayer::ApplyInventoryPacket(const std::vector<uint8_t>& data) {
    if(data.empty()) return false;
    Reader r(data);const uint32_t epoch=r.U32();
    if(data.front()==InventoryGrant) {
        InventoryItem item;if(!ReadInventoryItem(r,item,eLuxItemType_LastEnum)) return false;
        if(epoch==mlMapEpoch && !gpBase->mpInventory->GetItem(item.name)) {
            bool accepted=false;
            gpBase->mpInventory->AddItem(item.name,static_cast<eLuxItemType>(item.type),item.subtype,item.image,
                item.amount,item.value,item.extra,&accepted);
            // A consumed note/diary/currency entry can return no inventory item
            // while still succeeding. Only actual rejection fails the session;
            // the host then recovers the retained progression-item ledger.
            if(!accepted) return false;
        }
        return true;
    }
    if(data.front()==InventoryRemove) {
        const tString name=r.String(256);if(!r.Done() || name.empty()) return false;
        if(epoch==mlMapEpoch && gpBase->mpInventory->GetItem(name)) gpBase->mpInventory->RemoveItem(name);
        return true;
    }
    return false;
}
bool cLuxMultiplayer::FindMatchingMap() {
    std::vector<tWString> candidates;
    candidates.push_back(cString::To16Char(gpBase->msStartMapFolder+msMapName));
    candidates.push_back(cString::To16Char(gpBase->mpMapHandler->GetMapFolder()+msMapName));
    candidates.push_back(gpBase->mpEngine->GetResources()->GetFileSearcher()->GetFilePath(msMapName));
    tWString cacheRoot=LuxMultiplayerCacheRoot();
    // Match the candidate's canonical path on Unix, where the OS cache
    // directory may have a symlinked parent (for example XDG_CACHE_HOME).
    if(!cacheRoot.empty() && cPlatform::FolderExists(cacheRoot)) {
        const tWString canonicalRoot=cPlatform::GetFullFilePath(cacheRoot);
        if(!canonicalRoot.empty()) cacheRoot=canonicalRoot;
    }
    cacheRoot=cString::ToLowerCaseW(cString::ReplaceCharToW(cacheRoot,_W("\\"),_W("/")));
    if(!cacheRoot.empty() && cacheRoot.back()!=_W('/')) cacheRoot+=_W('/');
    for(const auto& candidate:candidates) {
        if(candidate.empty() || !cPlatform::FileExists(candidate)) continue;
        const tWString path=cPlatform::GetFullFilePath(candidate);
        if(path.empty()) continue;
        const tWString normalized=cString::ToLowerCaseW(cString::ReplaceCharToW(path,_W("\\"),_W("/")));
        // Resource search can retain previous session directories. Those are
        // disposable working copies, not installed maps to load by reference.
        if(!cacheRoot.empty() && normalized.compare(0,cacheRoot.size(),cacheRoot)==0) continue;
        std::vector<uint8_t> bytes;
        if(LuxReadMultiplayerMap(path,bytes) && bytes.size()==mlExpectedMapBytes && MapHash(bytes)==msMapHash) {
            LogDiagnostic("map-source","Reusing verified installed XML (%u bytes).",mlExpectedMapBytes);
            mvMapBytes.swap(bytes);msExistingMapPath=cString::To8Char(path);return true;
        }
    }
    if(LuxReadCachedMultiplayerMap(msMapHash,mlExpectedMapBytes,mvMapBytes)) {
        LogDiagnostic("map-source","Reusing verified downloaded cache (%u bytes).",mlExpectedMapBytes);return true;
    }
    LogDiagnostic("map-source","No matching installed XML or cache; requesting %u bytes from host.",mlExpectedMapBytes);
    mvMapBytes.clear();return false;
}
bool cLuxMultiplayer::LoadReceivedMap() {
    SetLoadPhase(eLuxMultiplayerLoadPhase_Loading,"Loading "+msMapName+"...",true);
    if(!LuxValidateMultiplayerMap(mvMapBytes,msStatus)) return false;
    tWString path=cString::To16Char(msExistingMapPath);
    if(!path.empty()) {
        // A local edit between the manifest and load must not swap in a
        // different map. The already verified bytes remain a usable fallback.
        std::vector<uint8_t> current;
        if(!LuxReadMultiplayerMap(path,current) || current.size()!=mlExpectedMapBytes || MapHash(current)!=msMapHash) {
            path.clear();msExistingMapPath.clear();
        }
    }
    const bool ownCopy=path.empty();
    tWString dir=ownCopy?_W(""):cString::GetFilePathW(path);
    if(ownCopy) {
        if(!CreateMapCacheDirectory(mlMapEpoch,dir)) {
            msStatus="Could not create a multiplayer map cache in your local application cache directory.";return false;
        }
        path=dir+cString::To16Char(msMapName);
        FILE* f=cPlatform::OpenFile(path,_W("wb"));
        if(!f) {RemoveEmptyDirectory(dir);msStatus="Could not write the received map to the multiplayer cache.";return false;}
        bool written=fwrite(mvMapBytes.data(),1,mvMapBytes.size(),f)==mvMapBytes.size();
        if(fclose(f)!=0) written=false;
        if(!written) {cPlatform::RemoveFile(path);RemoveEmptyDirectory(dir);msStatus="Failed to write the received map.";return false;}
        // Persistent cache entries can be deleted at any time: the loader uses
        // this private copy, also isolating simultaneously running instances.
        if(!LuxStoreCachedMultiplayerMap(msMapHash,mvMapBytes))
            LogDiagnosticWarning("map-source","Could not retain downloaded map in the multiplayer cache.");
    }
    RemoveReceivedMapFiles();
    msReceivedMapPath=ownCopy?cString::To8Char(path):"";
    msLoadedMapPath=cString::To8Char(path);
    LogDiagnostic("map-source","Loading %s; SHA-256 %s; XML '%s'",
        !ownCopy?"installed":(mbReusingMap?"cache":"download"),msMapHash.c_str(),msLoadedMapPath.c_str());
    mbLoading=true;
    gpBase->SetCustomStory(NULL);gpBase->mbHardMode=mbMapHardMode;
    gpBase->mpEngine->GetUpdater()->SetContainer("Default");
    gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
    bool ok=true;
    if(!mbHasClientMap || mbMapResetsGame) ok=gpBase->StartGame(msMapName,cString::To8Char(dir),msStartPos);
    else {
        // Match CheckMapChange, without resetting state that native level-door
        // transitions intentionally retain (including flashbacks and voices).
        gpBase->mpEffectHandler->GetFade()->FadeIn(2.0f);
        gpBase->mpPlayer->SetActive(true);
        gpBase->mpHelpFuncs->CleanupData();
        gpBase->mpEngine->GetSound()->GetSoundHandler()->FadeOutAll(eSoundEntryType_World,0.5f,true);
        cLuxMapHandler* maps=gpBase->mpMapHandler;
        cLuxMap* previous=maps->GetCurrentMap();
        maps->SetMapFolder(cString::To8Char(dir));
        cLuxMap* next=maps->LoadMap(msMapName,true);
        ok=next!=NULL;
        if(ok) {
            maps->SetCurrentMap(next,false,true,msStartPos);
            if(previous) maps->DestroyMap(previous,false);
        }
    }
    if(ok) mbHasClientMap=true;
    mbLoading=false;
    return ok;
}
void cLuxMultiplayer::Update(float dt) {
    mpUI->Update(dt);
    if(mbReturnToMenu) {
        mbReturnToMenu=false;mbLoading=true;
        gpBase->mpEngine->GetUpdater()->SetContainer("MainMenu");
        gpBase->Reset();mbLoading=false;
        gpBase->mpMainMenu->OnLeaveContainer("");gpBase->mpMainMenu->OnEnterContainer("");
    }
    // Steam invitations and lobby discovery must run in menus, even while offline.
    const bool wasSearching=mTransport.IsSteamLobbySearchPending();
    std::vector<hpl::cNetworkEvent> events;mTransport.Poll(events);
    if(wasSearching && !mTransport.IsSteamLobbySearchPending()) msStatus=mTransport.SteamStatus();
    for(const auto& event:events) {if(!IsActive()) break;HandleEvent(event);}
    const uint64_t invited=mTransport.ConsumeSteamJoinRequest();
    if(invited) {
        QueueSteamInvite(invited);
        if(!IsActive()) AcceptSteamInvite();
    }
    if(!IsActive()) return;
    if(IsHost() && !msPendingHostMap.empty()) ProcessHostMapChange();
    if(!IsActive()) return;
    if(IsHost()) {
        std::vector<uint32_t> expired;
        for(auto& p:mPeers) {
            p.second.age+=dt;p.second.requestCooldown=std::max(0.0f,p.second.requestCooldown-dt);
            p.second.interactionTokens=std::min(32.0f,p.second.interactionTokens+32.0f*dt);
            const unsigned long now=cPlatform::GetApplicationTime();
            if(!p.second.ready && now-p.second.lastJoinLogTime>=10000UL) {
                p.second.lastJoinLogTime=now;LogPeerJoinState(p.first,"still joining");
            }
            if(p.second.reliableSendFailed || (!p.second.ready && p.second.age>120)) expired.push_back(p.first);
            else SendMap(p.first,p.second);
        }
        for(uint32_t peer:expired) RejectPeer(peer,"Connection could not keep up with reliable world state or timed out during map load.");
    } else if(!mbReady) {
        mfJoinAge+=dt;
        const unsigned long now=cPlatform::GetApplicationTime();
        if(now-mlLastJoinLogTime>=10000UL) {
            mlLastJoinLogTime=now;
            MultiplayerLog("client still joining on map '%s' (epoch %u): phase=%s phase_elapsed=%.1fs join_age=%.1fs receiving=%d preparing=%d downloaded=%u/%u.",
                msMapName.c_str(),mlMapEpoch,LoadPhaseName(mLoadPhase),(now-mlLoadPhaseLogStart)/1000.0,
                mfJoinAge,mbReceiving,mbMapPreparing,mlMapEpoch?mlDownloadedMapBytes:0,mlMapEpoch?mlExpectedMapBytes:0);
        }
        if(mfJoinAge>120) {RejectPeer(0,"Timed out waiting for the host map.");return;}
    }
    if(mbReady) UpdateBackgroundWorld(eUpdateableMessage_Update,dt);
}
void cLuxMultiplayer::PreUpdate(float dt) {
    if(IsActive() && mbReady) mpWorld->PrepareEnemyPlayers();
    if(IsActive() && mbReady) UpdateBackgroundWorld(eUpdateableMessage_PreUpdate,dt);
}
void cLuxMultiplayer::PostUpdate(float dt) {
    if(IsActive() && mbReady) UpdateBackgroundWorld(eUpdateableMessage_PostUpdate,dt);
    if(IsActive() && mbReady) mpEntities->Update(dt);
    if(IsActive() && mbReady) mpWorld->Update(dt);
    if(IsActive() && mbReady) mpEnemies->Update(dt);
    if(IsActive() && mbReady) mpEffects->PostUpdate(dt);
}
void cLuxMultiplayer::UpdateBackgroundWorld(eUpdateableMessage phase,float dt) {
    if(gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()=="Default" || !gpBase->mpMapHandler->GetCurrentMap()) return;
    // Scene/physics already receive every engine phase globally. Forward the
    // same phases to gameplay while menus own input; hands/camera attachment
    // and other PostUpdate work must keep following the live simulation too.
    iLuxUpdateable* modules[]={gpBase->mpMapHandler,gpBase->mpMapHelper,gpBase->mpPlayer,
        gpBase->mpInsanityHandler,gpBase->mpEffectRenderer,gpBase->mpMusicHandler,gpBase->mpMessageHandler,
        gpBase->mpEffectHandler,gpBase->mpCompletionCountHandler,gpBase->mpGlobalDataHandler,
        gpBase->mpHintHandler,gpBase->mpPostEffectHandler};
    tString container=gpBase->mpEngine->GetUpdater()->GetCurrentContainerName();
    for(auto* module:modules) {
        module->RunMessage(phase,dt);
        if(!IsActive() || !gpBase->mpMapHandler->GetCurrentMap() || gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!=container) break;
    }
}
bool cLuxMultiplayer::RequestMapChange(const tString& map,const tString& start,const tString& a,const tString& b) {
    auto* current=gpBase->mpMapHandler->GetCurrentMap();
    const bool sameMap=current && IsSameMapDestination(map,current->GetName());
    if(IsClient() && mbApplyingScriptEffect && sameMap) return true;
    if(IsHost() && mbRemoteScriptTrigger && !mSettings.allowClientMapChanges) return false;
    if(IsHost() && mbRemoteScriptTrigger && sameMap) {
        if((!current->GetPlayerStart(start) && !current->GetFirstPlayerStart()) || mPeers.find(mlScriptPlayerPeer)==mPeers.end()) return false;
        Writer w(SameMapTeleport);w.U32(mlMapEpoch);w.String(AuthoredMapFilename(map));w.String(start);w.String(a);w.String(b);
        if(!Send(mlScriptPlayerPeer,w.data,true)) mPeers[mlScriptPlayerPeer].reliableSendFailed=true;
        return false;
    }
    if(!IsClient()) return true;
    if(!mSettings.allowClientMapChanges) {msStatus="The host has disabled client map changes.";return false;}
    const tString destination=AuthoredMapFilename(map);
    if(!mbReady || destination.empty()) return false;
    Writer w(MapChangeRequest);w.U32(mlMapEpoch);w.String(destination);w.String(start);w.String(a);w.String(b);Send(0,w.data,true);
    msStatus="Map change requested from host.";return false;
}
bool cLuxMultiplayer::RemotePlayerTouches(iLuxEntity* entity) {
    return GetRemotePlayerTouching(entity)!=UINT32_MAX;
}
uint32_t cLuxMultiplayer::GetRemotePlayerTouching(iLuxEntity* entity) {
    if(!IsHost() || !mSettings.allPlayersTriggerScripts || !entity) return UINT32_MAX;
    if(entity->GetEntityType()==eLuxEntityType_Area &&
       static_cast<iLuxArea*>(entity)->GetAreaType()==eLuxAreaType_Insanity) return UINT32_MAX;
    mpWorld->PrepareEnemyPlayers();
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
    if(!map) return UINT32_MAX;
    for(const auto& p:mpWorld->GetEnemyPlayers()) {
        if(p.peer==GetLocalPeerId() || !p.alive || !p.body) continue;
        iPhysicsBody* playerBody=p.body->GetCurrentBody();
        for(int i=0;i<entity->GetBodyNum();++i) {
            iPhysicsBody* body=entity->GetBody(i);
            if(LuxPlayerBodyTouches(map->GetPhysicsWorld(),playerBody,body)) return p.peer;
        }
    }
    return UINT32_MAX;
}
bool cLuxMultiplayer::RequestEntityInteraction(iLuxEntity* entity,iPhysicsBody* body,const cVector3f&) {
    if(!IsClient()) return true;
    if(!mbReady || !entity || !mSettings.allPlayersTriggerScripts) return false;
    for(int i=0;i<entity->GetBodyNum();++i) if(entity->GetBody(i)==body) {
        Writer w(EntityInteract);w.U32(mlMapEpoch);w.String(entity->GetName());w.U32(i);Send(0,w.data,true);break;
    }
    return false;
}
void cLuxMultiplayer::BroadcastScriptEffect(const std::vector<uint8_t>& effect) {
    if(!IsHost() || mbApplyingScriptEffect) return;
    // A native break can allocate an entity immediately before its script
    // callback creates another. Publish that allocation first on both live
    // connections and the replay stream, so authored IDs keep matching.
    if(mbReady && !effect.empty() && effect.front()!=EntityDefinition && effect.front()!=EntityRemoved &&
        !mpEntities->SyncCreatedProps()) {
        // The entity updater will stop the session between callbacks. Do not
        // tear down replication from inside a script/native destruction stack.
        if(mbHistoryComplete) LogDiagnostic("script-history","Late joins disabled because newly created entities could not be synchronized: %s",mpEntities->GetLastError().c_str());
        mbHistoryComplete=false;return;
    }
    // Bound late-join initialization history. Refuse joins once a complete replay cannot fit.
    if(mlScriptHistoryBytes+effect.size()<=256*1024) {mvScriptHistory.push_back(effect);mlScriptHistoryBytes+=effect.size();}
    else {
        if(mbHistoryComplete) LogDiagnostic("script-history","Late joins disabled: replay history reached its 256 KiB limit (stored=%u, next=%u, records=%u).",
            static_cast<unsigned>(mlScriptHistoryBytes),static_cast<unsigned>(effect.size()),static_cast<unsigned>(mvScriptHistory.size()));
        mbHistoryComplete=false;
    }
    Broadcast(effect,true);
}
bool cLuxMultiplayer::AllowObjectBreak(const tString& name) {
    if(IsClient()) return mbApplyingScriptEffect;
    if(IsHost()) {Writer w(ObjectBreak);w.U32(mlMapEpoch);w.String(name);BroadcastScriptEffect(w.data);}
    return true;
}
bool cLuxMultiplayer::RequestItemUse(const tString& item,const tString& entity,bool combine) {
    if(!IsClient()) return true;
    if(mbReady && mSettings.allPlayersTriggerScripts) {
        Writer w(combine?ItemCombineRequest:ItemUseRequest);w.U32(mlMapEpoch);w.String(item);w.String(entity);Send(0,w.data,true);
    }
    return false;
}
bool cLuxMultiplayer::SyncItemCallbacks(uint32_t peer) {
    if(!IsHost()) return true;
    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();if(!map) return true;
    const auto& callbacks=map->GetUseItemCallbacks();
    if(callbacks.size()>4096) return false;
    Writer w(ItemCallbacks);w.U32(mlMapEpoch);w.U32(static_cast<uint32_t>(callbacks.size()));
    for(const auto* c:callbacks) {w.String(c->msName);w.String(c->msItem);w.String(c->msEntity);w.U8(c->mbAutoDestroy);}
    const auto& combinations=gpBase->mpInventory->GetCombineCallbacks();
    if(combinations.size()>4096) return false;
    w.U32(static_cast<uint32_t>(combinations.size()));
    for(const auto* c:combinations) {w.String(c->msName);w.String(c->msItemA);w.String(c->msItemB);w.U8(c->mbAutoDestroy);}
    if(w.data.size()>hpl::cNetworkTransport::MaxMessageBytes) return false;
    if(peer==UINT32_MAX) {Broadcast(w.data,true);return true;}
    return Send(peer,w.data,true);
}
namespace {
bool RetainProgressionItem(uint32_t type) {
    return type!=eLuxItemType_Tinderbox && type!=eLuxItemType_LampOil && type!=eLuxItemType_Sanity &&
        type!=eLuxItemType_Health && type!=eLuxItemType_Lantern;
}
InventoryItem DescribeItem(cLuxProp_Item* item) {
    InventoryItem result;result.name=item->GetName();result.type=item->GetItemType();
    result.subtype=item->GetSubItemTypeName();result.image=item->GetImageFile();result.amount=item->GetAmount();
    result.value=item->GetStringVal();result.extra=item->GetExtraStringVal();return result;
}
}
void cLuxMultiplayer::RecordRemoteItem(uint32_t peer,cLuxProp_Item* item) {
    // Notes, diaries and coins are consumed immediately by BeforeAddItem; they
    // are journal/currency state, not entries the player still holds.
    if(item->GetItemType()!=eLuxItemType_Puzzle && item->GetItemType()!=eLuxItemType_HandObject) return;
    mRemoteItems[peer][item->GetName()]=DescribeItem(item);
}
bool cLuxMultiplayer::GiveInventoryItem(uint32_t peer,const InventoryItem& item) {
    if(peer==GetLocalPeerId()) {
        if(gpBase->mpInventory->GetItem(item.name)) return true;
        bool accepted=false;
        gpBase->mpInventory->AddItem(item.name,static_cast<eLuxItemType>(item.type),item.subtype,item.image,item.amount,item.value,item.extra,&accepted);
        return accepted;
    }
    if(item.type==eLuxItemType_Puzzle || item.type==eLuxItemType_HandObject) mRemoteItems[peer][item.name]=item;
    // A failed delivery disconnects the recipient; the ledger then recovers
    // the output on the host instead of losing a completed recipe's result.
    if(!Send(peer,WriteInventoryItem(mlMapEpoch,item),true)) mPeers[peer].reliableSendFailed=true;
    return true;
}
void cLuxMultiplayer::RecoverRemoteItems(uint32_t peer) {
    auto held=mRemoteItems.find(peer);if(held==mRemoteItems.end()) return;
    const auto items=held->second;mRemoteItems.erase(held);
    LogDiagnostic("inventory-recovery","Recovering %u tracked items from disconnected peer %u.",static_cast<unsigned>(items.size()),peer);
    for(const auto& entry:items) if(RetainProgressionItem(entry.second.type)) mPendingRecoveredItems[entry.first]=entry.second;
    RecoverPendingItems();
    if(!mPendingRecoveredItems.empty())
        LogDiagnosticWarning("inventory-recovery","%zu disconnected-player items could not be loaded into the host inventory; recovery data is retained and will be retried.",mPendingRecoveredItems.size());
}
void cLuxMultiplayer::RecoverPendingItems() {
    if(!IsHost()) return;
    const size_t before=mPendingRecoveredItems.size();
    for(auto it=mPendingRecoveredItems.begin();it!=mPendingRecoveredItems.end();) {
        const auto item=it->second;bool accepted=gpBase->mpInventory->GetItem(item.name)!=NULL;
        if(!accepted) gpBase->mpInventory->AddItem(item.name,static_cast<eLuxItemType>(item.type),item.subtype,item.image,
            item.amount,item.value,item.extra,&accepted,false);
        if(accepted) it=mPendingRecoveredItems.erase(it);else ++it;
    }
    if(mPendingRecoveredItems.size()!=before)
        LogDiagnostic("inventory-recovery","Recovered %u items to host; %u remain pending.",static_cast<unsigned>(before-mPendingRecoveredItems.size()),static_cast<unsigned>(mPendingRecoveredItems.size()));
}
bool cLuxMultiplayer::RouteInventoryGive(const InventoryItem& item) {
    if(!IsHost() || !mbCombiningInventory) return false;
    if(GiveInventoryItem(mlInventoryRecipient,item)) {
        ++mlInventoryMutation;
        if(mbAutoCombiningInventory) mAutoCombineItems.insert(item.name);
    }
    return true;
}
bool cLuxMultiplayer::RouteInventoryRemove(const tString& item) {
    if(!IsHost() || !mbCombiningInventory) return false;
    // Existing global GiveItem effects create shared copies on all peers.
    // Consuming one of those logical entries must retain its original shared
    // removal semantics, even when the recipe's output is privately owned.
    if(mSharedScriptItems.count(item)) {
        Writer w(InventoryRemove);w.U32(mlMapEpoch);w.String(item);Broadcast(w.data,true);
        if(gpBase->mpInventory->GetItem(item)) gpBase->mpInventory->RemoveItem(item);
        ForgetRemoteItem(item);++mlInventoryMutation;return true;
    }
    if((mbGroupInventory || mlInventoryRecipient==GetLocalPeerId()) && gpBase->mpInventory->GetItem(item)) {
        gpBase->mpInventory->RemoveItem(item);++mlInventoryMutation;
    }
    for(auto& owner:mRemoteItems) if((mbGroupInventory || owner.first==mlInventoryRecipient) && owner.second.count(item)) {
        Writer w(InventoryRemove);w.U32(mlMapEpoch);w.String(item);
        if(!Send(owner.first,w.data,true)) mPeers[owner.first].reliableSendFailed=true;
        owner.second.erase(item);++mlInventoryMutation;
    }
    mSharedScriptItems.erase(item);
    return true;
}
bool cLuxMultiplayer::HasGroupItem(const tString& item) const {
    if(gpBase->mpInventory->GetItem(item)) return true;
    for(const auto& owner:mRemoteItems) if(owner.second.count(item)) return true;
    return false;
}
bool cLuxMultiplayer::CombineInventoryItems(uint32_t peer,const tString& a,const tString& b,bool automatic) {
    if(!IsHost() || mbCombiningInventory) return false;
    auto* callback=gpBase->mpInventory->GetCombineCallback(a,b);if(!callback) return false;
    const tString name=callback->msName,function=callback->msFunction,itemA=callback->msItemA,itemB=callback->msItemB;
    const bool remove=callback->mbAutoDestroy;const uint64_t before=mlInventoryMutation;
    mbCombiningInventory=true;mbGroupInventory=automatic;mlInventoryRecipient=peer;
    {
        cLuxMultiplayerRemoteTriggerScope trigger(peer!=GetLocalPeerId(),peer);
        gpBase->mpInventory->RunScript(function+"(\""+itemA+"\", \""+itemB+"\")");
    }
    mbCombiningInventory=false;mbGroupInventory=false;
    // Conditional recipes may only display a hint. Keep them available until
    // the callback actually changes inventory during automatic attempts.
    // GUI callers may pass references to names owned by consumed ingredients.
    if(remove && (!automatic || before!=mlInventoryMutation) && gpBase->mpInventory->GetCombineCallback(itemA,itemB)==callback)
        gpBase->mpInventory->RemoveCombineCallback(name);
    return true;
}
void cLuxMultiplayer::AutoCombineInventory(uint32_t collector,const tString& acquired) {
    if(!IsHost() || mbAutoCombiningInventory || mbCombiningInventory) return;
    mbAutoCombiningInventory=true;
    mAutoCombineItems.clear();mAutoCombineItems.insert(acquired);
    InventoryRecipeAttempts attempted;
    // Callbacks may remove themselves/register follow-up recipes. Reacquire
    // the list after each call and bound work even for a cyclic custom script.
    for(unsigned pass=0;pass<128;++pass) {
        std::vector<InventoryRecipe> recipes;
        for(auto* callback:gpBase->mpInventory->GetCombineCallbacks()) {
            recipes.push_back({callback->msName,callback->msFunction,callback->msItemA,callback->msItemB});
        }
        InventoryOwners owners;
        for(int i=0;i<gpBase->mpInventory->GetItemNum();++i) owners[gpBase->mpInventory->GetItem(i)->GetName()].insert(GetLocalPeerId());
        for(const auto& owner:mRemoteItems) for(const auto& entry:owner.second) owners[entry.first].insert(owner.first);
        const auto* recipe=NextSplitRecipe(recipes,owners,mAutoCombineItems,attempted,mlInventoryMutation);
        if(!recipe) break;
        // Generic callbacks can handle different pairs differently. Track the
        // registration, allowing another pair after an unchanged failed recipe.
        attempted[recipe->name]=mlInventoryMutation;
        CombineInventoryItems(collector,recipe->a,recipe->b,true);
    }
    mbAutoCombiningInventory=false;
    mAutoCombineItems.clear();
}
void cLuxMultiplayer::RecordSharedItem(const tString& item) {
    if(IsHost() && gpBase->mpInventory->GetItem(item)) mSharedScriptItems.insert(item);
}
void cLuxMultiplayer::ForgetRemoteItem(const tString& item) {
    mSharedScriptItems.erase(item);
    for(auto& inventory:mRemoteItems) inventory.second.erase(item);
}
bool cLuxMultiplayer::HasRemoteItem(const tString& item) const {
    if(!IsHost()) return false;
    if(mbCombiningInventory && mbGroupInventory) return HasGroupItem(item);
    if(mSharedScriptItems.count(item)) return true;
    if(mbRemoteScriptTrigger) {
        auto inventory=mRemoteItems.find(mlScriptPlayerPeer);
        return inventory!=mRemoteItems.end() && inventory->second.count(item)!=0;
    }
    for(const auto& inventory:mRemoteItems) if(inventory.second.count(item)) return true;
    return false;
}
void cLuxMultiplayer::OnDraw(float dt) {
    if(!IsActive() || !mbReady || !gpBase->mpMapHandler->MapIsLoaded()) return;
    const tString container=gpBase->mpEngine->GetUpdater()->GetCurrentContainerName();
    if(container!="MainMenu" && container!="Inventory" && container!="Journal") return;
    // Hurt tint, sanity and flashes belong beneath the menu's dimming layer.
    gpBase->mpPlayer->RunHelperMessage(eUpdateableMessage_OnDraw,dt);
    gpBase->mpEffectHandler->OnDraw(dt);
    gpBase->mpInsanityHandler->OnDraw(dt);
}
bool cLuxMultiplayer::AllowPhysicsJointBreak(iLuxProp* prop,iPhysicsJoint* joint) {
    return mpEntities->AllowPhysicsJointBreak(prop,joint);
}
bool cLuxMultiplayer::BeginNativeInteraction(iLuxEntity* entity) {return mpEntities->BeginInteraction(entity);}
void cLuxMultiplayer::CompleteNativeInteraction(iLuxEntity* entity,bool succeeded) {mpEntities->CompleteInteraction(entity,succeeded);}
bool cLuxMultiplayer::DeferNativeInteractionCallback(iLuxEntity* entity) const {return mpEntities->DeferCallback(entity);}
void cLuxMultiplayer::RecordNativeDiaryIndex(const tString& name,int index) {mpEntities->RecordDiaryIndex(name,index);}
bool cLuxMultiplayer::DeferNativeDiaryPresentation(const tString& name,cLuxDiary* diary) {return mpEntities->DeferDiaryPresentation(name,diary);}
void cLuxMultiplayer::RecordNativeDiaryDecision(bool open) {mpEntities->RecordDiaryDecision(open);}
bool cLuxMultiplayer::HostChangeMap(const tString& map,const tString& start,bool preserve) {
    if(!IsHost() || map.empty()) return false;
    NotifyHostMapChange(map);
    msPendingHostMap=map;msPendingHostStart=start;mbPreserveHostPosition=preserve;return true;
}
void cLuxMultiplayer::SendMapPreparation(uint32_t peer,Peer& state) {
    Writer notice(MapPreparing);notice.U32(mlMapEpoch);notice.U32(mlMapTransition);notice.String(msPreparingMap);
    if(!Send(peer,notice.data,true)) state.reliableSendFailed=true;
    else mTransport.Flush(peer);
    state.age=0;
}
void cLuxMultiplayer::NotifyHostMapChange(const tString& map) {
    if(!IsHost() || !mlMapEpoch) return;
    msPreparingMap=cString::GetFileName(cString::SetFileExt(map,"map"));
    if(!SafeRelativePath(msPreparingMap) || msPreparingMap.size()>256) return;
    ++mlMapTransition;if(!mlMapTransition) ++mlMapTransition;
    mbMapPreparing=true;
    MultiplayerLog("host preparing map '%s' (current epoch %u, transition %u).",msPreparingMap.c_str(),mlMapEpoch,mlMapTransition);
    for(auto& peer:mPeers) if(peer.second.greeted) SendMapPreparation(peer.first,peer.second);
}
void cLuxMultiplayer::CancelHostMapChange(const tString& reason) {
    if(!IsHost() || !mbMapPreparing) return;
    MultiplayerLog("host cancelled map preparation (epoch %u, transition %u): %s",mlMapEpoch,mlMapTransition,reason.c_str());
    Writer notice(MapCancelled);notice.U32(mlMapEpoch);notice.U32(mlMapTransition);notice.String(reason.substr(0,512));
    mbMapPreparing=false;msPreparingMap.clear();
    for(auto& peer:mPeers) if(peer.second.greeted) {
        if(!Send(peer.first,notice.data,true)) peer.second.reliableSendFailed=true;
        else mTransport.Flush(peer.first);
    }
}
void cLuxMultiplayer::ProcessHostMapChange() {
    tString requested=msPendingHostMap,start=msPendingHostStart;msPendingHostMap.clear();
    msStatus="Loading hosted map: "+requested;gpBase->mpLoadScreenHandler->DrawMultiplayerScreen();
    tWString path=cString::To16Char(requested);
    if(!cPlatform::FileExists(path)) path=gpBase->mpEngine->GetResources()->GetFileSearcher()->GetFilePath(requested);
    std::vector<uint8_t> bytes;
    if(!LuxReadMultiplayerMap(path,bytes) || !LuxValidateMultiplayerMap(bytes,msStatus)) {
        if(bytes.empty()) msStatus="Cannot load selected host XML map: "+requested;
        CancelHostMapChange(msStatus);
        ShowWindow();return;
    }
    cVector3f position=gpBase->mpPlayer->GetCharacterBody()->GetPosition();
    cCamera* camera=gpBase->mpPlayer->GetCamera();
    cVector3f angles(camera->GetPitch(),camera->GetYaw(),0);
    gpBase->mpEngine->GetResources()->AddResourceDir(cString::GetFilePathW(path),false);
    gpBase->mpEngine->GetUpdater()->SetContainer("Default");
    gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
    // Native debug loads are new games, unlike level-door transitions. Reuse
    // that reset sequence while keeping the transport alive through Reset().
    mbLoading=true;
    const bool loaded=gpBase->StartGame(cString::To8Char(cString::GetFileNameW(path)),
        cString::To8Char(cString::GetFilePathW(path)),start);
    mbLoading=false;
    if(!loaded) {
        Stop("Failed to load host map after resetting the game.");
        mbReturnToMenu=true;ShowWindow();return;
    }
    if(mbPreserveHostPosition) {
        camera=gpBase->mpPlayer->GetCamera();camera->SetPitch(angles.x);camera->SetYaw(angles.y);camera->SetRoll(angles.z);
        gpBase->mpPlayer->GetCharacterBody()->SetPosition(position);
        gpBase->mpPlayer->GetCharacterBody()->SetYaw(angles.y);
    }
    msStatus="Hosting "+msMapName+". Synchronizing clients after debug map load.";
}
