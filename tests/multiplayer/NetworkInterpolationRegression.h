#ifndef MULTIPLAYER_NETWORK_INTERPOLATION_REGRESSION_H
#define MULTIPLAYER_NETWORK_INTERPOLATION_REGRESSION_H
#include "SkeletonInterpolationRegression.h"

// Included by the real two-process game harness after its private test access.
// Fixtures are removed before another fixed tick or network send can see them.
inline bool RunNetworkInterpolationRegression(tString& error)
{
    auto* replication = gpBase->mpMultiplayer->GetWorld();
    auto* map = gpBase->mpMapHandler->GetCurrentMap();
    if (!map || !gpBase->mpMultiplayer->IsReady()) { error = "interpolation fixture needs a ready map"; return false; }
    auto* physics = map->GetPhysicsWorld();
    auto* world = map->GetWorld();
    bool passed = true;
    const auto require = [&](bool condition, const char* message) {
        if (!condition && passed) { error = message; passed = false; }
    };
    const auto nearVector = [](const cVector3f& a, const cVector3f& b) { return (a - b).Length() < 0.0001f; };
    class cFixtureEntity : public iEntity3D {
    public:
        cFixtureEntity() : iEntity3D("network interpolation descendant") {}
        tString GetEntityType() { return "Test"; }
    } descendant;

    iPhysicsBody* body = physics->CreateBody("CodexNetworkInterpolationBody", physics->CreateBoxShape(cVector3f(0.1f), NULL));
    body->SetMass(1); body->SetGravity(false); body->SetCollide(false); body->SetActive(false);
    body->SetPosition(cVector3f(0, -1000, 0));
    body->AddChild(&descendant);
    descendant.SetPosition(cVector3f(0, 1, 0));
    replication->RefreshBodies();
    const uint64_t id = LuxWorldWire::BodyId(body->GetName(), body->GetUniqueID());
    iEntity3D::BeginRenderInterpolation(1);
    descendant.GetRenderWorldMatrix();
    iEntity3D::EndRenderInterpolation();
    iEntity3D::CaptureInterpolationState();

    LuxWorldWire::Body state = replication->CaptureBody(id, body);
    state.matrix[3] = 10;
    replication->ApplyBody(state, 1, true, false);
    for (float alpha : {0.0f, 0.25f, 0.75f, 1.0f})
    {
        iEntity3D::BeginRenderInterpolation(alpha);
        require(nearVector(descendant.GetRenderWorldPosition(), cVector3f(10, -999, 0)),
            "initial network placement interpolated through an obsolete descendant pose");
        iEntity3D::EndRenderInterpolation();
    }

    // A routine, small correction must remain continuous, while render samples
    // cannot change a captured network matrix, velocity, or authority lease.
    iEntity3D::CaptureInterpolationState();
    body->SetPosition(cVector3f(10.25f, -1000, 0));
    body->SetLinearVelocity(cVector3f(2, 0, 0));
    const auto before = replication->CaptureBody(id, body);
    for (float alpha : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        iEntity3D::BeginRenderInterpolation(alpha);
        require(nearVector(descendant.GetRenderWorldPosition(), cVector3f(10 + 0.25f * alpha, -999, 0)),
            "network body did not supply distinct presentation poses between fixed ticks");
        const auto during = replication->CaptureBody(id, body);
        for (unsigned i = 0; i < 12; ++i) require(during.matrix[i] == before.matrix[i], "render pose leaked into a network body snapshot");
        for (unsigned i = 0; i < 3; ++i) require(during.linear[i] == before.linear[i] && during.angular[i] == before.angular[i],
            "render sampling changed network body velocity");
        iEntity3D::EndRenderInterpolation();
    }
    uint32_t token = UINT32_MAX;
    while (replication->mLeases.count(token)) --token;
    cLuxMultiplayerWorld::Lease lease;
    lease.token = token; lease.owner = gpBase->mpMultiplayer->GetLocalPeerId(); lease.contact = true;
    lease.bodies.push_back(id); lease.identities[id] = std::make_pair(body, uint64_t(0));
    replication->mLeases[token] = lease; replication->mBodyLeases[id] = token;
    state.matrix[3] = 40;
    replication->ApplyBody(state, 2, true, false);
    require(nearVector(body->GetLocalPosition(), cVector3f(10.25f, -1000, 0)) && replication->OwnsSimulation(body),
        "a network correction overrode locally owned physics");
    replication->mBodyLeases.erase(id); replication->mLeases.erase(token);
    replication->ApplyBody(state, 3, false, false);
    iEntity3D::BeginRenderInterpolation(0.25f);
    require(nearVector(descendant.GetRenderWorldPosition(), cVector3f(40, -999, 0)),
        "large post-ownership correction retained pre-handoff presentation history");
    iEntity3D::EndRenderInterpolation();
    if (gpBase->mpMultiplayer->IsHost())
    {
        iEntity3D::CaptureInterpolationState();
        lease.owner = gpBase->mpMultiplayer->GetLocalPeerId() + 1;
        replication->mLeases[token] = lease; replication->mBodyLeases[id] = token;
        auto& track = replication->mBodies[id];
        track.target = state; track.target.matrix[3] = 60; track.targetAge = 0; track.hasTarget = true;
        replication->EndLease(token, false);
        iEntity3D::BeginRenderInterpolation(0.25f);
        require(nearVector(descendant.GetRenderWorldPosition(), cVector3f(60, -999, 0)),
            "consuming a distant owner's final handoff pose retained old presentation history");
        iEntity3D::EndRenderInterpolation();
    }
    body->RemoveChild(&descendant);
    replication->mBodies.erase(id);
    physics->DestroyBody(body);

    // Exercise the same scene node used by direct cylinder rendering and the
    // actual point light created by production multiplayer code.
    uint32_t peer = UINT32_MAX;
    while (replication->mPlayers.count(peer)) --peer;
    cLuxMultiplayerRemotePlayer remote;
    remote.position = remote.renderPosition = cVector3f(0, -1000, 0);
    remote.size = cVector3f(0.6f, 1.8f, 0.6f);
    remote.renderLanternOffset = cVector3f(0.2f, 0.7f, -0.2f);
    remote.lantern.active = true; remote.lantern.radius = 1;
    replication->mPlayers[peer] = remote;
    auto& node = replication->mPlayerRenderNodes[peer];
    node.reset(new cNode3D("network interpolation peer", false));
    node->SetPosition(remote.renderPosition);
    replication->UpdatePlayerLights();
    auto* light = world->GetLight("MultiplayerLantern_" + cString::ToString((int)peer));
    require(light != NULL, "remote interpolation fixture did not create its native point light");
    if (light)
    {
        iEntity3D::BeginRenderInterpolation(1);
        node->GetRenderWorldMatrix(); light->GetRenderWorldMatrix();
        iEntity3D::EndRenderInterpolation();
        iEntity3D::CaptureInterpolationState();
        node->SetPosition(remote.renderPosition + cVector3f(1, 0, 0));
        for (float alpha : {0.0f, 0.25f, 0.75f, 1.0f})
        {
            iEntity3D::BeginRenderInterpolation(alpha);
            const cVector3f position = node->GetRenderWorldPosition();
            require(nearVector(position, remote.renderPosition + cVector3f(alpha, 0, 0)),
                "remote cylinder still advances only at fixed simulation steps");
            require(nearVector(light->GetRenderWorldPosition(), position + remote.renderLanternOffset),
                "remote lantern and player cylinder use different presentation timelines");
            require(nearVector(replication->mPlayers[peer].position, remote.position),
                "remote render samples changed the received network pose");
            iEntity3D::EndRenderInterpolation();
        }
        node->SetPosition(cVector3f(100, -1000, 0));
        node->ResetRenderInterpolation();
        iEntity3D::BeginRenderInterpolation(0);
        require(nearVector(light->GetRenderWorldPosition(), cVector3f(100, -1000, 0) + remote.renderLanternOffset),
            "remote teleport failed to reset attached lantern history");
        iEntity3D::EndRenderInterpolation();
    }
    replication->RemovePlayerLight(peer);
    replication->mPlayerRenderNodes.erase(peer);
    replication->mPlayers.erase(peer);
    // The test samples global render history, so settle it before the next real
    // frame rather than leaving a test interval on unrelated world objects.
    gpBase->mpEngine->GetScene()->ResetInterpolationState();
    return passed && RunSkeletonInterpolationRegression(error);
}

#endif
