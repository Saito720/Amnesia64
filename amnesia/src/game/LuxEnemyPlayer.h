#ifndef LUX_ENEMY_PLAYER_H
#define LUX_ENEMY_PLAYER_H

#include "LuxBase.h"
#include <cstdint>

// A gameplay sample, independent of render interpolation and player-player
// collision. Remote character bodies are query proxies, never controllers.
struct cLuxEnemyPlayer
{
    uint32_t peer = 0;
    uint32_t life = 1;
    cVector3f position = 0, feet = 0, eyes = 0, size = 0, velocity = 0, forward = cVector3f(0,0,-1);
    float yaw = 0, pitch = 0, fov = 1.2f, aspect = 4.0f/3.0f;
    float speed = 0, lightLevel = 1, terror = 0, health = 0;
    bool alive = false, crouching = false, lantern = false, protectedFromEnemies = false;
    iCharacterBody* body = NULL;

    static cLuxEnemyPlayer Local(uint32_t peer = 0);
    bool Eligible() const { return alive && !protectedFromEnemies && body != NULL; }
};

#endif
