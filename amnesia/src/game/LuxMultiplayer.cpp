#include "LuxMultiplayer.h"
#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerProtocol.h"
#include "LuxMultiplayerUI.h"
#include "LuxMultiplayerWorld.h"
#include "LuxMultiplayerScript.h"
#include "LuxSteamLaunch.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxEntity.h"
#include "LuxProp_LevelDoor.h"
#include "LuxProp_Object.h"
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
#include <algorithm>
#include <cstdio>

using namespace luxnet;

cLuxMultiplayer::cLuxMultiplayer():iLuxUpdateable("LuxMultiplayer"),mpUI(NULL),mpWorld(NULL),
    mlScriptHistoryBytes(0),mlLocalPeer(0),mlMapEpoch(0),mlMapChecksum(0),mlExpectedMapBytes(0),
    mlPendingSteamInvite(0),msStatus("Offline"),mbLoading(false),mbReceiving(false),mbReady(false),mbReturnToMenu(false),
    mbApplyingScriptEffect(false),mbRemoteScriptTrigger(false),mbHistoryComplete(true),mbRestoreFocusWait(false),mbHasClientMap(false),mbPreserveHostPosition(false),mbSessionWorld(false),mfJoinAge(0) {
    mpWorld=hplNew(cLuxMultiplayerWorld,(this));
    mpUI=hplNew(cLuxMultiplayerUI,(this));
}
cLuxMultiplayer::~cLuxMultiplayer() {
    mTransport.Stop(); hplDelete(mpUI); hplDelete(mpWorld);
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
    if(settings.useSteam && !hpl::cNetworkTransport::InitializeSteam(msStatus)) return false;
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
    if(!LuxReadMultiplayerMap(source,preflight) || !LuxValidateMultiplayerMap(preflight,msStatus)) {
        if(preflight.empty()) msStatus="Cannot read the selected XML .map file (maximum 16 MiB): "+folder+map;
        return false;
    }
    folder=cString::To8Char(cString::GetFilePathW(source));
    gpBase->mpEngine->GetResources()->AddResourceDir(cString::GetFilePathW(source),false);
    gpBase->mpDebugHandler->SetFastForward(false);
    const bool listening=settings.useSteam ?
        mTransport.HostSteam(settings.maxPlayers-1,settings.publicLobby,map,msStatus) :
        mTransport.Host(settings.port,settings.maxPlayers-1,msStatus);
    if(!listening) return false;
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
bool cLuxMultiplayer::Join(const tString& address) {
    if(IsActive()) {msStatus="Disconnect the current session before joining another.";return false;}
    if(address.empty() || address.size()>255) {msStatus="Enter a host IP address, optionally followed by :port.";return false;}
    EnsureProfile(); gpBase->mpDebugHandler->SetFastForward(false);
    gpBase->mpDebugHandler->SetDebugWindowActive(false);
    if(!mTransport.Join(address,27015,msStatus)) return false;
    mSettings.useSteam=false;
    BeginClientSession();
    msStatus="Connecting to "+address+"...";
    return true;
}
bool cLuxMultiplayer::JoinSteamLobby(const tString& code) {
    if(IsActive()) {msStatus="Disconnect the current session before joining another.";return false;}
    uint64_t lobby=0;
    if(!luxsteam::ParseLobbyCode(code,lobby)) {msStatus="Enter the numeric Steam lobby code shared by the host.";return false;}
    if(!hpl::cNetworkTransport::InitializeSteam(msStatus)) return false;
    EnsureProfile();
    if(!mTransport.JoinSteamLobby(lobby,msStatus)) return false;
    mSettings.useSteam=true;
    gpBase->mpDebugHandler->SetFastForward(false);
    gpBase->mpDebugHandler->SetDebugWindowActive(false);
    BeginClientSession();
    msStatus="Joining Steam lobby...";
    return true;
}
void cLuxMultiplayer::BeginClientSession() {
    mbSessionWorld=true;
    mbRestoreFocusWait=gpBase->mpEngine->GetWaitIfAppOutOfFocus();
    gpBase->mpEngine->SetWaitIfAppOutOfFocus(false);
    mbReady=false;mbReceiving=false;mbReturnToMenu=false;mbHasClientMap=false;mfJoinAge=0;mlMapEpoch=0;mlLocalPeer=0;
    gpBase->mpEngine->GetUpdater()->SetContainer("MultiplayerLoading");
    if(gpBase->mpMapHandler->GetCurrentMap()) gpBase->mpMapHandler->GetCurrentMap()->GetWorld()->SetActive(false);
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
    bool client=IsClient();
    if(IsActive()) gpBase->mpEngine->SetWaitIfAppOutOfFocus(mbRestoreFocusWait);
    mpWorld->Shutdown();mTransport.Stop();mPeers.clear();
    mvMapBytes.clear();mvScriptHistory.clear();mlScriptHistoryBytes=0;
    msPendingHostMap.clear();
    mbReady=false;mbReceiving=false;mbHasClientMap=false;
    msStatus=reason.empty()?"Disconnected":reason;
    if(client && !mbLoading) mbReturnToMenu=true;
    RemoveReceivedMapFiles();
}
void cLuxMultiplayer::RemoveReceivedMapFiles() {
    if(msReceivedMapPath.empty()) return;
    // Only names derived from the locally generated cache file; no directory deletion.
    tWString path=cString::To16Char(msReceivedMapPath);
    cPlatform::RemoveFile(path);
    cPlatform::RemoveFile(cString::SetFileExtW(path,_W("map_cache")));
    cPlatform::RemoveFile(cString::SetFileExtW(path,_W("map_cache_fastload")));
    cPlatform::RemoveFile(cString::SetFileExtW(path,_W("cmap")));
    msReceivedMapPath.clear();
}
void cLuxMultiplayer::Reset() {
    mpWorld->Reset();
    if(!mbLoading && IsActive()) Stop("Session ended.");
    else if(!mbLoading) mbSessionWorld=false;
}
bool cLuxMultiplayer::ShouldSuppressOfflineSaves() const {
    return IsActive() || (mbSessionWorld && gpBase->mpMapHandler->GetCurrentMap()!=NULL);
}
void cLuxMultiplayer::OnQuit() {Stop("Session ended.");}
void cLuxMultiplayer::OnMapLeave(cLuxMap*) {mpWorld->Reset();}
void cLuxMultiplayer::ShowWindow(bool campaign) {mpUI->Show(campaign);}
void cLuxMultiplayer::ToggleWindow() {mpUI->Toggle();}
bool cLuxMultiplayer::IsWindowVisible() const {return mpUI->IsVisible();}
bool cLuxMultiplayer::IsSteamOverlayActive() const {return hpl::cNetworkTransport::SteamOverlayActive();}
bool cLuxMultiplayer::Send(uint32_t peer,const std::vector<uint8_t>& data,bool reliable) {return mTransport.Send(peer,data,reliable);}
void cLuxMultiplayer::Broadcast(const std::vector<uint8_t>& data,bool reliable) {
    if(!IsHost()) return;
    for(auto& peer:mPeers) if(peer.second.ready && !Send(peer.first,data,reliable) && reliable)
        peer.second.reliableSendFailed=true;
}
bool cLuxMultiplayer::CaptureMap(cLuxMap* map,const tString& start) {
    if(!LuxReadMultiplayerMap(map->GetWorld()->GetFilePath(),mvMapBytes)) return false;
    ++mlMapEpoch;if(!mlMapEpoch) ++mlMapEpoch;
    msMapName=cString::GetFileName(map->GetFileName());msStartPos=start;
    mTransport.SetSteamMapName(msMapName);
    mlMapChecksum=Checksum(mvMapBytes);
    mvScriptHistory.clear();mlScriptHistoryBytes=0;mbHistoryComplete=true;
    for(auto& peer:mPeers) {peer.second.ready=false;peer.second.beginSent=false;peer.second.endSent=false;peer.second.offset=0;peer.second.age=0;}
    return true;
}
void cLuxMultiplayer::OnMapLoaded(cLuxMap* map,const tString& start) {
    if(!IsActive() || !map) return;
    mpWorld->OnMapLoaded(map);
    if(IsHost()) {
        if(!CaptureMap(map,start)) Stop("Could not read the new map for transfer; session stopped.");
    }
}
void cLuxMultiplayer::RejectPeer(uint32_t peer,const tString& reason) {
    if(IsHost()) {mTransport.Disconnect(peer,reason);mpWorld->OnPeerDisconnected(peer);mPeers.erase(peer);}
    else {Stop(reason);ShowWindow();}
}
void cLuxMultiplayer::HandleEvent(const hpl::cNetworkEvent& event) {
    if(event.type==hpl::eNetworkEventType::SessionFailed) {
        Stop(event.reason);ShowWindow();
    } else if(event.type==hpl::eNetworkEventType::SessionReady) {
        msStatus=IsHost() ? "Hosting "+msMapName+" on Steam. Invite friends or share the lobby code." : "Steam lobby joined. Connecting to host...";
    } else if(event.type==hpl::eNetworkEventType::Connected) {
        if(IsHost()) {mPeers[event.peer]=Peer();}
        else {Writer w(Hello);w.U32(ProtocolVersion);Send(0,w.data,true);msStatus="Connected. Waiting for host map...";}
    } else if(event.type==hpl::eNetworkEventType::Disconnected) {
        if(IsHost()) {mPeers.erase(event.peer);mpWorld->OnPeerDisconnected(event.peer);}
        else {Stop("Disconnected: "+event.reason);ShowWindow();}
    } else if(event.type==hpl::eNetworkEventType::Message) HandlePacket(event.peer,event.data);
}
void cLuxMultiplayer::SendMap(uint32_t peer,Peer& state) {
    if(!state.greeted || state.ready || mvMapBytes.empty()) return;
    if(!state.beginSent) {
        Writer w(MapBegin);w.U32(ProtocolVersion);w.U32(peer);w.U32(mlMapEpoch);
        w.U32(static_cast<uint32_t>(mvMapBytes.size()));w.U32(mlMapChecksum);
        w.String(msMapName);w.String(msStartPos);w.U8(mSettings.allowClientMapChanges);w.U8(mSettings.allPlayersTriggerScripts);
        if(!Send(peer,w.data,true)) return;
        state.beginSent=true;
    }
    for(int i=0;i<2 && state.offset<mvMapBytes.size();++i) {
        size_t size=std::min(size_t(MapChunkBytes),mvMapBytes.size()-state.offset);
        Writer w(MapChunk);w.U32(mlMapEpoch);w.U32(state.offset);w.Bytes(mvMapBytes.data()+state.offset,size);
        if(!Send(peer,w.data,true)) return;
        state.offset+=static_cast<uint32_t>(size);
    }
    if(state.offset==mvMapBytes.size() && !state.endSent) {
        Writer w(MapEnd);w.U32(mlMapEpoch);
        state.endSent=Send(peer,w.data,true);
    }
}
void cLuxMultiplayer::HandlePacket(uint32_t peer,const std::vector<uint8_t>& data) {
    if(data.empty() || data.size()>hpl::cNetworkTransport::MaxMessageBytes) {RejectPeer(peer,"Invalid packet size.");return;}
    Reader r(data);uint8_t type=data[0];
    if(IsHost()) {
        auto it=mPeers.find(peer);if(it==mPeers.end()) return;
        Peer& state=it->second;
        if(type==Hello) {
            uint32_t version=r.U32();
            if(!r.Done() || state.greeted || version!=ProtocolVersion) {RejectPeer(peer,"Multiplayer protocol mismatch. Use the same Amnesia build as the host.");return;}
            if(!mbHistoryComplete) {RejectPeer(peer,"This session has exceeded the late-join script history limit. Join after the next map change.");return;}
            state.greeted=true;return;
        }
        if(!state.greeted) {RejectPeer(peer,"Expected multiplayer handshake.");return;}
        if(type==Ready) {
            uint32_t epoch=r.U32();if(!r.Done()) {RejectPeer(peer,"Malformed map acknowledgement.");return;}
            if(epoch!=mlMapEpoch) return;
            if(!state.endSent || state.ready) {RejectPeer(peer,"Unexpected map acknowledgement.");return;}
            if(!mbHistoryComplete) {RejectPeer(peer,"The session's initialization history exceeded its limit while joining.");return;}
            state.ready=true;state.age=0;
            for(const auto& effect:mvScriptHistory) if(!Send(peer,effect,true)) {RejectPeer(peer,"Script state exceeded the connection queue.");return;}
            if(!mpWorld->SendInitialState(peer)) {RejectPeer(peer,"World is too large for initial synchronization (32 MiB limit).");return;}
            msStatus="Hosting "+msMapName+". Connected clients: "+cString::ToString(static_cast<int>(mPeers.size()));return;
        }
        if(!state.ready) return;
        if(type==MapChangeRequest) {
            uint32_t epoch=r.U32();tString map=r.String(512),start=r.String(128),a=r.String(256),b=r.String(256);
            if(!r.Done() || !SafeRelativePath(map)) {RejectPeer(peer,"Invalid map change request.");return;}
            if(epoch!=mlMapEpoch || !mSettings.allowClientMapChanges || state.requestCooldown>0) return;
            // Resolve and validate an unlocked level door in the host's authoritative map.
            cLuxMap* current=gpBase->mpMapHandler->GetCurrentMap();
            const auto& poses=mpWorld->GetRemotePlayers();auto pose=poses.find(peer);
            if(!current || pose==poses.end() || pose->second.age>2) return;
            cLuxProp_LevelDoor* door=NULL;
            cLuxEntityIterator entities=current->GetEntityIterator();
            while(entities.HasNext()) {
                iLuxEntity* ent=entities.Next();
                if(ent->GetEntityType()!=eLuxEntityType_Prop || static_cast<iLuxProp*>(ent)->GetPropType()!=eLuxPropType_LevelDoor) continue;
                cLuxProp_LevelDoor* candidate=static_cast<cLuxProp_LevelDoor*>(ent);
                if(!candidate->IsActive() || candidate->GetLocked() || cString::SetFileExt(candidate->GetMapFile(),"map")!=cString::SetFileExt(map,"map") || candidate->GetStartPos()!=start) continue;
                for(int i=0;i<candidate->GetBodyNum();++i) if((candidate->GetBody(i)->GetWorldPosition()-pose->second.position).Length()<4.0f) door=candidate;
            }
            if(!door) return;
            state.requestCooldown=2.0f;
            door->OnInteract(door->GetBody(0),pose->second.position);return;
        }
        if(type==EntityInteract) {
            uint32_t epoch=r.U32();tString name=r.String(256);uint32_t index=r.U32();
            if(!r.Done()) {RejectPeer(peer,"Invalid interaction request.");return;}
            if(epoch!=mlMapEpoch || state.requestCooldown>0 || !mSettings.allPlayersTriggerScripts) return;
            cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
            iLuxEntity* ent=map?map->GetEntityByName(name):NULL;
            const auto& poses=mpWorld->GetRemotePlayers();auto pose=poses.find(peer);
            if(!ent || !ent->IsActive() || index>=uint32_t(ent->GetBodyNum()) || pose==poses.end()) return;
            iPhysicsBody* body=ent->GetBody(index);
            if((body->GetWorldPosition()-pose->second.position).Length()>4.0f || !ent->CanInteract(body)) return;
            state.requestCooldown=0.2f;
            // Interactive physics controllers are handled by leases, never through the host's player state.
            cLuxMultiplayerRemoteTriggerScope remoteTrigger(true);
            ent->RunInteractCallbackFunc();return;
        }
        if(type>=64) {if(!mpWorld->HandleMessage(peer,data)) RejectPeer(peer,"Malformed world message.");return;}
        RejectPeer(peer,"Unexpected client packet.");return;
    }
    if(!IsClient() || peer!=0) return;
    if(type==MapBegin) {
        uint32_t version=r.U32(),id=r.U32(),epoch=r.U32(),size=r.U32(),crc=r.U32();
        tString name=r.String(256),start=r.String(128);uint8_t changes=r.U8(),triggers=r.U8();
        if(!r.Done() || version!=ProtocolVersion || id==0 || epoch==0 || size==0 || size>MaxMapBytes ||
            !SafeRelativePath(name) || cString::GetFileName(name)!=name || cString::ToLowerCase(cString::GetFileExt(name))!="map" || changes>1 || triggers>1) {
            RejectPeer(0,"Host sent an invalid map manifest.");return;
        }
        mpWorld->Reset();mlLocalPeer=id;mlMapEpoch=epoch;mlExpectedMapBytes=size;mlMapChecksum=crc;
        msMapName=name;msStartPos=start;mSettings.allowClientMapChanges=changes!=0;mSettings.allPlayersTriggerScripts=triggers!=0;
        mvMapBytes.clear();mvMapBytes.reserve(size);mbReceiving=true;mbReady=false;mfJoinAge=0;
        gpBase->mpEngine->GetUpdater()->SetContainer("MultiplayerLoading");
        if(gpBase->mpMapHandler->GetCurrentMap()) gpBase->mpMapHandler->GetCurrentMap()->GetWorld()->SetActive(false);
        msStatus="Receiving host map: "+name;return;
    }
    if(type==MapChunk) {
        uint32_t epoch=r.U32(),offset=r.U32();
        if(epoch!=mlMapEpoch) return;
        if(!r.valid || !mbReceiving || offset!=mvMapBytes.size() || data.size()-r.pos>MapChunkBytes ||
            data.size()-r.pos>mlExpectedMapBytes-mvMapBytes.size()) {RejectPeer(0,"Invalid or out-of-order map chunk.");return;}
        mvMapBytes.insert(mvMapBytes.end(),data.begin()+r.pos,data.end());return;
    }
    if(type==MapEnd) {
        uint32_t epoch=r.U32();if(epoch!=mlMapEpoch) return;
        if(!r.Done() || !mbReceiving || mvMapBytes.size()!=mlExpectedMapBytes || Checksum(mvMapBytes)!=mlMapChecksum) {RejectPeer(0,"Host map transfer failed its size/checksum check.");return;}
        mbReceiving=false;
        if(!LoadReceivedMap()) {RejectPeer(0,msStatus);return;}
        mbReady=true;Writer w(Ready);w.U32(mlMapEpoch);Send(0,w.data,true);
        msStatus="Joined "+msMapName+". Session remains live while menus are open.";return;
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
bool cLuxMultiplayer::LoadReceivedMap() {
    if(!LuxValidateMultiplayerMap(mvMapBytes,msStatus)) return false;
    tWString dir=gpBase->msBaseSavePath+_W("multiplayer_cache/");
    cPlatform::CreateFolder(dir);
    dir+=cString::To16Char(cString::ToString(cPlatform::GetApplicationTime())+"_"+cString::ToString(mlMapEpoch))+_W("/");
    cPlatform::CreateFolder(dir);
    tWString path=dir+cString::To16Char(msMapName);
    FILE* f=cPlatform::OpenFile(path,_W("wb"));
    if(!f) {msStatus="Could not write the received map to the multiplayer cache.";return false;}
    bool ok=fwrite(mvMapBytes.data(),1,mvMapBytes.size(),f)==mvMapBytes.size();fclose(f);
    if(!ok) {cPlatform::RemoveFile(path);msStatus="Failed to write the received map.";return false;}
    RemoveReceivedMapFiles();
    msReceivedMapPath=cString::To8Char(path);
    mbLoading=true;
    gpBase->SetCustomStory(NULL);gpBase->mbHardMode=false;
    gpBase->mpEngine->GetUpdater()->SetContainer("Default");
    gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
    if(!mbHasClientMap) ok=gpBase->StartGame(msMapName,cString::To8Char(dir),msStartPos);
    else {
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
            if(p.second.reliableSendFailed || (!p.second.ready && p.second.age>120)) expired.push_back(p.first);
            else SendMap(p.first,p.second);
        }
        for(uint32_t peer:expired) RejectPeer(peer,"Connection could not keep up with reliable world state or timed out during map load.");
    } else if(!mbReady) {
        mfJoinAge+=dt;
        if(mfJoinAge>120) {RejectPeer(0,"Timed out receiving the host map.");return;}
    }
    if(mbReady) UpdateBackgroundWorld(dt);
}
void cLuxMultiplayer::PostUpdate(float dt) {if(IsActive() && mbReady) mpWorld->Update(dt);}
void cLuxMultiplayer::UpdateBackgroundWorld(float dt) {
    if(gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()=="Default" || !gpBase->mpMapHandler->GetCurrentMap()) return;
    // Scene/physics are already global engine modules; only gameplay modules need the extra tick.
    iLuxUpdateable* modules[]={gpBase->mpMapHandler,gpBase->mpMapHelper,gpBase->mpPlayer,
        gpBase->mpInsanityHandler,gpBase->mpEffectRenderer,gpBase->mpMusicHandler,gpBase->mpMessageHandler,
        gpBase->mpEffectHandler,gpBase->mpCompletionCountHandler,gpBase->mpGlobalDataHandler,
        gpBase->mpHintHandler,gpBase->mpPostEffectHandler};
    tString container=gpBase->mpEngine->GetUpdater()->GetCurrentContainerName();
    for(auto* module:modules) {
        module->Update(dt);
        if(!IsActive() || !gpBase->mpMapHandler->GetCurrentMap() || gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!=container) break;
    }
}
bool cLuxMultiplayer::RequestMapChange(const tString& map,const tString& start,const tString& a,const tString& b) {
    if(IsHost() && mbRemoteScriptTrigger && !mSettings.allowClientMapChanges) return false;
    if(!IsClient()) return true;
    if(!mSettings.allowClientMapChanges) {msStatus="The host has disabled client map changes.";return false;}
    if(!mbReady || !SafeRelativePath(map)) return false;
    Writer w(MapChangeRequest);w.U32(mlMapEpoch);w.String(map);w.String(start);w.String(a);w.String(b);Send(0,w.data,true);
    msStatus="Map change requested from host.";return false;
}
bool cLuxMultiplayer::RemotePlayerTouches(iLuxEntity* entity) {
    if(!IsHost() || !mSettings.allPlayersTriggerScripts || !entity) return false;
    for(const auto& p:mpWorld->GetRemotePlayers()) {
        if(p.second.age>2.0f) continue;
        cBoundingVolume bounds;bounds.SetSize(p.second.size);bounds.SetPosition(p.second.position);
        for(int i=0;i<entity->GetBodyNum();++i)
            if(cMath::CheckBVIntersection(bounds,*entity->GetBody(i)->GetBoundingVolume())) return true;
    }
    return false;
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
    // Bound late-join initialization history. Refuse joins once a complete replay cannot fit.
    if(mlScriptHistoryBytes+effect.size()<=256*1024) {mvScriptHistory.push_back(effect);mlScriptHistoryBytes+=effect.size();}
    else mbHistoryComplete=false;
    Broadcast(effect,true);
}
bool cLuxMultiplayer::AllowObjectBreak(const tString& name) {
    if(IsClient()) return mbApplyingScriptEffect;
    if(IsHost()) {Writer w(ObjectBreak);w.U32(mlMapEpoch);w.String(name);BroadcastScriptEffect(w.data);}
    return true;
}
bool cLuxMultiplayer::HostChangeMap(const tString& map,const tString& start,bool preserve) {
    if(!IsHost() || map.empty()) return false;
    msPendingHostMap=map;msPendingHostStart=start;mbPreserveHostPosition=preserve;return true;
}
void cLuxMultiplayer::ProcessHostMapChange() {
    tString requested=msPendingHostMap,start=msPendingHostStart;msPendingHostMap.clear();
    tWString path=cString::To16Char(requested);
    if(!cPlatform::FileExists(path)) path=gpBase->mpEngine->GetResources()->GetFileSearcher()->GetFilePath(requested);
    std::vector<uint8_t> bytes;
    if(!LuxReadMultiplayerMap(path,bytes) || !LuxValidateMultiplayerMap(bytes,msStatus)) {
        if(bytes.empty()) msStatus="Cannot load selected host XML map: "+requested;
        ShowWindow();return;
    }
    cLuxMapHandler* maps=gpBase->mpMapHandler;
    cLuxMap* previous=maps->GetCurrentMap();
    cVector3f position=gpBase->mpPlayer->GetCharacterBody()->GetPosition();
    cCamera* camera=gpBase->mpPlayer->GetCamera();
    cVector3f angles(camera->GetPitch(),camera->GetYaw(),camera->GetRoll());
    gpBase->mpEngine->GetResources()->AddResourceDir(cString::GetFilePathW(path),false);
    tString previousFolder=maps->GetMapFolder();
    maps->SetMapFolder(cString::To8Char(cString::GetFilePathW(path)));
    cLuxMap* next=maps->LoadMap(cString::To8Char(cString::GetFileNameW(path)),true);
    if(!next) {maps->SetMapFolder(previousFolder);msStatus="Failed to load host map.";ShowWindow();return;}
    gpBase->mpEngine->GetUpdater()->SetContainer("Default");
    gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
    maps->SetCurrentMap(next,true,true,start);
    if(previous) maps->DestroyMap(previous,false);
    if(mbPreserveHostPosition) {
        camera=gpBase->mpPlayer->GetCamera();camera->SetPitch(angles.x);camera->SetYaw(angles.y);camera->SetRoll(angles.z);
        gpBase->mpPlayer->GetCharacterBody()->SetPosition(position);
        gpBase->mpPlayer->GetCharacterBody()->SetYaw(angles.y);
    }
    msStatus="Hosting "+msMapName+". Synchronizing clients after debug map load.";
}
