#include "LuxMultiplayerEnemies.h"
#include "LuxMultiplayer.h"
#include "LuxEnemy.h"
#include "LuxMap.h"
#include "LuxMusicHandler.h"
#include "LuxPlayer.h"
#include "math/TransformInterpolation.h"
#include <algorithm>

namespace {
static_assert(eLuxEnemyType_LastEnum == 3, "Update enemy wire type validation when adding enemy types.");
static_assert(eLuxEnemyState_LastEnum == 21, "Update enemy wire state validation when adding AI states.");
const float SnapshotInterval = 1.0f / 20.0f;
const float PendingTimeout = 30.0f;
const float TeleportDistanceSquared = 1.5f * 1.5f;
const size_t MaxInitialBytes = 4 * 1024 * 1024;
cMatrixf MeshMatrix(const LuxEnemyWire::State& state) {
    cMatrixf matrix = cMatrixf::Identity;
    for(int row=0;row<3;++row) for(int col=0;col<4;++col) matrix.m[row][col] = state.mesh[row*4+col];
    return matrix;
}
cVector3f Position(const LuxEnemyWire::State& state) {
    return cVector3f(state.position[0],state.position[1],state.position[2]);
}
bool SameContinuousState(const LuxEnemyWire::State& a, const LuxEnemyWire::State& b) {
    for(int i=0;i<3;++i) if(a.position[i] != b.position[i]) return false;
    if(a.yaw != b.yaw || a.illumination != b.illumination || a.coverage != b.coverage ||
       a.animations.size() != b.animations.size() || a.lights.size() != b.lights.size()) return false;
    for(int i=0;i<12;++i) if(a.mesh[i] != b.mesh[i]) return false;
    for(size_t i=0;i<a.animations.size();++i) {
        const auto& x = a.animations[i]; const auto& y = b.animations[i];
        if(x.time != y.time || x.length != y.length || x.speed != y.speed ||
           x.baseSpeed != y.baseSpeed || x.weight != y.weight || x.fadeStep != y.fadeStep) return false;
    }
    for(size_t i=0;i<a.lights.size();++i) {
        const auto& x = a.lights[i]; const auto& y = b.lights[i];
        if(x.radius != y.radius) return false;
        for(int j=0;j<4;++j) if(x.color[j] != y.color[j]) return false;
        for(int j=0;j<12;++j) if(x.matrix[j] != y.matrix[j]) return false;
    }
    return true;
}
}

cLuxMultiplayerEnemies::cLuxMultiplayerEnemies(cLuxMultiplayer* session) : mpSession(session) {}

void cLuxMultiplayerEnemies::Reset() {
    // A map can already be gone when session teardown gets here. These records
    // deliberately contain no owning pointers into that map.
    mHost.clear(); mRemoved.clear(); mReplicas.clear(); mInitial.clear();
    mpMap = NULL; mlSequence = 0; mfSendTime = 0;
}

void cLuxMultiplayerEnemies::OnMapLoaded(cLuxMap* map) {
    Reset(); mpMap = map;
    if(!mpMap || !mpSession->IsClient()) return;
    auto it = mpMap->GetEnemyIterator();
    while(it.HasNext()) {
        iLuxEnemy* enemy = it.Next(); PrepareReplica(enemy);
        enemy->GetMeshEntity()->SetVisible(false);
        for(auto* light : enemy->mvLights) light->SetVisible(false);
    }
}

iLuxEnemy* cLuxMultiplayerEnemies::Find(const tString& name) const {
    if(!mpMap) return NULL;
    iLuxEntity* entity = mpMap->GetEntityByName(name,eLuxEntityType_Enemy);
    return entity && !entity->GetDestroyMe() ? static_cast<iLuxEnemy*>(entity) : NULL;
}

LuxEnemyWire::State cLuxMultiplayerEnemies::Capture(iLuxEnemy* enemy) {
    using namespace LuxEnemyWire;
    State state;
    state.epoch = mpSession->GetMapEpoch(); state.generation = enemy->GetRuntimeID();
    state.name = enemy->GetName(); state.type = uint8_t(enemy->GetEnemyType());
    // Inactive authored enemies have not entered their first state yet; the
    // constructor uses LastEnum until the first native state-machine update.
    // Represent that pending state without entering it on either machine.
    state.state = uint8_t(enemy->mCurrentState < eLuxEnemyState_LastEnum ? enemy->mCurrentState :
        (enemy->mNextState < eLuxEnemyState_LastEnum ? enemy->mNextState : eLuxEnemyState_Idle));
    state.target = enemy->GetTargetPeer();
    state.health = enemy->mfHealth;
    if(enemy->IsActive()) state.flags |= Active;
    if(enemy->mbDisabled) state.flags |= Disabled;
    if(enemy->mbDisableTriggers) state.flags |= DisableTriggers;
    if(enemy->mbHallucination) state.flags |= Hallucination;
    if(enemy->mbCausesSanityDecrease) state.flags |= SanityDecrease;
    if(enemy->mbPlayerDetected) state.flags |= PlayerDetected;
    if(enemy->mbPlayerInRange) state.flags |= PlayerInRange;
    if(enemy->mbCanSeePlayer) state.flags |= CanSeePlayer;
    if(enemy->mbTargetTerrorSource) state.flags |= TargetTerror;
    if(enemy->mlTargetMusicFlags & (1u << eLuxEnemyMusic_Attack)) state.flags |= AttackMusic;
    if(enemy->mlTargetMusicFlags & (1u << eLuxEnemyMusic_Search)) state.flags |= SearchMusic;
    iCharacterBody* character = enemy->GetCharacterBody();
    const cVector3f position = character->GetPosition();
    state.position[0] = position.x; state.position[1] = position.y; state.position[2] = position.z;
    state.yaw = character->GetYaw();
    iPhysicsBody* body = character->GetCurrentBody();
    if(body->IsActive()) state.flags |= CharacterActive;
    if(body->GetCollide()) state.flags |= Collide;
    if(body->GetCollideCharacter()) state.flags |= CollideCharacter;
    cMeshEntity* mesh = enemy->GetMeshEntity();
    if(mesh->IsActive()) state.flags |= MeshActive;
    if(mesh->IsVisible()) state.flags |= Visible;
    const cMatrixf& matrix = mesh->GetWorldMatrix();
    for(int row=0;row<3;++row) for(int col=0;col<4;++col) state.mesh[row*4+col] = matrix.m[row][col];
    if(enemy->mpCurrentAnimation) state.currentAnimation = enemy->mpCurrentAnimation->GetName();
    state.illumination = mesh->GetIlluminationAmount(); state.coverage = mesh->GetCoverageAmount();
    for(int i=0;i<mesh->GetAnimationStateNum();++i) {
        cAnimationState* source = mesh->GetAnimationState(i);
        if(!source->IsActive()) continue;
        Animation animation;
        animation.name = source->GetName(); animation.time = source->GetTimePosition();
        animation.length = source->GetLength(); animation.speed = source->GetSpeed();
        animation.baseSpeed = source->GetBaseSpeed(); animation.weight = source->GetWeight();
        animation.fadeStep = source->GetFadeStep();
        if(source->IsLooping()) animation.flags |= Loop;
        if(source->IsPaused()) animation.flags |= Paused;
        state.animations.push_back(animation);
    }
    for(auto* source : enemy->mvLights) {
        Light light;
        light.name = source->GetName();
        if(source->IsActive()) light.flags |= LightActive;
        if(source->GetVisibleVar()) light.flags |= LightVisible;
        const cColor& color = source->GetDiffuseColor();
        light.color[0] = color.r; light.color[1] = color.g; light.color[2] = color.b; light.color[3] = color.a;
        light.radius = source->GetRadius();
        const cMatrixf& transform = source->GetLocalMatrix();
        for(int row=0;row<3;++row) for(int col=0;col<4;++col) light.matrix[row*4+col] = transform.m[row][col];
        state.lights.push_back(light);
    }
    return state;
}

void cLuxMultiplayerEnemies::PrepareReplica(iLuxEnemy* enemy) {
    if(!enemy || enemy->GetDestroyMe()) return;
    // The query/collision shape still exists, but Newton must never integrate a
    // second enemy controller or run its movement/contact callbacks on clients.
    enemy->GetCharacterBody()->SetActive(false);
    enemy->GetCharacterBody()->StopMovement();
    enemy->mlstMessages.clear();
}

void cLuxMultiplayerEnemies::RemoveReplica(iLuxEnemy* enemy) {
    if(!enemy) return;
    PrepareReplica(enemy);
    enemy->mbActive = false;
    enemy->GetCharacterBody()->GetCurrentBody()->SetActive(false);
    enemy->GetMeshEntity()->SetActive(false);
    enemy->GetMeshEntity()->SetVisible(false);
    for(auto* light : enemy->mvLights) light->SetVisible(false);
    gpBase->mpPlayer->RemoveTerrorEnemy(enemy);
    gpBase->mpMusicHandler->RemoveEnemy(eLuxEnemyMusic_Attack,enemy);
    gpBase->mpMusicHandler->RemoveEnemy(eLuxEnemyMusic_Search,enemy);
    // Native destruction is deferred until the map's safe entity cleanup point.
    mpMap->DestroyEntity(enemy);
}

bool cLuxMultiplayerEnemies::Apply(Replica& replica, iLuxEnemy* enemy, bool snap) {
    using namespace LuxEnemyWire;
    const State& state = replica.state;
    const auto incompatible=[&](const tString& reason) {
        mpSession->LogDiagnostic("enemies","replica rejected name=%s generation=%llu: %s",
            state.name.c_str(),static_cast<unsigned long long>(state.generation),reason.c_str());return false;
    };
    if(uint8_t(enemy->GetEnemyType()) != state.type || state.state >= eLuxEnemyState_LastEnum)
        return incompatible("enemy type or state does not match the installed entity");
    cMeshEntity* mesh = enemy->GetMeshEntity();
    // Validate every resource reference before applying any state. An installed
    // .ent with incompatible animation names must not leave a partial replica.
    for(const auto& animation : state.animations)
        if(!mesh->GetAnimationStateFromName(animation.name)) return incompatible("missing animation '"+animation.name+"'");
    if(!state.currentAnimation.empty() && !mesh->GetAnimationStateFromName(state.currentAnimation))
        return incompatible("missing current animation '"+state.currentAnimation+"'");
    if(state.lights.size() != enemy->mvLights.size()) return incompatible("light count differs from host");
    for(size_t i=0;i<state.lights.size();++i)
        if(state.lights[i].name != enemy->mvLights[i]->GetName()) return incompatible("light name mismatch for '"+state.lights[i].name+"'");
    const bool first = !replica.applied || replica.runtime != enemy->GetRuntimeID();
    if(replica.waitLogged) {
        mpSession->LogDiagnosticLimited("enemy-wait","replica available name=%s waited=%.2fs",state.name.c_str(),replica.pendingTime);
        replica.waitLogged=false;
    }
    PrepareReplica(enemy);
    replica.startPosition = enemy->GetCharacterBody()->GetPosition();
    replica.startYaw = enemy->GetCharacterBody()->GetYaw();
    replica.startMesh = mesh->GetWorldMatrix();
    replica.blendTime = 0;
    replica.runtime = enemy->GetRuntimeID(); replica.pendingTime = 0;
    replica.applied = true;
    snap = snap || first || cMath::Vector3DistSqr(replica.startPosition,Position(state)) > TeleportDistanceSquared;

    // Avoid SetActive, ChangeState and PlayAnim: their native callbacks can
    // change AI, run scripts, damage players, and restart attacks.
    enemy->mbActive = (state.flags & Active) != 0;
    enemy->mbDisabled = (state.flags & Disabled) != 0;
    enemy->mbDisableTriggers = (state.flags & DisableTriggers) != 0;
    enemy->mbHallucination = (state.flags & Hallucination) != 0;
    enemy->mbCausesSanityDecrease = (state.flags & SanityDecrease) != 0;
    enemy->mfHealth = state.health;
    enemy->mPreviousState = enemy->mCurrentState;
    enemy->mCurrentState = static_cast<eLuxEnemyState>(state.state);
    enemy->mNextState = enemy->mCurrentState;
    enemy->mlTargetPeer = state.target;
    enemy->mbCanSeePlayer = (state.flags & CanSeePlayer) != 0;
    enemy->mbPlayerDetected = (state.flags & PlayerDetected) != 0;
    enemy->mbPlayerInRange = (state.flags & PlayerInRange) != 0;
    enemy->mbTargetTerrorSource = (state.flags & TargetTerror) != 0;
    enemy->mlTargetMusicFlags = uint8_t(((state.flags & AttackMusic) ? (1u << eLuxEnemyMusic_Attack) : 0) |
        ((state.flags & SearchMusic) ? (1u << eLuxEnemyMusic_Search) : 0));
    mesh->SetActive((state.flags & MeshActive) != 0);
    mesh->SetVisible((state.flags & Visible) != 0);
    mesh->SetIlluminationAmount(state.illumination); mesh->SetCoverageAmount(state.coverage);
    for(size_t i=0;i<state.lights.size();++i) {
        const Light& source = state.lights[i]; iLight* target = enemy->mvLights[i];
        target->SetActive((source.flags & LightActive) != 0);
        target->SetVisible((source.flags & LightVisible) != 0);
        target->SetDiffuseColor(cColor(source.color[0],source.color[1],source.color[2],source.color[3]));
        target->SetRadius(source.radius);
        cMatrixf matrix = cMatrixf::Identity;
        for(int row=0;row<3;++row) for(int col=0;col<4;++col) matrix.m[row][col] = source.matrix[row*4+col];
        target->SetMatrix(matrix);
    }
    iPhysicsBody* body = enemy->GetCharacterBody()->GetCurrentBody();
    body->SetActive((state.flags & CharacterActive) != 0);
    body->SetCollide((state.flags & Collide) != 0);
    body->SetCollideCharacter((state.flags & CollideCharacter) != 0);
    for(int i=0;i<mesh->GetAnimationStateNum();++i) mesh->GetAnimationState(i)->SetActive(false);
    for(const auto& animation : state.animations) {
        cAnimationState* target = mesh->GetAnimationStateFromName(animation.name);
        target->SetActive(true); target->SetLoop((animation.flags & Loop) != 0);
        target->SetPaused((animation.flags & Paused) != 0);
        target->SetLength(animation.length); target->SetTimePosition(animation.time);
        target->SetSpeed(animation.speed); target->SetBaseSpeed(animation.baseSpeed);
        target->SetWeight(animation.weight); target->SetFadeStep(animation.fadeStep);
    }
    enemy->mpCurrentAnimation = state.currentAnimation.empty() ? NULL : mesh->GetAnimationStateFromName(state.currentAnimation);
    if(snap) {
        replica.startPosition = Position(state); replica.startYaw = state.yaw; replica.startMesh = MeshMatrix(state);
        replica.blendTime = SnapshotInterval;
        ApplyPose(replica,enemy,0);
        mesh->ResetRenderInterpolation();
        body->ResetRenderInterpolation();
    }
    UpdateLocalMusic(enemy);
    return true;
}

void cLuxMultiplayerEnemies::ApplyPose(Replica& replica, iLuxEnemy* enemy, float dt) {
    replica.blendTime = std::min(SnapshotInterval,replica.blendTime + dt);
    const float alpha = replica.blendTime / SnapshotInterval;
    const cVector3f target = Position(replica.state);
    const cVector3f position = replica.startPosition + (target-replica.startPosition)*alpha;
    const float yaw = replica.startYaw + cMath::GetAngleDistanceRad(replica.startYaw,replica.state.yaw)*alpha;
    enemy->GetCharacterBody()->SetPosition(position,false,false);
    enemy->GetCharacterBody()->SetYaw(yaw);
    // Use the engine's affine interpolation so authored scale/shear/reflections
    // do not collapse to a unit rotation while a network sample is blending.
    const cMatrixf matrix = hpl::InterpolateTransform(replica.startMesh,MeshMatrix(replica.state),alpha);
    if(!(matrix == enemy->GetMeshEntity()->GetWorldMatrix())) enemy->GetMeshEntity()->SetWorldMatrix(matrix);
}

void cLuxMultiplayerEnemies::UpdateLocalMusic(iLuxEnemy* enemy) {
    const bool relevant = enemy->IsActive() && !enemy->IsDisabled() && enemy->GetHealth() > 0 &&
        enemy->GetTargetPeer() == mpSession->GetLocalPeerId() && !gpBase->mpPlayer->IsDead();
    // State names do not imply player-facing effects: e.g. a lurker's Search
    // state is food scavenging. Replicate native registrations rather than
    // inventing presentation from a generic state-to-music mapping.
    const bool hunting = relevant && (enemy->mlTargetMusicFlags & (1u << eLuxEnemyMusic_Attack));
    const bool searching = relevant && (enemy->mlTargetMusicFlags & (1u << eLuxEnemyMusic_Search));
    if(hunting) gpBase->mpMusicHandler->AddEnemy(eLuxEnemyMusic_Attack,enemy);
    else gpBase->mpMusicHandler->RemoveEnemy(eLuxEnemyMusic_Attack,enemy);
    if(searching) gpBase->mpMusicHandler->AddEnemy(eLuxEnemyMusic_Search,enemy);
    else gpBase->mpMusicHandler->RemoveEnemy(eLuxEnemyMusic_Search,enemy);
    if(relevant && enemy->mbTargetTerrorSource) gpBase->mpPlayer->AddTerrorEnemy(enemy);
    else gpBase->mpPlayer->RemoveTerrorEnemy(enemy);
}

void cLuxMultiplayerEnemies::UpdateReplica(iLuxEnemy* enemy, float dt) {
    if(!mpSession->IsClient()) return;
    PrepareReplica(enemy);
    // Scene::UpdateLogic advances mesh animation and its cosmetic sound events.
    // iLuxEnemy::UpdateAnimation is deliberately not called: its special markers
    // dispatch damage and state-machine events. Glow remains viewer-dependent.
    auto it = mReplicas.find(enemy->GetName());
    if(it != mReplicas.end() && it->second.applied && !it->second.removed &&
       it->second.runtime == enemy->GetRuntimeID()) {
        enemy->UpdateDarknessGlow(dt);
        UpdateLocalMusic(enemy);
    } else {
        enemy->GetMeshEntity()->SetVisible(false);
        for(auto* light : enemy->mvLights) light->SetVisible(false);
        enemy->GetCharacterBody()->GetCurrentBody()->SetActive(false);
    }
}

void cLuxMultiplayerEnemies::RefreshHost(bool movement, float dt) {
    std::set<tString> alive;
    auto it = mpMap->GetEnemyIterator();
    while(it.HasNext()) {
        iLuxEnemy* enemy = it.Next();
        if(enemy->GetDestroyMe()) continue;
        const tString& name = enemy->GetName(); alive.insert(name);
        if(alive.size() > LuxEnemyWire::MaxEnemies) {
            mpSession->Stop("The map has too many enemies to synchronize."); return;
        }
        HostTrack& track = mHost[name];
        track.heartbeat += dt;
        LuxEnemyWire::State state = Capture(enemy);
        const bool discrete = !track.sent || !LuxEnemyWire::SameDiscreteState(track.last,state);
        // A short heartbeat also repairs a lost final movement/animation packet
        // when the enemy stops changing and conditional updates become quiet.
        const bool heartbeat = track.heartbeat >= 0.5f;
        if(discrete || heartbeat || (movement && !SameContinuousState(track.last,state))) {
            state.sequence = ++mlSequence;
            if(discrete) state.flags |= LuxEnemyWire::Baseline;
            auto bytes = LuxEnemyWire::EncodeState(state);
            if(bytes.size() > LuxEnemyWire::MaxPacketBytes || state.animations.size() > LuxEnemyWire::MaxAnimations ||
               state.lights.size() > LuxEnemyWire::MaxLights) {
                mpSession->Stop("Enemy replication exceeds supported animation limits: " + name); return;
            }
            mpSession->Broadcast(bytes,discrete);
            state.flags &= ~LuxEnemyWire::Baseline;
            track.last = state; track.runtime = enemy->GetRuntimeID(); track.sent = true;
            track.heartbeat = 0;
        }
        mRemoved.erase(name);
    }
    for(auto host = mHost.begin();host != mHost.end();) {
        if(alive.count(host->first)) {++host;continue;}
        LuxEnemyWire::Removed removed;
        removed.epoch = mpSession->GetMapEpoch(); removed.sequence = ++mlSequence;
        removed.generation = host->second.runtime; removed.name = host->first;
        mRemoved[removed.name] = removed;
        if(mRemoved.size() + mHost.size() > LuxEnemyWire::MaxEnemies) {
            mpSession->Stop("Too many enemy lifetimes to synchronize in this map."); return;
        }
        mpSession->Broadcast(LuxEnemyWire::EncodeRemoved(removed),true);
        host = mHost.erase(host);
    }
}

bool cLuxMultiplayerEnemies::SendInitialState(uint32_t peer) {
    if(!mpMap || !mpSession->IsHost()) return false;
    std::deque<std::vector<uint8_t>> queue;
    size_t total = 0;
    auto it = mpMap->GetEnemyIterator();
    while(it.HasNext()) {
        iLuxEnemy* enemy = it.Next();
        if(enemy->GetDestroyMe()) continue;
        auto state = Capture(enemy); state.sequence = ++mlSequence; state.flags |= LuxEnemyWire::Baseline;
        auto bytes = LuxEnemyWire::EncodeState(state);
        if(state.animations.size() > LuxEnemyWire::MaxAnimations || state.lights.size() > LuxEnemyWire::MaxLights ||
           bytes.size() > LuxEnemyWire::MaxPacketBytes) {
            mpSession->LogDiagnostic("enemies","baseline exceeds limits peer=%u name=%s bytes=%zu animations=%zu lights=%zu",
                peer,state.name.c_str(),bytes.size(),state.animations.size(),state.lights.size());return false;
        }
        total += bytes.size(); queue.push_back(bytes);
        if(queue.size() > LuxEnemyWire::MaxEnemies || total > MaxInitialBytes) {
            mpSession->LogDiagnostic("enemies","baseline capacity exceeded peer=%u states=%zu bytes=%zu",peer,queue.size(),total);return false;
        }
    }
    for(const auto& item : mRemoved) {
        auto removed = item.second; removed.sequence = ++mlSequence;
        auto bytes = LuxEnemyWire::EncodeRemoved(removed);
        total += bytes.size(); queue.push_back(bytes);
        if(queue.size() > LuxEnemyWire::MaxEnemies || total > MaxInitialBytes) {
            mpSession->LogDiagnostic("enemies","removal baseline capacity exceeded peer=%u states=%zu bytes=%zu",peer,queue.size(),total);return false;
        }
    }
    mInitial[peer] = queue;
    mpSession->LogDiagnostic("enemies","baseline queued peer=%u states=%zu bytes=%zu",peer,queue.size(),total);
    return true;
}

void cLuxMultiplayerEnemies::OnPeerDisconnected(uint32_t peer) { mInitial.erase(peer); }

void cLuxMultiplayerEnemies::Update(float dt) {
    if(!mpMap || !mpSession->IsActive()) return;
    if(mpSession->IsHost()) {
        mfSendTime += dt;
        const bool movement = mfSendTime >= SnapshotInterval;
        if(movement) mfSendTime = std::fmod(mfSendTime,SnapshotInterval);
        RefreshHost(movement,dt);
        if(!mpMap || !mpSession->IsActive()) return;
        for(auto it = mInitial.begin();it != mInitial.end();) {
            auto& queue = it->second;
            for(unsigned i=0;i<16 && !queue.empty();++i) {
                if(!mpSession->Send(it->first,queue.front(),true)) break;
                queue.pop_front();
            }
            if(queue.empty()) {
                mpSession->LogDiagnostic("enemies","baseline sent peer=%u",it->first);
                it=mInitial.erase(it);
            } else ++it;
        }
        return;
    }
    // Includes inactive enemies, which the ordinary entity update intentionally
    // skips. All lookups validate runtime identity before touching an object.
    auto enemies = mpMap->GetEnemyIterator();
    while(enemies.HasNext()) PrepareReplica(enemies.Next());
    for(auto& entry : mReplicas) {
        Replica& replica = entry.second;
        iLuxEnemy* enemy = Find(entry.first);
        if(replica.removed) {
            if(enemy && (!replica.runtime || enemy->GetRuntimeID() == replica.runtime)) {
                replica.runtime = enemy->GetRuntimeID(); RemoveReplica(enemy);
            }
            continue;
        }
        if(!replica.confirmed || !enemy || (replica.waitingRuntime && enemy->GetRuntimeID() == replica.waitingRuntime)) {
            replica.pendingTime += dt;
            if(replica.pendingTime>=2 && !replica.waitLogged) {
                replica.waitLogged=true;
                mpSession->LogDiagnosticLimited("enemy-wait","waiting for replica name=%s baseline=%d localEntity=%d replacement=%d generation=%llu",
                    entry.first.c_str(),replica.confirmed,enemy!=NULL,replica.waitingRuntime!=0,
                    static_cast<unsigned long long>(replica.state.generation));
            }
            if(replica.pendingTime > PendingTimeout) {
                mpSession->Stop("The host enemy was not created by the map or its script: " + entry.first); return;
            }
            continue;
        }
        replica.waitingRuntime = 0;
        if(!replica.applied || replica.runtime != enemy->GetRuntimeID()) {
            if(!Apply(replica,enemy,true)) {
                mpSession->Stop("The installed enemy or its animations differ from the host: " + entry.first); return;
            }
        }
        ApplyPose(replica,enemy,dt);
    }
}

bool cLuxMultiplayerEnemies::HandleMessage(uint32_t peer, const std::vector<uint8_t>& data) {
    if(data.empty() || !mpSession->IsClient() || peer != 0) return false;
    if(data[0] == luxnet::EnemyState) {
        LuxEnemyWire::State state;
        if(!LuxEnemyWire::DecodeState(data,state)) return false;
        if(state.epoch != mpSession->GetMapEpoch()) return true;
        if(!mpMap) return false;
        const bool baseline = (state.flags & LuxEnemyWire::Baseline) != 0;
        state.flags &= ~LuxEnemyWire::Baseline;
        auto existing = mReplicas.find(state.name);
        if(existing != mReplicas.end()) {
            Replica& old = existing->second;
            if(state.generation < old.state.generation || (old.removed && state.generation == old.state.generation)) return true;
            if(!LuxEnemyWire::Newer(state.sequence,old.state.sequence)) {
                // A later unreliable pose can arrive before this reliable
                // baseline. Confirm creation, but never rewind the newer pose.
                if(state.generation == old.state.generation && baseline && !old.confirmed) old.confirmed = true;
                return true;
            }
        } else if(mReplicas.size() >= LuxEnemyWire::MaxEnemies) return false;
        Replica& replica = mReplicas[state.name];
        iLuxEnemy* enemy = Find(state.name);
        const bool generationChanged = replica.received && replica.state.generation != state.generation;
        if(generationChanged && enemy && replica.runtime == enemy->GetRuntimeID()) {
            // An unreliable snapshot can overtake the reliable ReplaceEntity
            // script. That script still needs the old object to obtain its
            // transform; deleting it here would prevent the replacement.
            replica.waitingRuntime = enemy->GetRuntimeID();
            PrepareReplica(enemy);
            enemy->GetCharacterBody()->GetCurrentBody()->SetActive(false);
            enemy->GetMeshEntity()->SetVisible(false);
            for(auto* light : enemy->mvLights) light->SetVisible(false);
            gpBase->mpPlayer->RemoveTerrorEnemy(enemy);
            gpBase->mpMusicHandler->RemoveEnemy(eLuxEnemyMusic_Attack,enemy);
            gpBase->mpMusicHandler->RemoveEnemy(eLuxEnemyMusic_Search,enemy);
            enemy = NULL;
        }
        const bool snap = !replica.received || generationChanged || replica.removed;
        if(!replica.received || generationChanged) replica.confirmed = baseline;
        else if(baseline) replica.confirmed = true;
        replica.state = state; replica.received = true; replica.removed = false;
        if(generationChanged) {replica.runtime = 0;replica.applied = false;replica.pendingTime = 0;}
        if(replica.confirmed && enemy && replica.waitingRuntime != enemy->GetRuntimeID()) {
            replica.waitingRuntime = 0;
            if(!Apply(replica,enemy,snap)) return false;
        }
        return true;
    }
    if(data[0] == luxnet::EnemyRemoved) {
        LuxEnemyWire::Removed removed;
        if(!LuxEnemyWire::DecodeRemoved(data,removed)) return false;
        if(removed.epoch != mpSession->GetMapEpoch()) return true;
        auto existing = mReplicas.find(removed.name);
        if(existing != mReplicas.end() &&
           (!LuxEnemyWire::Newer(removed.sequence,existing->second.state.sequence) ||
            removed.generation < existing->second.state.generation)) return true;
        if(existing == mReplicas.end() && mReplicas.size() >= LuxEnemyWire::MaxEnemies) return false;
        Replica& replica = mReplicas[removed.name];
        replica.state.name = removed.name; replica.state.epoch = removed.epoch;
        replica.state.sequence = removed.sequence; replica.state.generation = removed.generation;
        replica.received = true; replica.removed = true;
        iLuxEnemy* enemy = Find(removed.name);
        if(enemy && (!replica.runtime || enemy->GetRuntimeID() == replica.runtime)) {
            replica.runtime = enemy->GetRuntimeID(); RemoveReplica(enemy);
        }
        return true;
    }
    return false;
}
