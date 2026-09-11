// Compiles the production replication implementation against small game/session
// adapters, but uses the built HPL2 and Newton worlds, bodies, shapes and solver.
// No renderer, retail content, sockets or Amnesia configuration is required.
#include "../amnesia/src/game/LuxTypes.h"
#include "impl/PhysicsWorldNewton.h"
#include <cassert>
#include <iostream>
#include <cstdint>
#include <crtdbg.h>

#define LUX_BASE_H
#define LUX_MAP_H
#define LUX_MAP_HANDLER_H
#define LUX_PLAYER_H
#define LUX_PLAYER_STATE_H
#define LUX_INPUT_HANDLER_H
#define LUX_PROP_H
#define LUX_PROP_SWING_DOOR_H
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
    eLuxPropType type = eLuxPropType_Object;
    eLuxPropType GetPropType() { return type; }
    std::vector<iPhysicsBody*> bodies;
    bool pendingPlayerClearance = false;
    int GetBodyNum() { return static_cast<int>(bodies.size()); }
    iPhysicsBody* GetBody(int index) { return bodies[index]; }
    bool IsPlayerCollisionTemporarilyDisabled(iPhysicsBody* body) const
    { return pendingPlayerClearance && body && !body->GetCollideCharacter(); }
};
class cLuxProp_SwingDoor : public iLuxProp
{
public:
    bool closed = true, locked = false, disableAutoClose = true;
    cLuxProp_SwingDoor() { type = eLuxPropType_SwingDoor; }
    bool GetLocked() { return locked; }
    void SetClosed(bool value, bool) { closed = value; }
    void SetDisableAutoClose(bool value) { disableAutoClose = value; }
};
struct cLuxMultiplayerSettings { bool playerCollision = false; };
class cLuxMultiplayer
{
public:
    struct Packet { uint32_t peer; std::vector<uint8_t> bytes; bool reliable; };
    bool host, blocked = false;
    uint32_t peer;
    cLuxMultiplayerSettings settings;
    const cLuxMultiplayerSettings& GetSettings() const { return settings; }
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

static std::vector<uint8_t> PosePacket(uint32_t peer, uint32_t sequence, const cVector3f& position, float height = 1.8f)
{
    Writer writer(Pose, 7); writer.U32(peer); writer.U32(sequence);
    writer.F32(position.x); writer.F32(position.y); writer.F32(position.z);
    writer.F32(0.6f); writer.F32(height); writer.F32(0.6f); writer.F32(0);
    return writer.bytes;
}

static void CheckDropCollisionGuard()
{
    Fixture host(true, 0), client(false, 1);
    iLuxProp hostProp, clientProp;
    hostProp.bodies = {host.body}; clientProp.bodies = {client.body};
    host.body->SetUserData(&hostProp); client.body->SetUserData(&clientProp);
    host.player.character = host.physics.CreateCharacterBody("local_player", cVector3f(0.6f, 1.8f, 0.6f));
    host.player.character->SetPosition(0);
    host.Activate();
    assert(host.replication.RequestInteraction(host.body, eLuxPlayerState_InteractGrab, 0));
    // Native OnLeaveState disables collision until the prop clears the player,
    // then the player gate releases its lease. Restoration must honor that order.
    hostProp.pendingPlayerClearance = true; host.body->SetCollideCharacter(false);
    host.replication.ReleaseInteraction();
    assert(!host.body->GetCollideCharacter() && host.body->GetCollide());
    assert(!host.replication.IsEntityLeased(&hostProp));

    // A client's local post-drop guard also wins over reliable host snapshots.
    // Once native clearance completes, ordinary network flags apply again.
    hostProp.pendingPlayerClearance = false; host.body->SetCollideCharacter(true);
    clientProp.pendingPlayerClearance = true; client.body->SetCollideCharacter(false);
    host.session.sent.clear(); host.replication.Update(0.05f); Deliver(host, client);
    assert(!client.body->GetCollideCharacter() && client.body->GetCollide());
    clientProp.pendingPlayerClearance = false; client.body->SetCollideCharacter(true);
    host.Activate(); host.body->SetCollideCharacter(false);
    host.replication.Update(0.05f); Deliver(host, client);
    assert(!client.body->GetCollideCharacter());
    host.Activate(); host.body->SetCollideCharacter(true);
    host.replication.Update(0.05f); Deliver(host, client);
    assert(client.body->GetCollideCharacter());

    // A departed owner still restores both authored collision flags, including
    // originally disabled flags. No local drop guard exists on this host.
    host.Activate(); host.body->SetCollide(false); host.body->SetCollideCharacter(false);
    assert(host.replication.HandleMessage(1, PosePacket(1, 1, 0)));
    Writer request(LeaseRequest, 7); request.U32(1); request.U64(BodyId("test_crate"));
    assert(host.replication.HandleMessage(1, request.bytes));
    assert(host.replication.IsInteractionOwnedByOther(host.body));
    host.body->SetCollide(true); host.body->SetCollideCharacter(true);
    host.replication.OnPeerDisconnected(1);
    assert(!host.body->GetCollide() && !host.body->GetCollideCharacter());
}

static void CheckDuplicateNamedDrawers()
{
    Fixture host(true, 0), client(false, 1);
    iLuxProp hostCabinet, clientCabinet;
    iPhysicsBody* drawers[2][3] = {};
    iPhysicsBody* frames[2] = {};
    Fixture* peers[] = { &host, &client };
    iLuxProp* cabinets[] = { &hostCabinet, &clientCabinet };
    for (int peer = 0; peer < 2; ++peer)
    {
        auto& physics = peers[peer]->physics;
        frames[peer] = physics.CreateBody("cabinet_Body", physics.CreateBoxShape(cVector3f(0.2f), NULL));
        frames[peer]->SetUniqueID(17); frames[peer]->SetMass(0); frames[peer]->SetUserData(cabinets[peer]);
        cabinets[peer]->bodies.push_back(frames[peer]);
        // Deliberately reverse creation/body-vector order on the client. Only
        // authored IDs, not enumeration order, identify matching drawer bodies.
        for (int entry = 0; entry < 3; ++entry)
        {
            int index = peer == 0 ? entry : 2 - entry;
            auto* drawer = physics.CreateBody("cabinet_Body", physics.CreateBoxShape(cVector3f(0.2f), NULL));
            drawer->SetUniqueID(32 + index); drawer->SetMass(4); drawer->SetGravity(false);
            drawer->SetAutoDisable(false); drawer->SetPosition(cVector3f(0, 0.5f + index * 0.5f, 0));
            drawer->SetUserData(cabinets[peer]); cabinets[peer]->bodies.push_back(drawer);
            auto* joint = physics.CreateJointSlider("drawer_" + std::to_string(index), drawer->GetLocalPosition(),
                cVector3f(0, 0, 1), frames[peer], drawer);
            joint->SetMinDistance(0); joint->SetMaxDistance(0.6f);
            drawers[peer][index] = drawer;
            if (peer) drawer->SetPosition(drawer->GetLocalPosition() + cVector3f(20, 0, 0));
        }
    }
    host.Activate(); assert(host.replication.SendInitialState(1));
    host.replication.Update(1.0f / 60); Deliver(host, client);
    for (int index = 0; index < 3; ++index)
        assert(cMath::Vector3Dist(drawers[0][index]->GetLocalPosition(), drawers[1][index]->GetLocalPosition()) < 0.001f);

    host.Activate(); assert(host.replication.HandleMessage(1, PosePacket(1, 1, cVector3f(0, 1, 0))));
    host.session.sent.clear(); client.Activate();
    assert(!client.replication.RequestInteraction(drawers[1][2], eLuxPlayerState_InteractSlide, drawers[1][2]->GetLocalPosition()));
    assert(client.session.sent.size() == 1 && client.session.sent[0].bytes[0] == LeaseRequest);
    Deliver(client, host); Deliver(host, client);
    assert(client.player.state == eLuxPlayerState_InteractSlide);
    for (int index = 0; index < 3; ++index)
        assert(client.replication.OwnsInteraction(drawers[1][index]));
    host.Activate();
    assert(host.replication.IsInteractionOwnedByOther(frames[0]));
    assert(!host.replication.RequestInteraction(drawers[0][0], eLuxPlayerState_InteractSlide, drawers[0][0]->GetLocalPosition()));
    assert(!host.replication.IsInteractionOwnedByOther(drawers[0][0], 1));

    // Owner motion for one same-named drawer must not affect its siblings.
    const cVector3f closed = drawers[1][2]->GetLocalPosition();
    drawers[1][2]->SetLinearVelocity(cVector3f(0, 0, 1));
    for (int frame = 0; frame < 20; ++frame)
    {
        client.physics.Simulate(1.0f / 60); host.physics.Simulate(1.0f / 60);
        client.Activate(); client.replication.Update(1.0f / 60); Deliver(client, host);
        host.Activate(); host.replication.Update(1.0f / 60); Deliver(host, client);
    }
    client.Activate(); client.replication.ReleaseInteraction();
    Deliver(client, host); Deliver(host, client);
    assert(drawers[1][2]->GetLocalPosition().z > closed.z + 0.05f);
    // Release hands velocity back to the host; it does not teleport both peers
    // to an identical frame. Allow prediction and the periodic truth to settle.
    for (int frame = 0; frame < 180; ++frame)
    {
        client.physics.Simulate(1.0f / 60); host.physics.Simulate(1.0f / 60);
        client.Activate(); client.replication.Update(1.0f / 60); Deliver(client, host);
        host.Activate(); host.replication.Update(1.0f / 60); Deliver(host, client);
    }
    assert(drawers[0][2]->GetLocalPosition().z > closed.z + 0.05f);
    assert(cMath::Vector3Dist(drawers[0][2]->GetLocalPosition(), drawers[1][2]->GetLocalPosition()) < 0.01f);
    for (int index = 0; index < 2; ++index) assert(std::fabs(drawers[0][index]->GetLocalPosition().z) < 0.001f);
    host.Activate(); assert(!host.replication.IsInteractionOwnedByOther(frames[0]));
}

int main()
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    CheckDropCollisionGuard();
    CheckDuplicateNamedDrawers();
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
    cLuxProp_SwingDoor door; door.bodies = { host.body, panel };
    host.body->SetUserData(&door); panel->SetUserData(&door);
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
    assert(!door.closed && !door.disableAutoClose && host.replication.IsEntityLeased(&door));
    assert(host.replication.IsInteractionOwnedByOther(host.body) && host.replication.IsInteractionOwnedByOther(panel));
    assert(!host.replication.IsInteractionOwnedByOther(panel, 1) && host.replication.IsInteractionOwnedByOther(panel, 2));

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
    assert(host.body->GetCollide() && host.body->GetCollideCharacter());
    assert(!host.replication.IsInteractionOwnedByOther(panel) && !host.replication.IsEntityLeased(&door));
    assert(host.replication.HandleMessage(1, finalState.bytes));
    // Native interaction clears the scripted auto-close override even when a
    // door remains locked; the host must mirror that for a client's lease.
    door.closed = true; door.locked = true; door.disableAutoClose = true;
    host.session.sent.clear(); assert(host.replication.HandleMessage(2, request2.bytes));
    assert(host.session.sent.size() == 1 && host.session.sent[0].bytes[0] == LeaseGrant);
    assert(door.closed && !door.disableAutoClose);
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
    assert(!pendingClient.replication.IsInteractionOwnedByOther(pendingClient.body));
    pendingHost.Activate();
    for (int frame = 0; frame < 130; ++frame) pendingHost.replication.Update(1.0f / 60);
    Deliver(pendingHost, pendingClient);
    assert(pendingClient.player.state == eLuxPlayerState_Normal && !pendingClient.replication.OwnsInteraction(pendingClient.body));

    // The local movement controller collides with remote standing/crouching
    // cylinders only when enabled. Remote proxies never enter body snapshots.
    Fixture collision(false, 1);
    collision.body->SetPosition(cVector3f(20, 0, 0));
    collision.player.character = collision.physics.CreateCharacterBody("local_player", cVector3f(0.4f));
    collision.player.character->SetPosition(cVector3f(-2, 0, 0));
    assert(collision.replication.HandleMessage(0, PosePacket(0, 1, 0)));
    collision.replication.Update(1.0f / 60);
    assert(collision.player.character->CheckCharacterFits(cVector3f(0, 0, 0)));
    collision.session.settings.playerCollision = true;
    collision.replication.Update(1.0f / 60);
    assert(!collision.player.character->CheckCharacterFits(cVector3f(0, 0, 0)));
    assert(!collision.player.character->CheckCharacterFits(cVector3f(0, 1.0f, 0)));
    assert(collision.replication.HandleMessage(0, PosePacket(0, 2, cVector3f(0, -0.45f, 0), 0.9f)));
    collision.replication.Update(1.0f / 60);
    assert(collision.player.character->CheckCharacterFits(cVector3f(0, 1.0f, 0)));
    assert(!collision.player.character->CheckCharacterFits(cVector3f(0, -0.45f, 0)));
    for (int frame = 0; frame < 130; ++frame) collision.replication.Update(1.0f / 60);
    assert(collision.player.character->CheckCharacterFits(cVector3f(0, 0, 0)));
    assert(collision.replication.HandleMessage(0, PosePacket(0, 3, 0)));
    collision.replication.Update(1.0f / 60);
    assert(!collision.player.character->CheckCharacterFits(cVector3f(0, 0, 0)));
    collision.replication.OnPeerDisconnected(0);
    assert(collision.player.character->CheckCharacterFits(cVector3f(0, 0, 0)));
    assert(collision.replication.HandleMessage(0, PosePacket(0, 4, 0)));
    collision.replication.Update(1.0f / 60);
    collision.replication.Shutdown();
    assert(collision.player.character->CheckCharacterFits(cVector3f(0, 0, 0)));

    // A mass-zero MoveObject must be in the initial burst and follow host
    // transforms, including a mid-motion stop that never reaches its old goal.
    Fixture moverHost(true, 0), moverClient(false, 1);
    iLuxProp hostMover, clientMover;
    hostMover.type = clientMover.type = eLuxPropType_MoveObject;
    moverHost.body->SetUserData(&hostMover); moverClient.body->SetUserData(&clientMover);
    moverHost.body->SetMass(0); moverClient.body->SetMass(0);
    moverHost.body->SetPosition(cVector3f(5, 0, 0));
    moverHost.Activate(); assert(moverHost.replication.SendInitialState(1));
    moverHost.replication.Update(1.0f / 60); Deliver(moverHost, moverClient);
    assert(cMath::Vector3Dist(moverHost.body->GetLocalPosition(), moverClient.body->GetLocalPosition()) < 0.001f);
    for (int frame = 0; frame < 90; ++frame)
    {
        cMatrixf transform = cMath::MatrixRotateY(std::min(frame, 40) * 0.005f);
        transform.SetTranslation(cVector3f(5 + std::min(frame, 40) * 0.01f, 0, 0));
        moverHost.body->SetMatrix(transform);
        moverHost.Activate(); moverHost.replication.Update(1.0f / 60); Deliver(moverHost, moverClient, true);
        moverClient.Activate(); moverClient.replication.Update(1.0f / 60);
    }
    assert(cMath::Vector3Dist(moverHost.body->GetLocalPosition(), moverClient.body->GetLocalPosition()) < 0.025f);
    assert(cMath::MatrixEulerAngleDistance(moverHost.body->GetLocalMatrix(), moverClient.body->GetLocalMatrix()).Length() < 0.01f);

    // Sub-threshold movement must accumulate against the last transmitted
    // state, rather than the last sampled frame. Then deliberately lose the
    // final update: reliable periodic ground truth must repair the stopped body.
    Fixture slowHost(true, 0), slowClient(false, 1);
    iLuxProp slowHostProp, slowClientProp;
    slowHostProp.type = slowClientProp.type = eLuxPropType_MoveObject;
    slowHost.body->SetUserData(&slowHostProp); slowClient.body->SetUserData(&slowClientProp);
    slowHost.body->SetMass(0); slowClient.body->SetMass(0);
    for (int frame = 0; frame < 45; ++frame)
    {
        slowHost.body->SetPosition(cVector3f(frame * 0.0003f, 0, 0));
        slowHost.Activate(); slowHost.replication.Update(1.0f / 60); Deliver(slowHost, slowClient);
        slowClient.Activate(); slowClient.replication.Update(1.0f / 60);
    }
    assert(cMath::Vector3Dist(slowHost.body->GetLocalPosition(), slowClient.body->GetLocalPosition()) < 0.006f);
    slowHost.Activate(); slowHost.body->SetPosition(cVector3f(0.5f, 0, 0));
    slowHost.replication.Update(0.05f);
    unsigned lost = 0;
    for (const auto& packet : slowHost.session.sent)
        if (packet.bytes[0] == Bodies && !packet.reliable) ++lost;
    assert(lost > 0);
    slowHost.session.sent.clear();
    assert(cMath::Vector3Dist(slowHost.body->GetLocalPosition(), slowClient.body->GetLocalPosition()) > 0.4f);
    for (int frame = 0; frame < 100; ++frame)
    {
        slowHost.Activate(); slowHost.replication.Update(1.0f / 60); Deliver(slowHost, slowClient);
        slowClient.Activate(); slowClient.replication.Update(1.0f / 60);
    }
    assert(cMath::Vector3Dist(slowHost.body->GetLocalPosition(), slowClient.body->GetLocalPosition()) < 0.003f);
    std::cout << "Real Newton replication smoke passed: position error=" << positionError
              << ", velocity error=" << velocityError << "; assembly leases/door opening/crosshair availability, player collision/crouch/cleanup and static movers verified.\n";
}
