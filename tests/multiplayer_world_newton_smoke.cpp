// Compiles the production replication implementation against small game/session
// adapters, but uses the built HPL2 and Newton worlds, bodies, shapes and solver.
// No renderer, retail content, sockets or Amnesia configuration is required.
#include "../amnesia/src/game/LuxTypes.h"
#include "impl/PhysicsWorldNewton.h"
#include <cassert>
#include <iostream>
#include <cstdint>

#define LUX_BASE_H
#define LUX_MAP_H
#define LUX_MAP_HANDLER_H
#define LUX_PLAYER_H
#define LUX_PLAYER_STATE_H
#define LUX_INPUT_HANDLER_H
#define LUX_PROP_H
#define LUX_MULTIPLAYER_H

class cLuxMap
{
public:
    iPhysicsWorld* physics;
    iPhysicsWorld* GetPhysicsWorld() { return physics; }
};
class cLuxMapHandler
{
public:
    cLuxMap* map;
    cLuxMap* GetCurrentMap() { return map; }
};
class cLuxPlayer
{
public:
    iCharacterBody* character = NULL;
    eLuxPlayerState state = eLuxPlayerState_Normal;
    iCharacterBody* GetCharacterBody() { return character; }
    eLuxPlayerState GetCurrentState() { return state; }
    void ChangeState(eLuxPlayerState value) { state = value; }
};
class cLuxInputHandler
{
public:
    eLuxInputState GetState() { return eLuxInputState_Game; }
};
struct SmokeInput { bool pressed = true; bool IsTriggerd(int) { return pressed; } };
struct SmokeEngine { SmokeInput input; SmokeInput* GetInput() { return &input; } };
struct SmokeBase
{
    cLuxPlayer* mpPlayer;
    cLuxMapHandler* mpMapHandler;
    cLuxInputHandler* mpInputHandler;
    SmokeEngine* mpEngine;
};
SmokeBase* gpBase;
int hplMain(const tString&) { return 0; } // HPL's Windows entry point is unused.
class cLuxPlayerStateVars
{
public:
    static void SetupInteraction(iPhysicsBody*, const cVector3f&) {}
};
class iLuxEntity
{
public:
    eLuxEntityType GetEntityType() { return eLuxEntityType_Prop; }
};
class iLuxProp : public iLuxEntity
{
public:
    std::vector<iPhysicsBody*> bodies;
    int GetBodyNum() { return static_cast<int>(bodies.size()); }
    iPhysicsBody* GetBody(int index) { return bodies[index]; }
};
class cLuxMultiplayer
{
public:
    struct Packet { uint32_t peer; std::vector<uint8_t> bytes; bool reliable; };
    bool host, blocked = false;
    uint32_t peer;
    std::vector<Packet> sent;
    cLuxMultiplayer(bool isHost, uint32_t id) : host(isHost), peer(id) {}
    bool IsActive() const { return true; }
    bool IsHost() const { return host; }
    bool IsClient() const { return !host; }
    bool IsWindowVisible() const { return false; }
    uint32_t GetLocalPeerId() const { return peer; }
    uint32_t GetMapEpoch() const { return 7; }
    bool Send(uint32_t destination, const std::vector<uint8_t>& bytes, bool reliable)
    {
        if (blocked) return false;
        sent.push_back({ destination, bytes, reliable }); return true;
    }
    void Broadcast(const std::vector<uint8_t>& bytes, bool reliable) { Send(UINT32_MAX, bytes, reliable); }
};

#include "../amnesia/src/game/LuxMultiplayerWorld.cpp"

struct Fixture
{
    cPhysicsWorldNewton physics;
    cLuxMap map;
    cLuxMapHandler maps;
    cLuxPlayer player;
    cLuxInputHandler inputs;
    SmokeEngine engine;
    SmokeBase base;
    cLuxMultiplayer session;
    cLuxMultiplayerWorld replication;
    iPhysicsBody* body;

    Fixture(bool host, uint32_t peer) : session(host, peer), replication(&session)
    {
        physics.SetWorld(NULL); physics.SetWorldSize(cVector3f(-100), cVector3f(100));
        physics.SetGravity(0); physics.SetNumberOfThreads(1); physics.SetSaveContactPoints(false);
        iCollideShape* shape = physics.CreateBoxShape(cVector3f(0.5f), NULL);
        body = physics.CreateBody("test_crate", shape); body->SetMass(2); body->SetGravity(false);
        body->SetLinearDamping(0.001f); body->SetAngularDamping(0); body->SetAutoDisable(false);
        body->SetPosition(cVector3f(0, 0, 0));
        map.physics = &physics; maps.map = &map;
        base = { &player, &maps, &inputs, &engine };
        Activate(); replication.OnMapLoaded(&map);
    }
    void Activate() { gpBase = &base; }
};

static void Deliver(Fixture& from, Fixture& to, bool dropSome = false)
{
    static unsigned packetNumber = 0;
    std::vector<cLuxMultiplayer::Packet> packets; packets.swap(from.session.sent);
    to.Activate();
    for (const auto& packet : packets)
    {
        if (dropSome && !packet.reliable && ++packetNumber % 5 == 0) continue;
        if (packet.peer == UINT32_MAX || packet.peer == to.session.peer || !from.session.host)
            assert(to.replication.HandleMessage(from.session.peer, packet.bytes));
    }
}

static std::vector<uint8_t> PosePacket(uint32_t peer, uint32_t sequence, const cVector3f& position)
{
    Writer writer(Pose, 7); writer.U32(peer); writer.U32(sequence);
    writer.F32(position.x); writer.F32(position.y); writer.F32(position.z);
    writer.F32(0.6f); writer.F32(1.8f); writer.F32(0.6f); writer.F32(0);
    return writer.bytes;
}

int main()
{
    Fixture host(true, 0), client(false, 1);
    client.body->SetPosition(cVector3f(20, 0, 0));
    host.Activate(); host.session.blocked = true;
    assert(host.replication.SendInitialState(1));
    host.replication.Update(1.0f / 60);
    assert(host.session.sent.empty());
    host.session.blocked = false;
    host.replication.Update(1.0f / 60);
    Deliver(host, client);
    assert(cMath::Vector3Dist(host.body->GetLocalPosition(), client.body->GetLocalPosition()) < 0.001f);

    // Converge a disturbed client using real Newton integration and 20% loss of
    // unreliable snapshots. No periodic ground truth occurs during this window.
    client.body->SetPosition(cVector3f(-0.5f, 0, 0));
    host.body->SetLinearVelocity(cVector3f(2, 0, 0));
    for (int frame = 0; frame < 90; ++frame)
    {
        host.physics.Simulate(1.0f / 60); client.physics.Simulate(1.0f / 60);
        host.Activate(); host.replication.Update(1.0f / 60); Deliver(host, client, true);
        client.Activate(); client.replication.Update(1.0f / 60); client.session.sent.clear();
    }
    float positionError = cMath::Vector3Dist(host.body->GetLocalPosition(), client.body->GetLocalPosition());
    float velocityError = cMath::Vector3Dist(host.body->GetLinearVelocity(), client.body->GetLinearVelocity());
    assert(positionError < 0.15f && velocityError < 0.4f);

    // A valid originator echo and older pose are ignored, never treated as a
    // malformed packet that would disconnect a healthy player.
    client.Activate(); assert(client.replication.HandleMessage(0, PosePacket(1, 20, 0)));
    assert(client.replication.HandleMessage(0, PosePacket(2, 20, 0)));
    assert(client.replication.HandleMessage(0, PosePacket(2, 19, 0)));
    assert(client.replication.GetRemotePlayers().at(2).sequence == 20);

    host.Activate(); host.session.sent.clear();
    iPhysicsBody* panel = host.physics.CreateBody("jointed_panel", host.physics.CreateBoxShape(cVector3f(0.25f), NULL));
    panel->SetMass(1); panel->SetPosition(host.body->GetLocalPosition() + cVector3f(0.6f, 0, 0));
    host.physics.CreateJointBall("assembly_joint", host.body->GetLocalPosition() + cVector3f(0.3f, 0, 0), cVector3f(0, 1, 0), host.body, panel);
    host.body->SetGravity(true);
    assert(host.replication.HandleMessage(1, PosePacket(1, 50, host.body->GetLocalPosition())));
    assert(host.replication.HandleMessage(2, PosePacket(2, 50, host.body->GetLocalPosition())));
    Writer request1(LeaseRequest, 7); request1.U32(1); request1.U64(BodyId("test_crate"));
    Writer request2(LeaseRequest, 7); request2.U32(1); request2.U64(BodyId("jointed_panel"));
    host.session.sent.clear();
    assert(host.replication.HandleMessage(1, request1.bytes));
    assert(host.replication.HandleMessage(2, request2.bytes));
    uint32_t token = 0; unsigned grants = 0, denied = 0;
    for (const auto& packet : host.session.sent)
    {
        Reader reader(packet.bytes); uint8_t type = reader.U8(); reader.U32();
        if (type == LeaseGrant) { assert(reader.U32() == 1); token = reader.U32(); ++grants; }
        if (type == LeaseDenied) ++denied;
    }
    assert(token && grants == 1 && denied == 1);

    // Final owner release preserves its final throw velocity and gives the next
    // player the assembly. A delayed state with the revoked token is harmless.
    Body state = {}; // Filled from the current real Newton body below.
    state.id = BodyId("test_crate");
    for (int i = 0; i < 12; ++i) state.matrix[i] = host.body->GetLocalMatrix().v[i];
    Store(state.linear, cVector3f(4, 1, 0)); Store(state.angular, cVector3f(0, 1, 0)); state.flags = Awake | Active;
    Writer finalState(Bodies, 7); finalState.U32(100); finalState.U32(token); finalState.U8(0); finalState.U8(1); WriteBody(finalState, state);
    assert(host.replication.HandleMessage(1, finalState.bytes));
    assert(!host.body->GetGravity());
    Writer release(LeaseRelease, 7); release.U32(token); release.U8(1);
    assert(host.replication.HandleMessage(1, release.bytes));
    assert(cMath::Vector3Dist(host.body->GetLinearVelocity(), cVector3f(4, 1, 0)) < 0.001f);
    assert(host.body->GetGravity());
    assert(host.replication.HandleMessage(1, finalState.bytes));
    host.session.sent.clear(); assert(host.replication.HandleMessage(2, request2.bytes));
    assert(host.session.sent.size() == 1 && host.session.sent[0].bytes[0] == LeaseGrant);
    host.replication.OnPeerDisconnected(2);
    host.session.sent.clear(); assert(host.replication.HandleMessage(1, request1.bytes));
    assert(host.session.sent.size() == 1 && host.session.sent[0].bytes[0] == LeaseGrant);
    host.replication.Shutdown();

    // Pending interaction waits for the host. Cancelling while the grant is in
    // flight releases the reservation without sending stale local body state.
    Fixture pendingHost(true, 0), pendingClient(false, 1);
    pendingHost.Activate(); assert(pendingHost.replication.HandleMessage(1, PosePacket(1, 1, 0)));
    pendingHost.session.sent.clear(); pendingClient.Activate();
    assert(!pendingClient.replication.RequestInteraction(pendingClient.body, eLuxPlayerState_InteractGrab, 0));
    assert(pendingClient.player.state == eLuxPlayerState_Normal && !pendingClient.replication.OwnsInteraction(pendingClient.body));
    Deliver(pendingClient, pendingHost);
    pendingClient.engine.input.pressed = false;
    Deliver(pendingHost, pendingClient);
    assert(pendingClient.player.state == eLuxPlayerState_Normal);
    assert(pendingClient.session.sent.size() == 1 && pendingClient.session.sent[0].bytes[0] == LeaseRelease);
    Deliver(pendingClient, pendingHost); pendingHost.session.sent.clear();
    pendingClient.Activate(); pendingClient.engine.input.pressed = true;
    assert(!pendingClient.replication.RequestInteraction(pendingClient.body, eLuxPlayerState_InteractGrab, 0));
    Deliver(pendingClient, pendingHost); Deliver(pendingHost, pendingClient);
    assert(pendingClient.player.state == eLuxPlayerState_InteractGrab && pendingClient.replication.OwnsInteraction(pendingClient.body));
    pendingHost.Activate();
    for (int frame = 0; frame < 130; ++frame) pendingHost.replication.Update(1.0f / 60);
    Deliver(pendingHost, pendingClient);
    assert(pendingClient.player.state == eLuxPlayerState_Normal && !pendingClient.replication.OwnsInteraction(pendingClient.body));
    std::cout << "Real Newton replication smoke passed: position error=" << positionError
              << ", velocity error=" << velocityError << "; assembly leases/cancellation/expiry/release/disconnect verified.\n";
}
