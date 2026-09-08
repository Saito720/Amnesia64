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
class iLuxEntity;

struct cLuxMultiplayerSettings {
    tString map, startPos;
    bool useSteam = true;
    bool publicLobby = false;
    unsigned short port = 27015;
    unsigned maxPlayers = 4;
    bool allowClientMapChanges = false;
    bool allPlayersTriggerScripts = true;
};

// Global module: transport and world progression survive local menu containers.
class cLuxMultiplayer : public iLuxUpdateable {
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
    bool IsActive() const { return mTransport.IsActive(); }
    bool IsHost() const { return mTransport.IsHost(); }
    bool IsClient() const { return IsActive() && !IsHost(); }
    bool IsReady() const { return mbReady; }
    bool ShouldSuppressOfflineSaves() const;
    const tString& GetStatus() const { return msStatus; }
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
    bool RemotePlayerTouches(iLuxEntity* entity);
    bool RequestEntityInteraction(iLuxEntity* entity, iPhysicsBody* body, const cVector3f& pos);
    void BroadcastScriptEffect(const std::vector<uint8_t>& effect);
    bool AllowObjectBreak(const tString& name);
    bool IsApplyingScriptEffect() const { return mbApplyingScriptEffect; }
    bool SetRemoteScriptTrigger(bool remote) { bool old=mbRemoteScriptTrigger;mbRemoteScriptTrigger=remote;return old; }
private:
    struct Peer {
        bool greeted=false, ready=false, beginSent=false, endSent=false;
        uint32_t offset=0;
        float age=0, requestCooldown=0;
        bool reliableSendFailed=false;
    };
    void HandleEvent(const hpl::cNetworkEvent& event);
    void HandlePacket(uint32_t peer,const std::vector<uint8_t>& data);
    void SendMap(uint32_t peer,Peer& state);
    bool CaptureMap(cLuxMap* map,const tString& start);
    bool LoadReceivedMap();
    void RejectPeer(uint32_t peer,const tString& reason);
    void EnsureProfile();
    void BeginClientSession();
    void UpdateBackgroundWorld(float dt);
    void ProcessHostMapChange();
    void RemoveReceivedMapFiles();
    hpl::cNetworkTransport mTransport;
    cLuxMultiplayerUI* mpUI;
    cLuxMultiplayerWorld* mpWorld;
    cLuxMultiplayerSettings mSettings;
    std::map<uint32_t,Peer> mPeers;
    std::vector<uint8_t> mvMapBytes;
    std::vector<std::vector<uint8_t> > mvScriptHistory;
    size_t mlScriptHistoryBytes;
    uint32_t mlLocalPeer, mlMapEpoch, mlMapChecksum, mlExpectedMapBytes;
    uint64_t mlPendingSteamInvite;
    tString msStatus, msMapName, msStartPos, msReceivedMapPath;
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
