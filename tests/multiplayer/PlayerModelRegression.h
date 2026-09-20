#ifndef MULTIPLAYER_PLAYER_MODEL_REGRESSION_H
#define MULTIPLAYER_PLAYER_MODEL_REGRESSION_H

#include <cstring>
#include "graphics/BoneState.h"
#include "graphics/MeshCreator.h"

#ifdef LoadBitmap
#undef LoadBitmap
#endif

inline hpl::cSteamAvatarImage PlayerModelAvatarFixture()
{
    hpl::cSteamAvatarImage image;
    image.width = image.height = 64;
    image.rgba.resize(64*64*4);
    const unsigned char colors[4][4] = {{240,24,32,255},{24,240,48,255},{24,64,240,255},{240,224,24,255}};
    for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x)
        for (unsigned channel = 0; channel < 4; ++channel)
            image.rgba[4*(y*64+x)+channel] = colors[(y>=32?2:0)+(x>=32?1:0)][channel];
    return image;
}

// Retain real movement, speed limits and jump forces without running camera
// bobbing or ledge checks in this isolated controller.
class cPlayerModelMovementProbe : public cLuxMoveState_Normal
{
public:
    explicit cPlayerModelMovementProbe(cLuxPlayer* player) : cLuxMoveState_Normal(player) {}
    void OnUpdate(float dt) override
    {
        UpdateMovement(dt); UpdateSpeedMultipliers(dt); UpdateJumpAndGroundCheck(dt);
    }
};

// Uses the installed player_model.ent and native scene/skinning implementation.
// The temporary peer never enters the transport or a fixed physics tick.
inline bool RunPlayerModelRegression(tString& error, bool required)
{
    using World = cLuxMultiplayerWorld;
    auto* session = gpBase->mpMultiplayer;
    auto* replication = session->GetWorld();
    auto* map = gpBase->mpMapHandler->GetCurrentMap();
    if (!session->IsReady() || !map || replication->mPlayers.empty())
    { error = "player model fixture needs a ready peer and map"; return false; }
    replication->UpdatePlayerModels();
    if (replication->mbPlayerModelFailed && !required) return true;
    auto* world = map->GetWorld();
    bool passed = true;
    const auto require = [&](bool condition, const char* message) {
        if (!condition && passed) { error = message; passed = false; }
    };
    const auto nearVector = [](const cVector3f& a, const cVector3f& b) { return (a-b).Length() < 0.0002f; };
    const auto name = [](uint32_t peer) { return "MultiplayerPlayerModel_" + cString::ToString((int)peer); };
    const auto rootPosition = [](cMeshEntity* mesh, bool render) {
        auto* root = mesh->GetBoneStateFromName("Armature_root");
        return render ? root->GetRenderWorldPosition() : root->GetWorldPosition();
    };
    const auto countBodies = [&]() {
        unsigned count = 0; auto bodies = map->GetPhysicsWorld()->GetBodyIterator();
        while (bodies.HasNext()) { bodies.Next(); ++count; }
        return count;
    };
    for (const auto& entry : replication->mPlayers)
    {
        if (entry.second.age > 2 || !(entry.second.gameplay.flags & LuxWorldWire::PlayerAlive)) continue;
        auto* mesh = world->GetDynamicMeshEntity(name(entry.first));
        require(mesh && mesh->GetBoneStateFromName("Armature_root"), "connected peer has no native player_model.ent rig");
        if (mesh && mesh->GetBoneStateFromName("Armature_root"))
            require(nearVector(rootPosition(mesh, false), entry.second.renderFeetPosition), "connected peer root is detached from its smoothed feet");
    }
    require(!world->GetDynamicMeshEntity(name(session->GetLocalPeerId())), "local first-person player acquired a duplicate world model");
    if (!passed) return false;

    cLuxMultiplayerWorld fixture(session);
    fixture.mpMap = map;
    uint32_t peer = UINT32_MAX;
    while (replication->mPlayers.count(peer) || world->GetDynamicMeshEntity(name(peer))) --peer;
    auto& source = fixture.mPlayers[peer];
    source.gameplay.flags = LuxWorldWire::PlayerAlive | LuxWorldWire::PlayerOnGround;
    source.position = source.renderPosition = cVector3f(0, -1000, 0);
    source.size = cVector3f(0.6f, 1.8f, 0.6f);
    source.renderFeetPosition = source.position - cVector3f(0, source.size.y * 0.5f, 0);
    // The synthetic cadence samples below move along +X: face that direction
    // explicitly so their positive playback expectations mean forward walking.
    source.yaw = source.renderYaw = -kPi2f;
    source.gameplay.forward[0] = 1; source.gameplay.forward[2] = 0;
    const cVector3f initialFeet = source.renderFeetPosition;
    const unsigned originalBodies = countBodies();
    fixture.UpdatePlayerModels();
    auto* mesh = world->GetDynamicMeshEntity(name(peer));
    require(mesh && mesh->GetBoneStateFromName("Armature_root") && mesh->GetSubMeshEntityNum() > 0,
        "installed player model did not create a native skinned mesh and Armature_root");
    if (!passed) { fixture.Reset(); return false; }
    require(countBodies() == originalBodies, "creating the visual added physics or ragdoll bodies");
    require(!mesh->IsSaved() && !mesh->IsStatic() && mesh->IsVisible(), "player mesh is not an unsaved dynamic scene visual");
    require(!mesh->GetSkeletonPhysicsActive() && !mesh->GetSkeletonCollidersActive(), "player model enabled skeleton physics");
    auto& visual = fixture.mPlayerModels[peer];
    require(!visual.avatarMask && !visual.avatarTexture, "peer without a Steam avatar unexpectedly acquired a face mask");
    auto avatar = PlayerModelAvatarFixture();
    auto invalidAvatar = avatar; invalidAvatar.rgba.pop_back();
    require(!fixture.CreatePlayerAvatarMask(peer,visual,invalidAvatar) && !visual.avatarMask && !visual.avatarTexture,
        "invalid avatar pixels created a partial mask or texture");
    require(fixture.CreatePlayerAvatarMask(peer,visual,avatar), "valid avatar pixels did not create a native face mask");
    if (!passed) { fixture.Reset(); return false; }
    auto* mask = visual.avatarMask;
    auto* head = mesh->GetBoneStateFromName("Armature_mixamorig_Head");
    const tString maskName = mask->GetName();
    require(mask->GetParent() == head && mask->GetType() == eBillboardType_FixedAxis && !mask->IsSaved() &&
        !mask->IsHalo() && !mask->UsesOcclusionQuery() && !mask->GetRenderFlagBit(eRenderableFlag_ShadowCaster),
        "avatar mask is not an unsaved, fixed-axis, shadow-free head attachment");
    require(nearVector(mask->GetLocalMatrix().GetTranslation(),cVector3f(0,0.095f,0.16f)) &&
        (mask->GetSize()-cVector2f(0.23f,0.25f)).Length() < 0.00001f,
        "avatar mask changed its intended face offset or size");
    require(mask->GetMaterial() && mask->GetMaterial()->GetDepthTest() &&
        mask->GetMaterial()->GetTexture(eMaterialTexture_Diffuse) == visual.avatarTexture &&
        visual.avatarTexture->GetWidth() == 64 && visual.avatarTexture->GetHeight() == 64 &&
        visual.avatarTexture->GetWrapS() == eTextureWrap_ClampToEdge && visual.avatarTexture->GetWrapT() == eTextureWrap_ClampToEdge,
        "avatar texture dimensions, depth testing or edge sampling are incorrect");
    require(!fixture.CreatePlayerAvatarMask(peer,visual,avatar) && visual.avatarMask == mask,
        "repeated avatar availability replaced or duplicated an existing mask");
    auto* idle = mesh->GetAnimationStateFromName("idle");
    auto* walking = mesh->GetAnimationStateFromName("walking");
    auto* running = mesh->GetAnimationStateFromName("running");
    auto* jumping = mesh->GetAnimationStateFromName("jumping");
    auto* crouchedIdle = mesh->GetAnimationStateFromName("crouched_idle");
    auto* crouchedWalking = mesh->GetAnimationStateFromName("crouched_walking");
    const std::array<cAnimationState*,cLuxMultiplayerWorld::AnimationCount> clips =
        {idle,walking,running,jumping,crouchedIdle,crouchedWalking};
    for (auto* clip : clips) require(clip != NULL, "player model did not load all six locomotion clips or the crouching_idle alias");
    if (!passed) { fixture.Reset(); return false; }
    for (auto* clip : clips)
        require(clip == jumping ? !clip->IsLooping() : clip->IsActive() && clip->IsLooping(),
            "ground locomotion clips must loop continuously and jumping must be a one-shot");
    require(idle->GetWeight() == 1 && walking->GetWeight() == 0, "stationary player did not start in idle");
    const auto referenceSpeeds = fixture.mfPlayerStrideReferenceSpeeds;
    const float referenceSpeed = referenceSpeeds[cLuxMultiplayerWorld::AnimationWalking];
    const float idlePlaybackSpeed = idle->GetSpeed()*idle->GetBaseSpeed();
    // Independent samples of both low, rearward-moving toe bones put this
    // authored clip near 1.95 metres per source-animation second.
    require(fixture.mbPlayerStrideReferencesMeasured && std::isfinite(referenceSpeed) && referenceSpeed > 1.90f && referenceSpeed < 2.00f,
        "installed walking clip did not match its independently measured foot travel speed");
    require(referenceSpeeds[cLuxMultiplayerWorld::AnimationRunning] > 6.0f && referenceSpeeds[cLuxMultiplayerWorld::AnimationRunning] < 6.2f &&
        referenceSpeeds[cLuxMultiplayerWorld::AnimationCrouchedWalking] > 1.35f && referenceSpeeds[cLuxMultiplayerWorld::AnimationCrouchedWalking] < 1.45f,
        "running or crouched walking did not match its independently measured stride speed");
    {
        // This extra native instance owns fresh animation states while sharing
        // the imported mesh resource. Never alter the displayed player's rig.
        mesh->GetMesh()->IncUserCount();
        auto* probe = world->CreateMeshEntity("CodexWalkCalibrationProbe",mesh->GetMesh(),false);
        probe->Stop(); probe->SetActive(false);
        for (cNode3D* bone = probe->GetBoneStateFromName("Armature_root");
             bone && bone != probe->GetBoneStateRoot(); bone = bone->GetParent()) bone->SetActive(false);
        require(!probe->GetAnimationStateFromName("walking"), "raw calibration fixture unexpectedly already has an entity walking alias");
        if (!probe->GetAnimationStateFromName("walking"))
        {
            require(cLuxMultiplayerWorld::MeasurePlayerStrideSpeed(probe,"walking") == 0,
                "missing walking clip produced a usable stride calibration");
            std::vector<cMatrixf> bind;
            for (int i = 0; i < probe->GetBoneStateNum(); ++i) bind.push_back(probe->GetBoneState(i)->GetLocalMatrix());
            idle->GetAnimation()->IncUserCount();
            auto* falseWalk = probe->AddAnimation(idle->GetAnimation(),"walking",1);
            falseWalk->SetLoop(true);
            require(cLuxMultiplayerWorld::MeasurePlayerStrideSpeed(probe,"walking") == 0,
                "idle foot motion was mistaken for a reliable walking stride");
            require(!falseWalk->IsActive() && falseWalk->GetTimePosition() == 0,
                "failed calibration left its sampled animation running");
            for (int i = 0; i < probe->GetBoneStateNum(); ++i)
                for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
                    require(std::abs(probe->GetBoneState(i)->GetLocalMatrix().m[row][column]-bind[i].m[row][column]) < 0.000001f,
                        "failed calibration left sampled bones in place instead of restoring bind pose");
        }
        world->DestroyMeshEntity(probe);
    }
    for (int i = 0; i < mesh->GetAnimationStateNum(); ++i)
        if (std::find(clips.begin(),clips.end(),mesh->GetAnimationState(i)) == clips.end())
            require(!mesh->GetAnimationState(i)->IsActive(), "player model started an unrelated authored animation");
    const auto pose = [&]() {
        std::vector<cMatrixf> result;
        for (int i = 0; i < mesh->GetBoneStateNum(); ++i) result.push_back(mesh->GetBoneState(i)->GetLocalMatrix());
        return result;
    };
    const auto samePose = [&](const std::vector<cMatrixf>& expected) {
        for (int i = 0; i < mesh->GetBoneStateNum(); ++i)
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
                if (std::abs(mesh->GetBoneState(i)->GetLocalMatrix().m[r][c] - expected[i].m[r][c]) > 0.000001f) return false;
        return true;
    };
    const auto checkWeights = [&]() {
        float total = 0;
        for (auto* clip : clips)
        {
            require(clip->GetWeight() >= 0 && clip->GetWeight() <= 1,
                "locomotion blend weight left the zero-to-one range");
            total += clip->GetWeight();
        }
        require(std::abs(total-1) < 0.000001f, "six-way locomotion blend weights are not normalized");
    };
    const auto step = [&](float dt) {
        std::array<float,cLuxMultiplayerWorld::AnimationCount> previousTimes;
        for (size_t i = 0; i < clips.size(); ++i) previousTimes[i] = clips[i]->GetTimePosition();
        fixture.UpdatePlayerModels(dt);
        const auto continuous = [&](cAnimationState* clip, float before) {
            const float expected = cMath::Wrap(before + dt*clip->GetSpeed()*clip->GetBaseSpeed(), 0, clip->GetLength());
            return std::abs(clip->GetTimePosition()-expected) < 0.00001f;
        };
        for (size_t i = 0; i < clips.size(); ++i)
            if (clips[i] != jumping) require(continuous(clips[i], previousTimes[i]), "ground locomotion loop restarted or advanced more than once per tick");
        require(idle->GetSpeed()*idle->GetBaseSpeed() == idlePlaybackSpeed, "movement speed changed idle playback");
        require(fixture.mfPlayerStrideReferenceSpeeds == referenceSpeeds,
            "changing movement or animation phase changed the cached authored reference speed");
        require(nearVector(rootPosition(mesh, false), source.renderFeetPosition), "animation moved Armature_root away from the player feet");
        require(nearVector(mask->GetWorldPosition(),cMath::MatrixMul(head->GetWorldMatrix(),mask->GetLocalMatrix().GetTranslation())),
            "animated or aimed head detached the avatar mask");
        checkWeights();
    };
    const auto idleStart = pose();
    for (int i = 0; i < 20; ++i) step(1.0f/60);
    require(!samePose(idleStart), "stationary player's idle clip did not animate native bones");
    require(walking->GetWeight() == 0, "stationary player faded into walking");
    source.gameplay.velocity[1] = 3;
    for (int i = 0; i < 20; ++i) step(1.0f/60);
    require(walking->GetWeight() == 0, "vertical movement alone selected walking");
    source.gameplay.velocity[1] = 0;
    source.gameplay.velocity[0] = 0.08f;
    step(1.0f/60);
    require(walking->GetWeight() == 0, "sub-threshold standing jitter selected walking");
    source.gameplay.velocity[0] = 1;
    float previousWeight = walking->GetWeight();
    for (int i = 0; i < 6; ++i)
    {
        step(1.0f/60);
        require(walking->GetWeight() > previousWeight && walking->GetWeight() < 1,
            "starting to walk snapped or stalled instead of crossfading");
        previousWeight = walking->GetWeight();
    }
    // Compare the actual blended skeleton with each endpoint at identical clip
    // times, so advancing clip time cannot masquerade as pose interpolation.
    fixture.UpdatePlayerModels(0);
    const auto blendedPose = pose();
    const float blendedWeight = walking->GetWeight();
    idle->SetWeight(1); walking->SetWeight(0); mesh->UpdateLogic(0);
    require(!samePose(blendedPose), "partial walking blend still rendered only idle bones");
    idle->SetWeight(0); walking->SetWeight(1); mesh->UpdateLogic(0);
    require(!samePose(blendedPose), "partial walking blend snapped to only walking bones");
    idle->SetWeight(1-blendedWeight); walking->SetWeight(blendedWeight); mesh->UpdateLogic(0);
    // Reversing repeatedly while a transition is in flight must begin from the
    // currently displayed mix and retain both animation clocks.
    for (int reversal = 0; reversal < 6; ++reversal)
    {
        fixture.UpdatePlayerModels(0);
        const auto beforePose = pose();
        const float beforeWeight = walking->GetWeight();
        source.gameplay.velocity[0] = reversal%2 ? 1.0f : 0.0f;
        step(0);
        require(walking->GetWeight() == beforeWeight && samePose(beforePose), "reversing an unfinished blend snapped the weight or bone pose");
        step(1.0f/60);
        require(reversal%2 ? walking->GetWeight() > beforeWeight : walking->GetWeight() < beforeWeight,
            "reversed locomotion blend did not move toward its new state");
    }
    for (int i = 0; i < 24; ++i) step(1.0f/60);
    require(walking->GetWeight() == 1 && idle->GetWeight() == 0, "sustained horizontal movement never reached walking");
    const auto walkStart = pose();
    source.gameplay.velocity[0] = 0.08f;
    for (int i = 0; i < 12; ++i) step(1.0f/60);
    require(walking->GetWeight() == 1 && !samePose(walkStart), "walking hysteresis or native walking animation stopped unexpectedly");
    source.gameplay.velocity[0] = 0;
    source.gameplay.velocity[2] = -1;
    for (int i = 0; i < 12; ++i) step(1.0f/60);
    require(walking->GetWeight() == 1, "movement along the other horizontal axis stopped walking");
    source.gameplay.velocity[2] = 0;
    previousWeight = walking->GetWeight();
    for (int i = 0; i < 6; ++i)
    {
        step(1.0f/60);
        require(walking->GetWeight() < previousWeight && walking->GetWeight() > 0, "stopping snapped or stalled instead of crossfading to idle");
        previousWeight = walking->GetWeight();
    }
    for (int i = 0; i < 24; ++i) step(1.0f/60);
    require(idle->GetWeight() == 1 && walking->GetWeight() == 0, "stationary player never returned to idle");
    require(walking->GetSpeed() == 0, "stationary walking clock kept advancing at authored speed");
    // Change physical speed abruptly while preserving the current cycle. The
    // authored animation base multiplier must not scale the calibrated stride.
    for (float rate : {0.4f, 1.0f, 1.8f, 0.25f, 1.0f})
    {
        source.gameplay.velocity[0] = referenceSpeed*rate;
        step(1.0f/60);
        require(std::abs(walking->GetSpeed()*walking->GetBaseSpeed()-rate) < 0.00001f,
            "walking playback does not follow measured horizontal speed immediately and proportionally");
    }
    source.gameplay.velocity[0] = referenceSpeed*0.6f;
    source.gameplay.velocity[2] = referenceSpeed*0.8f;
    source.gameplay.velocity[1] = 8;
    step(1.0f/60);
    require(std::abs(walking->GetSpeed()*walking->GetBaseSpeed()-1) < 0.00001f,
        "diagonal or vertical movement distorted walking playback speed");
    const float authoredBaseSpeed = walking->GetBaseSpeed();
    walking->SetBaseSpeed(2);
    step(1.0f/60);
    require(std::abs(walking->GetSpeed()*walking->GetBaseSpeed()-1) < 0.00001f,
        "authored clip base speed was applied twice to stride calibration");
    walking->SetBaseSpeed(authoredBaseSpeed);
    source.gameplay.velocity[0] = source.gameplay.velocity[1] = source.gameplay.velocity[2] = 0;
    for (int i = 0; i < 24; ++i) step(1.0f/60);

    const uint8_t onGround = LuxWorldWire::PlayerAlive | LuxWorldWire::PlayerOnGround;
    const auto changeState = [&](World::PlayerAnimation target, uint8_t flags, float speed) {
        fixture.UpdatePlayerModels(0);
        const auto beforePose = pose();
        std::array<float,World::AnimationCount> beforeWeights;
        for (size_t i = 0; i < clips.size(); ++i) beforeWeights[i] = clips[i]->GetWeight();
        source.gameplay.flags = flags;
        source.gameplay.velocity[0] = speed;
        source.gameplay.velocity[1] = source.gameplay.velocity[2] = 0;
        step(0);
        for (size_t i = 0; i < clips.size(); ++i)
            require(clips[i]->GetWeight() == beforeWeights[i], "changing locomotion state immediately snapped a blend weight");
        require(samePose(beforePose), "changing locomotion state at zero elapsed time snapped the native bone pose");
        for (int i = 0; i < 24; ++i) step(1.0f/60);
        require(clips[target]->GetWeight() == 1, "locomotion flags selected the wrong settled animation");
    };
    changeState(World::AnimationRunning,onGround|LuxWorldWire::PlayerRunning,3.0f);
    changeState(World::AnimationIdle,onGround|LuxWorldWire::PlayerRunning,0);
    changeState(World::AnimationRunning,onGround|LuxWorldWire::PlayerRunning,0.3f);
    changeState(World::AnimationCrouchedIdle,onGround|LuxWorldWire::PlayerCrouching|LuxWorldWire::PlayerRunning,0);
    const auto crouchedStart = pose();
    for (int i = 0; i < 20; ++i) step(1.0f/60);
    require(!samePose(crouchedStart), "crouched idle did not animate the native bones");
    changeState(World::AnimationCrouchedWalking,onGround|LuxWorldWire::PlayerCrouching|LuxWorldWire::PlayerRunning,0.6f);
    changeState(World::AnimationWalking,onGround,1.0f);
    // Reverse among three unfinished transitions. Every contributing layer
    // must survive the next state change, not just the two newest animations.
    const World::PlayerAnimation interruptedTargets[] =
        {World::AnimationRunning,World::AnimationCrouchedWalking,World::AnimationCrouchedIdle,World::AnimationWalking,World::AnimationIdle};
    const uint8_t interruptedFlags[] =
        {uint8_t(onGround|LuxWorldWire::PlayerRunning),uint8_t(onGround|LuxWorldWire::PlayerCrouching),
         uint8_t(onGround|LuxWorldWire::PlayerCrouching),onGround,onGround};
    for (size_t state = 0; state < sizeof(interruptedTargets)/sizeof(interruptedTargets[0]); ++state)
    {
        fixture.UpdatePlayerModels(0);
        const auto beforePose = pose();
        std::array<float,World::AnimationCount> beforeWeights;
        for (size_t i = 0; i < clips.size(); ++i) beforeWeights[i] = clips[i]->GetWeight();
        source.gameplay.flags = interruptedFlags[state];
        source.gameplay.velocity[0] = state == 2 || state == 4 ? 0.0f : 1.0f;
        step(0);
        for (size_t i = 0; i < clips.size(); ++i)
            require(clips[i]->GetWeight() == beforeWeights[i], "interrupting a multiway blend discarded a contributing animation");
        require(samePose(beforePose), "interrupting a multiway blend snapped the displayed pose");
        for (int i = 0; i < 4; ++i) step(1.0f/60);
        require(clips[interruptedTargets[state]]->GetWeight() > beforeWeights[interruptedTargets[state]],
            "interrupted blend failed to move toward its new animation");
    }
    for (auto animation : {World::AnimationWalking,World::AnimationRunning,World::AnimationCrouchedWalking})
        for (float rate : {0.25f,1.0f,1.8f,0.4f})
        {
            source.gameplay.flags = onGround | (animation == World::AnimationRunning ? LuxWorldWire::PlayerRunning :
                animation == World::AnimationCrouchedWalking ? LuxWorldWire::PlayerCrouching : 0);
            source.gameplay.velocity[0] = referenceSpeeds[animation]*rate;
            step(1.0f/60);
            require(std::abs(clips[animation]->GetSpeed()*clips[animation]->GetBaseSpeed()-rate) < 0.00001f,
                "running or crouched-walking cadence did not follow its own measured stride speed");
        }
    const auto movementVector = [&](float forward, float sideways, float speed) {
        const float yaw = source.yaw;
        source.gameplay.velocity[0] = speed*(-std::sin(yaw)*forward+std::cos(yaw)*sideways);
        source.gameplay.velocity[1] = 0;
        source.gameplay.velocity[2] = speed*(-std::cos(yaw)*forward-std::sin(yaw)*sideways);
    };
    tString reverseSamples = "requested_gait,backward_clip,reference_speed,backward_rate,negative_wraps\n";
    const float crouchedIdleRate = crouchedIdle->GetSpeed()*crouchedIdle->GetBaseSpeed();
    const float inactiveJumpRate = jumping->GetSpeed()*jumping->GetBaseSpeed();
    for (auto gait : {World::AnimationWalking,World::AnimationRunning,World::AnimationCrouchedWalking})
    {
        const auto backwardClip = gait == World::AnimationRunning ? World::AnimationWalking : gait;
        source.gameplay.flags = onGround | (gait == World::AnimationRunning ? LuxWorldWire::PlayerRunning :
            gait == World::AnimationCrouchedWalking ? LuxWorldWire::PlayerCrouching : 0);
        const float speed = referenceSpeeds[gait]*0.8f;
        movementVector(1,0,speed); source.resetModelPose = true;
        fixture.UpdatePlayerModels(0);
        require(clips[gait]->GetWeight() == 1 && visual.gaitDirection == 1,
            "forward gait reset did not begin with positive playback");
        const auto forwardPose = pose();
        const float forwardTime = clips[gait]->GetTimePosition();
        movementVector(-1,0,speed);
        step(0);
        require(visual.movingBackward && visual.gaitDirection == 1 && samePose(forwardPose) &&
            clips[gait]->GetTimePosition() == forwardTime,
            "requesting backward movement immediately changed the phase or displayed pose");
        float previousDirection = 1;
        bool sawPositive = false, sawNegative = false;
        for (int tick = 0; tick < 24; ++tick)
        {
            step(1.0f/60);
            const float direction = clips[gait]->GetSpeed()*clips[gait]->GetBaseSpeed()/0.8f;
            require(direction <= previousDirection+0.00001f && previousDirection-direction < 0.3f,
                "forward-to-backward playback snapped or changed in the wrong direction");
            sawPositive = sawPositive || direction > 0; sawNegative = sawNegative || direction < 0;
            previousDirection = direction;
        }
        require(sawPositive && sawNegative && visual.gaitDirection == -1 && clips[backwardClip]->GetWeight() == 1,
            "backward gait did not settle through a gradual sign change into its intended clip");
        for (auto loop : {World::AnimationWalking,World::AnimationRunning,World::AnimationCrouchedWalking})
            require(std::abs(clips[loop]->GetSpeed()*clips[loop]->GetBaseSpeed()+speed/referenceSpeeds[loop]) < 0.00001f,
                "a backward gait clock lost its negative calibrated speed");
        require(crouchedIdle->GetSpeed()*crouchedIdle->GetBaseSpeed() == crouchedIdleRate &&
            jumping->GetSpeed()*jumping->GetBaseSpeed() == inactiveJumpRate,
            "reversing gait playback changed idle or jump playback");
        // Cross zero using the real animation clock, then complete two cycles.
        auto* reverseClip = clips[backwardClip];
        reverseClip->SetTimePosition(0.001f);
        fixture.UpdatePlayerModels(0);
        unsigned wraps = 0;
        const int reverseTicks = int(std::ceil(2.1f*reverseClip->GetLength()*60/(speed/referenceSpeeds[backwardClip])));
        for (int tick = 0; tick < reverseTicks; ++tick)
        {
            const float before = reverseClip->GetTimePosition();
            step(1.0f/60);
            if (reverseClip->GetTimePosition() > before) ++wraps;
        }
        require(wraps >= 2 && wraps <= 3, "negative native animation playback did not wrap continuously across zero");
        char sample[192];
        std::snprintf(sample,sizeof(sample),"%s,%s,%.6f,%.6f,%u\n",
            gait == World::AnimationRunning ? "running" : gait == World::AnimationWalking ? "walking" : "crouched_walking",
            backwardClip == World::AnimationWalking ? "walking" : "crouched_walking",referenceSpeeds[backwardClip],
            reverseClip->GetSpeed()*reverseClip->GetBaseSpeed(),wraps);
        reverseSamples += sample;
        for (float rate : {0.25f,1.0f,1.8f,0.4f})
        {
            movementVector(-1,0,referenceSpeeds[gait]*rate); step(1.0f/60);
            require(std::abs(reverseClip->GetSpeed()*reverseClip->GetBaseSpeed()+
                referenceSpeeds[gait]*rate/referenceSpeeds[backwardClip]) < 0.00001f,
                "backward slowdown or recovery lost the displayed clip's calibrated magnitude");
        }
        const float baseSpeed = reverseClip->GetBaseSpeed(); reverseClip->SetBaseSpeed(2);
        step(0);
        require(std::abs(reverseClip->GetSpeed()*reverseClip->GetBaseSpeed()+
            referenceSpeeds[gait]*0.4f/referenceSpeeds[backwardClip]) < 0.00001f,
            "backward stride calibration applied the authored base speed twice");
        reverseClip->SetBaseSpeed(baseSpeed);
        for (float forward : {-0.15f,0.15f,-0.05f,0.05f,0.0f})
        {
            movementVector(forward,std::sqrt(1-forward*forward),speed);
            for (int tick = 0; tick < 4; ++tick) step(1.0f/60);
            require(visual.movingBackward && visual.gaitDirection == -1,
                "near-strafe velocity jitter flipped a backward gait");
        }
        movementVector(0,0,0);
        for (int tick = 0; tick < 24; ++tick) step(1.0f/60);
        require(visual.movingBackward && reverseClip->GetSpeed() == 0 &&
            clips[gait == World::AnimationCrouchedWalking ? World::AnimationCrouchedIdle : World::AnimationIdle]->GetWeight() == 1,
            "stopping backward movement lost direction memory or selected a moving pose");
        movementVector(0,1,speed); step(1.0f/60);
        require(visual.movingBackward && visual.gaitDirection == -1, "resuming with a strafe lost backward direction memory");
        movementVector(1,0,speed);
        for (int tick = 0; tick < 4; ++tick) step(1.0f/60);
        const float interruptedDirection = visual.gaitDirection;
        fixture.UpdatePlayerModels(0); const auto interruptedPose = pose();
        movementVector(-1,0,speed); step(0);
        require(visual.gaitDirection == interruptedDirection && samePose(interruptedPose),
            "interrupting a direction blend snapped the signed speed or pose");
        for (int tick = 0; tick < 4; ++tick) step(1.0f/60);
        require(visual.gaitDirection < interruptedDirection, "interrupted direction blend did not return toward backward");
        movementVector(1,0,speed);
        for (int tick = 0; tick < 24; ++tick) step(1.0f/60);
        require(!visual.movingBackward && visual.gaitDirection == 1 && clips[gait]->GetWeight() == 1,
            "backward-to-forward movement did not restore its forward gait");
        movementVector(-1,0,speed); source.resetModelPose = true; fixture.UpdatePlayerModels(0);
        require(visual.movingBackward && visual.gaitDirection == -1 && clips[backwardClip]->GetWeight() == 1,
            "new-life backward movement started with forward playback");
        movementVector(0,0,0); source.resetModelPose = true; fixture.UpdatePlayerModels(0);
        require(!visual.movingBackward && visual.gaitDirection == 1, "stationary reset retained the previous life's backward direction");
    }
    mark(role+"-player-backward.csv",reverseSamples);
    // Gameplay body yaw determines direction even if presentation and head aim
    // are temporarily facing the opposite way while catching up.
    source.gameplay.flags = onGround;
    source.renderYaw = source.yaw+kPif;
    source.gameplay.forward[0] = std::sin(source.yaw); source.gameplay.forward[2] = std::cos(source.yaw);
    movementVector(1,0,referenceSpeed); source.resetModelPose = true; fixture.UpdatePlayerModels(0);
    step(1.0f/60);
    require(!visual.movingBackward && walking->GetSpeed() > 0, "render or head yaw overrode authoritative movement direction");
    source.renderYaw = source.yaw;
    source.gameplay.forward[0] = -std::sin(source.yaw); source.gameplay.forward[2] = -std::cos(source.yaw);
    source.resetModelPose = true; fixture.UpdatePlayerModels(0);
    changeState(World::AnimationIdle,onGround,0);
    // A takeoff signal wins over the character's lingering grounded grace bit.
    // The replicated full-flight flag remains set through descent and ends on landing.
    const float jumpStart = jumping->GetLength()*(4.0f/15.0f);
    const float jumpHold = jumping->GetLength()*0.56f;
    source.gameplay.flags = onGround|LuxWorldWire::PlayerJumping;
    movementVector(-1,0,1);
    source.gameplay.velocity[1] = 3;
    step(0);
    require(std::abs(jumping->GetTimePosition()-jumpStart) < 0.00001f && jumping->IsActive(),
        "native takeoff did not start the nonlooping jump at its authored takeoff pose");
    for (int i = 0; i < 10; ++i) step(1.0f/60);
    require(jumping->GetWeight() == 1 && jumping->GetTimePosition() > jumpStart && jumping->GetSpeed() > 0,
        "backward takeoff reversed the jump clip or failed to blend into jumping promptly");
    source.gameplay.flags = LuxWorldWire::PlayerAlive|LuxWorldWire::PlayerJumping;
    source.gameplay.velocity[1] = 0;
    for (int i = 0; i < 80; ++i) step(1.0f/60);
    require(jumping->GetWeight() == 1 && std::abs(jumping->GetTimePosition()-jumpHold) < 0.00001f,
        "airborne jump did not hold its pose after the takeoff animation");
    source.gameplay.velocity[1] = -3;
    for (int i = 0; i < 20; ++i) step(1.0f/60);
    require(jumping->GetWeight() == 1 && std::abs(jumping->GetTimePosition()-jumpHold) < 0.00001f,
        "descending jump restarted or returned to locomotion before landing");
    changeState(World::AnimationWalking,onGround,1);
    require(!jumping->IsActive() && jumping->GetWeight() == 0, "landed jump stayed active after its blend finished");
    source.gameplay.flags = LuxWorldWire::PlayerAlive;
    source.gameplay.velocity[1] = -2;
    for (int i = 0; i < 20; ++i) step(1.0f/60);
    require(jumping->GetWeight() == 0 && walking->GetWeight() == 1,
        "ordinary falling without a takeoff incorrectly started a new jump");
    source.gameplay.velocity[1] = 2;
    for (int i = 0; i < 20; ++i) step(1.0f/60);
    require(jumping->GetWeight() == 0 && walking->GetWeight() == 1,
        "ladder-like upward movement without a jump flag incorrectly started jumping");
    source.gameplay.flags |= LuxWorldWire::PlayerJumping;
    step(0);
    require(std::abs(jumping->GetTimePosition()-jumpStart) < 0.00001f,
        "a new upward airborne takeoff did not restart the jump clip exactly once");
    for (int i = 0; i < 10; ++i) step(1.0f/60);
    require(jumping->GetTimePosition() > jumpStart && jumping->GetTimePosition() < jumpHold,
        "repeated airborne packets restarted the jump every frame");
    changeState(World::AnimationCrouchedIdle,onGround|LuxWorldWire::PlayerCrouching,0);
    changeState(World::AnimationIdle,onGround,0);

    // Follow the same event multiplier used by flashbacks through production
    // movement limits, native acceleration/deceleration and the displacement
    // sampler used by outgoing player poses. Restore gameplay synchronously.
    {
        auto* player = gpBase->mpPlayer;
        auto* physics = map->GetPhysicsWorld();
        auto* originalBody = player->mpCharBody;
        auto* originalNormalMovement = player->mvMoveStates[eLuxMoveState_Normal];
        const auto originalMoveState = player->mMoveState;
        const auto originalHeadPosAdds = player->mvHeadPosAdds;
        const bool originalPressingRun = player->mbPressingRun, originalPressedMove = player->mbPressedMove;
        const float originalEventSpeed = player->GetEventMoveSpeedMul();
        const float originalScriptRunSpeed = player->GetScriptRunSpeedMul(), originalEventRunSpeed = player->GetEventRunSpeedMul();
        const auto oldWorldMin = physics->GetWorldSizeMin(), oldWorldMax = physics->GetWorldSizeMax();
        physics->SetWorldSize(cVector3f(-1500),cVector3f(1500));
        auto* floor = physics->CreateBody("CodexPlayerSpeedFloor",physics->CreateBoxShape(cVector3f(100,1,100),NULL));
        floor->SetMass(0); floor->SetPosition(cVector3f(0,999.5f,0));
        auto* body = physics->CreateCharacterBody("CodexPlayerSpeedBody",player->GetBodySize());
        body->AddExtraSize(player->GetBodyCrouchSize());
        body->SetMass(player->GetDefaultMass());
        body->SetFeetPosition(cVector3f(0,1000.01f,0));
        body->SetYaw(player->GetCamera()->GetYaw());
        body->SetCollideCharacter(false);
        player->mpCharBody = body;
        cPlayerModelMovementProbe movement(player);
        player->mvMoveStates[eLuxMoveState_Normal] = &movement;
        player->mMoveState = eLuxMoveState_Normal;
        player->mbPressingRun = false; player->mbPressedMove = true;
        movement.OnEnterState(eLuxMoveState_Normal);
        const float dt = gpBase->mpEngine->GetStepSize();
        for (int i = 0; i < 60; ++i) body->Update(dt);
        require(body->IsOnGround(), "native playback speed fixture did not settle on its isolated floor");
        const auto nativeStartFeet = body->GetFeetPosition();
        const auto captureMotion = [&]() {
            const auto sample = cLuxEnemyPlayer::Local(session->GetLocalPeerId());
            source.gameplay.flags = LuxWorldWire::PlayerAlive | (sample.onGround ? LuxWorldWire::PlayerOnGround : 0) |
                (sample.running ? LuxWorldWire::PlayerRunning : 0) | (sample.jumping ? LuxWorldWire::PlayerJumping : 0) |
                (sample.crouching ? LuxWorldWire::PlayerCrouching : 0);
            for (int axis = 0; axis < 3; ++axis) source.gameplay.velocity[axis] = sample.velocity.v[axis];
            source.yaw = source.renderYaw = body->GetYaw();
            for (int axis = 0; axis < 3; ++axis) source.gameplay.forward[axis] = sample.forward.v[axis];
            source.gameplay.pitch = sample.pitch;
            source.renderFeetPosition = initialFeet+(sample.feet-nativeStartFeet);
            return sample;
        };
        const auto move = [&](float multiplier, int ticks, float direction = 1.0f) {
            player->SetEventMoveSpeedMul(multiplier);
            cVector3f measured;
            for (int i = 0; i < ticks; ++i)
            {
                movement.Update(dt); body->Move(eCharDir_Forward,direction); body->Update(dt);
                measured = captureMotion().velocity;
                step(dt);
                const float horizontalSpeed = std::sqrt(measured.x*measured.x+measured.z*measured.z);
                const float rate = walking->GetSpeed()*walking->GetBaseSpeed();
                if (visual.gaitDirection == 1 || visual.gaitDirection == -1)
                    require(std::abs(rate-visual.gaitDirection*horizontalSpeed/referenceSpeed) < 0.0001f,
                        "native character slowdown did not reach signed walking playback through the outgoing pose velocity sampler");
                else require(std::abs(rate) <= horizontalSpeed/referenceSpeed+0.0001f,
                    "native direction reversal exceeded the measured walking cadence");
            }
            require(visual.gaitDirection == direction &&
                direction*(-std::sin(body->GetYaw())*measured.x-std::cos(body->GetYaw())*measured.z) > 0,
                "native movement direction did not reach the gait through authoritative body yaw");
            return std::sqrt(measured.x*measured.x+measured.z*measured.z);
        };
        const float normalSpeed = move(1,90);
        const float flashbackMultiplier = gpBase->mpGameCfg->GetFloat("Player_General","FlashbackMoveSpeedMul",0.5f);
        const float slowSpeed = move(flashbackMultiplier,90);
        require(normalSpeed > 0.1f && flashbackMultiplier > 0 && flashbackMultiplier < 1 &&
            std::abs(slowSpeed/normalSpeed-flashbackMultiplier) < 0.005f,
            "flashback event multiplier did not reduce measured native walking speed by its configured amount");
        const float recoveredSpeed = move(1,90);
        require(std::abs(recoveredSpeed-normalSpeed) < 0.005f,
            "clearing the slowdown did not restore the native walking speed and cadence");
        player->mbPressingRun = true;
        const float scriptedRunSpeed = move(1,90);
        require(captureMotion().running && running->GetWeight() == 1,
            "native running intent did not select running while a script limited its extra speed");
        player->SetScriptRunSpeedMul(1); player->SetEventRunSpeedMul(1);
        const float runSpeed = move(1,90);
        require(captureMotion().running && running->GetWeight() == 1 && runSpeed > normalSpeed,
            "native running controller did not select running through the outgoing pose sampler");
        const float slowRunSpeed = move(flashbackMultiplier,90);
        require(running->GetWeight() == 1 && std::abs(slowRunSpeed/runSpeed-flashbackMultiplier) < 0.005f,
            "native slowed running changed gait or lost its proportional cadence");
        player->mbPressingRun = false;
        const float backwardWalkSpeed = move(1,90,-1);
        const float slowedBackwardWalk = move(flashbackMultiplier,90,-1);
        const float recoveredBackwardWalk = move(1,90,-1);
        require(walking->GetWeight() == 1 && walking->GetSpeed() < 0 &&
            std::abs(slowedBackwardWalk/backwardWalkSpeed-flashbackMultiplier) < 0.005f &&
            std::abs(recoveredBackwardWalk-backwardWalkSpeed) < 0.005f,
            "native backward walking lost its clip or proportional slowdown/recovery");
        player->mbPressingRun = true;
        const float backwardRunSpeed = move(1,90,-1);
        const float slowedBackwardRun = move(flashbackMultiplier,90,-1);
        const float recoveredBackwardRun = move(1,90,-1);
        require(captureMotion().running && walking->GetWeight() == 1 && running->GetWeight() == 0 &&
            backwardRunSpeed > backwardWalkSpeed && std::abs(slowedBackwardRun/backwardRunSpeed-flashbackMultiplier) < 0.005f &&
            std::abs(recoveredBackwardRun-backwardRunSpeed) < 0.005f,
            "native backward running did not use faster reversed walking through slowdown/recovery");
        player->mbPressingRun = false;
        movement.OnCrouch(true);
        const float crouchSpeed = move(1,90);
        require(captureMotion().crouching && crouchedWalking->GetWeight() == 1 && crouchSpeed < normalSpeed,
            "native crouch controller did not select crouched walking through the outgoing pose sampler");
        const float backwardCrouchSpeed = move(1,90,-1);
        const float slowedBackwardCrouch = move(flashbackMultiplier,90,-1);
        const float recoveredBackwardCrouch = move(1,90,-1);
        require(crouchedWalking->GetWeight() == 1 && crouchedWalking->GetSpeed() < 0 &&
            std::abs(slowedBackwardCrouch/backwardCrouchSpeed-flashbackMultiplier) < 0.005f &&
            std::abs(recoveredBackwardCrouch-backwardCrouchSpeed) < 0.005f,
            "native crouched backward walking lost proportional slowdown/recovery");
        body->SetMoveSpeed(eCharDir_Forward,0); player->mbPressedMove = false;
        for (int i = 0; i < 30; ++i) { movement.Update(dt); body->Update(dt); captureMotion(); step(dt); }
        require(crouchedIdle->GetWeight() == 1, "stopping the native crouched body did not select crouched idle");
        movement.OnCrouch(true);
        for (int i = 0; i < 30; ++i) { movement.Update(dt); body->Update(dt); captureMotion(); step(dt); }
        require(!captureMotion().crouching && idle->GetWeight() == 1, "native uncrouch did not return to idle");
        movement.OnJump(true);
        require(captureMotion().jumping, "native jump input did not reach the outgoing pose sampler");
        bool sawAirborne = false, sawDescent = false, sawLanding = false, sawJumpClip = false;
        int landingTicks = 0;
        float maximumJumpHeight = 0;
        for (int i = 0; i < 180; ++i)
        {
            movement.Update(dt); body->Update(dt);
            const auto sample = captureMotion();
            step(dt);
            maximumJumpHeight = (std::max)(maximumJumpHeight,sample.feet.y-nativeStartFeet.y);
            sawAirborne = sawAirborne || !sample.onGround;
            sawDescent = sawDescent || (sawAirborne && sample.velocity.y < -0.1f);
            sawJumpClip = sawJumpClip || jumping->GetWeight() == 1;
            if (sawAirborne && !sample.onGround && sample.velocity.y < -0.1f)
                require(sample.jumping, "native descending jump disappeared from the outgoing pose state");
            if (sawAirborne && sawDescent && sample.onGround)
            {
                sawLanding = true;
                if (++landingTicks > 1) require(!sample.jumping, "native jump state remained set after the first grounded controller tick");
            }
        }
        require(sawAirborne && sawDescent && sawLanding && sawJumpClip && maximumJumpHeight > 0.1f && idle->GetWeight() == 1,
            "native jump did not render through takeoff, descent and landing back to idle");
        player->mpCharBody = originalBody;
        player->mvMoveStates[eLuxMoveState_Normal] = originalNormalMovement;
        player->mMoveState = originalMoveState;
        player->mvHeadPosAdds = originalHeadPosAdds;
        player->mbPressingRun = originalPressingRun; player->mbPressedMove = originalPressedMove;
        player->SetEventMoveSpeedMul(originalEventSpeed);
        player->SetScriptRunSpeedMul(originalScriptRunSpeed); player->SetEventRunSpeedMul(originalEventRunSpeed);
        physics->DestroyCharacterBody(body); physics->DestroyBody(floor);
        physics->SetWorldSize(oldWorldMin,oldWorldMax);
        source.gameplay.velocity[0] = source.gameplay.velocity[1] = source.gameplay.velocity[2] = 0;
        source.gameplay.flags = onGround;
        source.renderFeetPosition = initialFeet;
        for (int i = 0; i < 24; ++i) step(1.0f/60);
        char metrics[1024];
        std::snprintf(metrics,sizeof(metrics),"walking_reference_mps=%.6f running_reference_mps=%.6f crouched_reference_mps=%.6f baseline_mps=%.6f flashback_multiplier=%.6f slowed_mps=%.6f recovered_mps=%.6f scripted_run_mps=%.6f run_mps=%.6f slowed_run_mps=%.6f crouched_mps=%.6f jump_height=%.6f airborne=%d descent=%d landing=%d backward_walk_mps=%.6f slowed_backward_walk_mps=%.6f recovered_backward_walk_mps=%.6f backward_run_mps=%.6f slowed_backward_run_mps=%.6f recovered_backward_run_mps=%.6f backward_crouch_mps=%.6f slowed_backward_crouch_mps=%.6f recovered_backward_crouch_mps=%.6f\n",
            referenceSpeed,referenceSpeeds[World::AnimationRunning],referenceSpeeds[World::AnimationCrouchedWalking],normalSpeed,
            flashbackMultiplier,slowSpeed,recoveredSpeed,scriptedRunSpeed,runSpeed,slowRunSpeed,crouchSpeed,maximumJumpHeight,
            int(sawAirborne),int(sawDescent),int(sawLanding),backwardWalkSpeed,slowedBackwardWalk,recoveredBackwardWalk,
            backwardRunSpeed,slowedBackwardRun,recoveredBackwardRun,backwardCrouchSpeed,slowedBackwardCrouch,recoveredBackwardCrouch);
        mark(role+"-player-walk-speed.txt",metrics);
    }
    const auto setLook = [&](float yaw, float pitch) {
        source.gameplay.forward[0] = -std::sin(yaw);
        source.gameplay.forward[1] = 0;
        source.gameplay.forward[2] = -std::cos(yaw);
        source.gameplay.pitch = pitch;
    };
    const auto angleDifference = [](float a, float b) { return std::atan2(std::sin(a-b),std::cos(a-b)); };
    source.renderYaw = source.yaw = 0; setLook(0,0); source.resetModelPose = true;
    fixture.UpdatePlayerModels(0);
    setLook(cMath::ToRad(20),0.3f);
    step(1.0f/60);
    require(visual.bodyYaw == 0 && visual.headYaw > 0 && visual.headYaw < cMath::ToRad(20) &&
        visual.headPitch > 0 && visual.headPitch < 0.3f, "stationary head aim snapped or immediately turned the body");
    for (int i = 0; i < 100; ++i) step(1.0f/60);
    require(visual.bodyYaw == 0 && !visual.turningBody && std::abs(visual.headYaw-cMath::ToRad(20)) < 0.0001f,
        "small stationary gaze changes escaped the body-turn dead zone");
    setLook(cMath::ToRad(34),0.3f);
    for (int i = 0; i < 90; ++i) step(1.0f/60);
    require(visual.bodyYaw == 0 && !visual.turningBody, "body following started before the 35-degree gaze threshold");
    setLook(cMath::ToRad(40),1.2f);
    step(1.0f/60);
    require(visual.turningBody && visual.bodyYaw > 0 && visual.bodyYaw <= kPif/60+0.00001f,
        "large stationary gaze failed to start a bounded body turn");
    // A much larger step may exceed the ordinary speed to prevent neck lag.
    setLook(cMath::ToRad(80),1.2f);
    for (int i = 0; i < 120; ++i) step(1.0f/60);
    require(!visual.turningBody && std::abs(angleDifference(visual.lookYaw,visual.bodyYaw)) < cMath::ToRad(5.01f),
        "idle body following did not settle inside its five-degree stopping threshold");
    const float stoppedBodyYaw = visual.bodyYaw;
    for (int i = 0; i < 30; ++i) step(1.0f/60);
    require(visual.bodyYaw == stoppedBodyYaw, "settled idle body continued drifting inside the stopping dead zone");
    for (float sign : {-1.0f,1.0f})
    {
        source.renderYaw = 0; setLook(sign*cMath::ToRad(120),sign*1.4f); source.resetModelPose = true;
        fixture.UpdatePlayerModels(0);
        require(std::abs(visual.headYaw-sign*cMath::ToRad(45)) < 0.00001f &&
            std::abs(visual.headPitch-sign*cMath::ToRad(50)) < 0.00001f,
            "head yaw or pitch escaped its anatomical clamp");
    }
    // Compare native bone matrices at exactly the same authored clip time.
    // Node3D's legacy pre-transform getters return its post-transform fields,
    // so validate the evaluated bone instead of relying on those accessors.
    for (int animation = 0; animation < World::AnimationCount; ++animation)
    {
        source.renderYaw = 0; setLook(cMath::ToRad(25),0.35f);
        source.gameplay.flags = onGround | (animation == World::AnimationRunning ? LuxWorldWire::PlayerRunning :
            animation == World::AnimationJumping ? LuxWorldWire::PlayerJumping :
            animation == World::AnimationCrouchedIdle || animation == World::AnimationCrouchedWalking ? LuxWorldWire::PlayerCrouching : 0);
        source.gameplay.velocity[0] = animation == World::AnimationWalking || animation == World::AnimationRunning ||
            animation == World::AnimationCrouchedWalking ? 1.0f : 0.0f;
        source.resetModelPose = true;
        fixture.UpdatePlayerModels(0);
        head->SetUsePreTransform(false); mesh->UpdateLogic(0);
        const cMatrixf authored = head->GetLocalMatrix();
        fixture.UpdatePlayerModels(0);
        const cMatrixf expected = cMath::MatrixMul(authored,cMath::MatrixMul(cMath::MatrixRotateY(visual.headYaw),cMath::MatrixRotateX(-visual.headPitch)));
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            require(std::abs(head->GetLocalMatrix().m[row][column]-expected.m[row][column]) < 0.00002f,
                "head aiming replaced the authored animation or rotated about the wrong pivot");
        require(clips[animation]->GetWeight() == 1 && nearVector(rootPosition(mesh,false),source.renderFeetPosition),
            "aiming changed the chosen animation or detached the body feet");
    }
    source.gameplay.flags = onGround; source.gameplay.velocity[0] = 0;
    source.renderYaw = 3.1f; setLook(-3.1f,0); source.resetModelPose = true;
    fixture.UpdatePlayerModels(0);
    require(std::abs(visual.headYaw) < 0.1f, "head aiming turned the long way across the plus/minus-pi boundary");
    const float wrappedLook = visual.lookYaw;
    setLook(3.0f,0); step(1.0f/60);
    require(std::abs(angleDifference(visual.lookYaw,wrappedLook)) < 0.1f,
        "smoothed gaze jumped while crossing the yaw wrap boundary");
    source.renderYaw = 0; setLook(0,0); source.resetModelPose = true;
    fixture.UpdatePlayerModels(0);
    source.gameplay.velocity[0] = 1; source.renderYaw = 1.2f; setLook(1.2f,0);
    step(1.0f/60);
    require(visual.bodyYaw > 0 && visual.bodyYaw < 1.2f, "moving body yaw snapped instead of following its reported heading");
    for (int i = 0; i < 120; ++i) step(1.0f/60);
    require(std::abs(angleDifference(visual.bodyYaw,source.renderYaw)) < 0.0001f,
        "moving body did not converge to the reported character yaw");
    // Hold network samples between packets and accumulate successive output
    // rotations, so a complete revolution cannot hide a wrong-way turn.
    tString turnSamples = "moving,smoothed_body_input,degrees_per_second,simulation_hz,packet_hz,direction,wrong_way_ticks,head_flips,max_look_lag_degrees,max_body_lag_degrees,stopped_body_gap_degrees\n";
    for (bool moving : {false,true}) for (bool smoothedBodyInput : {false,true})
    for (float degreesPerSecond : {360.0f,720.0f,1440.0f})
    for (int simulationHz : {60,120}) for (int packetHz : {20,30}) for (float direction : {-1.0f,1.0f})
    {
        const float dt = 1.0f/simulationHz;
        const int packetInterval = simulationHz/packetHz;
        const float angularSpeed = direction*cMath::ToRad(degreesPerSecond);
        source.renderYaw = source.yaw = 0; setLook(0,0);
        visual.moving = moving;
        fixture.UpdatePlayerModelAim(visual,source,0,true);
        float heldYaw = 0, filteredBodyYaw = 0;
        float previousLook = visual.lookYaw, previousBody = visual.bodyYaw, previousHead = visual.headYaw;
        float previousWorldHead = visual.bodyYaw+visual.headYaw;
        float maxLookLag = 0, maxBodyLag = 0;
        unsigned wrongWayTicks = 0, headFlips = 0;
        for (int tick = 1; tick <= 8*simulationHz; ++tick)
        {
            const float seconds = tick*dt;
            if (tick%packetInterval == 0)
            {
                // Four seconds forward, two back, then two stationary.
                const float travelledSeconds = seconds <= 4 ? seconds : seconds <= 6 ? 8-seconds : 2;
                heldYaw = angleDifference(angularSpeed*travelledSeconds,0);
                source.yaw = heldYaw;
                setLook(heldYaw,0);
            }
            if (smoothedBodyInput)
            {
                // Supply an independently smoothed heading within the upstream
                // contract. This exercises aim with body/look packet lag apart.
                filteredBodyYaw += angleDifference(heldYaw,filteredBodyYaw)*(std::min)(1.0f,15*dt);
                const float lag = angleDifference(heldYaw,filteredBodyYaw);
                filteredBodyYaw += lag-cMath::Clamp(lag,-cMath::ToRad(20),cMath::ToRad(20));
                source.renderYaw = angleDifference(filteredBodyYaw,0);
            }
            else source.renderYaw = heldYaw;
            fixture.UpdatePlayerModelAim(visual,source,dt,false);
            const float bodyStep = angleDifference(visual.bodyYaw,previousBody);
            const float lookStep = angleDifference(visual.lookYaw,previousLook);
            const float worldHead = visual.bodyYaw+visual.headYaw;
            const float worldHeadStep = angleDifference(worldHead,previousWorldHead);
            const float headStep = visual.headYaw-previousHead;
            const float expectedDirection = seconds <= 4 ? direction : seconds > 4.25f && seconds <= 6 ? -direction : 0;
            if (expectedDirection && (expectedDirection*bodyStep < -0.0001f ||
                expectedDirection*lookStep < -0.0001f || expectedDirection*worldHeadStep < -0.0001f)) ++wrongWayTicks;
            if (expectedDirection && std::abs(headStep) > cMath::ToRad(80)) ++headFlips;
            maxLookLag = (std::max)(maxLookLag,std::abs(angleDifference(heldYaw,visual.lookYaw)));
            maxBodyLag = (std::max)(maxBodyLag,std::abs(angleDifference(heldYaw,visual.bodyYaw)));
            require(std::isfinite(visual.bodyYaw) && std::isfinite(visual.lookYaw) && std::isfinite(visual.headYaw) &&
                std::abs(visual.headYaw) <= cMath::ToRad(45)+0.0001f, "rapid turning produced invalid or unclamped head aim");
            previousBody = visual.bodyYaw; previousLook = visual.lookYaw;
            previousHead = visual.headYaw; previousWorldHead = worldHead;
        }
        const float stoppedBodyGap = std::abs(angleDifference(heldYaw,visual.bodyYaw));
        const float settledBody = visual.bodyYaw;
        for (int tick = 0; tick < simulationHz; ++tick) fixture.UpdatePlayerModelAim(visual,source,dt,false);
        char turnFailure[192];
        std::snprintf(turnFailure,sizeof(turnFailure),"rapid turn regression: moving=%d smoothed=%d rate=%.0f sim=%d packets=%d direction=%.0f",
            int(moving),int(smoothedBodyInput),degreesPerSecond,simulationHz,packetHz,direction);
        require(wrongWayTicks == 0 && headFlips == 0 && maxLookLag <= cMath::ToRad(20)+0.001f &&
            maxBodyLag <= cMath::ToRad(65)+0.001f && stoppedBodyGap < cMath::ToRad(5.1f) &&
            std::abs(angleDifference(heldYaw,visual.lookYaw)) < 0.001f &&
            std::abs(angleDifference(settledBody,visual.bodyYaw)) < 0.001f,turnFailure);
        char turnSample[256];
        std::snprintf(turnSample,sizeof(turnSample),"%d,%d,%.0f,%d,%d,%.0f,%u,%u,%.6f,%.6f,%.6f\n",
            int(moving),int(smoothedBodyInput),degreesPerSecond,simulationHz,packetHz,direction,wrongWayTicks,headFlips,
            cMath::ToDeg(maxLookLag),cMath::ToDeg(maxBodyLag),cMath::ToDeg(stoppedBodyGap));
        turnSamples += turnSample;
    }
    mark(role+"-player-turns.csv",turnSamples);
    iEntity3D::CaptureInterpolationState();
    source.renderYaw = -2.3f; setLook(-2.0f,-1.2f); source.renderFeetPosition += cVector3f(3,0,0);
    source.resetModelPose = true;
    fixture.UpdatePlayerModels(0);
    require(std::abs(visual.bodyYaw+2.3f) < 0.00001f && std::abs(visual.lookYaw+2.0f) < 0.00001f &&
        std::abs(visual.headPitch+cMath::ToRad(50)) < 0.00001f, "teleport retained old smoothed body or head aim");
    iEntity3D::BeginRenderInterpolation(0);
    require(nearVector(head->GetRenderWorldPosition(),head->GetWorldPosition()) &&
        nearVector(mask->GetRenderWorldPosition(),mask->GetWorldPosition()), "teleport retained old head or mask render history");
    iEntity3D::EndRenderInterpolation();
    source.renderFeetPosition = initialFeet; source.renderYaw = source.yaw = 0; setLook(0,0);
    source.gameplay.velocity[0] = 0; source.gameplay.flags = onGround; source.resetModelPose = true;
    fixture.UpdatePlayerModels(0);
    const auto checkPose = [&](float yaw, bool render) {
        const cMatrixf matrix = render ? mesh->GetRenderWorldMatrix() : mesh->GetWorldMatrix();
        require(nearVector(cMath::MatrixMul3x3(matrix, cVector3f(0,0,-1)), cVector3f(-std::sin(yaw),0,-std::cos(yaw))),
            "player mesh forward does not match character body yaw");
        require(nearVector(cMath::MatrixMul3x3(matrix, cVector3f(0,1,0)), cVector3f(0,1,0)),
            "player model tilted or changed authored scale");
    };
    for (float yaw : {0.0f, 0.5f*kPif, kPif, -0.5f*kPif})
    {
        source.yaw = source.renderYaw = yaw;
        setLook(yaw,0); source.resetModelPose = true;
        fixture.UpdatePlayerModels();
        require(nearVector(rootPosition(mesh, false), initialFeet), "turning moved Armature_root away from the feet");
        checkPose(yaw, false);
    }
    // A real scene capture must interpolate the rig and its skin, with no
    // rotation of a nonzero bone-origin correction around the feet.
    source.yaw = source.renderYaw = 0;
    setLook(0,0);
    source.resetModelPose = true;
    fixture.UpdatePlayerModels();
    iEntity3D::BeginRenderInterpolation(1);
    rootPosition(mesh, true); mesh->GetRenderWorldMatrix();
    for (int i = 0; i < mesh->GetSubMeshEntityNum(); ++i) mesh->GetSubMeshEntity(i)->UpdateGraphicsForFrame(0);
    iEntity3D::EndRenderInterpolation();
    iEntity3D::CaptureInterpolationState();
    source.renderFeetPosition += cVector3f(1, 0, 0);
    source.renderYaw = 0.5f*kPif;
    setLook(source.renderYaw,0);
    fixture.UpdatePlayerModelAim(visual,source,0,true);
    fixture.UpdatePlayerModels();
    const cVector3f receivedPosition = source.position;
    const auto beforeRendering = pose();
    const float idleBeforeRendering = idle->GetTimePosition(), walkBeforeRendering = walking->GetTimePosition();
    for (float alpha : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        iEntity3D::BeginRenderInterpolation(alpha);
        require(nearVector(rootPosition(mesh, true), initialFeet+cVector3f(alpha,0,0)),
            "rendered rig root does not follow the interpolated feet");
        checkPose(alpha*0.5f*kPif, true);
        require(nearVector(mask->GetRenderWorldPosition(),cMath::MatrixMul(head->GetRenderWorldMatrix(),mask->GetLocalMatrix().GetTranslation())),
            "avatar mask detached from the interpolated head pose");
        for (int i = 0; i < mesh->GetSubMeshEntityNum(); ++i) mesh->GetSubMeshEntity(i)->UpdateGraphicsForFrame(1.0f/240);
        require(samePose(beforeRendering) && source.position == receivedPosition && idle->GetTimePosition() == idleBeforeRendering &&
            walking->GetTimePosition() == walkBeforeRendering, "rendering changed animation clocks, simulated bones or received network position");
        iEntity3D::EndRenderInterpolation();
    }
    // The cylinder height changes immediately, while the actual crouch pose
    // blends in over time without moving the feet or scaling the rig.
    source.position.y -= 0.4f; source.renderPosition.y -= 0.4f; source.size.y -= 0.8f;
    source.gameplay.flags |= LuxWorldWire::PlayerCrouching;
    fixture.UpdatePlayerModels();
    require(nearVector(rootPosition(mesh, false), source.renderFeetPosition) && samePose(beforeRendering), "crouching changed the model anchor or selected a different pose");
    for (int i = 0; i < 24; ++i) step(1.0f/60);
    require(crouchedIdle->GetWeight() == 1 && !samePose(beforeRendering), "crouching never blended into the authored crouched idle pose");
    checkPose(source.renderYaw, false);
    source.renderFeetPosition += cVector3f(20,3,7); source.resetModelPose = true;
    fixture.UpdatePlayerModels();
    iEntity3D::BeginRenderInterpolation(0);
    require(nearVector(rootPosition(mesh, true), source.renderFeetPosition), "teleport retained obsolete rig render history");
    iEntity3D::EndRenderInterpolation();
    source.gameplay.flags &= ~LuxWorldWire::PlayerAlive;
    fixture.UpdatePlayerModels();
    require(!world->GetDynamicMeshEntity(name(peer)), "dead peer retained its standing player model");
    require(!world->GetBillboard(maskName), "dead peer retained its avatar billboard");
    source.gameplay.flags = LuxWorldWire::PlayerAlive|LuxWorldWire::PlayerJumping;
    source.gameplay.velocity[1] = -3;
    ++source.gameplay.life;
    fixture.UpdatePlayerModels();
    require(world->GetDynamicMeshEntity(name(peer)) != NULL, "respawning did not recreate the player model");
    if (auto* respawned = world->GetDynamicMeshEntity(name(peer)))
    {
        require(respawned->GetAnimationStateFromName("jumping")->GetWeight() == 1,
            "a newly observed descending jump required an earlier local takeoff pose");
        require(!fixture.mPlayerModels[peer].avatarMask, "peer with no Steam avatar retained its previous-life synthetic mask");
        require(fixture.CreatePlayerAvatarMask(peer,fixture.mPlayerModels[peer],avatar), "fresh player life could not recreate its avatar mask");
    }
    source.age = 2.1f;
    fixture.UpdatePlayerModels();
    require(!world->GetDynamicMeshEntity(name(peer)), "stale peer retained its player model");
    require(!world->GetBillboard(maskName), "stale peer retained its avatar billboard");
    source.age = 0;
    fixture.UpdatePlayerModels();
    require(world->GetDynamicMeshEntity(name(peer)) != NULL, "fresh peer did not regain its player model");
    require(fixture.CreatePlayerAvatarMask(peer,fixture.mPlayerModels[peer],avatar), "fresh peer could not recreate its avatar mask");
    fixture.Reset();
    require(!world->GetDynamicMeshEntity(name(peer)) && fixture.mPlayerModels.empty(), "world reset retained a player model or presentation node");
    require(!world->GetBillboard(maskName), "world reset retained an avatar billboard");
    require(!fixture.mbPlayerStrideReferencesMeasured && fixture.mfPlayerStrideReferenceSpeeds == decltype(referenceSpeeds){},
        "world reset retained the previous map's walking calibration");
    require(countBodies() == originalBodies, "player visual lifecycle changed the world's physics bodies");

    // Exercise actual packet placement using the connected peer. A host's
    // forwarded pose has that client's own ID, which the client ignores.
    const uint32_t connected = replication->mPlayers.begin()->first;
    const auto saved = replication->mPlayers[connected];
    const auto savedTerror = replication->mEnemyTerror;
    const cMatrixf savedCenter = replication->mPlayerRenderNodes[connected]->GetLocalMatrix();
    cLuxMultiplayerRemotePlayer incoming = saved;
    incoming.position = cVector3f(100,-1000,100); incoming.size = cVector3f(0.6f,1.8f,0.6f);
    incoming.yaw = 3.1f; incoming.gameplay.flags = LuxWorldWire::PlayerAlive;
    const auto receive = [&]() {
        LuxWorldWire::Writer packet(LuxWorldWire::Pose, session->GetMapEpoch());
        packet.U32(connected); packet.U32(++incoming.sequence);
        for (float value : {incoming.position.x,incoming.position.y,incoming.position.z,incoming.size.x,incoming.size.y,incoming.size.z,incoming.yaw}) packet.F32(value);
        LuxWorldWire::WriteLantern(packet,incoming.lantern); LuxWorldWire::WritePlayerState(packet,incoming.gameplay);
        require(replication->HandleMessage(session->IsHost()?connected:0,packet.bytes), "valid player fixture pose was rejected");
    };
    receive();
    require(replication->mPlayers[connected].resetModelPose && nearVector(replication->mPlayers[connected].renderFeetPosition,
        incoming.position-cVector3f(0,incoming.size.y*0.5f,0)), "large network correction did not reset the player feet");
    replication->UpdatePlayerModels();
    const cVector3f standingFeet = replication->mPlayers[connected].renderFeetPosition;
    incoming.position.y -= 0.4f; incoming.size.y -= 0.8f; incoming.yaw = -3.1f;
    incoming.gameplay.flags |= LuxWorldWire::PlayerCrouching;
    receive();
    require(!replication->mPlayers[connected].resetModelPose && nearVector(replication->mPlayers[connected].renderFeetPosition,standingFeet),
        "routine crouch/yaw packet snapped or displaced the player feet");
    replication->mPlayers[connected].age = 2.1f;
    incoming.position.x += 1;
    receive();
    require(replication->mPlayers[connected].resetModelPose && nearVector(replication->mPlayers[connected].renderFeetPosition,
        incoming.position-cVector3f(0,incoming.size.y*0.5f,0)), "fresh pose after staleness retained old player history");
    replication->UpdatePlayerModels();
    ++incoming.gameplay.life; incoming.position.z += 1;
    receive();
    require(replication->mPlayers[connected].resetModelPose && nearVector(replication->mPlayers[connected].renderFeetPosition,
        incoming.position-cVector3f(0,incoming.size.y*0.5f,0)), "new player life retained the previous model location");
    replication->UpdatePlayerModels(0);
    // Check real packet branch alignment without advancing unrelated campaign,
    // contact or enemy simulation through the broad world Update method.
    for (float direction : {-1.0f,1.0f}) for (float packetDegrees : {12.0f,24.0f,48.0f,72.0f})
    {
        incoming.yaw = 3.1f;
        receive();
        for (int packet = 0; packet < 40; ++packet)
        {
            replication->mPlayers[connected].renderYaw = incoming.yaw-direction*cMath::ToRad(20);
            incoming.yaw = angleDifference(incoming.yaw+direction*cMath::ToRad(packetDegrees),0);
            incoming.gameplay.forward[0] = -std::sin(incoming.yaw);
            incoming.gameplay.forward[2] = -std::cos(incoming.yaw);
            receive();
            const auto& received = replication->mPlayers[connected];
            require(!received.resetModelPose && std::abs(received.yaw-received.renderYaw-
                direction*cMath::ToRad(20+packetDegrees)) < 0.0001f,
                "successive native yaw packets lost their turn branch across wraparound");
        }
    }
    replication->mPlayers[connected] = saved;
    replication->mPlayers[connected].resetModelPose = true;
    replication->mEnemyTerror = savedTerror;
    replication->mPlayerRenderNodes[connected]->SetMatrix(savedCenter);
    replication->mPlayerRenderNodes[connected]->ResetRenderInterpolation();
    replication->UpdatePlayerModels();
    replication->UpdatePlayerLights();
    replication->PrepareEnemyPlayers();
    gpBase->mpEngine->GetScene()->ResetInterpolationState();
    if (passed) mark(role+"-player-models.txt", "six locomotion clips, three measured strides, proportional native cadence, event slowdown/recovery, run/crouch/jump states, multiway crossfades, continuous loop clocks, head aim composition and clamps, idle body dead zones and yaw wrapping, moving body follow, optional avatar attachment/material/lifecycle, feet anchoring, render interpolation and physics-free teardown passed");
    return passed;
}

// Focused mode renders the real mesh against an uncluttered background before
// restoring the gameplay camera. The model uses ordinary world lighting/skinning.
class cPlayerModelScreenshot
{
    std::unique_ptr<cLuxMultiplayerWorld> fixture;
    cWorld* world = NULL;
    cViewport* viewport = NULL;
    cCamera* camera = NULL;
    cCamera* originalCamera = NULL;
    cPostEffectComposite* originalPostEffects = NULL;
    std::vector<cGuiSet*> originalGuiSets;
    uint32_t peer = UINT32_MAX;
    unsigned view = 0;
    unsigned rendered = 0;
    unsigned animationFrame = 0;
    bool captured = false;
    bool advanceFrame = true;
    tString animationSamples = "frame,seconds,state,flags,horizontal_speed,feet_height,idle_weight,idle_time,idle_rate,walking_weight,walking_time,walking_rate,running_weight,running_time,running_rate,jumping_weight,jumping_time,jumping_rate,crouched_idle_weight,crouched_idle_time,crouched_idle_rate,crouched_walking_weight,crouched_walking_time,crouched_walking_rate,body_yaw,look_yaw,head_yaw,head_pitch,gait_direction,moving_backward\n";
    iLight* light = NULL;
    cMeshEntity* avatarOccluder = NULL;
    unsigned visibleAvatarPixels = 0;
public:
    void Reset()
    {
        if (!fixture) return;
        viewport->SetCamera(originalCamera);
        viewport->SetPostEffectComposite(originalPostEffects);
        for (auto* guiSet : originalGuiSets) viewport->AddGuiSet(guiSet);
        originalGuiSets.clear();
        gpBase->mpEngine->GetScene()->DestroyCamera(camera); camera = NULL;
        world->DestroyLight(light); light = NULL;
        if (avatarOccluder) { world->DestroyMeshEntity(avatarOccluder); avatarOccluder = NULL; }
        fixture->Reset(); fixture.reset();
        gpBase->mpEngine->GetScene()->ResetInterpolationState();
    }
    int Update(tString& error)
    {
        if (!fixture)
        {
            auto* map = gpBase->mpMapHandler->GetCurrentMap();
            if (!map) { error = "player model screenshot lost its map"; return -1; }
            world = map->GetWorld();
            fixture.reset(new cLuxMultiplayerWorld(gpBase->mpMultiplayer));
            fixture->mpMap = map;
            while (world->GetDynamicMeshEntity("MultiplayerPlayerModel_"+cString::ToString((int)peer))) --peer;
            auto& source = fixture->mPlayers[peer];
            source.gameplay.flags = LuxWorldWire::PlayerAlive|LuxWorldWire::PlayerOnGround;
            source.position = source.renderPosition = cVector3f(0,-1000,0);
            source.size = cVector3f(0.6f,1.8f,0.6f);
            source.renderFeetPosition = cVector3f(0,-1000.9f,0);
            viewport = gpBase->mpMapHandler->GetViewport();
            originalCamera = viewport->GetCamera();
            originalPostEffects = viewport->GetPostEffectComposite();
            viewport->SetPostEffectComposite(NULL);
            auto guiSets = viewport->GetGuiSetIterator();
            while (guiSets.HasNext()) originalGuiSets.push_back(guiSets.Next());
            for (auto* guiSet : originalGuiSets) viewport->RemoveGuiSet(guiSet);
            camera = gpBase->mpEngine->GetScene()->CreateCamera(eCameraMoveMode_Fly);
            camera->SetFOV(0.9f); camera->SetAspect(originalCamera->GetAspect());
            camera->SetNearClipPlane(0.05f); camera->SetFarClipPlane(20);
            viewport->SetCamera(camera);
            light = world->CreateLightPoint("CodexPlayerModelScreenshot", "", false);
            light->SetPosition(cVector3f(-1,-998.7f,-2)); light->SetRadius(8);
            light->SetDiffuseColor(cColor(1.5f,1.5f,1.5f,1)); light->SetCastShadows(false); light->SetIsSaved(false);
        }
        if (captured)
        {
            captured = false;
            rendered = 0;
            advanceFrame = true;
            if (view < 2) ++view;
            else if (view == 2 && ++animationFrame == 792)
            {
                mark(role+"-player-animation-frames.csv", animationSamples);
                view = 3;
            }
            else if (view == 3) view = 4;
            else if (view == 4) { Reset(); return 1; }
        }
        auto& source = fixture->mPlayers[peer];
        if (advanceFrame)
        {
            source.yaw = source.renderYaw = view == 1 ? kPi2f : view == 2 ? 0.2f*kPif : 0;
            source.gameplay.forward[0] = -std::sin(source.renderYaw);
            source.gameplay.forward[1] = 0;
            source.gameplay.forward[2] = -std::cos(source.renderYaw);
            source.gameplay.pitch = 0;
            if (view < 2)
            {
                source.resetModelPose = true;
                fixture->UpdatePlayerModels();
            }
            else if (view == 2)
            {
                // The first twelve seconds cover all six clips. Ground movement
                // stays in place for comparison; jumping follows a visible body
                // arc so the pinned root and removed extra hip lift are visible.
                using World = cLuxMultiplayerWorld;
                source.gameplay.flags = LuxWorldWire::PlayerAlive|LuxWorldWire::PlayerOnGround;
                source.gameplay.velocity[0] = source.gameplay.velocity[1] = source.gameplay.velocity[2] = 0;
                source.renderFeetPosition = cVector3f(0,-1000.9f,0);
                const auto speed = [&](World::PlayerAnimation clip, float rate) {
                    const float magnitude = fixture->mfPlayerStrideReferenceSpeeds[clip]*rate;
                    source.gameplay.velocity[0] = -std::sin(source.yaw)*magnitude;
                    source.gameplay.velocity[2] = -std::cos(source.yaw)*magnitude;
                };
                if (animationFrame >= 24 && animationFrame < 60) speed(World::AnimationWalking,1);
                else if (animationFrame >= 60 && animationFrame < 96)
                {
                    source.gameplay.flags |= LuxWorldWire::PlayerRunning;
                    speed(World::AnimationRunning,0.7f);
                }
                else if (animationFrame >= 96 && animationFrame < 120) speed(World::AnimationWalking,0.5f);
                else if (animationFrame >= 120 && animationFrame < 180)
                {
                    source.gameplay.flags |= LuxWorldWire::PlayerCrouching;
                    if (animationFrame >= 144) speed(World::AnimationCrouchedWalking,0.8f);
                }
                else if ((animationFrame >= 196 && animationFrame < 220) || (animationFrame >= 252 && animationFrame < 276))
                {
                    const float flight = (animationFrame-(animationFrame < 220 ? 196 : 252)+1)/24.0f;
                    source.gameplay.flags = LuxWorldWire::PlayerAlive|LuxWorldWire::PlayerJumping;
                    source.gameplay.velocity[1] = std::cos(flight*kPif)*2.2f;
                    source.renderFeetPosition.y += std::sin(flight*kPif)*0.7f;
                }
                else if (animationFrame >= 220 && animationFrame < 240) speed(World::AnimationWalking,1);
                else if (animationFrame >= 240 && animationFrame < 252) source.gameplay.flags |= LuxWorldWire::PlayerCrouching;
                float gaze = source.renderYaw;
                if (animationFrame >= 288 && animationFrame < 312) { gaze += cMath::ToRad(25); source.gameplay.pitch = 0.3f; }
                else if (animationFrame >= 312 && animationFrame < 348) { gaze += cMath::ToRad(80); source.gameplay.pitch = 0.65f; }
                else if (animationFrame >= 348 && animationFrame < 384) { gaze -= cMath::ToRad(80); source.gameplay.pitch = -0.65f; }
                if (animationFrame >= 432 && animationFrame < 576)
                {
                    // One second spinning, one reversing, one stopped, first
                    // idle and then moving. Input advances30 degrees per frame.
                    const unsigned phase = (animationFrame-432)%72;
                    gaze += 4*kPif*(phase < 24 ? (phase+1)/24.0f : phase < 48 ? (47-phase)/24.0f : 0);
                    source.yaw = source.renderYaw = std::atan2(std::sin(gaze),std::cos(gaze));
                    if (animationFrame >= 504 && phase < 48) speed(World::AnimationWalking,1);
                }
                else if (animationFrame >= 576)
                {
                    // Each posture walks forward, reverses for1.5seconds, then
                    // returns forward without restarting any animation clock.
                    const unsigned gait = (animationFrame-576)/72;
                    const unsigned phase = (animationFrame-576)%72;
                    const float direction = phase >= 24 && phase < 60 ? -1.0f : 1.0f;
                    if (gait == 1) source.gameplay.flags |= LuxWorldWire::PlayerRunning;
                    else if (gait == 2) source.gameplay.flags |= LuxWorldWire::PlayerCrouching;
                    speed(gait == 2 ? World::AnimationCrouchedWalking : World::AnimationWalking,
                        direction*(gait == 1 ? 1.4f : 0.8f));
                }
                source.gameplay.forward[0] = -std::sin(gaze);
                source.gameplay.forward[2] = -std::cos(gaze);
                if (animationFrame == 0) source.resetModelPose = true;
                fixture->UpdatePlayerModels(1.0f/24);
                // Capture this exact sample independently of the harness's
                // presentation alpha or number of repeated rendered frames.
                fixture->mPlayerModels[peer].node->ResetRenderInterpolation();
            }
            else
            {
                source.gameplay.flags = LuxWorldWire::PlayerAlive|LuxWorldWire::PlayerOnGround;
                source.gameplay.velocity[0] = source.gameplay.velocity[1] = source.gameplay.velocity[2] = 0;
                source.renderFeetPosition = cVector3f(0,-1000.9f,0);
                source.resetModelPose = true;
                fixture->UpdatePlayerModels(0);
            }
            auto& visual = fixture->mPlayerModels[peer];
            if (!visual.avatarMask && !fixture->CreatePlayerAvatarMask(peer,visual,PlayerModelAvatarFixture()))
            { error = "avatar screenshot could not create its synthetic native mask"; return -1; }
            if (view == 4 && !avatarOccluder)
            {
                const tString material = cString::To8Char(visual.mesh->GetSubMeshEntity(0)->GetMaterial()->GetFullPath());
                auto* mesh = gpBase->mpEngine->GetGraphics()->GetMeshCreator()->CreateBox("CodexAvatarOccluder",cVector3f(0.7f,0.7f,0.08f),material);
                if (!mesh) { error = "avatar depth probe could not create its native occluder"; return -1; }
                mesh->IncUserCount();
                avatarOccluder = world->CreateMeshEntity("CodexAvatarOccluder",mesh,false);
                avatarOccluder->SetIsSaved(false);
                avatarOccluder->SetPosition(visual.avatarMask->GetWorldPosition()+cVector3f(0,0,-0.25f));
            }
            advanceFrame = false;
        }
        camera->SetPosition(view < 3 ? cVector3f(0,-999.9f,-3.4f) :
            fixture->mPlayerModels[peer].avatarMask->GetWorldPosition()+cVector3f(0,0,-1.05f));
        camera->SetYaw(kPif); camera->SetPitch(0); camera->SetRoll(0);
        camera->ResetInterpolation();
        return 0;
    }
    bool OnPostRender(tString& error)
    {
        if (!fixture || captured) return true;
        // Let the new camera and lighting settle before recording either angle.
        // Campaign post effects and GUI overlays are detached for this fixture.
        if (++rendered < (view != 2 || animationFrame == 0 ? 30u : 1u)) return true;
        cBitmap* bitmap = gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
        if (!bitmap) { error = "player model screenshot readback failed"; return false; }
        char suffix[96];
        if (view == 2) std::snprintf(suffix,sizeof(suffix),"-player-animation-%03u.png",animationFrame);
        else if (view < 2) std::snprintf(suffix,sizeof(suffix),"-player-model-%s.png",view ? "side" : "front");
        else std::snprintf(suffix,sizeof(suffix),"-player-avatar-%s.png",view == 3 ? "visible" : "occluded");
        const tString path = outputDir+"/"+role+suffix;
        auto* bitmapLoader = gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler();
        bool saved = bitmapLoader->SaveBitmap(bitmap,cString::To16Char(path),0);
        if (!saved)
        {
            // Bundled DevIL ilSaveF narrows the PNG byte count to ILboolean;
            // complete files whose size is divisible by 256 report failure.
            // Accept only an exact readback of this frame, never a partial PNG.
            auto* readback = bitmapLoader->LoadBitmap(cString::To16Char(path),0);
            saved = readback && readback->GetSize() == bitmap->GetSize() &&
                readback->GetPixelFormat() == bitmap->GetPixelFormat() &&
                readback->GetBytesPerPixel() == bitmap->GetBytesPerPixel();
            if (saved)
            {
                const size_t rowBytes = size_t(bitmap->GetWidth())*bitmap->GetBytesPerPixel();
                const unsigned char* expected = bitmap->GetData(0,0)->mpData;
                const unsigned char* actual = readback->GetData(0,0)->mpData;
                // Framebuffer rows start at the bottom; PNG loading starts at the top.
                for (int row = 0; saved && row < bitmap->GetHeight(); ++row)
                    saved = std::memcmp(actual+row*rowBytes,
                        expected+(bitmap->GetHeight()-1-row)*rowBytes,rowBytes) == 0;
            }
            if (readback) hplDelete(readback);
            if (saved) Log("Player capture verified exact PNG readback after DevIL save status failure: %s\n",path.c_str());
        }
        if (view >= 3)
        {
            unsigned counts[4] = {}; double x[4] = {}, y[4] = {};
            const int channels = bitmap->GetBytesPerPixel();
            const unsigned char* pixels = bitmap->GetData(0,0)->mpData;
            for (int row = 0; row < bitmap->GetHeight(); ++row) for (int column = 0; column < bitmap->GetWidth(); ++column)
            {
                const unsigned char* p = pixels+channels*(row*bitmap->GetWidth()+column);
                const int color = p[0]>180 && p[1]<90 && p[2]<90 ? 0 :
                    p[1]>180 && p[0]<90 && p[2]<100 ? 1 :
                    p[2]>180 && p[0]<90 && p[1]<130 ? 2 :
                    p[0]>180 && p[1]>180 && p[2]<90 ? 3 : -1;
                if (color >= 0) { ++counts[color]; x[color] += column; y[color] += row; }
            }
            const unsigned total = counts[0]+counts[1]+counts[2]+counts[3];
            bool valid = true;
            if (view == 3)
            {
                visibleAvatarPixels = total;
                for (int color = 0; color < 4; ++color) valid = valid && counts[color] > 25;
                if (valid)
                {
                    for (int color = 0; color < 4; ++color) { x[color] /= counts[color]; y[color] /= counts[color]; }
                    valid = x[0] < x[1] && x[2] < x[3] && y[0] > y[2] && y[1] > y[3];
                }
            }
            else valid = visibleAvatarPixels > 100 && total < (std::max)(8u,visibleAvatarPixels/100);
            char metrics[192];
            std::snprintf(metrics,sizeof(metrics),"red=%u green=%u blue=%u yellow=%u visible_total=%u\n",counts[0],counts[1],counts[2],counts[3],visibleAvatarPixels);
            mark(role+(view == 3 ? "-player-avatar-visible.txt" : "-player-avatar-occluded.txt"),metrics);
            if (!valid)
            {
                hplDelete(bitmap);
                error = view == 3 ? "native avatar mask is missing, mirrored or vertically inverted" : "native avatar mask rendered through opaque world geometry";
                return false;
            }
        }
        hplDelete(bitmap);
        if (!saved) { error = "player model screenshot save failed"; return false; }
        if (view == 2)
        {
            auto* mesh = fixture->mPlayerModels[peer].mesh;
            const char* names[] = {"idle","walking","running","jumping","crouched_idle","crouched_walking"};
            char sample[384];
            const auto& velocity = fixture->mPlayers[peer].gameplay.velocity;
            const float speed = std::sqrt(velocity[0]*velocity[0]+velocity[2]*velocity[2]);
            std::snprintf(sample,sizeof(sample),"%u,%.6f,%s,%u,%.6f,%.6f",animationFrame,animationFrame/24.0f,
                names[fixture->mPlayerModels[peer].animation],unsigned(fixture->mPlayers[peer].gameplay.flags),speed,
                fixture->mPlayers[peer].renderFeetPosition.y+1000.9f);
            animationSamples += sample;
            for (const char* name : names)
            {
                auto* clip = mesh->GetAnimationStateFromName(name);
                std::snprintf(sample,sizeof(sample),",%.6f,%.6f,%.6f",clip->GetWeight(),clip->GetTimePosition(),clip->GetSpeed()*clip->GetBaseSpeed());
                animationSamples += sample;
            }
            const auto& visual = fixture->mPlayerModels[peer];
            std::snprintf(sample,sizeof(sample),",%.6f,%.6f,%.6f,%.6f,%.6f,%u\n",visual.bodyYaw,visual.lookYaw,visual.headYaw,visual.headPitch,
                visual.gaitDirection,unsigned(visual.movingBackward));
            animationSamples += sample;
        }
        captured = true;
        return true;
    }
};

#endif
