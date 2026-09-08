#ifndef LUX_MULTIPLAYER_WORLD_H
#define LUX_MULTIPLAYER_WORLD_H

#include "LuxBase.h"
#include "LuxMultiplayerWorldProtocol.h"
#include <map>
#include <set>
#include <deque>

class cLuxMultiplayer;
class cLuxMap;

struct cLuxMultiplayerRemotePlayer
{
    cVector3f position, size, renderPosition;
    float yaw, age;
    uint32_t sequence;
    cLuxMultiplayerRemotePlayer() : position(0), size(0), renderPosition(0), yaw(0), age(0), sequence(0) {}
};

// All Newton access runs on the game thread. The host owns the world, with an
// exclusive, short-lived simulation lease for a player's grabbed assembly.
class cLuxMultiplayerWorld
{
public:
    explicit cLuxMultiplayerWorld(cLuxMultiplayer* apSession);
    void OnMapLoaded(cLuxMap* apMap);
    void Shutdown();
    void Reset();
    void Update(float afTimeStep);
    void RenderSolid(cRendererCallbackFunctions* apFunctions);
    bool HandleMessage(uint32_t alPeer, const std::vector<uint8_t>& avMessage);
    void OnPeerDisconnected(uint32_t alPeer);
    bool SendInitialState(uint32_t alPeer);

    // A client returns false while waiting for the host. On grant, the pending
    // state is entered only if the interact button is still held in the game.
    bool RequestInteraction(iPhysicsBody* apBody, eLuxPlayerState aState, const cVector3f& avFocus);
    void ReleaseInteraction();
    bool OwnsInteraction(iPhysicsBody* apBody) const;
    static bool IsInteractionState(eLuxPlayerState aState);
    const std::map<uint32_t, cLuxMultiplayerRemotePlayer>& GetRemotePlayers() const { return mPlayers; }

private:
    struct BodyTrack
    {
        tString name;
        iPhysicsBody* body;
        LuxWorldWire::Body lastSent, target;
        uint32_t receivedSequence;
        float targetAge;
        bool sent, received, hasTarget;
        BodyTrack() : body(NULL), receivedSequence(0), targetAge(0), sent(false), received(false), hasTarget(false) {}
    };
    struct Lease
    {
        uint32_t owner, token;
        float remaining;
        std::vector<uint64_t> bodies;
        std::map<uint64_t, bool> originalGravity;
        Lease() : owner(0), token(0), remaining(0) {}
    };

    void RefreshBodies();
    iPhysicsBody* FindBody(uint64_t alId) const;
    LuxWorldWire::Body CaptureBody(uint64_t alId, iPhysicsBody* apBody) const;
    void SendBodyBatch(uint32_t alPeer, bool abBroadcast, const std::vector<LuxWorldWire::Body>& avBodies,
                       bool abReliable, bool abGroundTruth, uint32_t alToken = 0);
    void SendPose();
    void ApplyTargets(float afTimeStep);
    void ApplyBody(const LuxWorldWire::Body& aState, uint32_t alSequence, bool abGroundTruth, bool abFromOwner);
    bool GrantLease(uint32_t alPeer, uint64_t alBody, uint32_t alRequest);
    void EndLease(uint32_t alToken, bool abBroadcast);
    void SendLease(const Lease& aLease, uint32_t alRequest, uint32_t alPeer, bool abBroadcast);
    void AcceptPendingInteraction();
    void CancelPendingInteraction();
    bool InteractionStillPressed() const;

    cLuxMultiplayer* mpSession;
    cLuxMap* mpMap;
    std::map<uint64_t, BodyTrack> mBodies;
    std::map<uint32_t, cLuxMultiplayerRemotePlayer> mPlayers;
    std::map<uint32_t, Lease> mLeases;
    std::map<uint64_t, uint32_t> mBodyLeases;
    std::set<uint64_t> mAmbiguousBodies;
    std::map<uint32_t, std::deque<std::vector<uint8_t> > > mInitialPackets;
    size_t mlInitialBytes;
    bool mbLocalInteractionStarted;
    uint32_t mlSequence, mlLeaseCounter, mlLocalLease, mlRequestCounter, mlPendingRequest;
    uint64_t mlPendingBody;
    eLuxPlayerState mPendingState, mPendingPreviousState;
    cVector3f mvPendingFocus;
    float mfSendTime, mfGroundTruthTime, mfRenewTime, mfPendingTime;
};

#endif
