#include "LuxMultiplayer.h"
#include "LuxDebugHandler.h" // DEBUG
#include <enet/enet.h>

//-----------------------------------------------------------------------

void cLuxMultiplayer::Update(float afTimeStep)
{
	//gpBase->mpDebugHandler->AddMessage(_W("UPDATE"), false);
}

//-----------------------------------------------------------------------

void cLuxMultiplayer::OnSessionStart(eSessionType aType)
{
	mSessionType = aType;
	mbSessionActive = true;
}

void cLuxMultiplayer::OnSessionExit()
{
	mbSessionActive = false;
}

//-----------------------------------------------------------------------

void cLuxMultiplayer::OnPeerConnected(_ENetPeer* apPeer)
{
	gpBase->mpDebugHandler->AddMessage(_W("CONNECTED"), false);
}

void cLuxMultiplayer::OnPeerDisconnected(_ENetPeer* apPeer)
{
	gpBase->mpDebugHandler->AddMessage(_W("DISCONNECTED"), false);
}

void cLuxMultiplayer::OnPacketReceived(_ENetPeer* apPeer, _ENetPacket* apPacket)
{
	gpBase->mpDebugHandler->AddMessage(_W("PACKET"), false);
	enet_packet_destroy(apPacket);
}

//-----------------------------------------------------------------------
