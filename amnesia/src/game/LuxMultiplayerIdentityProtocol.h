#ifndef LUX_MULTIPLAYER_IDENTITY_PROTOCOL_H
#define LUX_MULTIPLAYER_IDENTITY_PROTOCOL_H
#include "LuxMultiplayerProtocol.h"
#include <map>
#include <set>

namespace luxnet {
typedef std::map<uint32_t, uint64_t> PeerSteamIdentities;
inline std::vector<uint8_t> WritePlayerIdentities(const PeerSteamIdentities& identities) {
    Writer w(PlayerIdentities);w.U8(static_cast<uint8_t>(identities.size()));
    for(const auto& entry:identities) {
        w.U32(entry.first);w.U32(static_cast<uint32_t>(entry.second));w.U32(static_cast<uint32_t>(entry.second>>32));
    }
    return w.data;
}
inline bool ReadPlayerIdentities(Reader& r,PeerSteamIdentities& identities) {
    const uint8_t count=r.U8();
    if(!count || count>16) return false;
    PeerSteamIdentities decoded;std::set<uint64_t> accounts;
    for(uint8_t i=0;i<count;++i) {
        const uint32_t peer=r.U32(),low=r.U32(),high=r.U32();
        const uint64_t account=static_cast<uint64_t>(low)|(static_cast<uint64_t>(high)<<32);
        if(peer==UINT32_MAX || !account || !decoded.emplace(peer,account).second || !accounts.insert(account).second) return false;
    }
    if(!r.Done() || !decoded.count(0)) return false;
    identities.swap(decoded);return true;
}
}
#endif
