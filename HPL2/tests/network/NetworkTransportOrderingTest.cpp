#include "network/NetworkTransport.h"
#include <steam/steamnetworkingsockets.h>
#include <cstdlib>
#include <iostream>

using namespace hpl;
static void Require(bool condition,const char* message)
{
    if(!condition) { std::cerr<<"FAIL: "<<message<<std::endl;std::exit(1); }
}
static void RequireNoGameplayEvents(const std::vector<cNetworkEvent>& events)
{
    for(const auto& event:events)
        Require(event.type==eNetworkEventType::Diagnostic,"no connection or message before deferred Connected callback");
}
static uint32_t RequireOrderedDelivery(const std::vector<cNetworkEvent>& events)
{
    bool connected=false,received=false;uint32_t peer=UINT32_MAX;
    for(const auto& event:events) {
        if(event.type==eNetworkEventType::Connected) { Require(!connected,"Connected emitted once");connected=true;peer=event.peer; }
        if(event.type==eNetworkEventType::Message) {
            Require(connected,"Connected precedes early reliable message");
            Require(!received && event.peer==peer && event.data==SteamNetworkingSockets()->earlyMessage,"early message preserved once for correct peer");
            received=true;
        }
    }
    Require(connected && received,"deferred callback releases buffered message");return peer;
}
static unsigned CountDiagnostics(const std::vector<cNetworkEvent>& events,const std::string& needle)
{
    unsigned count=0;
    for(const auto& event:events) if(event.type==eNetworkEventType::Diagnostic && event.reason.find(needle)!=std::string::npos) ++count;
    return count;
}
int main()
{
    auto* api=SteamNetworkingSockets();
    cNetworkTransport host,client;std::string error;
    Require(host.Host(27015,2,error),"host starts");
    const auto incoming=api->Incoming(api->lastListener);
    std::vector<cNetworkEvent> events;host.Poll(events);
    RequireNoGameplayEvents(events);
    Require(api->connections.at(incoming).messages.size()==1,"early Hello remains inside SDK until callback");
    api->ReleaseConnected();events.clear();host.Poll(events);
    const uint32_t peer=RequireOrderedDelivery(events);
    Require(peer!=0,"incoming peer has nonzero ID");
    events.clear();host.Poll(events);Require(events.empty(),"idle host produces no repeated diagnostics");

    Require(client.Join("127.0.0.1",27015,error),"client starts");
    events.clear();client.Poll(events);RequireNoGameplayEvents(events);
    api->ReleaseConnected();events.clear();client.Poll(events);
    Require(RequireOrderedDelivery(events)==0,"client connection obeys the same ordering contract");

    api->sendResult=k_EResultIgnored;
    Require(!host.Send(peer,{1},false),"NoDelay drop remains a false send result");
    events.clear();host.Poll(events);Require(CountDiagnostics(events,"send failed")==0,"expected NoDelay drops do not flood logs");
    api->sendResult=k_EResultLimitExceeded;
    for(int i=0;i<100;++i) Require(!host.Send(peer,{1},true),"failed sends preserve caller result");
    events.clear();host.Poll(events);
    Require(CountDiagnostics(events,"send failed")==1,"repeated SDK send errors log once per connection");
    Require(CountDiagnostics(events,"pending_reliable=4096")==1,"send error includes aggregate queue health");

    const auto failed=api->Incoming(api->lastListener);
    events.clear();host.Poll(events);RequireNoGameplayEvents(events);
    api->failGroupAssignment=true;api->ReleaseConnected();events.clear();host.Poll(events);
    bool disconnected=false;
    for(const auto& event:events) {
        Require(event.type!=eNetworkEventType::Connected && event.type!=eNetworkEventType::Message,"failed receive setup exposes neither Connected nor data");
        if(event.type==eNetworkEventType::Disconnected) disconnected=true;
    }
    Require(disconnected && api->connections.at(failed).closed,"receive setup failure closes only affected peer");
    Require(host.IsHost() && host.IsActive(),"receive setup failure leaves listener running");
    api->failGroupAssignment=false;
    host.Disconnect(peer,"test diagnostic draining");
    events.clear();host.DrainDiagnostics(events);
    Require(CountDiagnostics(events,"local disconnect")==1,"teardown can drain final connection health diagnostics");
    RequireNoGameplayEvents(events);
    events.clear();host.Poll(events);
    Require(events.size()==1 && events[0].type==eNetworkEventType::Disconnected && events[0].reason=="test diagnostic draining",
        "diagnostic draining preserves pending gameplay event and its reason");
    client.Stop();host.Stop();
    std::cout<<"PASS: deferred Connected callbacks preserve host/client message order; receive setup failure; bounded send diagnostics; diagnostic drain preserves gameplay events"<<std::endl;
}
