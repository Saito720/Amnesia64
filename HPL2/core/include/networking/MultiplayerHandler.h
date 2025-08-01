#ifndef HPL_MULTIPLAYER_HANDLER_H
#define HPL_MULTIPLAYER_HANDLER_H

#include "networking/NetworkTypes.h"

struct _ENetPeer;
struct _ENetPacket;

namespace hpl {

    class iMultiplayerHandler
    {
    public:
        virtual ~iMultiplayerHandler() {}

        virtual void OnSessionStart(eSessionType aType) = 0;
        virtual void OnSessionExit() = 0;

        virtual void OnPeerConnected(_ENetPeer* apPeer) = 0;
        virtual void OnPeerDisconnected(_ENetPeer* apPeer) = 0;
        virtual void OnPacketReceived(_ENetPeer* apPeer, _ENetPacket* apPacket) = 0;
    };
}

#endif // HPL_MULTIPLAYER_HANDLER_H
