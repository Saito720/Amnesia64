#ifndef LUX_MULTIPLAYER_TRIGGER_GEOMETRY_H
#define LUX_MULTIPLAYER_TRIGGER_GEOMETRY_H

#include "physics/PhysicsWorld.h"
#include "physics/PhysicsBody.h"
#include "math/Math.h"
#include "math/BoundingVolume.h"

// Use the same narrow-phase query for the local character and the remote
// character proxy; an area's world AABB alone includes space outside rotated
// or otherwise non-box-shaped triggers.
inline bool LuxPlayerBodyTouches(hpl::iPhysicsWorld* world, hpl::iPhysicsBody* player, hpl::iPhysicsBody* area)
{
    if(!hpl::cMath::CheckBVIntersection(*player->GetBoundingVolume(),*area->GetBoundingVolume())) return false;
    hpl::cCollideData collision;
    collision.SetMaxSize(1);
    return world->CheckShapeCollision(player->GetShape(),player->GetLocalMatrix(),
        area->GetShape(),area->GetLocalMatrix(),collision,1,false);
}

#endif
