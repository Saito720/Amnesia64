#ifndef LUX_MULTIPLAYER_H
#define LUX_MULTIPLAYER_H

#include "LuxBase.h"

struct _ENetPeer;
struct _ENetPacket;

class cLuxMultiplayer : public iMultiplayerHandler, public iLuxUpdateable
{
public:
    cLuxMultiplayer() : iLuxUpdateable("LuxMultiplayer") {}

    void Update(float afTimeStep) override;

    void OnSessionStart(eSessionType aType);
    void OnSessionExit();

    void OnPeerConnected(_ENetPeer* apPeer) override;
    void OnPeerDisconnected(_ENetPeer* apPeer) override;
    void OnPacketReceived(_ENetPeer* apPeer, _ENetPacket* apPacket) override;

private:
    eSessionType mSessionType;
    bool mbSessionActive;
};

#endif // LUX_MULTIPLAYER_H
