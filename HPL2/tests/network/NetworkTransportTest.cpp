#include "network/NetworkTransport.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace hpl;
static void Require(bool condition, const std::string& message)
{
    if(!condition) { std::cerr << "FAIL: " << message << std::endl; cNetworkTransport::ShutdownSteam(); std::exit(1); }
}

int main()
{
    cNetworkTransport host, client, overflow;
    std::string error;
#ifdef HPL_USE_STEAMWORKS
    Require(cNetworkTransport::InitializeSteam(error), error);
#endif
    Require(!host.Host(0, 2, error), "zero port rejected");
    Require(!client.Join("", 27015, error), "empty host rejected");
    uint16_t port = 27640;
    while(port < 27660 && !host.Host(port, 1, error)) ++port;
    Require(host.IsHost(), error);
    Require(client.Join("127.0.0.1", port, error), error);
    bool hostConnected = false, clientConnected = false, reliableReceived = false;
    bool unreliableReceived = false, rejected = false, disconnected = false;
    uint32_t clientPeer = 0;
    std::vector<uint8_t> reliable(60000);
    for(size_t i=0; i<reliable.size(); ++i) reliable[i] = static_cast<uint8_t>(i * 17);
    const std::vector<uint8_t> unreliable = {1,2,3,4};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while(std::chrono::steady_clock::now() < deadline && !(hostConnected && clientConnected))
    {
        std::vector<cNetworkEvent> hostEvents, clientEvents;
        host.Poll(hostEvents); client.Poll(clientEvents);
        for(const auto& event : hostEvents) if(event.type == eNetworkEventType::Connected)
        { hostConnected = true; clientPeer = event.peer; Require(clientPeer != 0, "host assigns nonzero IDs"); }
        for(const auto& event : clientEvents) if(event.type == eNetworkEventType::Connected)
        { clientConnected = true; Require(event.peer == 0, "client addresses host as zero"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(hostConnected && clientConnected, "loopback connects");
    Require(!client.Send(0, std::vector<uint8_t>(cNetworkTransport::MaxMessageBytes + 1), true), "oversize send rejected");
    Require(!host.Send(100000, reliable, true), "unknown peer rejected");
    Require(client.Send(0, reliable, true), "reliable send queues");
    Require(overflow.Join("localhost:" + std::to_string(port), port, error), error);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while(std::chrono::steady_clock::now() < deadline && !(reliableReceived && unreliableReceived && rejected))
    {
        host.Send(clientPeer, unreliable, false);
        std::vector<cNetworkEvent> hostEvents, clientEvents, overflowEvents;
        host.Poll(hostEvents); client.Poll(clientEvents); overflow.Poll(overflowEvents);
        for(const auto& event : hostEvents) if(event.type == eNetworkEventType::Message)
        { Require(event.data == reliable, "fragmented reliable message preserves every byte"); reliableReceived = true; }
        for(const auto& event : clientEvents) if(event.type == eNetworkEventType::Message)
        { Require(event.data == unreliable, "unreliable message preserves bytes"); unreliableReceived = true; }
        for(const auto& event : overflowEvents) if(event.type == eNetworkEventType::Disconnected)
        { Require(event.reason.find("full") != std::string::npos, "capacity rejection explains failure"); rejected = true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(reliableReceived && unreliableReceived && rejected, "both delivery modes and capacity rejection");
    host.Disconnect(clientPeer, "test disconnect");
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(std::chrono::steady_clock::now() < deadline && !disconnected)
    {
        std::vector<cNetworkEvent> events;
        client.Poll(events);
        for(const auto& event : events) if(event.type == eNetworkEventType::Disconnected)
        { disconnected = true; Require(event.reason == "test disconnect", "disconnect reason round trips"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(disconnected, "client observes disconnect");
    client.Stop(); overflow.Stop(); host.Stop();
    Require(!client.IsActive() && !host.IsActive(), "Stop releases state");
    Require(host.Host(port, 2, error), "transport can restart after last library user stops: " + error);
    host.Stop();
    cNetworkTransport::ShutdownSteam();
    std::cout << "PASS: connect, peer IDs, reliable fragmentation, unreliable delivery, capacity, disconnect, cleanup, restart" << std::endl;
}
