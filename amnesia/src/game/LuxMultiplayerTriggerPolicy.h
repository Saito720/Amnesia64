#ifndef LUX_MULTIPLAYER_TRIGGER_POLICY_H
#define LUX_MULTIPLAYER_TRIGGER_POLICY_H

#include <cstdint>

namespace luxnet {
// Runtime-only script identity. Peer numbers can be reused after rehosting, so
// remembered callbacks also carry the session which gave that number meaning.
// A stale origin stays remote but resolves to no peer, never to the host or a
// new player that happened to receive the same peer number.
struct PlayerScriptOrigin {
    bool remote=false;
    uint32_t peer=UINT32_MAX;
    uint64_t session=0;
    uint32_t PeerInSession(uint64_t currentSession) const {
        return session==currentSession ? peer : UINT32_MAX;
    }
};
// Player callbacks observe the union of local and remote occupancy.
// A leave event belongs to the final occupied sample, even though its peer no
// longer overlaps. UINT32_MAX means there is no current remote occupant.
inline PlayerScriptOrigin UpdatePlayerTriggerOrigin(bool local, uint32_t remote, uint64_t session,
    PlayerScriptOrigin& previousRemote) {
    const PlayerScriptOrigin current = !local && remote!=UINT32_MAX ?
        PlayerScriptOrigin{true,remote,session} : PlayerScriptOrigin{};
    const PlayerScriptOrigin origin = local || remote != UINT32_MAX ? current : previousRemote;
    previousRemote = current;
    return origin;
}
}

#endif
