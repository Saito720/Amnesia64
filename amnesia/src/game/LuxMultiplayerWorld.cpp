#include "LuxMultiplayerWorld.h"
#include "LuxMultiplayer.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxPlayerState.h"
#include "LuxInputHandler.h"
#include "LuxProp.h"
#include "LuxProp_SwingDoor.h"

#include <algorithm>

using namespace LuxWorldWire;

namespace
{
    const float SnapshotStep = 1.0f / 20.0f;
    const float LeaseDuration = 2.0f;
    const float ContactGrace = 1.0f;
    const float ContactRetry = 0.25f;
    const size_t MaxContactLeases = 4;

    uint64_t NetworkBodyId(iPhysicsBody* body) { return BodyId(body->GetName(), body->GetUniqueID()); }
    uint64_t BodyEntityRuntimeId(iPhysicsBody* body)
    {
        iLuxEntity* entity=static_cast<iLuxEntity*>(body->GetUserData());
        return entity?entity->GetRuntimeID():0;
    }

    bool PreserveLocalDropCollision(iPhysicsBody* body)
    {
        iLuxEntity* entity = static_cast<iLuxEntity*>(body->GetUserData());
        return entity && entity->GetEntityType() == eLuxEntityType_Prop &&
            static_cast<iLuxProp*>(entity)->IsPlayerCollisionTemporarilyDisabled(body);
    }

    cVector3f Vector(const float* p) { return cVector3f(p[0], p[1], p[2]); }
    void Store(float* p, const cVector3f& v) { p[0] = v.x; p[1] = v.y; p[2] = v.z; }
    cVector3f Position(const Body& b) { return cVector3f(b.matrix[3], b.matrix[7], b.matrix[11]); }
    cVector3f Limit(const cVector3f& value, float maximum)
    {
        float length = value.Length();
        return length > maximum ? value * (maximum / length) : value;
    }
    cMatrixf Matrix(const Body& b)
    {
        cMatrixf matrix = cMatrixf::Identity;
        for (int i = 0; i < 12; ++i) matrix.v[i] = b.matrix[i];
        return matrix;
    }
    cMatrixf PredictedMatrix(const Body& state, float age)
    {
        const float lead = state.flags & Awake ? std::min(age, 0.1f) : 0;
        cMatrixf matrix = Matrix(state);
        const cVector3f spin = Vector(state.angular);
        const float speed = spin.Length();
        if (speed > 0.001f && lead > 0)
        {
            const float halfAngle = speed * lead * 0.5f;
            const cVector3f axis = spin * (std::sin(halfAngle) / speed);
            cQuaternion rotation = cQuaternion(std::cos(halfAngle), axis.x, axis.y, axis.z) *
                cQuaternion(matrix.GetRotation());
            rotation.Normalize(); rotation.ToRotationMatrix(matrix);
        }
        matrix.SetTranslation(Position(state) + Vector(state.linear) * lead);
        return matrix;
    }
    bool Changed(const Body& a, const Body& b)
    {
        if (a.flags != b.flags) return true;
        for (int i = 0; i < 12; ++i) if (std::fabs(a.matrix[i] - b.matrix[i]) > 0.002f) return true;
        for (int i = 0; i < 3; ++i)
            if (std::fabs(a.linear[i] - b.linear[i]) > 0.02f || std::fabs(a.angular[i] - b.angular[i]) > 0.02f) return true;
        return false;
    }
}

cLuxMultiplayerWorld::cLuxMultiplayerWorld(cLuxMultiplayer* apSession) : mpSession(apSession), mpMap(NULL)
{
    Reset();
}

void cLuxMultiplayerWorld::Reset()
{
    // The current map is still alive during OnMapLeave and a map download. On
    // other reset paths its world may already be gone and owns the colliders.
    if (mpMap && gpBase->mpMapHandler->GetCurrentMap() == mpMap)
    {
        while (!mPlayerColliders.empty()) RemovePlayerCollider(mPlayerColliders.begin()->first);
        while (!mPlayerLights.empty()) RemovePlayerLight(*mPlayerLights.begin());
    }
    mPlayerColliders.clear();
    mPlayerLights.clear();
    // The map may already have been destroyed; never dereference cached bodies
    // here. Normal disconnect releases the interaction before resetting.
    mpMap = NULL;
    mBodies.clear(); mPlayers.clear(); mLeases.clear(); mBodyLeases.clear(); mAmbiguousBodies.clear();
    mContactAges.clear(); mContactRequests.clear();
    mInitialPackets.clear(); mlInitialBytes = 0; mbLocalInteractionStarted = false;
    mlSequence = mlLeaseCounter = mlLocalLease = mlRequestCounter = mlPendingRequest = 0;
    mlPendingBody = 0;
    mPendingState = mPendingPreviousState = eLuxPlayerState_Normal;
    mvPendingFocus = 0;
    mfSendTime = mfGroundTruthTime = mfRenewTime = mfPendingTime = 0;
}

void cLuxMultiplayerWorld::OnMapLoaded(cLuxMap* apMap)
{
    Reset();
    mpMap = apMap;
    RefreshBodies();
}

void cLuxMultiplayerWorld::Shutdown()
{
    if (mpMap && gpBase->mpMapHandler->GetCurrentMap() == mpMap)
    {
        RefreshBodies();
        if (gpBase->mpPlayer && IsInteractionState(gpBase->mpPlayer->GetCurrentState()))
            gpBase->mpPlayer->ChangeState(eLuxPlayerState_Normal);
        ReleaseInteraction();
        while (!mLeases.empty()) EndLease(mLeases.begin()->first, false);
    }
    Reset();
}

void cLuxMultiplayerWorld::RefreshBodies()
{
    if (!mpMap || !mpMap->GetPhysicsWorld()) return;
    std::set<uint64_t> present;
    std::set<uint64_t> collisions;
    cPhysicsBodyIterator iterator = mpMap->GetPhysicsWorld()->GetBodyIterator();
    while (iterator.HasNext())
    {
        iPhysicsBody* body = iterator.Next();
        if (body->IsCharacter()) continue;
        uint64_t id = NetworkBodyId(body);
        iLuxEntity* entity = static_cast<iLuxEntity*>(body->GetUserData());
        bool mover = entity && entity->GetEntityType() == eLuxEntityType_Prop &&
            static_cast<iLuxProp*>(entity)->GetPropType() == eLuxPropType_MoveObject;
        // Static MoveObjects (bookshelves, bridges and ladders) move through an
        // HPL motor, not Newton integration. Keep formerly dynamic bodies indexed
        // too, so scripted changes to StaticPhysics do not lose replication.
        if (body->GetMass() <= 0 && !mover && !mBodies.count(id)) continue;
        if (!present.insert(id).second) { collisions.insert(id); continue; }
        BodyTrack& track = mBodies[id];
        const uint64_t runtimeId=BodyEntityRuntimeId(body);
        if (track.body != body || track.entityRuntimeId != runtimeId)
        {
            track.sent = false; track.received = false; track.hasTarget = false;
            mContactAges.erase(id);mContactRequests.erase(id);
            if(mlPendingRequest && mlPendingBody==id) CancelPendingInteraction();
        }
        track.body = body;
        track.entityRuntimeId = runtimeId;
        track.name = body->GetName();
    }
    // Colliding identities (including duplicate authored IDs) are excluded rather than ever
    // binding a network packet to an arbitrary body.
    for (std::set<uint64_t>::iterator it = collisions.begin(); it != collisions.end(); ++it)
    {
        if (mAmbiguousBodies.insert(*it).second)
            Warning("Multiplayer excluded physics bodies with ambiguous network identities.\n");
        present.erase(*it);
    }
    for (std::map<uint64_t, BodyTrack>::iterator it = mBodies.begin(); it != mBodies.end(); )
    {
        if (!present.count(it->first)) it = mBodies.erase(it); else ++it;
    }
    // Stable authored IDs intentionally survive replacement. Authority must not:
    // bind each lease to the actual granted body/entity instance, including when
    // the allocator reuses the old body's address for its replacement.
    std::vector<uint32_t> invalid;
    for(const auto& entry:mLeases)
        for(uint64_t id:entry.second.bodies)
        {
            auto track=mBodies.find(id);
            if(track==mBodies.end() || !LeaseMatchesBody(entry.second,id,track->second.body))
            {invalid.push_back(entry.first);break;}
        }
    for(uint32_t token:invalid) EndLease(token,mpSession->IsHost());
    if(mlPendingRequest && !mBodies.count(mlPendingBody)) CancelPendingInteraction();
}

iPhysicsBody* cLuxMultiplayerWorld::FindBody(uint64_t alId) const
{
    std::map<uint64_t, BodyTrack>::const_iterator it = mBodies.find(alId);
    if (!mpMap || it == mBodies.end()) return NULL;
    // Every public entry point refreshes this transient index before using it.
    // Avoid PhysicsWorld::GetBody's linear name search for every snapshot body.
    iPhysicsBody* body = it->second.body;
    return body && !body->IsCharacter() ? body : NULL;
}

Body cLuxMultiplayerWorld::CaptureBody(uint64_t alId, iPhysicsBody* apBody) const
{
    Body state = {};
    state.id = alId;
    const cMatrixf& matrix = apBody->GetLocalMatrix();
    for (int i = 0; i < 12; ++i) state.matrix[i] = matrix.v[i];
    Store(state.linear, Limit(apBody->GetLinearVelocity(), 150));
    Store(state.angular, Limit(apBody->GetAngularVelocity(), 150));
    state.flags = (apBody->GetEnabled() ? Awake : 0) | (apBody->IsActive() ? Active : 0) | (apBody->GetGravity() ? Gravity : 0) |
        (apBody->GetCollide() ? Collide : 0) | (apBody->GetCollideCharacter() ? CollideCharacter : 0);
    return state;
}

void cLuxMultiplayerWorld::SendBodyBatch(uint32_t alPeer, bool abBroadcast, const std::vector<Body>& avBodies,
                                        bool abReliable, bool abGroundTruth, uint32_t alToken)
{
    for (size_t first = 0; first < avBodies.size(); first += MaxBodiesPerPacket)
    {
        Writer writer(Bodies, mpSession->GetMapEpoch());
        writer.U32(++mlSequence);
        writer.U32(alToken);
        writer.U8(abGroundTruth ? 1 : 0);
        size_t count = std::min(MaxBodiesPerPacket, avBodies.size() - first);
        writer.U8(uint8_t(count));
        for (size_t i = 0; i < count; ++i) WriteBody(writer, avBodies[first + i]);
        if (abBroadcast) mpSession->Broadcast(writer.bytes, abReliable);
        else mpSession->Send(alPeer, writer.bytes, abReliable);
    }
}

bool cLuxMultiplayerWorld::SendInitialState(uint32_t alPeer)
{
    if (!mpSession->IsHost() || !mpMap || mInitialPackets.count(alPeer)) return false;
    RefreshBodies();
    // Bound retained snapshots, and trickle the initial burst into GNS with
    // retries. A large map must not silently lose bodies when its send queue fills.
    if (mlInitialBytes + mBodies.size() * 100 > 32 * 1024 * 1024) return false;
    std::vector<Body> states;
    for (std::map<uint64_t, BodyTrack>::iterator it = mBodies.begin(); it != mBodies.end(); ++it)
        if (iPhysicsBody* body = FindBody(it->first)) states.push_back(CaptureBody(it->first, body));
    for (size_t first = 0; first < states.size(); first += MaxBodiesPerPacket)
    {
        Writer writer(Bodies, mpSession->GetMapEpoch()); writer.U32(++mlSequence); writer.U32(0); writer.U8(1);
        size_t count = std::min(MaxBodiesPerPacket, states.size() - first); writer.U8(uint8_t(count));
        for (size_t i = 0; i < count; ++i) WriteBody(writer, states[first + i]);
        mlInitialBytes += writer.bytes.size(); mInitialPackets[alPeer].push_back(writer.bytes);
    }
    for (std::map<uint32_t, Lease>::iterator it = mLeases.begin(); it != mLeases.end(); ++it)
        SendLease(it->second, 0, alPeer, false);
    SendPose();
    return true;
}

void cLuxMultiplayerWorld::SendPose()
{
    if (!gpBase->mpPlayer || !gpBase->mpPlayer->GetCharacterBody()) return;
    iCharacterBody* body = gpBase->mpPlayer->GetCharacterBody();
    cVector3f position = body->GetPosition();
    cVector3f size = body->GetSize();
    Writer writer(Pose, mpSession->GetMapEpoch());
    writer.U32(mpSession->GetLocalPeerId()); writer.U32(++mlSequence);
    writer.F32(position.x); writer.F32(position.y); writer.F32(position.z);
    writer.F32(size.x); writer.F32(size.y); writer.F32(size.z); writer.F32(body->GetYaw());
    Lantern lantern;
    if(iLight* light = gpBase->mpPlayer->GetVisibleLanternLight())
    {
        const cVector3f offset = light->GetWorldPosition() - position;
        const cColor color = light->GetDiffuseColor();
        lantern.active = true;
        for(unsigned i=0; i<3; ++i) lantern.offset[i] = offset.v[i];
        lantern.color[0]=color.r;lantern.color[1]=color.g;lantern.color[2]=color.b;lantern.color[3]=color.a;
        lantern.radius = light->GetRadius();
        // A locally modified hand asset must not publish an invalid pose.
        Writer sample(Pose, 0);WriteLantern(sample, lantern);
        Reader check(sample.bytes);check.U8();check.U32();ReadLantern(check);
        if(!check.Done()) lantern = Lantern();
    }
    WriteLantern(writer, lantern);
    if (mpSession->IsHost()) mpSession->Broadcast(writer.bytes, false);
    else mpSession->Send(0, writer.bytes, false);
}

void cLuxMultiplayerWorld::Update(float afTimeStep)
{
    if (!mpSession->IsActive() || !mpMap) return;
    float dt = std::max(0.0f, std::min(afTimeStep, 0.25f));
    for (std::map<uint32_t, std::deque<std::vector<uint8_t> > >::iterator it = mInitialPackets.begin(); it != mInitialPackets.end(); )
    {
        for (int budget = 0; budget < 8 && !it->second.empty(); ++budget)
        {
            if (!mpSession->Send(it->first, it->second.front(), true)) break;
            mlInitialBytes -= it->second.front().size(); it->second.pop_front();
        }
        if (it->second.empty()) it = mInitialPackets.erase(it); else ++it;
    }
    RefreshBodies();
    for (auto it = mContactAges.begin(); it != mContactAges.end(); )
        if ((it->second += dt) > ContactGrace * 2) it = mContactAges.erase(it); else ++it;
    for (auto it = mContactRequests.begin(); it != mContactRequests.end(); )
        if ((it->second += dt) >= ContactRetry) it = mContactRequests.erase(it); else ++it;
    for (std::map<uint32_t, cLuxMultiplayerRemotePlayer>::iterator it = mPlayers.begin(); it != mPlayers.end(); ++it)
    {
        it->second.age += dt;
        it->second.renderPosition += (it->second.position - it->second.renderPosition) * std::min(1.0f, dt * 15);
        const auto& light = it->second.lantern;
        it->second.renderLanternOffset += (cVector3f(light.offset[0],light.offset[1],light.offset[2]) -
            it->second.renderLanternOffset) * std::min(1.0f, dt * 15);
    }
    UpdatePlayerColliders();
    UpdatePlayerLights();
    if (mlPendingRequest)
    {
        mfPendingTime += dt;
        if (!InteractionStillPressed() || mfPendingTime > 2) CancelPendingInteraction();
    }
    std::vector<uint32_t> expired;
    if (mpSession->IsHost())
    {
        for (std::map<uint32_t, Lease>::iterator it = mLeases.begin(); it != mLeases.end(); ++it)
        {
            Lease& lease = it->second;
            if (lease.contact || lease.owner != mpSession->GetLocalPeerId()) lease.remaining -= dt;
            bool missing = false;
            for (size_t i = 0; i < lease.bodies.size(); ++i) if (!FindBody(lease.bodies[i])) missing = true;
            if (lease.remaining <= 0 || missing) expired.push_back(it->first);
        }
        for (size_t i = 0; i < expired.size(); ++i) EndLease(expired[i], true);
    }
    else
    {
        for (auto& entry : mLeases)
            if (entry.second.contact && entry.second.owner == mpSession->GetLocalPeerId() &&
                (entry.second.remaining -= dt) <= 0) expired.push_back(entry.first);
        for (uint32_t token : expired) ReleaseContact(token);
    }
    if (mlLocalLease && !IsInteractionState(gpBase->mpPlayer->GetCurrentState())) ReleaseInteraction();
    if (mpSession->IsClient() && mlLocalLease)
    {
        mfRenewTime += dt;
        if (mfRenewTime >= 0.5f)
        {
            mfRenewTime = 0;
            Writer writer(LeaseRelease, mpSession->GetMapEpoch()); writer.U32(mlLocalLease); writer.U8(0);
            mpSession->Send(0, writer.bytes, true);
        }
    }
    ApplyTargets(dt);
    mfSendTime += dt;
    mfGroundTruthTime += dt;
    if (mfSendTime < SnapshotStep) return;
    mfSendTime = 0;
    SendPose();
    if (mpSession->IsHost())
    {
        bool groundTruth = mfGroundTruthTime >= 2;
        if (groundTruth) mfGroundTruthTime = 0;
        std::vector<Body> updates, reliable;
        for (std::map<uint64_t, BodyTrack>::iterator it = mBodies.begin(); it != mBodies.end(); ++it)
        {
            iPhysicsBody* body = FindBody(it->first);
            if (!body) continue;
            Body state = CaptureBody(it->first, body);
            BodyTrack& track = it->second;
            // Accepted owner snapshots are relayed directly below. Sampling the
            // host's smoothed follower here would add a second correction delay.
            auto lock = mBodyLeases.find(it->first);
            if (lock != mBodyLeases.end() && mLeases[lock->second].owner != mpSession->GetLocalPeerId()) continue;
            bool sleepingChanged = track.sent && track.lastSent.flags != state.flags;
            if (!track.sent || groundTruth || sleepingChanged)
            {
                reliable.push_back(state);
                track.lastSent = state; track.sent = true;
            }
            else if ((body->GetMass() > 0 && (state.flags & Awake)) || Changed(state, track.lastSent))
            {
                updates.push_back(state);
                track.lastSent = state; track.sent = true;
            }
        }
        // Reliable correction is not an unconditional teleport. Initial bursts
        // and handoffs explicitly request hard placement; routine truth does not.
        SendBodyBatch(0, true, reliable, true, false);
        SendBodyBatch(0, true, updates, false, false);
    }
    else
    {
        for (const auto& entry : mLeases)
        {
            const Lease& lease = entry.second;
            if (lease.owner != mpSession->GetLocalPeerId() || (!lease.contact && entry.first != mlLocalLease)) continue;
            std::vector<Body> states;
            for (uint64_t id : lease.bodies)
                if (iPhysicsBody* body = FindBody(id)) states.push_back(CaptureBody(id, body));
            SendBodyBatch(0, false, states, false, false, entry.first);
        }
    }
}

void cLuxMultiplayerWorld::ApplyBody(const Body& aState, uint32_t alSequence, bool abGroundTruth, bool abFromOwner)
{
    std::map<uint64_t, BodyTrack>::iterator it = mBodies.find(aState.id);
    if (it == mBodies.end()) return;
    BodyTrack& track = it->second;
    if (track.received && !Newer(alSequence, track.receivedSequence)) return;
    iPhysicsBody* body = FindBody(aState.id);
    if (!body) return;
    const bool initial = !track.received;
    track.received = true; track.receivedSequence = alSequence;
    if (!abFromOwner && OwnsSimulation(body)) return;
    track.target = aState; track.targetAge = 0; track.hasTarget = true;
    body->SetActive((aState.flags & Active) != 0);
    body->SetGravity((aState.flags & Gravity) != 0);
    body->SetCollide((aState.flags & Collide) != 0);
    if (!PreserveLocalDropCollision(body)) body->SetCollideCharacter((aState.flags & CollideCharacter) != 0);
    float error = cMath::Vector3Dist(body->GetLocalPosition(), Position(aState));
    float rotationError = 0;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            rotationError = std::max(rotationError, std::fabs(body->GetLocalMatrix().v[row*4+col] - aState.matrix[row*4+col]));
    const bool settled = !(aState.flags & Awake) && body->GetMass() > 0 && error < 0.02f && rotationError < 0.02f;
    if (abGroundTruth || (!abFromOwner && initial) || error > 1.5f || settled)
    {
        body->SetMatrix(Matrix(aState));
        body->SetLinearVelocity(Vector(aState.linear));
        body->SetAngularVelocity(Vector(aState.angular));
        if (settled) track.hasTarget = false;
    }
    // Setting velocity alone does not wake Newton. A sleeping follower also
    // needs to simulate while it converges toward the host's resting pose.
    if ((aState.flags & Awake) || (!settled && (error >= 0.02f || rotationError >= 0.02f))) body->Enable();
}

void cLuxMultiplayerWorld::ApplyTargets(float afTimeStep)
{
    for (std::map<uint64_t, BodyTrack>::iterator it = mBodies.begin(); it != mBodies.end(); ++it)
    {
        BodyTrack& track = it->second;
        if (!track.hasTarget) continue;
        iPhysicsBody* body = FindBody(it->first);
        if (!body || OwnsSimulation(body)) { track.hasTarget = false; continue; }
        track.targetAge += afTimeStep;
        if ((track.target.flags & Awake) && track.targetAge > 0.25f) { track.hasTarget = false; continue; }
        // Newton continues integrating contacts and producing impacts. Correct
        // errors with bounded velocity bias instead of teleporting every frame.
        const bool resting = !(track.target.flags & Awake);
        cVector3f targetVelocity = resting ? cVector3f(0) : Vector(track.target.linear);
        const cMatrixf predictedMatrix = PredictedMatrix(track.target, track.targetAge);
        const cVector3f predicted = predictedMatrix.GetTranslation();
        if (body->GetMass() <= 0)
        {
            body->SetMatrix(cMath::MatrixSlerp(std::min(1.0f, afTimeStep * 20), body->GetLocalMatrix(), predictedMatrix, true));
            continue;
        }
        cVector3f positionError = predicted - body->GetLocalPosition();
        cVector3f correction = Limit(positionError * 8, 5);
        float blend = 1.0f - std::exp(-afTimeStep * 20);
        body->SetLinearVelocity(body->GetLinearVelocity() * (1 - blend) + (targetVelocity + correction) * blend);
        cQuaternion current(body->GetLocalMatrix().GetRotation());
        cQuaternion target(predictedMatrix.GetRotation());
        cQuaternion inverse(current.w, -current.v.x, -current.v.y, -current.v.z);
        cQuaternion difference = target * inverse;
        if (difference.w < 0) difference = difference * -1.0f;
        cVector3f angularCorrection = Limit(difference.v * 12, 8);
        body->SetAngularVelocity(body->GetAngularVelocity() * (1 - blend) +
            ((resting ? cVector3f(0) : Vector(track.target.angular)) + angularCorrection) * blend);
        if (resting && positionError.Length() < 0.02f && difference.v.Length() < 0.01f)
        {
            body->SetMatrix(Matrix(track.target)); body->SetLinearVelocity(0); body->SetAngularVelocity(0);
            track.hasTarget = false;
        }
    }
}

bool cLuxMultiplayerWorld::IsInteractionState(eLuxPlayerState aState)
{
    return aState >= eLuxPlayerState_InteractGrab && aState <= eLuxPlayerState_InteractSlide;
}

bool cLuxMultiplayerWorld::OwnsInteraction(iPhysicsBody* apBody) const
{
    if (!apBody || !mlLocalLease) return false;
    uint32_t owner=0,token=0;
    return GetSimulationLease(apBody,owner,token) && owner==mpSession->GetLocalPeerId() && token==mlLocalLease;
}

bool cLuxMultiplayerWorld::OwnsSimulation(iPhysicsBody* body) const
{
    uint32_t owner=0,token=0;
    if(!GetSimulationLease(body,owner,token)) return false;
    auto lease = mLeases.find(token);
    return lease != mLeases.end() && owner == mpSession->GetLocalPeerId() &&
        (lease->second.contact || lease->first == mlLocalLease);
}

bool cLuxMultiplayerWorld::LeaseMatchesBody(const Lease& lease,uint64_t id,iPhysicsBody* body) const
{
    auto identity=lease.identities.find(id);
    return body && identity!=lease.identities.end() && identity->second.first==body &&
        identity->second.second==BodyEntityRuntimeId(body);
}

bool cLuxMultiplayerWorld::GetSimulationLease(iPhysicsBody* body, uint32_t& owner, uint32_t& token) const
{
    owner=0;token=0;
    if(!body) return false;
    auto lock=mBodyLeases.find(NetworkBodyId(body));
    if(lock==mBodyLeases.end()) return false;
    auto lease=mLeases.find(lock->second);
    if(lease==mLeases.end() || !LeaseMatchesBody(lease->second,NetworkBodyId(body),body)) return false;
    owner=lease->second.owner;token=lease->second.token;
    return true;
}

bool cLuxMultiplayerWorld::NearPlayer(uint32_t peer, iPhysicsBody* body) const
{
    if (!body || !body->IsActive() || body->GetMass() <= 0 || !body->GetCollideCharacter()) return false;
    cVector3f position, size;
    if (peer == mpSession->GetLocalPeerId())
    {
        iCharacterBody* player = gpBase->mpPlayer->GetCharacterBody();
        if (!player) return false;
        position = player->GetPosition(); size = player->GetSize();
    }
    else
    {
        auto player = mPlayers.find(peer);
        if (player == mPlayers.end() || player->second.age > 1) return false;
        position = player->second.position; size = player->second.size;
    }
    // A small margin accommodates pose transit time. Use shape bounds rather
    // than the pivot, which may be far away on a chair or connected assembly.
    const cVector3f margin = size * 0.5f + cVector3f(0.35f);
    const cVector3f minimum = body->GetBoundingVolume()->GetMin();
    const cVector3f maximum = body->GetBoundingVolume()->GetMax();
    return position.x + margin.x >= minimum.x && position.x - margin.x <= maximum.x &&
        position.y + margin.y >= minimum.y && position.y - margin.y <= maximum.y &&
        position.z + margin.z >= minimum.z && position.z - margin.z <= maximum.z;
}

bool cLuxMultiplayerWorld::AllowPlayerContact(iPhysicsBody* body)
{
    if (!mpSession->IsActive()) return true;
    if (!mpMap || !body || body->IsCharacter() || !mpSession->IsReady()) return false;
    const uint64_t id = NetworkBodyId(body);
    // A newly scripted body can contact the character before the next index
    // refresh. Compare the pointer without dereferencing a stale cached body.
    auto indexed = mBodies.find(id);
    if (indexed == mBodies.end() || indexed->second.body != body ||
        indexed->second.entityRuntimeId!=BodyEntityRuntimeId(body) || body->GetMass() <= 0) return false;
    mContactAges[id] = 0;
    auto lock = mBodyLeases.find(id);
    if (lock != mBodyLeases.end())
    {
        auto lease = mLeases.find(lock->second);
        if (lease == mLeases.end() || lease->second.owner != mpSession->GetLocalPeerId()) return false;
        if (!lease->second.contact) return OwnsInteraction(body);
        lease->second.remaining = ContactGrace;
    }
    if (mpSession->IsHost())
        return OwnsSimulation(body) || GrantLease(mpSession->GetLocalPeerId(), id, 0, true);
    if (!mContactRequests.count(id) && mContactRequests.size() < MaxContactLeases * 2)
    {
        mContactRequests[id] = 0;
        SendPose();
        Writer request(ContactRequest, mpSession->GetMapEpoch()); request.U64(id);
        mpSession->Send(0, request.bytes, true);
    }
    // The Newton character still resolves the collision. Only its forces wait
    // for authority, preventing a locally tipped chair from becoming passable.
    return OwnsSimulation(body);
}

void cLuxMultiplayerWorld::ReleaseContact(uint32_t token)
{
    auto lease = mLeases.find(token);
    if (lease == mLeases.end()) return;
    std::vector<Body> states;
    for (uint64_t id : lease->second.bodies)
        if (iPhysicsBody* body = FindBody(id)) states.push_back(CaptureBody(id, body));
    SendBodyBatch(0, false, states, true, false, token);
    Writer release(LeaseRelease, mpSession->GetMapEpoch()); release.U32(token); release.U8(1);
    mpSession->Send(0, release.bytes, true);
    EndLease(token, false);
}

bool cLuxMultiplayerWorld::IsInteractionOwnedByOther(iPhysicsBody* apBody) const
{
    return IsInteractionOwnedByOther(apBody, mpSession->GetLocalPeerId());
}

bool cLuxMultiplayerWorld::IsInteractionOwnedByOther(iPhysicsBody* apBody, uint32_t alPeer) const
{
    if (!mpSession->IsActive() || !apBody) return false;
    std::map<uint64_t, uint32_t>::const_iterator body = mBodyLeases.find(NetworkBodyId(apBody));
    if (body != mBodyLeases.end())
    {
        std::map<uint32_t, Lease>::const_iterator lease = mLeases.find(body->second);
        if (lease != mLeases.end() && LeaseMatchesBody(lease->second,body->first,apBody) &&
            !lease->second.contact && lease->second.owner != alPeer) return true;
    }
    // Static handles/frame bodies can belong to the same leased prop without
    // participating in its dynamic assembly. They must not offer interaction.
    iLuxEntity* entity = static_cast<iLuxEntity*>(apBody->GetUserData());
    if (!entity || entity->GetEntityType() != eLuxEntityType_Prop) return false;
    iLuxProp* prop = static_cast<iLuxProp*>(entity);
    for (int i = 0; i < prop->GetBodyNum(); ++i)
    {
        if (!prop->GetBody(i)) continue;
        body = mBodyLeases.find(NetworkBodyId(prop->GetBody(i)));
        if (body == mBodyLeases.end()) continue;
        std::map<uint32_t, Lease>::const_iterator lease = mLeases.find(body->second);
        if (lease != mLeases.end() && LeaseMatchesBody(lease->second,body->first,prop->GetBody(i)) &&
            !lease->second.contact && lease->second.owner != alPeer) return true;
    }
    return false;
}

bool cLuxMultiplayerWorld::IsEntityLeased(iLuxProp* apProp) const
{
    if (!mpSession->IsActive() || !apProp) return false;
    for (int i = 0; i < apProp->GetBodyNum(); ++i)
    {
        if (!apProp->GetBody(i)) continue;
        auto lock = mBodyLeases.find(NetworkBodyId(apProp->GetBody(i)));
        if (lock == mBodyLeases.end()) continue;
        auto lease = mLeases.find(lock->second);
        if (lease != mLeases.end() && LeaseMatchesBody(lease->second,lock->first,apProp->GetBody(i)) && !lease->second.contact) return true;
    }
    return false;
}

void cLuxMultiplayerWorld::RemovePlayerCollider(uint32_t alPeer)
{
    std::map<uint32_t, iCharacterBody*>::iterator collider = mPlayerColliders.find(alPeer);
    if (collider == mPlayerColliders.end()) return;
    mpMap->GetPhysicsWorld()->DestroyCharacterBody(collider->second);
    mPlayerColliders.erase(collider);
}

void cLuxMultiplayerWorld::RemovePlayerLight(uint32_t peer)
{
    cWorld* world = mpMap ? mpMap->GetWorld() : NULL;
    if(world)
        if(iLight* light = world->GetLight("MultiplayerLantern_" + cString::ToString((int)peer))) world->DestroyLight(light);
    mPlayerLights.erase(peer);
}

void cLuxMultiplayerWorld::UpdatePlayerLights()
{
    cWorld* world = mpMap->GetWorld();
    if(!world) return;
    for(const auto& entry : mPlayers)
    {
        const auto& player = entry.second;
        if(!player.lantern.active || player.age > 2)
        {
            if(mPlayerLights.count(entry.first)) RemovePlayerLight(entry.first);
            continue;
        }
        const tString name = "MultiplayerLantern_" + cString::ToString((int)entry.first);
        iLight* light = world->GetLight(name);
        if(!light)
        {
            light = world->CreateLightPoint(name, "", false);
            light->SetIsSaved(false);
            light->SetCastShadows(false);
            mPlayerLights.insert(entry.first);
        }
        const Lantern& source = player.lantern;
        light->SetPosition(player.renderPosition + player.renderLanternOffset);
        light->SetRadius(source.radius);
        light->SetDiffuseColor(cColor(source.color[0],source.color[1],source.color[2],source.color[3]));
    }
}

void cLuxMultiplayerWorld::UpdatePlayerColliders()
{
    if (!mpSession->GetSettings().playerCollision)
    {
        while (!mPlayerColliders.empty()) RemovePlayerCollider(mPlayerColliders.begin()->first);
        return;
    }
    for (std::map<uint32_t, cLuxMultiplayerRemotePlayer>::const_iterator it = mPlayers.begin(); it != mPlayers.end(); ++it)
    {
        const cLuxMultiplayerRemotePlayer& player = it->second;
        if (player.age > 2)
        {
            RemovePlayerCollider(it->first);
            continue;
        }
        iCharacterBody* collider = mPlayerColliders.count(it->first) ? mPlayerColliders[it->first] : NULL;
        if (collider && cMath::Vector3Dist(collider->GetSize(), player.size) > 0.01f)
        {
            RemovePlayerCollider(it->first);
            collider = NULL;
        }
        if (!collider)
        {
            collider = mpMap->GetPhysicsWorld()->CreateCharacterBody("MultiplayerPlayer_" + cString::ToString((int)it->first), player.size);
            // Only the local player's controller decides local movement. These
            // remote cylinders participate in character sweeps, but never run
            // their own controller or push host-authoritative dynamic objects.
            collider->SetActive(false);
            collider->SetMass(1000000);
            collider->GetCurrentBody()->SetCollide(false);
            collider->GetCurrentBody()->SetCollideCharacter(true);
            collider->GetCurrentBody()->SetCollideFlags(eFlagBit_All);
            mPlayerColliders[it->first] = collider;
        }
        collider->SetPosition(player.renderPosition);
        collider->GetCurrentBody()->SetActive(true);
    }
}

bool cLuxMultiplayerWorld::InteractionStillPressed() const
{
    return !mpSession->IsWindowVisible() && gpBase->mpInputHandler->GetState() == eLuxInputState_Game &&
        gpBase->mpEngine->GetInput()->IsTriggerd(eLuxAction_Interact) &&
        gpBase->mpPlayer->GetCurrentState() == mPendingPreviousState;
}

bool cLuxMultiplayerWorld::RequestInteraction(iPhysicsBody* apBody, eLuxPlayerState aState, const cVector3f& avFocus)
{
    if (!mpSession->IsActive()) return true;
    if (!mpMap || !apBody) return false;
    if (OwnsInteraction(apBody)) return true;
    if (IsInteractionOwnedByOther(apBody)) return false;
    RefreshBodies();
    uint64_t id = NetworkBodyId(apBody);
    if (!FindBody(id) || apBody->GetMass() <= 0) return false;
    if (mpSession->IsHost())
    {
        bool granted = GrantLease(mpSession->GetLocalPeerId(), id, 0);
        if (granted) mbLocalInteractionStarted = true;
        return granted;
    }
    if (mlPendingRequest) return false;
    mlPendingRequest = ++mlRequestCounter;
    if (mlPendingRequest == 0) mlPendingRequest = ++mlRequestCounter;
    mlPendingBody = id; mPendingState = aState;
    mPendingPreviousState = gpBase->mpPlayer->GetCurrentState(); mvPendingFocus = avFocus; mfPendingTime = 0;
    Writer writer(LeaseRequest, mpSession->GetMapEpoch()); writer.U32(mlPendingRequest); writer.U64(id);
    mpSession->Send(0, writer.bytes, true);
    return false;
}

void cLuxMultiplayerWorld::CancelPendingInteraction()
{
    mlPendingRequest = 0; mlPendingBody = 0; mfPendingTime = 0;
}

void cLuxMultiplayerWorld::AcceptPendingInteraction()
{
    iPhysicsBody* body = FindBody(mlPendingBody);
    if (!mlPendingRequest || !body || !InteractionStillPressed() || !OwnsInteraction(body))
    {
        CancelPendingInteraction(); ReleaseInteraction(); return;
    }
    iLuxEntity* entity = static_cast<iLuxEntity*>(body->GetUserData());
    if(!entity || entity->GetEntityType()!=eLuxEntityType_Prop || entity->GetDestroyMe() ||
       !entity->IsActive() || entity->GetInteractionDisabled() || !entity->CanInteract(body))
    {
        CancelPendingInteraction(); ReleaseInteraction(); return;
    }
    eLuxPlayerState state = mPendingState;
    cLuxPlayerStateVars::SetupInteraction(body, mvPendingFocus);
    CancelPendingInteraction();
    mbLocalInteractionStarted = true;
    gpBase->mpPlayer->ChangeState(state);
    if(gpBase->mpPlayer->GetCurrentState()!=state) ReleaseInteraction();
}

void cLuxMultiplayerWorld::SendLease(const Lease& aLease, uint32_t alRequest, uint32_t alPeer, bool abBroadcast)
{
    Writer writer(LeaseGrant, mpSession->GetMapEpoch());
    writer.U32(aLease.owner); writer.U32(aLease.token); writer.U32(alRequest); writer.U8(aLease.contact ? 1 : 0);
    writer.U8(uint8_t(aLease.bodies.size()));
    for (size_t i = 0; i < aLease.bodies.size(); ++i) writer.U64(aLease.bodies[i]);
    if (abBroadcast) mpSession->Broadcast(writer.bytes, true); else mpSession->Send(alPeer, writer.bytes, true);
}

bool cLuxMultiplayerWorld::GrantLease(uint32_t alPeer, uint64_t alBody, uint32_t alRequest, bool abContact)
{
    RefreshBodies();
    iPhysicsBody* body = FindBody(alBody);
    if (!body || body->GetMass() <= 0) return false;
    cVector3f playerPosition;
    if (alPeer == mpSession->GetLocalPeerId()) playerPosition = gpBase->mpPlayer->GetCharacterBody()->GetPosition();
    else
    {
        std::map<uint32_t, cLuxMultiplayerRemotePlayer>::iterator player = mPlayers.find(alPeer);
        if (player == mPlayers.end() || player->second.age > 1) return false;
        playerPosition = player->second.position;
    }
    if (abContact ? !NearPlayer(alPeer, body) : cMath::Vector3Dist(playerPosition, body->GetLocalPosition()) > 6) return false;
    size_t contacts = 0;
    for (std::map<uint32_t, Lease>::iterator it = mLeases.begin(); it != mLeases.end(); ++it)
        if (it->second.owner == alPeer)
        {
            if (it->second.contact) ++contacts;
            else if (!abContact) return false;
        }
    if (abContact && contacts >= MaxContactLeases) return false;
    Lease lease;
    lease.owner = alPeer; lease.contact = abContact; lease.remaining = abContact ? ContactGrace : LeaseDuration;
    lease.token = ++mlLeaseCounter; if (lease.token == 0) lease.token = ++mlLeaseCounter;
    std::vector<iPhysicsBody*> group;
    group.push_back(body);
    // Lock the entire prop and connected movable joint assembly: grabbing two
    // different sub-bodies must not start two competing interaction controllers.
    iLuxEntity* entity = static_cast<iLuxEntity*>(body->GetUserData());
    iLuxProp* prop = entity && entity->GetEntityType() == eLuxEntityType_Prop ? static_cast<iLuxProp*>(entity) : NULL;
    if (prop)
        for (int i = 0; i < prop->GetBodyNum(); ++i)
        {
            iPhysicsBody* candidate = prop->GetBody(i);
            if (candidate && candidate->GetMass() > 0 && std::find(group.begin(), group.end(), candidate) == group.end()) group.push_back(candidate);
        }
    std::set<uint32_t> superseded;
    for (size_t i = 0; i < group.size(); ++i)
    {
        if (group.size() > MaxLeaseBodies) return false;
        iPhysicsBody* candidate = group[i];
        uint64_t id = NetworkBodyId(candidate);
        if (!FindBody(id)) return false;
        auto previous = mBodyLeases.find(id);
        if (previous != mBodyLeases.end())
        {
            auto old = mLeases.find(previous->second);
            if (old == mLeases.end() || abContact || !old->second.contact) return false;
            superseded.insert(previous->second);
        }
        lease.bodies.push_back(id); lease.originalGravity[id] = candidate->GetGravity();
        lease.identities[id]=std::make_pair(candidate,BodyEntityRuntimeId(candidate));
        lease.originalCollide[id] = candidate->GetCollide();
        lease.originalCollideCharacter[id] = candidate->GetCollideCharacter();
        for (int joint = 0; joint < candidate->GetJointNum(); ++joint)
        {
            iPhysicsJoint* connection = candidate->GetJoint(joint);
            iPhysicsBody* neighbours[2] = { connection->GetParentBody(), connection->GetChildBody() };
            for (int n = 0; n < 2; ++n)
                if (neighbours[n] && neighbours[n]->GetMass() > 0 && !neighbours[n]->IsCharacter() &&
                    std::find(group.begin(), group.end(), neighbours[n]) == group.end()) group.push_back(neighbours[n]);
        }
    }
    // Deliberate interaction takes priority over passive contact. Validate the
    // entire assembly first, then revoke contact ownership in reliable order.
    for (uint32_t token : superseded) EndLease(token, true);
    if (alPeer != mpSession->GetLocalPeerId())
    {
        std::vector<Body> baseline;
        for (uint64_t id : lease.bodies) baseline.push_back(CaptureBody(id, FindBody(id)));
        SendBodyBatch(alPeer, false, baseline, true, true);
    }
    mLeases[lease.token] = lease;
    for (size_t i = 0; i < lease.bodies.size(); ++i)
    {
        mBodyLeases[lease.bodies[i]] = lease.token;
        // A previous owner's sequence space must not reject the next owner's updates.
        mBodies[lease.bodies[i]].received = false; mBodies[lease.bodies[i]].hasTarget = false;
    }
    if (!abContact && alPeer == mpSession->GetLocalPeerId()) mlLocalLease = lease.token;
    // Client OnInteract cannot mutate the host's closed hinge limit. Release
    // that limit only after the host has granted exclusive assembly ownership.
    if (!abContact && prop && prop->GetPropType() == eLuxPropType_SwingDoor)
    {
        cLuxProp_SwingDoor* door = static_cast<cLuxProp_SwingDoor*>(prop);
        if (!door->GetLocked()) door->SetClosed(false, true);
        door->SetDisableAutoClose(false);
    }
    SendLease(lease, alRequest, 0, true);
    return true;
}

void cLuxMultiplayerWorld::ReleaseInteraction()
{
    RefreshBodies();
    CancelPendingInteraction();
    if (!mlLocalLease) return;
    uint32_t token = mlLocalLease;
    // Restore the interaction's local mass/gravity settings before the final
    // reliable release snapshot (called after OnLeaveState by the player gate).
    if (mpSession->IsClient())
    {
        std::vector<Body> states;
        std::map<uint32_t, Lease>::iterator lease = mLeases.find(token);
        if (mbLocalInteractionStarted && lease != mLeases.end())
            for (size_t i = 0; i < lease->second.bodies.size(); ++i)
                if (iPhysicsBody* body = FindBody(lease->second.bodies[i])) states.push_back(CaptureBody(lease->second.bodies[i], body));
        SendBodyBatch(0, false, states, true, false, token);
        Writer writer(LeaseRelease, mpSession->GetMapEpoch()); writer.U32(token); writer.U8(1);
        mpSession->Send(0, writer.bytes, true);
        mlLocalLease = 0;
        mbLocalInteractionStarted = false;
    }
    else EndLease(token, true);
}

void cLuxMultiplayerWorld::EndLease(uint32_t alToken, bool abBroadcast)
{
    std::map<uint32_t, Lease>::iterator it = mLeases.find(alToken);
    if (it == mLeases.end()) return;
    Lease lease = it->second;
    // Releasing a local state can refresh the body index recursively. Remove
    // this lease first, so replacement invalidation cannot release it twice.
    mLeases.erase(alToken);
    if (mlLocalLease == alToken)
    {
        mlLocalLease = 0;
        mbLocalInteractionStarted = false;
        if (gpBase->mpPlayer && IsInteractionState(gpBase->mpPlayer->GetCurrentState()))
            gpBase->mpPlayer->ChangeState(eLuxPlayerState_Normal);
    }
    for (size_t i = 0; i < lease.bodies.size(); ++i)
    {
        auto lock=mBodyLeases.find(lease.bodies[i]);
        if(lock!=mBodyLeases.end() && lock->second==alToken) mBodyLeases.erase(lock);
        std::map<uint64_t, BodyTrack>::iterator track = mBodies.find(lease.bodies[i]);
        if(track==mBodies.end() || !LeaseMatchesBody(lease,lease.bodies[i],track->second.body)) continue;
        if (track != mBodies.end())
        {
            if (mpSession->IsHost())
            {
                // Consume the owner's final reliable velocity before returning
                // authority. Otherwise a release/throw can lose its last impulse.
                iPhysicsBody* body = FindBody(lease.bodies[i]);
                if (body && track->second.hasTarget && track->second.targetAge <= 0.25f && lease.owner != mpSession->GetLocalPeerId())
                {
                    body->SetMatrix(PredictedMatrix(track->second.target, track->second.targetAge));
                    body->SetLinearVelocity(Vector(track->second.target.linear));
                    body->SetAngularVelocity(Vector(track->second.target.angular));
                }
                track->second.received = false;
                track->second.sent = false;
            }
            // Clients retain the host sequence across lease changes so delayed
            // unreliable snapshots cannot roll back the just-released object.
            track->second.hasTarget = false;
        }
        if (mpSession->IsHost())
        {
            iPhysicsBody* body = FindBody(lease.bodies[i]);
            if (body)
            {
                // Passive contact never overrides these flags. Preserve any
                // newer native changes, such as a sticky area's attachment.
                if (!lease.contact)
                {
                    body->SetGravity(lease.originalGravity[lease.bodies[i]]);
                    body->SetCollide(lease.originalCollide[lease.bodies[i]]);
                    if (!PreserveLocalDropCollision(body))
                        body->SetCollideCharacter(lease.originalCollideCharacter[lease.bodies[i]]);
                }
                body->Enable();
            }
        }
    }
    if (abBroadcast)
    {
        std::vector<Body> finalStates;
        for (uint64_t id : lease.bodies)
            if (iPhysicsBody* body = FindBody(id))
                if(LeaseMatchesBody(lease,id,body)) finalStates.push_back(CaptureBody(id, body));
        SendBodyBatch(0, true, finalStates, true, false);
        Writer writer(LeaseRelease, mpSession->GetMapEpoch()); writer.U32(alToken); writer.U8(1);
        mpSession->Broadcast(writer.bytes, true);
    }
}

bool cLuxMultiplayerWorld::HandleMessage(uint32_t alPeer, const std::vector<uint8_t>& avMessage)
{
    if (!mpSession->IsActive() || !mpMap) return true;
    Reader reader(avMessage); uint8_t type = reader.U8(); uint32_t epoch = reader.U32();
    if (!reader.valid) return false;
    if (epoch != mpSession->GetMapEpoch()) return true;
    if (mpSession->IsClient() && alPeer != 0) return false;
    RefreshBodies();
    if (type == Pose)
    {
        uint32_t peer = reader.U32(), sequence = reader.U32();
        cLuxMultiplayerRemotePlayer player;
        player.position.x = reader.F32(100000); player.position.y = reader.F32(100000); player.position.z = reader.F32(100000);
        player.size.x = reader.F32(5); player.size.y = reader.F32(5); player.size.z = reader.F32(5); player.yaw = reader.F32(100000);
        player.lantern = ReadLantern(reader);
        if (!reader.Done() || player.size.x < 0.1f || player.size.y < 0.1f || player.size.z < 0.1f ||
            (mpSession->IsHost() && peer != alPeer)) return false;
        if (peer == mpSession->GetLocalPeerId()) return true; // Host also relays to the originator.
        std::map<uint32_t, cLuxMultiplayerRemotePlayer>::iterator old = mPlayers.find(peer);
        if (old != mPlayers.end() && !Newer(sequence, old->second.sequence)) return true;
        player.sequence = sequence; player.renderPosition = old == mPlayers.end() ? player.position : old->second.renderPosition;
        player.renderLanternOffset = old != mPlayers.end() && old->second.lantern.active ?
            old->second.renderLanternOffset : cVector3f(player.lantern.offset[0],player.lantern.offset[1],player.lantern.offset[2]);
        if (old != mPlayers.end() && cMath::Vector3Dist(player.position, player.renderPosition) > 8) player.renderPosition = player.position;
        mPlayers[peer] = player;
        if (mpSession->IsHost()) mpSession->Broadcast(avMessage, false);
        return true;
    }
    if (type == Bodies)
    {
        uint32_t sequence = reader.U32(), token = reader.U32(); uint8_t groundTruth = reader.U8();
        std::vector<Body> states;
        if (groundTruth > 1 || !ReadBodies(reader, states)) return false;
        if (mpSession->IsHost())
        {
            std::map<uint32_t, Lease>::iterator lease = mLeases.find(token);
            std::map<uint32_t, cLuxMultiplayerRemotePlayer>::iterator player = mPlayers.find(alPeer);
            if (groundTruth) return false;
            if (lease == mLeases.end()) return true; // An unreliable state can arrive after lease revocation.
            if (lease->second.owner != alPeer) return false;
            if (player == mPlayers.end() || player->second.age > 2) return true;
            // Validate every body and the authority lease before any mutation.
            for (size_t i = 0; i < states.size(); ++i)
            {
                std::map<uint64_t, uint32_t>::iterator lock = mBodyLeases.find(states[i].id);
                if (lock == mBodyLeases.end() || lock->second != token) return false;
                if (cMath::Vector3Dist(Position(states[i]), player->second.position) > 12) return true;
            }
            // A simulation stream is not evidence of continuing player contact.
            // Only nearby contact requests extend that short lease.
            if (!lease->second.contact) lease->second.remaining = LeaseDuration;
            if (lease->second.contact)
                for (Body& state : states)
                {
                    iPhysicsBody* body = FindBody(state.id);
                    if (!body || body->GetMass() <= 0) return true;
                    state.flags = (state.flags & Awake) | (CaptureBody(state.id, body).flags & ~Awake);
                }
        }
        else if (token != 0) return false;
        std::vector<Body> accepted;
        for (const Body& state : states)
        {
            // Sequence validation also protects relaying: delayed owner packets
            // must not be repackaged with a fresh host sequence and roll back peers.
            auto track = mBodies.find(state.id);
            const bool fresh = track != mBodies.end() && (!track->second.received || Newer(sequence, track->second.receivedSequence));
            ApplyBody(state, sequence, groundTruth != 0, mpSession->IsHost());
            if (mpSession->IsHost() && fresh) accepted.push_back(state);
        }
        if (mpSession->IsHost()) SendBodyBatch(0, true, accepted, false, false);
        return true;
    }
    if (type == ContactRequest)
    {
        const uint64_t id = reader.U64();
        if (!reader.Done() || !mpSession->IsHost()) return false;
        iPhysicsBody* body = FindBody(id);
        if (!NearPlayer(alPeer, body)) return true;
        auto lock = mBodyLeases.find(id);
        if (lock != mBodyLeases.end())
        {
            auto lease = mLeases.find(lock->second);
            if (lease != mLeases.end() && lease->second.contact && lease->second.owner == alPeer)
                lease->second.remaining = ContactGrace;
        }
        else GrantLease(alPeer, id, 0, true);
        return true;
    }
    if (type == LeaseRequest)
    {
        uint32_t request = reader.U32(); uint64_t body = reader.U64();
        if (!reader.Done() || !request || !mpSession->IsHost()) return false;
        if (!GrantLease(alPeer, body, request))
        {
            Writer writer(LeaseDenied, epoch); writer.U32(request); mpSession->Send(alPeer, writer.bytes, true);
        }
        return true;
    }
    if (type == LeaseGrant)
    {
        Lease lease; lease.owner = reader.U32(); lease.token = reader.U32(); uint32_t request = reader.U32();
        const uint8_t contact = reader.U8(); uint8_t count = reader.U8();
        if (mpSession->IsHost() || !lease.token || contact > 1 || (contact && request) || !count || count > MaxLeaseBodies) return false;
        lease.contact = contact != 0; lease.remaining = lease.contact ? ContactGrace : LeaseDuration;
        for (uint8_t i = 0; i < count; ++i)
        {
            uint64_t id = reader.U64();
            if (std::find(lease.bodies.begin(), lease.bodies.end(), id) != lease.bodies.end()) return false;
            lease.bodies.push_back(id);
        }
        if (!reader.Done()) return false;
        for(uint64_t id:lease.bodies)
            if(iPhysicsBody* body=FindBody(id)) lease.identities[id]=std::make_pair(body,BodyEntityRuntimeId(body));
        mLeases[lease.token] = lease;
        for (size_t i = 0; i < lease.bodies.size(); ++i)
        {
            mBodyLeases[lease.bodies[i]] = lease.token;
            if (lease.owner == mpSession->GetLocalPeerId())
            {
                auto track = mBodies.find(lease.bodies[i]);
                if (track != mBodies.end()) track->second.hasTarget = false;
            }
        }
        if (lease.owner == mpSession->GetLocalPeerId() && lease.contact)
        {
            bool recent = false;
            for (uint64_t id : lease.bodies)
            {
                auto age = mContactAges.find(id);
                if (age != mContactAges.end() && age->second < ContactGrace) recent = true;
            }
            if (!recent) ReleaseContact(lease.token);
        }
        else if (lease.owner == mpSession->GetLocalPeerId())
        {
            mlLocalLease = lease.token;
            if (mlPendingRequest == request && request) AcceptPendingInteraction();
            else ReleaseInteraction(); // A cancelled request must never start a delayed grab.
        }
        return true;
    }
    if (type == LeaseRelease)
    {
        uint32_t token = reader.U32(); uint8_t release = reader.U8();
        if (!reader.Done() || release > 1) return false;
        std::map<uint32_t, Lease>::iterator lease = mLeases.find(token);
        if (lease == mLeases.end()) return true;
        if (mpSession->IsHost())
        {
            if (lease->second.owner != alPeer) return false;
            if (release) EndLease(token, true);
            else if (!lease->second.contact) lease->second.remaining = LeaseDuration;
        }
        else if (release) EndLease(token, false);
        else return false;
        return true;
    }
    if (type == LeaseDenied)
    {
        uint32_t request = reader.U32();
        if (!reader.Done() || mpSession->IsHost()) return false;
        if (request == mlPendingRequest) CancelPendingInteraction();
        return true;
    }
    if (type == PeerGone)
    {
        uint32_t peer = reader.U32();
        if (!reader.Done() || mpSession->IsHost()) return false;
        OnPeerDisconnected(peer); return true;
    }
    return false;
}

void cLuxMultiplayerWorld::OnPeerDisconnected(uint32_t alPeer)
{
    RefreshBodies();
    std::map<uint32_t, std::deque<std::vector<uint8_t> > >::iterator pending = mInitialPackets.find(alPeer);
    if (pending != mInitialPackets.end())
    {
        for (size_t i = 0; i < pending->second.size(); ++i) mlInitialBytes -= pending->second[i].size();
        mInitialPackets.erase(pending);
    }
    RemovePlayerCollider(alPeer);
    RemovePlayerLight(alPeer);
    mPlayers.erase(alPeer);
    std::vector<uint32_t> leases;
    for (std::map<uint32_t, Lease>::iterator it = mLeases.begin(); it != mLeases.end(); ++it)
        if (it->second.owner == alPeer) leases.push_back(it->first);
    for (size_t i = 0; i < leases.size(); ++i) EndLease(leases[i], mpSession->IsHost());
    if (mpSession->IsHost())
    {
        Writer writer(PeerGone, mpSession->GetMapEpoch()); writer.U32(alPeer); mpSession->Broadcast(writer.bytes, true);
    }
}

void cLuxMultiplayerWorld::RenderSolid(cRendererCallbackFunctions* apFunctions)
{
    if (!mpSession->IsActive() || !mpMap || mPlayers.empty()) return;
    apFunctions->SetProgram(NULL); apFunctions->SetTextureRange(NULL, 0);
    apFunctions->SetVertexBuffer(NULL); apFunctions->SetMatrix(NULL);
    apFunctions->SetBlendMode(eMaterialBlendMode_None); apFunctions->SetDepthTest(true);
    apFunctions->SetDepthWrite(true); apFunctions->SetCullActive(false);
    iLowLevelGraphics* graphics = apFunctions->GetLowLevelGfx();
    for (std::map<uint32_t, cLuxMultiplayerRemotePlayer>::const_iterator it = mPlayers.begin(); it != mPlayers.end(); ++it)
    {
        const cLuxMultiplayerRemotePlayer& player = it->second;
        if (player.age > 10) continue;
        float radius = player.size.x * 0.5f, height = player.size.y;
        cVector3f base = player.renderPosition - cVector3f(0, height * 0.5f, 0);
        cVector3f top = base + cVector3f(0, height, 0);
        cColor color = it->first == 0 ? cColor(0.82f, 0.62f, 0.25f, 1) : cColor(0.25f, 0.65f, 0.8f, 1);
        const int segments = 16;
        for (int segment = 0; segment < segments; ++segment)
        {
            float angle1 = float(segment) * 6.283185307f / segments;
            float angle2 = float(segment + 1) * 6.283185307f / segments;
            cVector3f offset1(std::cos(angle1) * radius, 0, std::sin(angle1) * radius);
            cVector3f offset2(std::cos(angle2) * radius, 0, std::sin(angle2) * radius);
            float shade = 0.65f + 0.35f * float(segment) / segments;
            tVertexVec quad(4);
            quad[0] = cVertex(base + offset1, color * shade); quad[1] = cVertex(base + offset2, color * shade);
            quad[2] = cVertex(top + offset2, color * shade); quad[3] = cVertex(top + offset1, color * shade);
            graphics->DrawQuad(quad);
            tVertexVec triangle(3);
            triangle[0] = cVertex(top, color); triangle[1] = cVertex(top + offset1, color); triangle[2] = cVertex(top + offset2, color);
            graphics->DrawTriangle(triangle);
            triangle[0] = cVertex(base, color); triangle[1] = cVertex(base + offset2, color); triangle[2] = cVertex(base + offset1, color);
            graphics->DrawTriangle(triangle);
        }
    }
    apFunctions->SetCullActive(true);
}
