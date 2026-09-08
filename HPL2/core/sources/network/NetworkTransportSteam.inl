// Included only by NetworkTransport.cpp. This backend links steam_api, never the
// standalone GameNetworkingSockets runtime: their similarly named APIs are not
// interchangeable and must not coexist in a process.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#include <steam/steam_api.h>
#include "NetworkSteamValidation.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <utility>

#ifndef HPL_STEAM_APP_ID
#error Steamworks builds must define HPL_STEAM_APP_ID from the repository steam_appid.txt
#endif

namespace hpl
{
    const size_t cNetworkTransport::MaxMessageBytes;
    namespace
    {
        typedef std::chrono::steady_clock cSteamClock;
        bool gSteamInitialized = false;
        bool gSteamPumping = false;
        bool gSteamOverlayActive = false;
        std::string gSteamStatus;
        uint64_t gSteamJoinRequest = 0;

        // Development runs use retail Amnesia as their working directory, which
        // may contain another game's steam_appid.txt. Select only a verified file
        // beside this executable during Init; never modify the retail directory.
        // Production installs omit this file and use Steam's launch context.
        struct cSteamDevelopmentContext
        {
#ifdef _WIN32
            std::wstring oldDirectory, oldApp, oldGame;
#else
            std::string oldDirectory, oldApp, oldGame;
#endif
            bool changed = false, hadApp = false, hadGame = false;
            bool valid = true;
            std::string error;
            cSteamDevelopmentContext()
            {
                FILE* file = nullptr;
#ifdef _WIN32
                wchar_t executable[32768] = {};
                const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
                if(length == 0 || length >= 32768) return;
                std::wstring directory(executable, length);
                const size_t slash = directory.find_last_of(L"/\\");
                if(slash == std::wstring::npos) return;
                directory.resize(slash);
                _wfopen_s(&file, (directory + L"/steam_appid.txt").c_str(), L"rb");
#else
                char executable[4096] = {};
#ifdef __APPLE__
                uint32_t length = sizeof(executable);
                if(_NSGetExecutablePath(executable, &length) != 0) return;
#else
                const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
                if(length <= 0 || static_cast<size_t>(length) >= sizeof(executable) - 1) return;
                executable[length] = 0;
#endif
                std::string directory(executable);
                const size_t slash = directory.find_last_of('/');
                if(slash == std::string::npos) return;
                directory.resize(slash);
                file = fopen((directory + "/steam_appid.txt").c_str(), "rb");
#endif
                if(!file) return;
                char bytes[64] = {};
                const size_t count = fread(bytes, 1, sizeof(bytes), file);
                fclose(file);
                std::string id(bytes, count);
                while(!id.empty() && (id.back() == '\r' || id.back() == '\n' || id.back() == ' ' || id.back() == '\t')) id.pop_back();
                uint64_t parsed = 0;
                if(!steam_detail::ParseId(id, parsed) || parsed != HPL_STEAM_APP_ID)
                {
                    valid = false;
                    error = "The steam_appid.txt beside this executable does not match this build's App ID " + std::to_string(HPL_STEAM_APP_ID) + ".";
                    return;
                }
#ifdef _WIN32
                wchar_t current[32768] = {};
                const DWORD cwdLength = GetCurrentDirectoryW(32768, current);
                if(cwdLength == 0 || cwdLength >= 32768) { valid = false; error = "Unable to preserve the game's working directory for Steam initialization."; return; }
                oldDirectory.assign(current, cwdLength);
                wchar_t app[32768] = {}, game[32768] = {};
                SetLastError(ERROR_SUCCESS);
                const DWORD appLength = GetEnvironmentVariableW(L"SteamAppId", app, 32768);
                hadApp = appLength != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
                SetLastError(ERROR_SUCCESS);
                const DWORD gameLength = GetEnvironmentVariableW(L"SteamGameId", game, 32768);
                hadGame = gameLength != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
                if(appLength >= 32768 || gameLength >= 32768) { valid = false; error = "Steam launch environment is too large."; return; }
                oldApp.assign(app, appLength); oldGame.assign(game, gameLength);
                changed = true;
                const std::wstring expected = std::to_wstring(HPL_STEAM_APP_ID);
                if(!SetCurrentDirectoryW(directory.c_str()) || !SetEnvironmentVariableW(L"SteamAppId", expected.c_str()) || !SetEnvironmentVariableW(L"SteamGameId", expected.c_str()))
                { valid = false; error = "Unable to select the development Steam App ID."; }
#else
                char current[4096] = {};
                if(!getcwd(current, sizeof(current))) { valid = false; error = "Unable to preserve the game's working directory for Steam initialization."; return; }
                oldDirectory = current;
                const char* app = getenv("SteamAppId"); const char* game = getenv("SteamGameId");
                hadApp = app != nullptr; hadGame = game != nullptr;
                if(app) oldApp = app; if(game) oldGame = game;
                changed = true;
                if(chdir(directory.c_str()) != 0 || setenv("SteamAppId", id.c_str(), 1) != 0 || setenv("SteamGameId", id.c_str(), 1) != 0)
                { valid = false; error = "Unable to select the development Steam App ID."; }
#endif
            }
            ~cSteamDevelopmentContext()
            {
                if(!changed) return;
#ifdef _WIN32
                SetCurrentDirectoryW(oldDirectory.c_str());
                SetEnvironmentVariableW(L"SteamAppId", hadApp ? oldApp.c_str() : nullptr);
                SetEnvironmentVariableW(L"SteamGameId", hadGame ? oldGame.c_str() : nullptr);
#else
                const int restored = chdir(oldDirectory.c_str()); (void)restored;
                if(hadApp) setenv("SteamAppId", oldApp.c_str(), 1); else unsetenv("SteamAppId");
                if(hadGame) setenv("SteamGameId", oldGame.c_str(), 1); else unsetenv("SteamGameId");
#endif
            }
        };
        bool IsLobby(uint64_t id) { const CSteamID value(id); return value.IsValid() && value.IsLobby(); }
        bool IsPlayer(uint64_t id) { const CSteamID value(id); return value.IsValid() && value.BIndividualAccount(); }
        bool LobbyContains(uint64_t lobby, uint64_t player)
        {
            if(!lobby || !player) return false;
            const int count = SteamMatchmaking()->GetNumLobbyMembers(CSteamID(lobby));
            for(int i = 0; i < count && i < 64; ++i)
                if(SteamMatchmaking()->GetLobbyMemberByIndex(CSteamID(lobby), i).ConvertToUint64() == player) return true;
            return false;
        }
    }

    struct cNetworkTransport::cImpl
    {
        struct cCallbacks;
        struct cOperation;
        static cCallbacks* callbacks;
        static std::set<cImpl*> instances;
        static std::map<HSteamListenSocket, cImpl*> listeners;
        static std::map<HSteamNetConnection, cImpl*> connections;

        bool active = false, host = false, steam = false, searching = false, failed = false;
        unsigned maxPeers = 0;
        uint32_t nextPeer = 1;
        uint64_t lobby = 0, expectedHost = 0;
        std::string mapName;
        HSteamListenSocket listen = k_HSteamListenSocket_Invalid;
        HSteamNetPollGroup group = k_HSteamNetPollGroup_Invalid;
        std::map<uint32_t, HSteamNetConnection> peers;
        std::map<uint32_t, uint64_t> identities;
        std::set<uint64_t> rejected;
        std::vector<cNetworkEvent> pending;
        std::vector<cSteamLobbyInfo> lobbies;
        cSteamClock::time_point sessionDeadline = cSteamClock::time_point::max();
        cSteamClock::time_point searchDeadline = cSteamClock::time_point::max();
        cSteamClock::time_point nextMembershipCheck;

        cImpl() { instances.insert(this); }
        ~cImpl() { instances.erase(this); }
        bool Init(std::string& error)
        {
            if(!cNetworkTransport::InitializeSteam(error)) return false;
            group = SteamNetworkingSockets()->CreatePollGroup();
            if(group == k_HSteamNetPollGroup_Invalid) { error = "Unable to create the networking receive group."; return false; }
            active = true;
            return true;
        }
        static int Options(SteamNetworkingConfigValue_t* options, bool p2p)
        {
            // SDK status events are registered with SteamAPI_RunCallbacks below.
            // Do not use the standalone ISteamNetworkingSockets::RunCallbacks.
            options[0].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, 30000);
            options[1].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, 15000);
            options[2].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, 4 * 1024 * 1024);
            options[3].SetInt32(k_ESteamNetworkingConfig_RecvBufferSize, 4 * 1024 * 1024);
            options[4].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages, 1024);
            options[5].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize, static_cast<int>(MaxMessageBytes));
            if(p2p)
            {
                // Force Valve relay routes. Disabling ICE prevents direct public
                // or private IP candidates and requires no router configuration.
                options[6].SetInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Disable);
            }
            else options[6].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
            return 7;
        }
        uint32_t PeerFor(HSteamNetConnection connection) const
        {
            for(const auto& item : peers) if(item.second == connection) return item.first;
            return UINT32_MAX;
        }
        void Queue(eNetworkEventType type, uint32_t peer = 0, const std::string& reason = std::string())
        {
            cNetworkEvent event; event.type = type; event.peer = peer; event.reason = reason;
            pending.push_back(std::move(event));
        }
        void CloseSession();
        void Fail(const std::string& error)
        {
            const bool wasHost = host, wasSteam = steam;
            CloseSession();
            // Preserve role until the game consumes SessionFailed and calls
            // Stop: its world/focus cleanup depends on IsClient/IsActive.
            active = true; host = wasHost; steam = wasSteam; failed = true;
            pending.clear();
            Queue(eNetworkEventType::SessionFailed, 0, error);
            gSteamStatus = error;
        }
        bool ValidateLobby(std::string& error) const
        {
            if(!steam || !lobby) return true;
            ISteamMatchmaking* api = SteamMatchmaking();
            uint64_t owner = 0;
            if(!steam_detail::ValidContract(api->GetLobbyData(CSteamID(lobby), "hpl_protocol"),
                api->GetLobbyData(CSteamID(lobby), "hpl_host"), api->GetLobbyOwner(CSteamID(lobby)).ConvertToUint64(),
                api->GetLobbyMemberLimit(CSteamID(lobby)), owner) || owner != expectedHost || !LobbyContains(lobby, expectedHost))
            { error = "The original host has left or the Steam lobby is incompatible. Host migration is not supported."; return false; }
            if(!LobbyContains(lobby, SteamUser()->GetSteamID().ConvertToUint64()))
            { error = "You are no longer a member of the Steam lobby."; return false; }
            return true;
        }
        void CheckSession();
        static void StatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
        static void Pump();
    };

    // Call-result objects outlive canceled game sessions. Steam's async lobby
    // operations cannot be canceled: a late successful result must leave its
    // lobby instead of attaching to a restarted session or retaining membership.
    struct cNetworkTransport::cImpl::cOperation
    {
        enum eKind { Create, Join, Search } kind;
        cImpl* owner;
        bool done = false;
        uint64_t requestedLobby = 0;
        CCallResult<cOperation, LobbyCreated_t> createResult;
        CCallResult<cOperation, LobbyEnter_t> joinResult;
        CCallResult<cOperation, LobbyMatchList_t> searchResult;
        cOperation(eKind value, cImpl* target) : kind(value), owner(target) {}
        void Created(LobbyCreated_t* result, bool failed)
        {
            done = true;
            const uint64_t id = result ? result->m_ulSteamIDLobby : 0;
            if(!owner)
            {
                if(!failed && result && result->m_eResult == k_EResultOK && IsLobby(id)) SteamMatchmaking()->LeaveLobby(CSteamID(id));
                return;
            }
            cImpl* target = owner; owner = nullptr;
            if(failed || !result || result->m_eResult != k_EResultOK || !IsLobby(id))
            { target->Fail("Steam could not create the lobby (result " + std::to_string(result ? static_cast<int>(result->m_eResult) : -1) + "). Check your Steam connection and this app's Steamworks configuration."); return; }
            target->lobby = id;
            target->expectedHost = SteamUser()->GetSteamID().ConvertToUint64();
            if(SteamMatchmaking()->GetLobbyOwner(CSteamID(id)).ConvertToUint64() != target->expectedHost)
            { target->Fail("Steam did not assign this player ownership of the new lobby."); return; }
            SteamNetworkingConfigValue_t options[7];
            target->listen = SteamNetworkingSockets()->CreateListenSocketP2P(0, Options(options, true), options);
            if(target->listen == k_HSteamListenSocket_Invalid)
            { target->Fail("Unable to create the Steam relay listener. Another session may already use this Steam account's virtual port."); return; }
            listeners[target->listen] = target;
            ISteamMatchmaking* api = SteamMatchmaking();
            const std::string hostText = std::to_string(target->expectedHost);
            const std::string name = steam_detail::DisplayText(SteamFriends()->GetPersonaName(), 80) + "'s session";
            if(!api->SetLobbyData(CSteamID(id), "hpl_protocol", steam_detail::Protocol) ||
               !api->SetLobbyData(CSteamID(id), "hpl_host", hostText.c_str()) ||
               !api->SetLobbyData(CSteamID(id), "name", name.c_str()) ||
               !api->SetLobbyData(CSteamID(id), "map", target->mapName.c_str()) ||
               !api->SetLobbyJoinable(CSteamID(id), true))
            { target->Fail("Steam could not publish the session's lobby settings."); return; }
            target->sessionDeadline = cSteamClock::time_point::max();
            gSteamStatus = "Steam lobby ready. Friends can join through Steam.";
            target->Queue(eNetworkEventType::SessionReady);
        }
        void Joined(LobbyEnter_t* result, bool failed)
        {
            done = true;
            const uint64_t id = result ? result->m_ulSteamIDLobby : 0;
            const bool entered = !failed && result && result->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess && IsLobby(id);
            if(!owner) { if(entered) SteamMatchmaking()->LeaveLobby(CSteamID(id)); return; }
            cImpl* target = owner; owner = nullptr;
            if(!entered || id != requestedLobby)
            {
                if(entered) SteamMatchmaking()->LeaveLobby(CSteamID(id));
                target->Fail("Steam could not join this lobby (result " + std::to_string(result ? result->m_EChatRoomEnterResponse : 0) + "). It may be full, closed, or restricted to the host's friends.");
                return;
            }
            target->lobby = id;
            target->expectedHost = SteamMatchmaking()->GetLobbyOwner(CSteamID(id)).ConvertToUint64();
            std::string error;
            if(!IsPlayer(target->expectedHost) || target->expectedHost == SteamUser()->GetSteamID().ConvertToUint64())
            { target->Fail("The lobby no longer has a remote host. Hosting and joining require separate Steam accounts."); return; }
            if(!target->ValidateLobby(error)) { target->Fail(error); return; }
            SteamNetworkingIdentity identity; identity.Clear(); identity.SetSteamID64(target->expectedHost);
            SteamNetworkingConfigValue_t options[7];
            const HSteamNetConnection connection = SteamNetworkingSockets()->ConnectP2P(identity, 0, Options(options, true), options);
            if(connection == k_HSteamNetConnection_Invalid)
            { target->Fail("Unable to begin an authenticated Steam relay connection to the host."); return; }
            target->peers[0] = connection; target->identities[0] = target->expectedHost;
            connections[connection] = target;
            if(!SteamNetworkingSockets()->SetConnectionPollGroup(connection, target->group))
            { target->Fail("Unable to receive messages from the Steam host."); return; }
            target->sessionDeadline = cSteamClock::now() + std::chrono::seconds(45);
            gSteamStatus = "Joined Steam lobby; connecting through Valve's relay network...";
            target->Queue(eNetworkEventType::SessionReady);
        }
        void Searched(LobbyMatchList_t* result, bool failed)
        {
            done = true;
            if(!owner) return;
            cImpl* target = owner; owner = nullptr;
            target->searching = false;
            target->searchDeadline = cSteamClock::time_point::max();
            target->lobbies.clear();
            if(failed || !result) { gSteamStatus = "Steam lobby search failed. Check your Steam connection and try again."; return; }
            ISteamMatchmaking* api = SteamMatchmaking();
            for(uint32 i = 0; i < result->m_nLobbiesMatching && i < 50; ++i)
            {
                const CSteamID id = api->GetLobbyByIndex(static_cast<int>(i));
                uint64_t claimedHost = 0;
                const int limit = api->GetLobbyMemberLimit(id);
                // GetLobbyOwner is only defined while a member; search validates
                // metadata shape here, then validates the actual owner on entry.
                if(!id.IsValid() || !id.IsLobby() || std::string(api->GetLobbyData(id, "hpl_protocol")) != steam_detail::Protocol ||
                    !steam_detail::ParseId(api->GetLobbyData(id, "hpl_host"), claimedHost) || !IsPlayer(claimedHost) || limit < 2 || limit > 64) continue;
                cSteamLobbyInfo listing;
                listing.id = id.ConvertToUint64();
                listing.name = steam_detail::DisplayText(api->GetLobbyData(id, "name"), 96);
                listing.map = steam_detail::DisplayText(api->GetLobbyData(id, "map"), 255);
                const int players = api->GetNumLobbyMembers(id);
                if(players < 1 || players > limit) continue;
                listing.players = static_cast<unsigned>(players); listing.maxPlayers = static_cast<unsigned>(limit);
                if(listing.name.empty()) listing.name = "Steam session";
                target->lobbies.push_back(std::move(listing));
            }
            gSteamStatus = "Found " + std::to_string(target->lobbies.size()) + " compatible Steam sessions.";
        }
    };

    struct cNetworkTransport::cImpl::cCallbacks
    {
        std::list<std::unique_ptr<cOperation>> operations;
        CCallback<cCallbacks, SteamNetConnectionStatusChangedCallback_t> status;
        CCallback<cCallbacks, GameLobbyJoinRequested_t> invite;
        CCallback<cCallbacks, GameOverlayActivated_t> overlay;
        cCallbacks() : status(this, &cCallbacks::Status), invite(this, &cCallbacks::Invite), overlay(this, &cCallbacks::Overlay) {}
        void Status(SteamNetConnectionStatusChangedCallback_t* info) { cImpl::StatusChanged(info); }
        void Overlay(GameOverlayActivated_t* info) { gSteamOverlayActive = info && info->m_bActive != 0; }
        void Invite(GameLobbyJoinRequested_t* info)
        {
            if(info && info->m_steamIDLobby.IsValid() && info->m_steamIDLobby.IsLobby())
                gSteamJoinRequest = info->m_steamIDLobby.ConvertToUint64();
        }
        bool LobbyOperationPending() const
        {
            // Serialize create/join even after cancellation: Steam lobby
            // membership is process-wide, so a late leave must not undo a newer
            // join to the same lobby.
            for(const auto& operation : operations) if(!operation->done && operation->kind != cOperation::Search) return true;
            return false;
        }
    };
    cNetworkTransport::cImpl::cCallbacks* cNetworkTransport::cImpl::callbacks = nullptr;
    std::set<cNetworkTransport::cImpl*> cNetworkTransport::cImpl::instances;
    std::map<HSteamListenSocket, cNetworkTransport::cImpl*> cNetworkTransport::cImpl::listeners;
    std::map<HSteamNetConnection, cNetworkTransport::cImpl*> cNetworkTransport::cImpl::connections;

    bool cNetworkTransport::InitializeSteam(std::string& error)
    {
        error.clear();
        if(gSteamInitialized) return true;
        {
            cSteamDevelopmentContext context;
            if(!context.valid) { error = context.error; gSteamStatus = error; return false; }
            SteamErrMsg detail = {};
            if(SteamAPI_InitEx(&detail) != k_ESteamAPIInitResult_OK)
            {
                error = std::string("Steam initialization failed: ") + detail + " Start Steam with an account that owns App ID " + std::to_string(HPL_STEAM_APP_ID) + ".";
                gSteamStatus = error; return false;
            }
        }
        if(!SteamUtils() || SteamUtils()->GetAppID() != HPL_STEAM_APP_ID)
        {
            error = "Steam initialized the wrong App ID (" + std::to_string(SteamUtils() ? SteamUtils()->GetAppID() : 0) + "); this build requires " + std::to_string(HPL_STEAM_APP_ID) + ". Launch this title through Steam, or place its matching development steam_appid.txt beside the executable.";
            SteamAPI_Shutdown(); gSteamStatus = error; return false;
        }
        if(!SteamUser() || !SteamMatchmaking() || !SteamFriends() || !SteamNetworkingSockets() || !SteamNetworkingUtils())
        { error = "Steam did not provide the required networking interfaces. Update and restart Steam."; SteamAPI_Shutdown(); gSteamStatus = error; return false; }
        gSteamInitialized = true;
        cImpl::callbacks = new cImpl::cCallbacks();
        SteamNetworkingUtils()->InitRelayNetworkAccess();
        gSteamStatus = "Steam initialized for App ID " + std::to_string(HPL_STEAM_APP_ID) + ".";
        return true;
    }
    void cNetworkTransport::ShutdownSteam()
    {
        if(!gSteamInitialized) return;
        for(cImpl* instance : cImpl::instances) { instance->CloseSession(); instance->pending.clear(); }
        delete cImpl::callbacks; cImpl::callbacks = nullptr;
        SteamAPI_Shutdown();
        gSteamInitialized = false; gSteamJoinRequest = 0; gSteamOverlayActive = false;
        gSteamStatus = "Steam is shut down.";
    }
    bool cNetworkTransport::SteamAvailable() { return gSteamInitialized && SteamUser()->BLoggedOn(); }
    bool cNetworkTransport::SteamOverlayActive() { return gSteamInitialized && gSteamOverlayActive; }
    std::string cNetworkTransport::SteamStatus()
    {
        if(!gSteamInitialized) return gSteamStatus.empty() ? "Steam has not been initialized." : gSteamStatus;
        if(!SteamUser()->BLoggedOn()) return "Steam is offline. Sign in to host or join a Steam session.";
        SteamRelayNetworkStatus_t relay = {};
        SteamNetworkingUtils()->GetRelayNetworkStatus(&relay);
        if(relay.m_eAvail < k_ESteamNetworkingAvailability_Unknown && relay.m_debugMsg[0])
            return gSteamStatus + " Relay status: " + steam_detail::DisplayText(relay.m_debugMsg, sizeof(relay.m_debugMsg) - 1);
        return gSteamStatus;
    }
    cNetworkTransport::cNetworkTransport() : mpImpl(new cImpl) {}
    cNetworkTransport::~cNetworkTransport() { Stop(); delete mpImpl; }

    void cNetworkTransport::cImpl::CloseSession()
    {
        if(callbacks) for(auto& operation : callbacks->operations)
            if(operation->owner == this) operation->owner = nullptr;
        searching = false; searchDeadline = cSteamClock::time_point::max();
        if(gSteamInitialized)
        {
            ISteamNetworkingSockets* api = SteamNetworkingSockets();
            for(const auto& item : peers)
            {
                connections.erase(item.second);
                api->CloseConnection(item.second, 1000, host ? "The host ended the session." : "The player disconnected.", false);
            }
            if(listen != k_HSteamListenSocket_Invalid) { listeners.erase(listen); api->CloseListenSocket(listen); }
            if(group != k_HSteamNetPollGroup_Invalid) api->DestroyPollGroup(group);
            if(lobby)
            {
                if(host && SteamMatchmaking()->GetLobbyOwner(CSteamID(lobby)) == SteamUser()->GetSteamID()) SteamMatchmaking()->SetLobbyJoinable(CSteamID(lobby), false);
                SteamMatchmaking()->LeaveLobby(CSteamID(lobby));
            }
        }
        peers.clear(); identities.clear(); rejected.clear();
        listen = k_HSteamListenSocket_Invalid; group = k_HSteamNetPollGroup_Invalid;
        active = false; host = false; steam = false; failed = false;
        lobby = 0; expectedHost = 0; nextPeer = 1; maxPeers = 0;
        sessionDeadline = cSteamClock::time_point::max();
        mapName.clear();
    }
    void cNetworkTransport::Stop() { mpImpl->CloseSession(); mpImpl->pending.clear(); }

    bool cNetworkTransport::HostSteam(unsigned maxPeers, bool publicLobby, const std::string& map, std::string& error)
    {
        error.clear();
        if(maxPeers < 1 || maxPeers > 63) { error = "A Steam session supports 1 to 63 remote players."; return false; }
        if(!InitializeSteam(error)) return false;
        if(!SteamAvailable()) { error = SteamStatus(); return false; }
        if(cImpl::callbacks->LobbyOperationPending()) { error = "The previous Steam lobby request is still finishing. Try again in a moment."; return false; }
        Stop();
        if(!mpImpl->Init(error)) { Stop(); return false; }
        mpImpl->steam = true; mpImpl->host = true; mpImpl->maxPeers = maxPeers;
        mpImpl->mapName = steam_detail::DisplayText(map.c_str(), 255);
        mpImpl->sessionDeadline = cSteamClock::now() + std::chrono::seconds(45);
        std::unique_ptr<cImpl::cOperation> operation(new cImpl::cOperation(cImpl::cOperation::Create, mpImpl));
        const SteamAPICall_t call = SteamMatchmaking()->CreateLobby(publicLobby ? k_ELobbyTypePublic : k_ELobbyTypeFriendsOnly, static_cast<int>(maxPeers + 1));
        if(call == k_uAPICallInvalid) { error = "Steam could not start creating the lobby."; Stop(); return false; }
        operation->createResult.Set(call, operation.get(), &cImpl::cOperation::Created);
        cImpl::callbacks->operations.push_back(std::move(operation));
        gSteamStatus = "Creating Steam lobby...";
        return true;
    }
    bool cNetworkTransport::JoinSteamLobby(uint64_t lobby, std::string& error)
    {
        error.clear();
        if(!IsLobby(lobby)) { error = "Enter a valid numeric Steam lobby ID."; return false; }
        if(!InitializeSteam(error)) return false;
        if(!SteamAvailable()) { error = SteamStatus(); return false; }
        if(cImpl::callbacks->LobbyOperationPending()) { error = "The previous Steam lobby request is still finishing. Try again in a moment."; return false; }
        Stop();
        if(!mpImpl->Init(error)) { Stop(); return false; }
        mpImpl->steam = true;
        mpImpl->sessionDeadline = cSteamClock::now() + std::chrono::seconds(45);
        std::unique_ptr<cImpl::cOperation> operation(new cImpl::cOperation(cImpl::cOperation::Join, mpImpl));
        operation->requestedLobby = lobby;
        const SteamAPICall_t call = SteamMatchmaking()->JoinLobby(CSteamID(lobby));
        if(call == k_uAPICallInvalid) { error = "Steam could not start joining this lobby."; Stop(); return false; }
        operation->joinResult.Set(call, operation.get(), &cImpl::cOperation::Joined);
        cImpl::callbacks->operations.push_back(std::move(operation));
        gSteamStatus = "Joining Steam lobby...";
        return true;
    }
    bool cNetworkTransport::RequestSteamLobbies(std::string& error)
    {
        error.clear();
        if(!InitializeSteam(error)) return false;
        if(!SteamAvailable()) { error = SteamStatus(); return false; }
        if(mpImpl->searching) return true;
        // Steam only supports one list request at a time per process.
        for(const auto& pending : cImpl::callbacks->operations)
            if(!pending->done && pending->kind == cImpl::cOperation::Search)
            { error = "The previous Steam lobby search is still finishing."; return false; }
        SteamMatchmaking()->AddRequestLobbyListStringFilter("hpl_protocol", steam_detail::Protocol, k_ELobbyComparisonEqual);
        SteamMatchmaking()->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
        SteamMatchmaking()->AddRequestLobbyListFilterSlotsAvailable(1);
        SteamMatchmaking()->AddRequestLobbyListResultCountFilter(50);
        const SteamAPICall_t call = SteamMatchmaking()->RequestLobbyList();
        if(call == k_uAPICallInvalid) { error = "Steam could not begin a lobby search."; return false; }
        std::unique_ptr<cImpl::cOperation> operation(new cImpl::cOperation(cImpl::cOperation::Search, mpImpl));
        operation->searchResult.Set(call, operation.get(), &cImpl::cOperation::Searched);
        cImpl::callbacks->operations.push_back(std::move(operation));
        mpImpl->searching = true; mpImpl->lobbies.clear();
        mpImpl->searchDeadline = cSteamClock::now() + std::chrono::seconds(25);
        gSteamStatus = "Searching Steam lobbies...";
        return true;
    }
    bool cNetworkTransport::IsSteamSession() const { return mpImpl->active && mpImpl->steam; }
    uint64_t cNetworkTransport::GetSteamLobbyID() const { return mpImpl->lobby; }
    void cNetworkTransport::SetSteamMapName(const std::string& map)
    {
        mpImpl->mapName = steam_detail::DisplayText(map.c_str(), 255);
        if(IsHost() && IsSteamSession() && mpImpl->lobby)
            SteamMatchmaking()->SetLobbyData(CSteamID(mpImpl->lobby), "map", mpImpl->mapName.c_str());
    }
    bool cNetworkTransport::IsSteamLobbySearchPending() const { return mpImpl->searching; }
    const std::vector<cSteamLobbyInfo>& cNetworkTransport::GetSteamLobbies() const { return mpImpl->lobbies; }
    uint64_t cNetworkTransport::ConsumeSteamJoinRequest()
    { const uint64_t request = gSteamJoinRequest; gSteamJoinRequest = 0; return request; }
    bool cNetworkTransport::InviteSteamFriends(std::string& error)
    {
        error.clear();
        if(!IsSteamSession() || !mpImpl->lobby || !SteamAvailable()) { error = "Create or join a Steam lobby before inviting friends."; return false; }
        if(!SteamUtils()->IsOverlayEnabled()) { error = "The Steam overlay is unavailable. Enable it for this title, or share the lobby ID with your friend."; return false; }
        SteamFriends()->ActivateGameOverlayInviteDialog(CSteamID(mpImpl->lobby));
        return true;
    }

    void cNetworkTransport::cImpl::StatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
    {
        if(!info) return;
        auto found = connections.find(info->m_hConn);
        cImpl* owner = found == connections.end() ? nullptr : found->second;
        if(!owner && info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid)
        {
            auto listener = listeners.find(info->m_info.m_hListenSocket);
            if(listener != listeners.end()) owner = listener->second;
        }
        if(!owner || !owner->active) return;
        ISteamNetworkingSockets* api = SteamNetworkingSockets();
        const uint64_t remote = info->m_info.m_identityRemote.GetSteamID64();
        if(info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting && owner->host)
        {
            if(owner->PeerFor(info->m_hConn) != UINT32_MAX) return;
            if(owner->steam)
            {
                std::string error;
                if(!owner->ValidateLobby(error) || !IsPlayer(remote) || remote == owner->expectedHost ||
                    !LobbyContains(owner->lobby, remote) || owner->rejected.count(remote))
                { api->CloseConnection(info->m_hConn, 1003, "Only current members of this Steam lobby may connect.", false); return; }
                for(const auto& item : owner->identities) if(item.second == remote)
                { api->CloseConnection(info->m_hConn, 1003, "This Steam player is already connected.", false); return; }
            }
            if(owner->peers.size() >= owner->maxPeers || owner->nextPeer == UINT32_MAX)
            { api->CloseConnection(info->m_hConn, 1001, "The multiplayer session is full.", false); return; }
            if(api->AcceptConnection(info->m_hConn) != k_EResultOK || !api->SetConnectionPollGroup(info->m_hConn, owner->group))
            { api->CloseConnection(info->m_hConn, 1002, "Unable to accept the connection.", false); return; }
            const uint32_t peer = owner->nextPeer++;
            owner->peers[peer] = info->m_hConn; owner->identities[peer] = remote;
            connections[info->m_hConn] = owner;
        }
        else if(info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected)
        {
            const uint32_t peer = owner->PeerFor(info->m_hConn);
            if(peer == UINT32_MAX) return;
            if(owner->steam)
            {
                std::string error;
                if(!owner->ValidateLobby(error)) { owner->Fail(error); return; }
                const auto identity = owner->identities.find(peer);
                if(identity == owner->identities.end() || identity->second != remote || !LobbyContains(owner->lobby, remote))
                {
                    if(!owner->host) { owner->Fail("Steam host identity or lobby membership changed."); return; }
                    api->CloseConnection(info->m_hConn, 1003, "Steam identity or lobby membership changed.", false);
                    connections.erase(info->m_hConn); owner->peers.erase(peer); owner->identities.erase(peer);
                    owner->Queue(eNetworkEventType::Disconnected, peer, "Steam identity or lobby membership changed.");
                    return;
                }
            }
            owner->sessionDeadline = cSteamClock::time_point::max();
            owner->Queue(eNetworkEventType::Connected, peer);
        }
        else if(info->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer || info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
        {
            const uint32_t peer = owner->PeerFor(info->m_hConn);
            if(peer != UINT32_MAX)
            {
                const std::string reason = steam_detail::DisplayText(info->m_info.m_szEndDebug, sizeof(info->m_info.m_szEndDebug) - 1);
                owner->Queue(eNetworkEventType::Disconnected, peer, reason.empty() ? "The network connection closed." : reason);
                owner->peers.erase(peer); owner->identities.erase(peer);
            }
            connections.erase(info->m_hConn); api->CloseConnection(info->m_hConn, 0, nullptr, false);
        }
    }
    void cNetworkTransport::cImpl::CheckSession()
    {
        const auto now = cSteamClock::now();
        if(searching && now >= searchDeadline)
        {
            searching = false;
            for(auto& operation : callbacks->operations)
                if(operation->owner == this && operation->kind == cOperation::Search) operation->owner = nullptr;
            gSteamStatus = "Steam lobby search timed out. Check your connection and try again.";
        }
        if(!active || !steam || failed) return;
        if(now >= sessionDeadline) { Fail("Steam session setup timed out. Check both players' Steam connections and app access."); return; }
        if(now < nextMembershipCheck) return;
        nextMembershipCheck = now + std::chrono::milliseconds(250);
        if(!SteamUser()->BLoggedOn()) { Fail("The Steam connection was lost. Sign back in to reconnect to multiplayer."); return; }
        std::string error;
        if(!ValidateLobby(error)) { Fail(error); return; }
        if(host && lobby)
        {
            for(auto item = identities.begin(); item != identities.end();)
            {
                if(LobbyContains(lobby, item->second)) { ++item; continue; }
                const uint32_t peer = item->first;
                const auto connection = peers.find(peer);
                if(connection != peers.end())
                {
                    connections.erase(connection->second);
                    SteamNetworkingSockets()->CloseConnection(connection->second, 1003, "The player left the Steam lobby.", false);
                    peers.erase(connection);
                }
                Queue(eNetworkEventType::Disconnected, peer, "The player left the Steam lobby.");
                item = identities.erase(item);
            }
        }
    }
    void cNetworkTransport::cImpl::Pump()
    {
        if(!gSteamInitialized || gSteamPumping) return;
        gSteamPumping = true;
        SteamAPI_RunCallbacks();
        for(auto it = callbacks->operations.begin(); it != callbacks->operations.end();)
            if((*it)->done) it = callbacks->operations.erase(it); else ++it;
        for(cImpl* instance : instances) instance->CheckSession();
        gSteamPumping = false;
    }
    void cNetworkTransport::Poll(std::vector<cNetworkEvent>& events)
    {
        cImpl::Pump(); // Includes invites and searches while at the main menu.
        for(auto& event : mpImpl->pending) events.push_back(std::move(event));
        mpImpl->pending.clear();
        if(!mpImpl->active || mpImpl->group == k_HSteamNetPollGroup_Invalid) return;
        size_t received = 0;
        for(unsigned i = 0; i < 512 && received < 4 * 1024 * 1024; ++i)
        {
            SteamNetworkingMessage_t* message = nullptr;
            if(SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(mpImpl->group, &message, 1) <= 0) break;
            const uint32_t peer = mpImpl->PeerFor(message->m_conn);
            if(peer != UINT32_MAX && message->m_cbSize >= 0 && static_cast<size_t>(message->m_cbSize) <= MaxMessageBytes)
            {
                cNetworkEvent event; event.type = eNetworkEventType::Message; event.peer = peer;
                const uint8_t* bytes = static_cast<const uint8_t*>(message->m_pData);
                if(message->m_cbSize) event.data.assign(bytes, bytes + message->m_cbSize);
                received += event.data.size(); events.push_back(std::move(event));
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
        return SteamNetworkingSockets()->SendMessageToConnection(found->second, data.data(), static_cast<uint32_t>(data.size()), flags, nullptr) == k_EResultOK;
    }
    void cNetworkTransport::Disconnect(uint32_t peer, const std::string& reason)
    {
        const auto found = mpImpl->peers.find(peer);
        if(found == mpImpl->peers.end()) return;
        SteamNetworkingSockets()->CloseConnection(found->second, 1000, reason.substr(0, 127).c_str(), false);
        cImpl::connections.erase(found->second); mpImpl->peers.erase(found);
        auto identity = mpImpl->identities.find(peer);
        if(identity != mpImpl->identities.end())
        {
            if(mpImpl->steam && mpImpl->host) mpImpl->rejected.insert(identity->second);
            mpImpl->identities.erase(identity);
        }
        mpImpl->Queue(eNetworkEventType::Disconnected, peer, reason);
    }
    bool cNetworkTransport::IsHost() const { return mpImpl->active && mpImpl->host; }
    bool cNetworkTransport::IsActive() const { return mpImpl->active; }

    // Optional direct-IP/LAN mode using the same Steam SDK runtime. Authentication
    // and lobby membership are deliberately exclusive to HostSteam/JoinSteamLobby.
    bool cNetworkTransport::Host(uint16_t port, unsigned maxPeers, std::string& error)
    {
        Stop(); error.clear();
        if(!port || maxPeers < 1 || maxPeers > 63) { error = "Use a port from 1 to 65535 and 1 to 63 remote players."; return false; }
        if(!mpImpl->Init(error)) { Stop(); return false; }
        mpImpl->host = true; mpImpl->maxPeers = maxPeers;
        SteamNetworkingIPAddr address; address.Clear(); address.m_port = port;
        SteamNetworkingConfigValue_t options[7];
        mpImpl->listen = SteamNetworkingSockets()->CreateListenSocketIP(address, cImpl::Options(options, false), options);
        if(mpImpl->listen == k_HSteamListenSocket_Invalid)
        { error = "Unable to listen on UDP port " + std::to_string(port) + ". It may already be in use."; Stop(); return false; }
        cImpl::listeners[mpImpl->listen] = mpImpl; return true;
    }
    static bool ResolveSteamDirectAddress(const std::string& text, uint16_t defaultPort, SteamNetworkingIPAddr& result)
    {
        if(text.empty() || text.size() > 255) return false;
        result.Clear();
        if(result.ParseString(text.c_str())) { if(result.m_port == 0) result.m_port = defaultPort; return result.m_port != 0; }
        std::string hostname = text; unsigned port = defaultPort;
        const size_t colon = text.find(':');
        if(colon != std::string::npos)
        {
            if(text.find(':', colon + 1) != std::string::npos || colon + 1 == text.size()) return false;
            hostname = text.substr(0, colon); port = 0;
            for(size_t i = colon + 1; i < text.size(); ++i)
            { if(text[i] < '0' || text[i] > '9') return false; port = port * 10 + text[i] - '0'; if(port > 65535) return false; }
        }
        if(!port || hostname.empty()) return false;
        addrinfo hints = {}; hints.ai_socktype = SOCK_DGRAM; hints.ai_family = AF_INET;
        addrinfo* addresses = nullptr;
        if(getaddrinfo(hostname.c_str(), nullptr, &hints, &addresses) != 0) return false;
        const sockaddr_in* address = reinterpret_cast<const sockaddr_in*>(addresses->ai_addr);
        result.SetIPv4(ntohl(address->sin_addr.s_addr), static_cast<uint16_t>(port));
        freeaddrinfo(addresses); return true;
    }
    bool cNetworkTransport::Join(const std::string& text, uint16_t defaultPort, std::string& error)
    {
        Stop(); error.clear();
        if(!mpImpl->Init(error)) { Stop(); return false; }
        SteamNetworkingIPAddr address;
        if(!ResolveSteamDirectAddress(text, defaultPort, address))
        { error = "Invalid or unresolved host. Enter an IP address or hostname, optionally followed by :port."; Stop(); return false; }
        SteamNetworkingConfigValue_t options[7];
        const HSteamNetConnection connection = SteamNetworkingSockets()->ConnectByIPAddress(address, cImpl::Options(options, false), options);
        if(connection == k_HSteamNetConnection_Invalid) { error = "Unable to start a connection to the host."; Stop(); return false; }
        mpImpl->peers[0] = connection; cImpl::connections[connection] = mpImpl;
        if(!SteamNetworkingSockets()->SetConnectionPollGroup(connection, mpImpl->group))
        { error = "Unable to receive messages from the host."; Stop(); return false; }
        return true;
    }
}
