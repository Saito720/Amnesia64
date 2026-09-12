#ifndef MULTIPLAYER_CONTACT_NEWTON_TESTS_H
#define MULTIPLAYER_CONTACT_NEWTON_TESTS_H
#include "impl/PhysicsBodyNewton.h"

class ContactGate : public iCharacterBodyCallback
{
    cLuxMultiplayerWorld& world;
public:
    explicit ContactGate(cLuxMultiplayerWorld& value) : world(value) {}
    void OnGravityCollide(iCharacterBody*, iPhysicsBody*, cCollideData*) {}
    void OnHitGround(iCharacterBody*, const cVector3f&) {}
    bool AllowBodyPush(iCharacterBody*, iPhysicsBody* body) { return world.AllowPlayerContact(body); }
};

static unsigned CountPackets(const Fixture& fixture, uint8_t type)
{
    unsigned count = 0;
    for (const auto& packet : fixture.session.sent) if (packet.bytes[0] == type) ++count;
    return count;
}

static void CheckContactOwnership()
{
    Fixture host(true, 0), client(false, 1);
    client.player.character = client.physics.CreateCharacterBody("contact_player", cVector3f(0.6f, 1.8f, 0.6f));
    client.player.character->SetPosition(cVector3f(0, 0, 0.55f));
    client.player.character->SetMaxPushMass(100); client.player.character->SetPushForce(100);
    ContactGate gate(client.replication); client.player.character->SetCallback(&gate);
    cCharacterBodyCollidePush push(client.player.character);
    cCollideData contact; contact.SetMaxSize(1); contact.mlNumOfPoints = 1;
    contact.mvContactPoints[0].mvPoint = cVector3f(0, 0, 0.25f);
    contact.mvContactPoints[0].mvNormal = cVector3f(0, 0, 1);
    client.Activate();
    push.OnCollision(client.body, &contact);
    client.physics.Simulate(1.0f / 60);
    assert(client.body->GetLinearVelocity().Length() < 0.0001f);
    assert(!client.replication.OwnsSimulation(client.body));
    assert(CountPackets(client, ContactRequest) == 1);
    // Repeated Newton contacts in one frame do not flood reliable requests.
    for (int i = 0; i < 20; ++i) push.OnCollision(client.body, &contact);
    assert(CountPackets(client, ContactRequest) == 1);
    Deliver(client, host); Deliver(host, client);
    assert(client.replication.OwnsSimulation(client.body));
    assert(!client.replication.OwnsInteraction(client.body) && client.player.state == eLuxPlayerState_Normal);
    push.OnCollision(client.body, &contact);
    client.physics.Simulate(1.0f / 60);
    assert(client.body->GetLinearVelocity().Length() > 0.01f);

    // Contact ownership remains exclusive even when another nearby player asks.
    host.Activate(); assert(host.replication.HandleMessage(2, PosePacket(2, 1, 0)));
    host.session.sent.clear();
    Writer competing(ContactRequest, 7); competing.U64(BodyId("test_crate"));
    assert(host.replication.HandleMessage(2, competing.bytes));
    assert(CountPackets(host, LeaseGrant) == 0);
    assert(host.replication.HandleMessage(1, std::vector<uint8_t>(competing.bytes.begin(), competing.bytes.end()-1)) == false);
    host.session.sent.clear();

    // A validated owner state is relayed as received, rather than sampling the
    // host's following body and adding another smoothing delay.
    client.Activate(); client.body->SetPosition(cVector3f(0.4f, 0, 0));
    client.body->SetLinearVelocity(cVector3f(1, 0, 0)); client.replication.Update(0.05f);
    std::vector<uint8_t> ownerState;
    for (const auto& packet : client.session.sent) if (packet.bytes[0] == Bodies) ownerState = packet.bytes;
    assert(!ownerState.empty());
    Deliver(client, host);
    bool relayed = false;
    for (const auto& packet : host.session.sent) if (packet.bytes[0] == Bodies)
    {
        Reader r(packet.bytes); r.U8(); r.U32(); r.U32(); assert(r.U32() == 0); r.U8();
        std::vector<Body> states; assert(ReadBodies(r, states));
        for (const Body& body : states) if (body.id == BodyId("test_crate"))
        { assert(std::fabs(body.matrix[3] - 0.4f) < 0.001f); relayed = true; }
    }
    assert(relayed);
    host.session.sent.clear(); assert(host.replication.HandleMessage(1, ownerState));
    assert(CountPackets(host, Bodies) == 0); // Duplicate owner sequence cannot become fresh relay state.

    // Brief loss of contact keeps the same owner. Owner snapshots alone must
    // not renew the lease forever once the player has stopped touching it.
    for (int i = 0; i < 2; ++i)
    {
        host.Activate(); host.replication.Update(0.2f);
        client.Activate(); client.replication.Update(0.2f); Deliver(client, host); Deliver(host, client);
    }
    client.Activate(); assert(client.replication.OwnsSimulation(client.body));
    for (int i = 0; i < 6; ++i)
    {
        host.Activate(); host.replication.Update(0.2f);
        client.Activate(); client.replication.Update(0.2f); Deliver(client, host); Deliver(host, client);
    }
    assert(!client.replication.OwnsSimulation(client.body));

    // A stale/far contact request cannot acquire the assembly.
    host.Activate(); assert(host.replication.HandleMessage(2, PosePacket(2, 10, cVector3f(10, 0, 0))));
    host.session.sent.clear(); assert(host.replication.HandleMessage(2, competing.bytes));
    assert(CountPackets(host, LeaseGrant) == 0);

    // Deliberate grab preempts passive ownership; it never starts a second PID.
    client.Activate(); client.body->SetPosition(host.body->GetLocalPosition());
    client.player.character->SetPosition(client.body->GetLocalPosition()+cVector3f(0,0,0.5f));
    assert(!client.replication.AllowPlayerContact(client.body));
    Deliver(client, host); Deliver(host, client);
    assert(client.replication.OwnsSimulation(client.body));
    host.Activate(); assert(host.replication.HandleMessage(2, PosePacket(2, 11, host.body->GetLocalPosition())));
    Writer grab(LeaseRequest, 7); grab.U32(1); grab.U64(BodyId("test_crate"));
    assert(host.replication.HandleMessage(2, grab.bytes)); Deliver(host, client);
    assert(!client.replication.OwnsSimulation(client.body));
    assert(client.replication.IsInteractionOwnedByOther(client.body));
    assert(!client.replication.AllowPlayerContact(client.body));
    host.Activate(); host.replication.OnPeerDisconnected(2); Deliver(host, client);
    assert(!client.replication.IsInteractionOwnedByOther(client.body));
    client.player.character->SetCallback(NULL);

    // Contact does not temporarily override native body settings. Expiration
    // must retain changes made while it was held (for example sticky areas).
    Fixture attached(true, 0);
    attached.body->SetGravity(true); attached.body->SetCollide(true); attached.body->SetCollideCharacter(true);
    assert(attached.replication.HandleMessage(1, PosePacket(1, 1, 0)));
    attached.session.sent.clear();
    assert(attached.replication.HandleMessage(1, competing.bytes));
    assert(CountPackets(attached, LeaseGrant) == 1);
    attached.body->SetGravity(false); attached.body->SetCollide(false); attached.body->SetCollideCharacter(false);
    attached.session.sent.clear();
    for (int i = 0; i < 5; ++i) attached.replication.Update(0.25f);
    assert(CountPackets(attached, LeaseRelease) == 1);
    assert(!attached.body->GetGravity() && !attached.body->GetCollide() && !attached.body->GetCollideCharacter());

    // An authored identity can be reused by a scripted replacement. Neither a
    // stale owner packet nor lease teardown may mutate the replacement body.
    Fixture replaced(true,0);
    assert(replaced.replication.HandleMessage(1,PosePacket(1,1,0)));
    Writer request(LeaseRequest,7);request.U32(1);request.U64(BodyId("test_crate"));
    assert(replaced.replication.HandleMessage(1,request.bytes));
    uint32_t owner=0,token=0;
    assert(replaced.replication.GetSimulationLease(replaced.body,owner,token) && owner==1);
    replaced.bodyProp.bodies.clear();replaced.physics.DestroyBody(replaced.body);
    iLuxProp replacementProp;
    replaced.body=replaced.physics.CreateBody("test_crate",replaced.physics.CreateBoxShape(cVector3f(0.5f),NULL));
    replaced.body->SetMass(2);replaced.body->SetGravity(true);replaced.body->SetPosition(cVector3f(3,0,0));
    replaced.body->SetUserData(&replacementProp);replacementProp.bodies.push_back(replaced.body);
    uint32_t ignoredOwner=0,ignoredToken=0;
    assert(!replaced.replication.GetSimulationLease(replaced.body,ignoredOwner,ignoredToken));
    Body stale={};stale.id=BodyId("test_crate");stale.matrix[0]=stale.matrix[5]=stale.matrix[10]=1;
    stale.matrix[3]=5;stale.linear[0]=8;stale.flags=Awake|Active|Collide|CollideCharacter;
    Writer stalePacket(Bodies,7);stalePacket.U32(5);stalePacket.U32(token);stalePacket.U8(0);stalePacket.U8(1);WriteBody(stalePacket,stale);
    replaced.session.sent.clear();assert(replaced.replication.HandleMessage(1,stalePacket.bytes));
    assert(CountPackets(replaced,LeaseRelease)==1 && !replaced.replication.GetSimulationLease(replaced.body,ignoredOwner,ignoredToken));
    assert(replaced.body->GetGravity() && replaced.body->GetLinearVelocity().Length()<0.001f &&
           std::fabs(replaced.body->GetLocalPosition().x-3)<0.001f);
    // Also cover an unchanged body address with a different runtime owner,
    // which is the identity failure produced by allocator address reuse.
    assert(replaced.replication.HandleMessage(1,request.bytes));
    assert(replaced.replication.GetSimulationLease(replaced.body,owner,token));
    iLuxProp reusedAddressProp;reusedAddressProp.bodies.push_back(replaced.body);replaced.body->SetUserData(&reusedAddressProp);
    assert(!replaced.replication.GetSimulationLease(replaced.body,ignoredOwner,ignoredToken));
    replaced.replication.Update(0.01f);
    assert(!replaced.replication.GetSimulationLease(replaced.body,ignoredOwner,ignoredToken));
    std::cout << "Lease generation rejects same-ID replacements, stale owner updates and reused body addresses.\n";
    std::cout << "Contact ownership: native force gate, exclusive grant, grace/expiry, direct relay, stale packets and interaction priority passed.\n";
}

static void CheckSmoothCorrections()
{
    Fixture host(true, 0), client(false, 1);
    host.Activate(); assert(host.replication.SendInitialState(1));
    host.replication.Update(0.05f); Deliver(host, client);
    client.body->SetPosition(cVector3f(0.2f, 0, 0));
    host.Activate(); host.replication.Update(0.25f); host.replication.Update(0.25f);
    host.replication.Update(0.25f); host.replication.Update(0.25f);
    host.replication.Update(0.25f); host.replication.Update(0.25f);
    host.replication.Update(0.25f); host.replication.Update(0.25f);
    Deliver(host, client);
    assert(std::fabs(client.body->GetLocalPosition().x - 0.2f) < 0.001f);
    for (int i = 0; i < 120; ++i)
    {
        host.Activate(); host.replication.Update(1.0f/60); Deliver(host, client);
        client.Activate(); client.replication.Update(1.0f/60); client.physics.Simulate(1.0f/60);
    }
    assert(client.body->GetLocalPosition().Length() < 0.025f);
    std::cout << "Routine reliable corrections converge without an immediate pose jump.\n";

    // A sleeping authority is normally sent only when its sleep flag changes,
    // then in occasional reliable truth. One stationary target must thaw a
    // frozen follower and finish correcting position AND orientation without
    // relying on another stream of awake snapshots to keep correction alive.
    Fixture sleepingHost(true, 0), sleepingClient(false, 1);
    sleepingHost.Activate(); assert(sleepingHost.replication.SendInitialState(1));
    sleepingHost.replication.Update(0.05f); Deliver(sleepingHost, sleepingClient);
    sleepingHost.body->SetAutoDisable(true);
    for (int i = 0; i < 600 && sleepingHost.body->GetEnabled(); ++i)
        sleepingHost.physics.Simulate(1.0f/60);
    assert(!sleepingHost.body->GetEnabled());

    cMatrixf displaced = cMath::MatrixRotateY(0.3f);
    displaced.SetTranslation(cVector3f(0.3f, 0, 0));
    sleepingClient.body->SetMatrix(displaced);
    sleepingClient.body->SetLinearVelocity(0); sleepingClient.body->SetAngularVelocity(0);
    sleepingClient.body->SetAutoDisable(true);
    NewtonBody* frozen = static_cast<cPhysicsBodyNewton*>(sleepingClient.body)->GetNewtonBody();
    NewtonBodySetFreezeState(frozen, 1);
    assert(NewtonBodyGetFreezeState(frozen) == 1);

    sleepingHost.Activate(); sleepingHost.replication.Update(0.05f);
    Deliver(sleepingHost, sleepingClient);
    assert(std::fabs(sleepingClient.body->GetLocalPosition().x - 0.3f) < 0.001f);
    assert(std::fabs(sleepingClient.body->GetLocalMatrix().v[2] - displaced.v[2]) < 0.001f);
    sleepingClient.Activate();
    for (int i = 0; i < 300; ++i)
    {
        const cVector3f before = sleepingClient.body->GetLocalPosition();
        sleepingClient.replication.Update(1.0f/60);
        sleepingClient.physics.Simulate(1.0f/60);
        assert((sleepingClient.body->GetLocalPosition() - before).Length() < 0.05f);
    }
    assert(sleepingClient.body->GetLocalPosition().Length() < 0.02f);
    float rotationError = 0;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            rotationError = std::max(rotationError, std::fabs(
                sleepingClient.body->GetLocalMatrix().v[row*4+column] -
                sleepingHost.body->GetLocalMatrix().v[row*4+column]));
    assert(rotationError < 0.02f);
    std::cout << "A frozen follower converges smoothly to a single sleeping position/rotation snapshot.\n";
}
#endif
