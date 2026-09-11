#ifndef HPL_NETWORK_TRANSPORT_H
#define HPL_NETWORK_TRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hpl
{
    enum class eNetworkEventType { Connected, Disconnected, Message, SessionReady, SessionFailed };

    struct cSteamLobbyInfo
    {
        uint64_t id = 0;
        std::string name, map;
        unsigned players = 0, maxPlayers = 0;
    };

    struct cNetworkEvent
    {
        eNetworkEventType type;
        uint32_t peer;
        std::vector<uint8_t> data;
        std::string reason;
    };

    // Main-thread-only transport. HPL_USE_STEAMWORKS selects Steam's runtime;
    // standalone builds retain direct IP without a Steam account or runtime.
    // Clients address their host as peer 0; host-side peer IDs start at 1.
    // Poll appends events. Reliable traffic must be chunked below MaxMessageBytes.
    class cNetworkTransport
    {
    public:
        static const size_t MaxMessageBytes = 64 * 1024;
        // Initialize before creating the graphics window for Steam overlay hooks.
        // Shutdown after all game objects/transports have been destroyed.
        static bool InitializeSteam(std::string& error);
        static void ShutdownSteam();
        static bool SteamAvailable();
        static bool SteamOverlayActive();
        static std::string SteamStatus();
        cNetworkTransport();
        ~cNetworkTransport();
        bool Host(uint16_t port, unsigned maxPeers, std::string& error);
        bool Join(const std::string& address, uint16_t defaultPort, std::string& error);
        // Lobby operations complete asynchronously through SessionReady/Failed.
        // maxPeers excludes the host. Steam sessions keep their original owner;
        // host migration is intentionally unsupported.
        bool HostSteam(unsigned maxPeers, bool publicLobby, const std::string& map, std::string& error);
        bool JoinSteamLobby(uint64_t lobby, std::string& error);
        bool IsSteamSession() const;
        uint64_t GetSteamLobbyID() const;
        void SetSteamMapName(const std::string& map);
        bool RequestSteamLobbies(std::string& error);
        bool IsSteamLobbySearchPending() const;
        const std::vector<cSteamLobbyInfo>& GetSteamLobbies() const;
        uint64_t ConsumeSteamJoinRequest();
        bool InviteSteamFriends(std::string& error);
        void Stop();
        void Poll(std::vector<cNetworkEvent>& events);
        bool Send(uint32_t peer, const std::vector<uint8_t>& data, bool reliable);
        // Promptly packetize queued traffic before a blocking map load. This
        // does not wait for delivery or change reliable ordering.
        void Flush(uint32_t peer);
        void Disconnect(uint32_t peer, const std::string& reason);
        bool IsHost() const;
        bool IsActive() const;

    private:
        cNetworkTransport(const cNetworkTransport&) = delete;
        cNetworkTransport& operator=(const cNetworkTransport&) = delete;
        struct cImpl;
        cImpl* mpImpl;
    };
}
#endif
