#ifndef HPL_SESSION_H
#define HPL_SESSION_H

#include "networking/MultiplayerHandler.h"
#include <enet/enet.h>
#include <string> // address type

namespace hpl {

	class cPeer;

	class cSession
	{
	public:
		cSession(eSessionType aType, const std::string& asAddress, int alPort);
		~cSession();

		void Update();
		void Disconnect();

		void OnPeerConnected(ENetPeer* apPeer);
		void OnPeerDisconnected(ENetPeer* apPeer);
		void OnPacketReceived(ENetPeer* apPeer, ENetPacket* apPacket);

		void SendPacketToServer(const void* apData, size_t alDataLength);
		void BroadcastPacketToClients(const void* apData, size_t alDataLength);
		void SendPacketToPeer(_ENetPeer* apPeer, const void* apData, size_t alDataLength);

		bool IsValid() const { return mbInitialized; };
		bool IsPendingKill() const { return mbPendingKill; }
		eSessionState GetState() const { return mSessionState; }
		void SetMultiplayerHandler(iMultiplayerHandler* apHandler) { mpMultiplayerHandler = apHandler; }

	private:
		eSessionType mSessionType;
		eSessionState mSessionState;
		bool mbInitialized;
		bool mbPendingKill;

		iMultiplayerHandler* mpMultiplayerHandler;
		cPeer* mpPeer;
	};
}

#endif // HPL_SESSION_H
