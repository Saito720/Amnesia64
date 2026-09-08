#include "network/NetworkTransport.h"
#include "../../core/sources/network/NetworkSteamValidation.h"
#include <steam/steam_api.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace hpl;
static void Require(bool condition, const std::string& message)
{
    if(!condition) { std::cerr << "FAIL: " << message << std::endl; cNetworkTransport::ShutdownSteam(); std::exit(1); }
}
static void Pump(cNetworkTransport& transport, unsigned milliseconds)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while(std::chrono::steady_clock::now() < until)
    {
        std::vector<cNetworkEvent> events; transport.Poll(events);
        for(const auto& event : events)
            Require(event.type != eNetworkEventType::SessionFailed, event.reason);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int main()
{
    std::string error;
    Require(cNetworkTransport::InitializeSteam(error), error);
    Require(cNetworkTransport::SteamAvailable(), cNetworkTransport::SteamStatus());
    Require(SteamUtils()->GetAppID() == HPL_STEAM_APP_ID, "uses the configured App ID instead of retail working directory");
    std::cout << cNetworkTransport::SteamStatus() << std::endl;
    cNetworkTransport transport;
    Require(!transport.JoinSteamLobby(1, error), "rejects invalid lobby IDs");
    Require(!transport.HostSteam(0, false, "test.map", error), "rejects zero remote player capacity");
    Require(!transport.HostSteam(64, false, "test.map", error), "rejects oversized Steam lobby capacity");

    // Stop before an async result arrives; it must never attach to a dead session.
    {
        cNetworkTransport canceled;
        Require(canceled.HostSteam(3, false, "canceled.map", error), error);
        Require(canceled.IsActive() && canceled.IsHost() && canceled.IsSteamSession(), "pending host exposes its role immediately");
        canceled.Stop();
        Require(!canceled.IsActive(), "cancel stops the local session immediately");
    }
    const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    bool started = false;
    while(std::chrono::steady_clock::now() < retryDeadline && !started)
    {
        Pump(transport, 20);
        Require(!transport.IsActive() && transport.GetSteamLobbyID() == 0, "late canceled lobby result stays detached");
        started = transport.HostSteam(3, false, "initial.map", error);
    }
    Require(started, "can host after canceled request finishes: " + error);
    transport.SetSteamMapName("updated-before-create.map");
    bool ready = false;
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while(std::chrono::steady_clock::now() < readyDeadline && !ready)
    {
        std::vector<cNetworkEvent> events; transport.Poll(events);
        for(const auto& event : events)
        {
            Require(event.type != eNetworkEventType::SessionFailed, event.reason);
            if(event.type == eNetworkEventType::SessionReady) ready = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(ready && transport.IsHost() && transport.GetSteamLobbyID() != 0, "Steam creates and publishes a friends-only lobby");
    const CSteamID lobby(transport.GetSteamLobbyID());
    Require(SteamMatchmaking()->GetLobbyOwner(lobby) == SteamUser()->GetSteamID(), "actual lobby owner matches host");
    Require(SteamMatchmaking()->GetLobbyMemberLimit(lobby) == 4, "capacity includes host exactly once");
    Require(std::string(SteamMatchmaking()->GetLobbyData(lobby, "hpl_protocol")) == steam_detail::Protocol, "protocol metadata is published");
    Require(std::string(SteamMatchmaking()->GetLobbyData(lobby, "map")) == "updated-before-create.map", "latest map name survives async creation");
    transport.SetSteamMapName("after-transition.map");
    Require(std::string(SteamMatchmaking()->GetLobbyData(lobby, "map")) == "after-transition.map", "map transition updates the same lobby");
    Pump(transport, 1000);
    Require(transport.IsActive() && transport.GetSteamLobbyID() == lobby.ConvertToUint64(), "idle lobby stays valid");
    SteamRelayNetworkStatus_t relay = {};
    SteamNetworkingUtils()->GetRelayNetworkStatus(&relay);
    const auto relayDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while(relay.m_eAvail != k_ESteamNetworkingAvailability_Current && std::chrono::steady_clock::now() < relayDeadline)
    { Pump(transport, 50); SteamNetworkingUtils()->GetRelayNetworkStatus(&relay); }
    std::cout << "Relay availability=" << relay.m_eAvail << ", config=" << relay.m_eAvailNetworkConfig << ", anyRelay=" << relay.m_eAvailAnyRelay << ", " << relay.m_debugMsg << std::endl;
    Require(transport.ConsumeSteamJoinRequest() == 0, "test sends no invites or join requests");
    // A stale/spoofed advertised host must terminate the session while preserving
    // the role until the coordinator can clean up its world and focus settings.
    Require(SteamMatchmaking()->SetLobbyData(lobby, "hpl_host", "1"), "set incompatible test metadata");
    bool failed = false;
    const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(std::chrono::steady_clock::now() < failureDeadline && !failed)
    {
        std::vector<cNetworkEvent> events; transport.Poll(events);
        for(const auto& event : events) if(event.type == eNetworkEventType::SessionFailed) failed = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(failed && transport.IsActive() && transport.IsHost() && transport.GetSteamLobbyID() == 0, "incompatible lobby fails closed and preserves role until Stop");
    transport.Stop();
    Pump(transport, 200);
    Require(!transport.IsActive() && transport.GetSteamLobbyID() == 0, "clean session shutdown");
    cNetworkTransport::ShutdownSteam();
    Require(!cNetworkTransport::SteamAvailable(), "runtime shuts down cleanly");
    std::cout << "PASS: correct App ID, invalid requests, late cancellation, friends-only lobby, metadata/map updates, Steam relay initialization, teardown" << std::endl;
}
