#include "LuxEnemyPlayer.h"
#include "LuxPlayer.h"
#include "LuxPlayerHelpers.h"
#include "LuxMoveState_Normal.h"
#include "LuxMultiplayer.h"
#include "LuxMultiplayerWorld.h"

cLuxEnemyPlayer cLuxEnemyPlayer::Local(uint32_t peer)
{
    cLuxEnemyPlayer sample;
    sample.peer = peer;
    if(!gpBase || !gpBase->mpPlayer) return sample;
    cLuxPlayer* player = gpBase->mpPlayer;
    if(gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsActive())
        sample.life=gpBase->mpMultiplayer->GetWorld()->GetLocalPlayerLife();
    sample.body = player->GetCharacterBody();
    if(!sample.body) return sample;
    sample.position = sample.body->GetPosition();
    sample.feet = sample.body->GetFeetPosition();
    sample.size = sample.body->GetSize();
    sample.velocity = sample.body->GetVelocity(gpBase->mpEngine->GetStepSize());
    // Ground contact has a short grace period after takeoff. The force
    // velocity distinguishes a rising jump from native stair stepping.
    sample.onGround = sample.body->IsOnGround() && sample.body->GetForceVelocity().y <= 0.1f;
    cCamera* camera = player->GetCamera();
    sample.eyes = camera->GetPosition();
    sample.forward = camera->GetForward();
    sample.yaw = camera->GetYaw(); sample.pitch = camera->GetPitch();
    sample.fov = camera->GetFOV(); sample.aspect = camera->GetAspect();
    sample.alive = !player->IsDead();
    sample.health = player->GetHealth();
    sample.protectedFromEnemies = player->GetHelperFlashback()->IsActive() || player->GetHelperDeath()->IsActive();
    sample.lantern = player->GetHelperLantern()->IsActive();
    sample.speed = player->GetAvgSpeed();
    sample.lightLevel = player->GetHelperLightLevel()->GetNormalLightLevel();
    sample.terror = player->GetTerror();
    if(player->GetCurrentMoveState() == eLuxMoveState_Normal)
    {
        auto* movement = static_cast<cLuxMoveState_Normal*>(player->GetCurrentMoveStateData());
        sample.crouching = movement->IsCrouching();
        sample.running = movement->IsRunning();
        sample.jumping = movement->IsJumping();
    }
    return sample;
}
