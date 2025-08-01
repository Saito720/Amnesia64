#include "networking/Client.h"
#include "networking/Session.h"
#include "system/LowLevelSystem.h" // Logging

namespace hpl {

	//-----------------------------------------------------------------------

	cClient::cClient(cSession* apOwner) : mpServerPeer(NULL)
	{
		mpOwnerSession = apOwner;
		mpHost = enet_host_create(NULL, 1, 2, 0, 0);
		if (mpHost == NULL)
		{
			Error("  [ENet]: Could not create client host!\n");
		}
	}

	cClient::~cClient() {}

	//-----------------------------------------------------------------------

	bool cClient::Connect(const std::string& asAddress, enet_uint16 alPort)
	{
		ENetAddress address;
		enet_address_set_host(&address, asAddress.c_str());
		address.port = alPort;

		mpServerPeer = enet_host_connect(mpHost, &address, 2, 0);
		if (mpServerPeer == NULL)
		{
			Error("  [ENet]: Failed to initiate connection!\n");
			return false;
		}

		return true;
	}

	void cClient::Disconnect()
	{
		if (mpServerPeer)
		{
			enet_peer_disconnect(mpServerPeer, 0);
		}
	}

	//-----------------------------------------------------------------------

	void cClient::SendPacket(const void* apData, size_t alDataLength)
	{
		if (mpServerPeer == NULL) return;

		ENetPacket* packet = enet_packet_create(apData, alDataLength, ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, 0, packet);
	}

	void cClient::ProcessEvent(const ENetEvent& event)
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
			mpServerPeer = NULL;
			break;

		case ENET_EVENT_TYPE_NONE:
			break;
		}
	}

	//-----------------------------------------------------------------------
}
