#ifndef LUX_MULTIPLAYER_ENTITIES_H
#define LUX_MULTIPLAYER_ENTITIES_H
#include "LuxMultiplayerProtocol.h"
#include <map>
#include <set>
#include <deque>

class cLuxMultiplayer;
class iLuxEntity;
class iLuxProp;
class cLuxDiary;

// Native interactions have one host-approved winner. Entity snapshots carry
// gameplay flags and joint constraints independently of the physics poses.
class cLuxMultiplayerEntities {
public:
    explicit cLuxMultiplayerEntities(cLuxMultiplayer* session);
    void Reset();
    void Update(float dt);
    void OnPeerDisconnected(uint32_t peer);
    bool SendInitialState(uint32_t peer);
    bool HandleMessage(uint32_t peer, const std::vector<uint8_t>& data);
    bool BeginInteraction(iLuxEntity* entity);
    void CompleteInteraction(iLuxEntity* entity, bool succeeded);
    bool DeferCallback(iLuxEntity* entity) const;
    void RecordDiaryIndex(const std::string& name, int index);
    bool DeferDiaryPresentation(const std::string& name, cLuxDiary* diary);
    void RecordDiaryDecision(bool open);
private:
    struct Claim { uint32_t peer, token; float age; uint64_t runtimeId; bool callbackOnly, diary; };
    struct PendingDiary { std::string name; cLuxDiary* diary; float age; };
    bool IsNative(iLuxEntity* entity) const;
    bool Eligible(iLuxEntity* entity);
    std::vector<uint8_t> Capture(iLuxProp* prop);
    bool Apply(const std::vector<uint8_t>& data);
    bool Commit(iLuxEntity* entity, bool remote, bool callbackOnly=false, int diaryIndex=-1);
    void BroadcastState(iLuxEntity* entity);
    cLuxMultiplayer* mpSession;
    std::map<std::string, Claim> mClaims;
    std::map<std::string, std::vector<uint8_t> > mLastStates;
    std::map<std::string, uint64_t> mRemovedItems;
    std::map<uint32_t, std::deque<std::string> > mInitial;
    std::map<uint32_t, PendingDiary> mPendingDiaries;
    bool* mpDiaryDecision;
    std::string msPending, msGranted;
    uint32_t mlToken, mlGrantedToken;
    uint64_t mlPendingRuntimeID;
    int mlDiaryIndex;
    float mfSnapshotTime, mfPendingTime, mfGroundTruthTime;
    bool mbCallback;
};
#endif
