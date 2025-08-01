#ifndef HPL_PEER_H
#define HPL_PEER_H

#include <enet/enet.h>

namespace hpl {

	class cSession;

	class cPeer
	{
	public:
		virtual ~cPeer();

		void Update();
		virtual void Disconnect() = 0;
		ENetHost* GetHost() { return mpHost; }

	protected:
		cPeer();

		virtual void ProcessEvent(const ENetEvent& event) = 0;

		ENetHost* mpHost;
		cSession* mpOwnerSession;
	};
};

#endif // HPL_PEER_H
