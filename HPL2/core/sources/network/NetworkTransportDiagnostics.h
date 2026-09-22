#ifndef HPL_NETWORK_TRANSPORT_DIAGNOSTICS_H
#define HPL_NETWORK_TRANSPORT_DIAGNOSTICS_H

// Included after the selected networking SDK. Never log addresses, authenticated
// identities, or message payloads; numeric connection/peer IDs are process-local.
#include <cstdio>
#include <string>

namespace hpl { namespace network_detail {
inline std::string ConnectionStateText(ESteamNetworkingConnectionState state)
{
    const char* name = "Unknown";
    switch(state)
    {
    case k_ESteamNetworkingConnectionState_None: name = "None"; break;
    case k_ESteamNetworkingConnectionState_Connecting: name = "Connecting"; break;
    case k_ESteamNetworkingConnectionState_FindingRoute: name = "FindingRoute"; break;
    case k_ESteamNetworkingConnectionState_Connected: name = "Connected"; break;
    case k_ESteamNetworkingConnectionState_ClosedByPeer: name = "ClosedByPeer"; break;
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: name = "ProblemDetectedLocally"; break;
    default: break;
    }
    return std::string(name) + "(" + std::to_string(static_cast<int>(state)) + ")";
}
inline std::string ConnectionText(HSteamNetConnection connection, uint32_t peer)
{
    return "connection=" + std::to_string(connection) + " peer=" +
        (peer == UINT32_MAX ? "unknown" : std::to_string(peer));
}
inline std::string ConnectionHealthText(ISteamNetworkingSockets* api, HSteamNetConnection connection)
{
    SteamNetConnectionRealTimeStatus_t status = {};
    const EResult result = api->GetConnectionRealTimeStatus(connection, &status, 0, nullptr);
    if(result != k_EResultOK) return " health_unavailable=" + std::to_string(static_cast<int>(result));
    char text[256];
    std::snprintf(text, sizeof(text),
        " state=%d ping_ms=%d quality_local=%.3f quality_remote=%.3f pending_reliable=%d pending_unreliable=%d unacked_reliable=%d",
        static_cast<int>(status.m_eState), status.m_nPing, status.m_flConnectionQualityLocal,
        status.m_flConnectionQualityRemote, status.m_cbPendingReliable, status.m_cbPendingUnreliable,
        status.m_cbSentUnackedReliable);
    return text;
}
inline std::string DiagnosticText(const char* value, size_t limit)
{
    std::string result;
    if(!value) return result;
    for(size_t i = 0; i < limit && value[i]; ++i)
        result += static_cast<unsigned char>(value[i]) < 32 ? ' ' : value[i];
    return result;
}
} }
#endif
