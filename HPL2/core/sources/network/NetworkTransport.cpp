#include "network/NetworkTransport.h"

#ifdef HPL_USE_STEAMWORKS
#include "NetworkTransportSteam.inl"
#else

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <algorithm>
#include <map>
#include <utility>

namespace hpl
{
    const size_t cNetworkTransport::MaxMessageBytes;

    struct cNetworkTransport::cImpl
    {
        bool active = false;
        bool host = false;
        unsigned maxPeers = 0;
        uint32_t nextPeer = 1;
        HSteamListenSocket listen = k_HSteamListenSocket_Invalid;
        HSteamNetPollGroup group = k_HSteamNetPollGroup_Invalid;
        std::map<uint32_t, HSteamNetConnection> peers;
        std::vector<cNetworkEvent> pending;

        static unsigned users;
        static std::map<HSteamListenSocket, cImpl*> listeners;
        static std::map<HSteamNetConnection, cImpl*> connections;

        bool Init(std::string& error)
        {
            if(users == 0)
            {
                SteamDatagramErrMsg detail;
                if(!GameNetworkingSockets_Init(nullptr, detail))
                {
                    error = std::string("GameNetworkingSockets initialization failed: ") + detail;
                    return false;
                }
            }
            ++users;
            active = true;
            group = SteamNetworkingSockets()->CreatePollGroup();
            if(group == k_HSteamNetPollGroup_Invalid)
            {
                error = "Unable to create the networking receive group.";
                return false;
            }
            return true;
        }

        static int Options(SteamNetworkingConfigValue_t* options)
        {
            options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(StatusChanged));
            options[1].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, 15000);
            options[2].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, 15000);
            options[3].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, 4 * 1024 * 1024);
            options[4].SetInt32(k_ESteamNetworkingConfig_RecvBufferSize, 4 * 1024 * 1024);
            options[5].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages, 1024);
            options[6].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize, static_cast<int>(MaxMessageBytes));
            // Direct-IP sessions use encrypted, self-signed connections without Steam authentication.
            options[7].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
            return 8;
        }

        uint32_t PeerFor(HSteamNetConnection connection) const
        {
            for(const auto& peer : peers)
                if(peer.second == connection) return peer.first;
            return UINT32_MAX;
        }

        void Queue(eNetworkEventType type, uint32_t peer, const std::string& reason = std::string())
        {
            cNetworkEvent event;
            event.type = type;
            event.peer = peer;
            event.reason = reason;
            pending.push_back(std::move(event));
        }

        static void StatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
        {
            auto found = connections.find(info->m_hConn);
            cImpl* owner = found == connections.end() ? nullptr : found->second;
            if(!owner && info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid)
            {
                const auto listener = listeners.find(info->m_info.m_hListenSocket);
                if(listener != listeners.end()) owner = listener->second;
            }
            if(!owner) return;
            ISteamNetworkingSockets* api = SteamNetworkingSockets();
            if(info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting && owner->host)
            {
                // Count pending handshakes as well as established connections.
                if(owner->peers.size() >= owner->maxPeers || owner->nextPeer == UINT32_MAX)
                {
                    api->CloseConnection(info->m_hConn, 1001, "The multiplayer session is full.", false);
                    return;
                }
                if(api->AcceptConnection(info->m_hConn) != k_EResultOK ||
                   !api->SetConnectionPollGroup(info->m_hConn, owner->group))
                {
                    api->CloseConnection(info->m_hConn, 1002, "Unable to accept the connection.", false);
                    return;
                }
                owner->peers[owner->nextPeer++] = info->m_hConn;
                connections[info->m_hConn] = owner;
            }
            else if(info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected)
            {
                const uint32_t peer = owner->PeerFor(info->m_hConn);
                if(peer != UINT32_MAX) owner->Queue(eNetworkEventType::Connected, peer);
            }
            else if(info->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                    info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
            {
                const uint32_t peer = owner->PeerFor(info->m_hConn);
                if(peer != UINT32_MAX)
                {
                    owner->Queue(eNetworkEventType::Disconnected, peer, info->m_info.m_szEndDebug);
                    owner->peers.erase(peer);
                }
                connections.erase(info->m_hConn);
                api->CloseConnection(info->m_hConn, 0, nullptr, false);
            }
        }
    };

    unsigned cNetworkTransport::cImpl::users = 0;
    std::map<HSteamListenSocket, cNetworkTransport::cImpl*> cNetworkTransport::cImpl::listeners;
    std::map<HSteamNetConnection, cNetworkTransport::cImpl*> cNetworkTransport::cImpl::connections;

    cNetworkTransport::cNetworkTransport() : mpImpl(new cImpl) {}
    cNetworkTransport::~cNetworkTransport() { Stop(); delete mpImpl; }

    bool cNetworkTransport::Host(uint16_t port, unsigned maxPeers, std::string& error)
    {
        Stop();
        error.clear();
        if(port == 0 || maxPeers == 0 || maxPeers > 63)
        {
            error = "Use a port from 1 to 65535 and 1 to 63 remote players.";
            return false;
        }
        if(!mpImpl->Init(error)) { Stop(); return false; }
        mpImpl->host = true;
        mpImpl->maxPeers = maxPeers;
        SteamNetworkingIPAddr address;
        address.Clear();
        address.m_port = port;
        SteamNetworkingConfigValue_t options[8];
        const int count = cImpl::Options(options);
        mpImpl->listen = SteamNetworkingSockets()->CreateListenSocketIP(address, count, options);
        if(mpImpl->listen == k_HSteamListenSocket_Invalid)
        {
            error = "Unable to listen on UDP port " + std::to_string(port) + ". It may already be in use.";
            Stop();
            return false;
        }
        cImpl::listeners[mpImpl->listen] = mpImpl;
        return true;
    }

    static bool ResolveAddress(const std::string& text, uint16_t defaultPort, SteamNetworkingIPAddr& result)
    {
        if(text.empty() || text.size() > 255) return false;
        result.Clear();
        if(result.ParseString(text.c_str()))
        {
            if(result.m_port == 0) result.m_port = defaultPort;
            return result.m_port != 0;
        }
        std::string hostname = text;
        unsigned port = defaultPort;
        const size_t colon = text.find(':');
        if(colon != std::string::npos)
        {
            if(text.find(':', colon + 1) != std::string::npos || colon + 1 == text.size()) return false;
            hostname = text.substr(0, colon);
            port = 0;
            for(size_t i = colon + 1; i < text.size(); ++i)
            {
                if(text[i] < '0' || text[i] > '9') return false;
                port = port * 10 + text[i] - '0';
                if(port > 65535) return false;
            }
        }
        if(port == 0 || hostname.empty()) return false;
        addrinfo hints = {};
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_family = AF_INET;
        addrinfo* addresses = nullptr;
        if(getaddrinfo(hostname.c_str(), nullptr, &hints, &addresses) != 0) return false;
        const sockaddr_in* address = reinterpret_cast<const sockaddr_in*>(addresses->ai_addr);
        result.SetIPv4(ntohl(address->sin_addr.s_addr), static_cast<uint16_t>(port));
        freeaddrinfo(addresses);
        return true;
    }

    bool cNetworkTransport::Join(const std::string& addressText, uint16_t defaultPort, std::string& error)
    {
        Stop();
        error.clear();
        // Initialization also starts Winsock, needed for hostname resolution.
        if(!mpImpl->Init(error)) { Stop(); return false; }
        SteamNetworkingIPAddr address;
        if(!ResolveAddress(addressText, defaultPort, address))
        {
            error = "Invalid or unresolved host. Enter an IP address or hostname, optionally followed by :port.";
            Stop();
            return false;
        }
        SteamNetworkingConfigValue_t options[8];
        const int count = cImpl::Options(options);
        const HSteamNetConnection connection = SteamNetworkingSockets()->ConnectByIPAddress(address, count, options);
        if(connection == k_HSteamNetConnection_Invalid)
        {
            error = "Unable to start a connection to the host.";
            Stop();
            return false;
        }
        mpImpl->peers[0] = connection;
        cImpl::connections[connection] = mpImpl;
        if(!SteamNetworkingSockets()->SetConnectionPollGroup(connection, mpImpl->group))
        {
            error = "Unable to receive messages from the host.";
            Stop();
            return false;
        }
        return true;
    }

    void cNetworkTransport::Stop()
    {
        if(mpImpl->active)
        {
            ISteamNetworkingSockets* api = SteamNetworkingSockets();
            for(const auto& peer : mpImpl->peers)
            {
                cImpl::connections.erase(peer.second);
                api->CloseConnection(peer.second, 1000, mpImpl->host ? "The host ended the session." : "The player disconnected.", false);
            }
            if(mpImpl->listen != k_HSteamListenSocket_Invalid)
            {
                cImpl::listeners.erase(mpImpl->listen);
                api->CloseListenSocket(mpImpl->listen);
            }
            if(mpImpl->group != k_HSteamNetPollGroup_Invalid) api->DestroyPollGroup(mpImpl->group);
            if(--cImpl::users == 0) GameNetworkingSockets_Kill();
        }
        *mpImpl = cImpl();
    }

    void cNetworkTransport::Poll(std::vector<cNetworkEvent>& events)
    {
        if(!mpImpl->active) return;
        ISteamNetworkingSockets* api = SteamNetworkingSockets();
        api->RunCallbacks();
        for(auto& event : mpImpl->pending) events.push_back(std::move(event));
        mpImpl->pending.clear();
        // Bound work per frame so a sender cannot starve game updates.
        size_t received = 0;
        for(unsigned i = 0; i < 512 && received < 4 * 1024 * 1024; ++i)
        {
            SteamNetworkingMessage_t* message = nullptr;
            if(api->ReceiveMessagesOnPollGroup(mpImpl->group, &message, 1) <= 0) break;
            const uint32_t peer = mpImpl->PeerFor(message->m_conn);
            if(peer != UINT32_MAX && message->m_cbSize >= 0 && static_cast<size_t>(message->m_cbSize) <= MaxMessageBytes)
            {
                cNetworkEvent event;
                event.type = eNetworkEventType::Message;
                event.peer = peer;
                const uint8_t* bytes = static_cast<const uint8_t*>(message->m_pData);
                if(message->m_cbSize) event.data.assign(bytes, bytes + message->m_cbSize);
                received += event.data.size();
                events.push_back(std::move(event));
            }
            message->Release();
        }
    }

    bool cNetworkTransport::Send(uint32_t peer, const std::vector<uint8_t>& data, bool reliable)
    {
        if(!mpImpl->active || data.size() > MaxMessageBytes) return false;
        const auto found = mpImpl->peers.find(peer);
        if(found == mpImpl->peers.end()) return false;
        const int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay;
        return SteamNetworkingSockets()->SendMessageToConnection(found->second, data.data(),
            static_cast<uint32_t>(data.size()), flags, nullptr) == k_EResultOK;
    }

    void cNetworkTransport::Disconnect(uint32_t peer, const std::string& reason)
    {
        const auto found = mpImpl->peers.find(peer);
        if(found == mpImpl->peers.end()) return;
        SteamNetworkingSockets()->CloseConnection(found->second, 1000, reason.substr(0, 127).c_str(), false);
        cImpl::connections.erase(found->second);
        mpImpl->peers.erase(found);
        mpImpl->Queue(eNetworkEventType::Disconnected, peer, reason);
    }

    bool cNetworkTransport::IsHost() const { return mpImpl->active && mpImpl->host; }
    bool cNetworkTransport::IsActive() const { return mpImpl->active; }

    bool cNetworkTransport::InitializeSteam(std::string& error)
    { error = "This build uses standalone networking. Build with Steamworks enabled to use Steam lobbies."; return false; }
    void cNetworkTransport::ShutdownSteam() {}
    bool cNetworkTransport::SteamAvailable() { return false; }
    bool cNetworkTransport::SteamOverlayActive() { return false; }
    std::string cNetworkTransport::SteamStatus()
    { return "Steamworks is not enabled in this build; direct IP networking is available."; }
    bool cNetworkTransport::HostSteam(unsigned, bool, const std::string&, std::string& error)
    { return InitializeSteam(error); }
    bool cNetworkTransport::JoinSteamLobby(uint64_t, std::string& error)
    { return InitializeSteam(error); }
    bool cNetworkTransport::IsSteamSession() const { return false; }
    uint64_t cNetworkTransport::GetSteamLobbyID() const { return 0; }
    void cNetworkTransport::SetSteamMapName(const std::string&) {}
    bool cNetworkTransport::RequestSteamLobbies(std::string& error) { return InitializeSteam(error); }
    bool cNetworkTransport::IsSteamLobbySearchPending() const { return false; }
    const std::vector<cSteamLobbyInfo>& cNetworkTransport::GetSteamLobbies() const
    { static const std::vector<cSteamLobbyInfo> empty; return empty; }
    uint64_t cNetworkTransport::ConsumeSteamJoinRequest() { return 0; }
    bool cNetworkTransport::InviteSteamFriends(std::string& error) { return InitializeSteam(error); }
}
#endif
