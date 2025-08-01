#ifndef HPL_SERVER_H
#define HPL_SERVER_H

#include "networking/Peer.h"

namespace hpl {

	class cSession;

	class cServer : public cPeer
	{
	public:
		cServer(cSession* apOwner, enet_uint16 alPort, int alMaxConnections);
		virtual ~cServer();

		void Disconnect() override;

		void BroadcastPacket(const void* apData, size_t alDataLength);
		void SendPacketToPeer(_ENetPeer* apPeer, const void* apData, size_t alDataLength);

	protected:
		void ProcessEvent(const ENetEvent& event) override;
	};
}

#endif // HPL_SERVER_H
