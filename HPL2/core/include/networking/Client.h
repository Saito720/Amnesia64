#ifndef HPL_CLIENT_H
#define HPL_CLIENT_H

#include "networking/Peer.h"
#include <string> // address type

namespace hpl {

	class cClient : public cPeer
	{
	public:
		cClient(cSession* apOwner);
		virtual ~cClient();

		bool Connect(const std::string& asAddress, enet_uint16 alPort);
		void Disconnect() override;

		void SendPacket(const void* apData, size_t alDataLength);

	protected:
		void ProcessEvent(const ENetEvent& event) override;

	private:
		ENetPeer* mpServerPeer;
	};
}

#endif // HPL_CLIENT_H
