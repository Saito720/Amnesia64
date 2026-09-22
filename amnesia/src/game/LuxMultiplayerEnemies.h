#ifndef LUX_MULTIPLAYER_ENEMIES_H
#define LUX_MULTIPLAYER_ENEMIES_H
#include "LuxBase.h"
#include "LuxMultiplayerEnemyProtocol.h"
#include <deque>
#include <map>

class cLuxMultiplayer;
class iLuxEnemy;

class cLuxMultiplayerEnemies {
public:
    explicit cLuxMultiplayerEnemies(cLuxMultiplayer* session);
    void Reset();
    void OnMapLoaded(cLuxMap* map);
    void Update(float dt);
    void OnPeerDisconnected(uint32_t peer);
    bool SendInitialState(uint32_t peer);
    bool HandleMessage(uint32_t peer, const std::vector<uint8_t>& data);
    void UpdateReplica(iLuxEnemy* enemy, float dt);
private:
    struct HostTrack {
        uint64_t runtime = 0;
        LuxEnemyWire::State last;
        bool sent = false;
        float heartbeat = 0;
    };
    struct Replica {
        LuxEnemyWire::State state;
        uint64_t runtime = 0;
        uint64_t waitingRuntime = 0;
        cVector3f startPosition = cVector3f(0);
        cMatrixf startMesh = cMatrixf::Identity;
        float startYaw = 0, blendTime = 0, pendingTime = 0;
        bool received = false, applied = false, removed = false, confirmed = false;
        bool waitLogged = false;
    };
    iLuxEnemy* Find(const tString& name) const;
    LuxEnemyWire::State Capture(iLuxEnemy* enemy);
    void PrepareReplica(iLuxEnemy* enemy);
    bool Apply(Replica& replica, iLuxEnemy* enemy, bool snap);
    void ApplyPose(Replica& replica, iLuxEnemy* enemy, float dt);
    void RemoveReplica(iLuxEnemy* enemy);
    void RefreshHost(bool movement, float dt);
    void UpdateLocalMusic(iLuxEnemy* enemy);
    cLuxMultiplayer* mpSession;
    cLuxMap* mpMap = NULL;
    std::map<tString,HostTrack> mHost;
    std::map<tString,LuxEnemyWire::Removed> mRemoved;
    std::map<tString,Replica> mReplicas;
    std::map<uint32_t,std::deque<std::vector<uint8_t>>> mInitial;
    uint32_t mlSequence = 0;
    float mfSendTime = 0;
};
#endif
