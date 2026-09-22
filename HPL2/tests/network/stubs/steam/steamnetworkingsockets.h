#ifndef HPL_TEST_FAKE_NETWORKING_SOCKETS_H
#define HPL_TEST_FAKE_NETWORKING_SOCKETS_H

// Deterministic SDK fixture for the real standalone transport implementation.
// A connection can already contain data while its Connected callback is held.
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

typedef uint32_t HSteamNetConnection;
typedef uint32_t HSteamListenSocket;
typedef uint32_t HSteamNetPollGroup;
const HSteamNetConnection k_HSteamNetConnection_Invalid = 0;
const HSteamListenSocket k_HSteamListenSocket_Invalid = 0;
const HSteamNetPollGroup k_HSteamNetPollGroup_Invalid = 0;
typedef char SteamDatagramErrMsg[1024];
enum EResult { k_EResultOK=1, k_EResultNoConnection=3, k_EResultInvalidParam=8, k_EResultLimitExceeded=25, k_EResultIgnored=41 };
enum ESteamNetworkingConnectionState {
    k_ESteamNetworkingConnectionState_None=0, k_ESteamNetworkingConnectionState_Connecting=1,
    k_ESteamNetworkingConnectionState_FindingRoute=2, k_ESteamNetworkingConnectionState_Connected=3,
    k_ESteamNetworkingConnectionState_ClosedByPeer=4, k_ESteamNetworkingConnectionState_ProblemDetectedLocally=5
};
enum ESteamNetworkingConfigValue {
    k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, k_ESteamNetworkingConfig_TimeoutInitial,
    k_ESteamNetworkingConfig_TimeoutConnected, k_ESteamNetworkingConfig_SendBufferSize,
    k_ESteamNetworkingConfig_RecvBufferSize, k_ESteamNetworkingConfig_RecvBufferMessages,
    k_ESteamNetworkingConfig_RecvMaxMessageSize, k_ESteamNetworkingConfig_IP_AllowWithoutAuth
};
const int k_nSteamNetworkingSend_Reliable=8, k_nSteamNetworkingSend_UnreliableNoDelay=4;
struct SteamNetworkingConfigValue_t {
    void* callback=nullptr;
    void SetPtr(ESteamNetworkingConfigValue, void* value) { callback=value; }
    void SetInt32(ESteamNetworkingConfigValue, int32_t) {}
};
struct SteamNetworkingIPAddr {
    uint16_t m_port=0;
    void Clear() { m_port=0; }
    bool ParseString(const char* text) { return std::string(text)=="127.0.0.1"; }
    void SetIPv4(uint32_t, uint16_t port) { m_port=port; }
};
struct SteamNetConnectionInfo_t {
    HSteamListenSocket m_hListenSocket=0;
    ESteamNetworkingConnectionState m_eState=k_ESteamNetworkingConnectionState_None;
    int m_eEndReason=0;
    char m_szEndDebug[128]={};
};
struct SteamNetConnectionStatusChangedCallback_t {
    HSteamNetConnection m_hConn=0;
    SteamNetConnectionInfo_t m_info;
    ESteamNetworkingConnectionState m_eOldState=k_ESteamNetworkingConnectionState_None;
};
struct SteamNetConnectionRealTimeStatus_t {
    ESteamNetworkingConnectionState m_eState=k_ESteamNetworkingConnectionState_Connected;
    int m_nPing=42;
    float m_flConnectionQualityLocal=0.9f, m_flConnectionQualityRemote=0.8f;
    int m_cbPendingReliable=4096, m_cbPendingUnreliable=0, m_cbSentUnackedReliable=1024;
};
struct SteamNetworkingMessage_t {
    HSteamNetConnection m_conn;
    std::vector<uint8_t> storage;
    void* m_pData;
    int m_cbSize;
    SteamNetworkingMessage_t(HSteamNetConnection connection, const std::vector<uint8_t>& data)
        : m_conn(connection), storage(data), m_pData(storage.data()), m_cbSize(static_cast<int>(storage.size())) {}
    void Release() { delete this; }
};
class ISteamNetworkingSockets {
public:
    typedef void (*Callback)(SteamNetConnectionStatusChangedCallback_t*);
    struct Connection {
        SteamNetConnectionInfo_t info;
        Callback callback=nullptr;
        HSteamNetPollGroup group=0;
        std::deque<std::vector<uint8_t>> messages;
        bool closed=false;
    };
    std::map<HSteamNetConnection,Connection> connections;
    std::map<HSteamListenSocket,Callback> listeners;
    std::set<HSteamNetPollGroup> groups;
    std::vector<SteamNetConnectionStatusChangedCallback_t> callbacks, heldConnected;
    HSteamNetConnection lastConnection=100;
    HSteamListenSocket lastListener=0;
    HSteamNetPollGroup lastGroup=0;
    EResult sendResult=k_EResultOK;
    bool failGroupAssignment=false;
    const std::vector<uint8_t> earlyMessage={1,14,0,0,0};

    HSteamNetPollGroup CreatePollGroup() { groups.insert(++lastGroup);return lastGroup; }
    bool DestroyPollGroup(HSteamNetPollGroup group) { groups.erase(group);return true; }
    HSteamListenSocket CreateListenSocketIP(const SteamNetworkingIPAddr&, int, SteamNetworkingConfigValue_t* options)
    { listeners[++lastListener]=reinterpret_cast<Callback>(options[0].callback);return lastListener; }
    bool CloseListenSocket(HSteamListenSocket listener) { listeners.erase(listener);return true; }
    HSteamNetConnection ConnectByIPAddress(const SteamNetworkingIPAddr&, int, SteamNetworkingConfigValue_t* options)
    {
        const auto id=++lastConnection;
        Connection& connection=connections[id];connection.callback=reinterpret_cast<Callback>(options[0].callback);
        connection.info.m_eState=k_ESteamNetworkingConnectionState_Connected;connection.messages.push_back(earlyMessage);
        HoldConnected(id);return id;
    }
    HSteamNetConnection Incoming(HSteamListenSocket listener)
    {
        const auto id=++lastConnection;
        Connection& connection=connections[id];connection.callback=listeners.at(listener);
        connection.info.m_hListenSocket=listener;connection.info.m_eState=k_ESteamNetworkingConnectionState_Connecting;
        SteamNetConnectionStatusChangedCallback_t event;event.m_hConn=id;event.m_info=connection.info;
        callbacks.push_back(event);return id;
    }
    void HoldConnected(HSteamNetConnection id)
    {
        SteamNetConnectionStatusChangedCallback_t event;event.m_hConn=id;event.m_info=connections.at(id).info;
        event.m_eOldState=k_ESteamNetworkingConnectionState_Connecting;heldConnected.push_back(event);
    }
    void ReleaseConnected() { callbacks.insert(callbacks.end(),heldConnected.begin(),heldConnected.end());heldConnected.clear(); }
    EResult AcceptConnection(HSteamNetConnection id)
    {
        Connection& connection=connections.at(id);connection.info.m_eState=k_ESteamNetworkingConnectionState_Connected;
        connection.messages.push_back(earlyMessage);HoldConnected(id);return k_EResultOK;
    }
    bool SetConnectionPollGroup(HSteamNetConnection id, HSteamNetPollGroup group)
    {
        if(failGroupAssignment || !groups.count(group) || connections.at(id).closed) return false;
        connections.at(id).group=group;return true;
    }
    bool CloseConnection(HSteamNetConnection id, int, const char*, bool)
    { connections.at(id).closed=true;connections.at(id).messages.clear();return true; }
    void RunCallbacks()
    {
        std::vector<SteamNetConnectionStatusChangedCallback_t> batch;batch.swap(callbacks);
        for(auto& event:batch) connections.at(event.m_hConn).callback(&event);
    }
    int ReceiveMessagesOnPollGroup(HSteamNetPollGroup group, SteamNetworkingMessage_t** output, int)
    {
        for(auto& item:connections) if(!item.second.closed && item.second.group==group && !item.second.messages.empty()) {
            *output=new SteamNetworkingMessage_t(item.first,item.second.messages.front());item.second.messages.pop_front();return 1;
        }
        return 0;
    }
    EResult SendMessageToConnection(HSteamNetConnection, const void*, uint32_t, int, int64_t*) { return sendResult; }
    EResult FlushMessagesOnConnection(HSteamNetConnection) { return k_EResultOK; }
    EResult GetConnectionRealTimeStatus(HSteamNetConnection id, SteamNetConnectionRealTimeStatus_t* status, int, void*)
    {
        if(connections.at(id).closed) return k_EResultNoConnection;
        *status=SteamNetConnectionRealTimeStatus_t();return k_EResultOK;
    }
};
inline ISteamNetworkingSockets* SteamNetworkingSockets() { static ISteamNetworkingSockets api;return &api; }
inline bool GameNetworkingSockets_Init(const void*, SteamDatagramErrMsg&) { return true; }
inline void GameNetworkingSockets_Kill() {}
#endif
