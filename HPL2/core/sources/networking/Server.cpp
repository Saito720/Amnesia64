#include "networking/Server.h"
#include "networking/Session.h"
#include "system/LowLevelSystem.h" // Logging

namespace hpl {

	//-----------------------------------------------------------------------

	cServer::cServer(cSession* apOwner, enet_uint16 alPort, int alMaxConnections)
	{
		mpOwnerSession = apOwner;

		ENetAddress address;
		address.host = ENET_HOST_ANY;
		address.port = alPort;

		mpHost = enet_host_create(&address, alMaxConnections, 2, 0, 0);
		if (mpHost == NULL)
		{
			Error("  [ENet]: Could not create server host!\n");
		}
	}

	cServer::~cServer() {}

	//-----------------------------------------------------------------------

	void cServer::Disconnect()
	{
		for (size_t i = 0; i < mpHost->peerCount; ++i)
		{
			enet_peer_disconnect(&mpHost->peers[i], 0);
		}
	}

	//-----------------------------------------------------------------------

	void cServer::BroadcastPacket(const void* apData, size_t alDataLength)
	{
		ENetPacket* packet = enet_packet_create(apData, alDataLength, ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(mpHost, 0, packet);
	}

	void cServer::SendPacketToPeer(_ENetPeer* apPeer, const void* apData, size_t alDataLength)
	{
		ENetPacket* packet = enet_packet_create(apData, alDataLength, ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(apPeer, 0, packet);
	}

	void cServer::ProcessEvent(const ENetEvent& event)
	{
		switch (event.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
			mpOwnerSession->OnPeerConnected(event.peer);
			break;

		case ENET_EVENT_TYPE_RECEIVE:
			mpOwnerSession->OnPacketReceived(event.peer, event.packet);
			break;

		case ENET_EVENT_TYPE_DISCONNECT:
			mpOwnerSession->OnPeerDisconnected(event.peer);
			break;

		case ENET_EVENT_TYPE_NONE:
			break;
		}
	}

	//-----------------------------------------------------------------------
}
