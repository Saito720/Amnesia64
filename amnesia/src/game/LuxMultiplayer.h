#ifndef LUX_MULTIPLAYER_H
#define LUX_MULTIPLAYER_H
#include "LuxBase.h"
#include "network/NetworkTransport.h"
#include <cstdint>
#include <map>
#include <set>
#include <vector>

class cLuxMultiplayerUI;
class cLuxMultiplayerWorld;
class cLuxMultiplayerEntities;
class iLuxEntity;
class cLuxDiary;

enum eLuxMultiplayerLoadPhase {
    eLuxMultiplayerLoadPhase_None, eLuxMultiplayerLoadPhase_Connecting,
    eLuxMultiplayerLoadPhase_Preparing, eLuxMultiplayerLoadPhase_Checking,
    eLuxMultiplayerLoadPhase_Downloading, eLuxMultiplayerLoadPhase_Loading
};

struct cLuxMultiplayerSettings {
    tString map, startPos;
    bool useSteam = true;
    bool publicLobby = false;
    unsigned short port = 27015;
    unsigned maxPlayers = 4;
    bool allowClientMapChanges = false;
    bool allPlayersTriggerScripts = true;
    bool playerCollision = false;
};

// Global module: transport and world progression survive local menu containers.
class cLuxMultiplayer : public iLuxUpdateable {
    friend class cLuxMultiplayerEntities;
public:
    cLuxMultiplayer();
    ~cLuxMultiplayer();
    void Update(float afTimeStep);
    void PostUpdate(float afTimeStep);
    void Reset();
    void OnQuit();
    void OnMapLeave(cLuxMap* apMap);
    void OnMapLoaded(cLuxMap* apMap, const tString& asStartPos);
    bool Host(const cLuxMultiplayerSettings& settings);
    bool Join(const tString& address);
    bool JoinSteamLobby(const tString& code);
    bool IsSteamAvailable() const { return hpl::cNetworkTransport::SteamAvailable(); }
    bool IsSteamOverlayActive() const;
    tString GetSteamStatus() const { return hpl::cNetworkTransport::SteamStatus(); }
    bool IsSteamSession() const { return mTransport.IsSteamSession(); }
    uint64_t GetSteamLobbyID() const { return mTransport.GetSteamLobbyID(); }
    void RefreshSteamLobbies();
    void RetrySteam();
    bool IsSteamLobbySearchPending() const { return mTransport.IsSteamLobbySearchPending(); }
    const std::vector<hpl::cSteamLobbyInfo>& GetSteamLobbies() const { return mTransport.GetSteamLobbies(); }
    void InviteSteamFriends();
    void QueueSteamInvite(uint64_t lobby);
    uint64_t GetPendingSteamInvite() const { return mlPendingSteamInvite; }
    void AcceptSteamInvite();
    void DismissSteamInvite() { mlPendingSteamInvite=0; }
    void Stop(const tString& reason="");
    void ClearMapCache();
    bool IsActive() const { return mTransport.IsActive(); }
    bool IsHost() const { return mTransport.IsHost(); }
    bool IsClient() const { return IsActive() && !IsHost(); }
    bool IsReady() const { return mbReady; }
    bool ShouldSuppressOfflineSaves() const;
    const tString& GetStatus() const { return msStatus; }
    eLuxMultiplayerLoadPhase GetLoadPhase() const { return mLoadPhase; }
    const tString& GetLoadScreenStatus() const { return mLoadPhase==eLuxMultiplayerLoadPhase_None?msStatus:msLoadScreenStatus; }
    uint32_t GetDownloadReceivedBytes() const { return mlDownloadedMapBytes; }
    uint32_t GetDownloadTotalBytes() const { return mlExpectedMapBytes; }
    const cLuxMultiplayerSettings& GetSettings() const { return mSettings; }
    uint32_t GetLocalPeerId() const { return mlLocalPeer; }
    uint32_t GetMapEpoch() const { return mlMapEpoch; }
    cLuxMultiplayerWorld* GetWorld() { return mpWorld; }
    bool Send(uint32_t peer, const std::vector<uint8_t>& data, bool reliable);
    void Broadcast(const std::vector<uint8_t>& data, bool reliable);
    void ShowWindow(bool campaign=false);
    void ToggleWindow();
    bool IsWindowVisible() const;
    bool RequestMapChange(const tString& map,const tString& start,const tString& startSound,const tString& endSound);
    bool HostChangeMap(const tString& map,const tString& start="",bool preservePlayerPosition=false);
    void NotifyHostMapChange(const tString& map);
    void CancelHostMapChange(const tString& reason);
    bool RemotePlayerTouches(iLuxEntity* entity);
    bool RequestEntityInteraction(iLuxEntity* entity, iPhysicsBody* body, const cVector3f& pos);
    void BroadcastScriptEffect(const std::vector<uint8_t>& effect);
    bool AllowObjectBreak(const tString& name);
    bool BeginNativeInteraction(iLuxEntity* entity);
    void CompleteNativeInteraction(iLuxEntity* entity, bool succeeded);
    bool DeferNativeInteractionCallback(iLuxEntity* entity) const;
    void RecordNativeDiaryIndex(const tString& name, int index);
    bool DeferNativeDiaryPresentation(const tString& name, cLuxDiary* diary);
    void RecordNativeDiaryDecision(bool open);
    bool IsApplyingScriptEffect() const { return mbApplyingScriptEffect; }
    bool SetRemoteScriptTrigger(bool remote) { bool old=mbRemoteScriptTrigger;mbRemoteScriptTrigger=remote;return old; }
private:
    struct Peer {
        bool greeted=false, ready=false, beginSent=false, endSent=false, transferRequested=false;
        uint32_t offset=0;
        float age=0, requestCooldown=0;
        bool reliableSendFailed=false;
    };
    void HandleEvent(const hpl::cNetworkEvent& event);
    void HandlePacket(uint32_t peer,const std::vector<uint8_t>& data);
    void SendMap(uint32_t peer,Peer& state);
    bool CaptureMap(cLuxMap* map,const tString& start);
    bool LoadReceivedMap();
    bool FindMatchingMap();
    void RejectPeer(uint32_t peer,const tString& reason);
    void EnsureProfile();
    void BeginClientSession();
    void EnterClientLoading();
    void SetLoadPhase(eLuxMultiplayerLoadPhase phase,const tString& status,bool present=false);
    void SendMapPreparation(uint32_t peer,Peer& state);
    void UpdateBackgroundWorld(float dt);
    void ProcessHostMapChange();
    void RemoveReceivedMapFiles();
    hpl::cNetworkTransport mTransport;
    cLuxMultiplayerUI* mpUI;
    cLuxMultiplayerWorld* mpWorld;
    cLuxMultiplayerEntities* mpEntities;
    cLuxMultiplayerSettings mSettings;
    std::map<uint32_t,Peer> mPeers;
    std::vector<uint8_t> mvMapBytes;
    std::vector<std::vector<uint8_t> > mvScriptHistory;
    size_t mlScriptHistoryBytes;
    uint32_t mlLocalPeer, mlMapEpoch, mlMapChecksum, mlExpectedMapBytes;
    uint32_t mlDownloadedMapBytes=0;
    uint64_t mlPendingSteamInvite;
    tString msStatus, msMapName, msStartPos, msReceivedMapPath;
    tString msMapHash, msExistingMapPath, msLoadedMapPath;
    bool mbReusingMap=false;
    eLuxMultiplayerLoadPhase mLoadPhase=eLuxMultiplayerLoadPhase_None;
    eLuxMultiplayerLoadPhase mResumeLoadPhase=eLuxMultiplayerLoadPhase_None;
    tString msLoadScreenStatus, msResumeLoadStatus, msPreparingMap;
    uint32_t mlMapTransition=0;
    bool mbMapPreparing=false, mbResumeReady=false;
    // Preserve in-flight old-map events if a host load fails and is cancelled.
    std::vector<std::vector<uint8_t> > mvPreparingPackets;
    size_t mlPreparingPacketBytes=0;
    tString msPendingHostMap, msPendingHostStart;
    bool mbLoading, mbReceiving, mbReady, mbReturnToMenu, mbApplyingScriptEffect, mbRemoteScriptTrigger, mbHistoryComplete;
    bool mbRestoreFocusWait;
    bool mbHasClientMap;
    bool mbPreserveHostPosition;
    bool mbSessionWorld;
    float mfJoinAge;
};

class cLuxMultiplayerRemoteTriggerScope {
public:
    explicit cLuxMultiplayerRemoteTriggerScope(bool remote):mpSession(gpBase->mpMultiplayer),mbOld(false) {
        if(mpSession) mbOld=mpSession->SetRemoteScriptTrigger(remote);
    }
    ~cLuxMultiplayerRemoteTriggerScope() {if(mpSession) mpSession->SetRemoteScriptTrigger(mbOld);}
private:
    cLuxMultiplayer* mpSession;bool mbOld;
};
#endif
