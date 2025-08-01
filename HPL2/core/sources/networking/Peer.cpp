#include "networking/Peer.h"

namespace hpl {

	cPeer::cPeer() : mpHost(NULL) {}

	cPeer::~cPeer()
	{
		if (mpHost != NULL)
		{
			enet_host_flush(mpHost);
			enet_host_destroy(mpHost);
		}
	}

	void cPeer::Update()
	{
		if (mpHost == NULL) return;

		ENetEvent event;
		while (enet_host_service(mpHost, &event, 0) > 0)
		{
			ProcessEvent(event);
		}
	}
}
