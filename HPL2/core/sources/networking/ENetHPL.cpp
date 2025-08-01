#include "networking/ENetHPL.h"
#include "system/LowLevelSystem.h" // Logging
#include <enet/enet.h>

namespace hpl {

	cENet::cENet()
	{
		if (enet_initialize() != 0)
		{
			FatalError("Could not initialize ENet!\n");
		}
	}

	cENet::~cENet()
	{
		enet_deinitialize();
	}
}
