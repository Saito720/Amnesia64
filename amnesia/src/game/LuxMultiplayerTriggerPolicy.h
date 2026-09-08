#ifndef LUX_MULTIPLAYER_TRIGGER_POLICY_H
#define LUX_MULTIPLAYER_TRIGGER_POLICY_H

namespace luxnet {
// Player callbacks observe the union of local and remote occupancy. A leave
// callback belongs to the last occupied sample, since nobody overlaps anymore.
inline bool UpdatePlayerTriggerOrigin(bool local, bool remote, bool& previousRemoteOnly) {
    const bool remoteOnly = !local && remote;
    const bool origin = local || remote ? remoteOnly : previousRemoteOnly;
    previousRemoteOnly = remoteOnly;
    return origin;
}
}

#endif
