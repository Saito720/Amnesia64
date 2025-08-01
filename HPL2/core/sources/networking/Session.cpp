#include "networking/Session.h"
#include "networking/Server.h"
#include "networking/Client.h"
#include "system/LowLevelSystem.h" // Logging

namespace hpl {

	//-----------------------------------------------------------------------

	cSession::cSession(eSessionType aType, const std::string& asAddress, int alPort)
	{
		mSessionType = aType;
		mSessionState = eSessionState_Idle;
		mbInitialized = false;
		mbPendingKill = false;
		mpMultiplayerHandler = NULL;
		mpPeer = NULL;

		if (mSessionType == eSessionType_Host)
		{
			mpPeer = hplNew(cServer, (this, alPort, 32));
			if (mpPeer != NULL)
			{
				mbInitialized = true;
				mSessionState = eSessionState_Connected;
			}
		}
		else
		{
			cClient* pClient = hplNew(cClient, (this));
			if (pClient->Connect(asAddress, alPort))
			{
				mpPeer = pClient;
				mbInitialized = true;
				mSessionState = eSessionState_Connecting;
			}
			else
			{
				hplDelete(pClient);
				mbInitialized = false;
			}
		}
	}

	cSession::~cSession()
	{
		if (mpPeer) hplDelete(mpPeer);
	}

	//-----------------------------------------------------------------------

	void cSession::Update()
	{
		if (!mpPeer) return;
		mpPeer->Update();
	}

	void cSession::Disconnect()
	{
		if (!mpPeer) return;

		if (mSessionState == eSessionState_Connected)
		{
			mSessionState = eSessionState_Disconnecting;
			mpPeer->Disconnect();

			if (mSessionType == eSessionType_Host && mpPeer->GetHost()->connectedPeers == 0)
			{
				mSessionState = eSessionState_Idle;
				mbPendingKill = true;
			}
		}
		else if (mSessionState == eSessionState_Connecting)
		{
			mSessionState = eSessionState_Idle;
			mbPendingKill = true;
		}
	}

	//-----------------------------------------------------------------------

	void cSession::OnPeerConnected(ENetPeer* apPeer)
	{
		mSessionState = eSessionState_Connected;

		if (mpMultiplayerHandler)
		{
			mpMultiplayerHandler->OnPeerConnected(apPeer);
		}
	}

	void cSession::OnPeerDisconnected(ENetPeer* apPeer)
	{
		if (mSessionType == eSessionType_Client)
		{
			mSessionState = eSessionState_Idle;
			mbPendingKill = true;
		}
		else
		{
			if (mSessionState == eSessionState_Disconnecting && mpPeer->GetHost()->connectedPeers == 0)
			{
				mSessionState = eSessionState_Idle;
				mbPendingKill = true;
			}
		}

		if (mpMultiplayerHandler)
		{
			mpMultiplayerHandler->OnPeerDisconnected(apPeer);
		}
	}

	void cSession::OnPacketReceived(ENetPeer* apPeer, ENetPacket* apPacket)
	{
		if (mpMultiplayerHandler)
		{
			mpMultiplayerHandler->OnPacketReceived(apPeer, apPacket);
		}
		else
		{
			enet_packet_destroy(apPacket);
		}
	}

	//-----------------------------------------------------------------------

	void cSession::SendPacketToServer(const void* apData, size_t alDataLength)
	{
		if (mSessionType != eSessionType_Client || !mpPeer) return;

		cClient* pClient = static_cast<cClient*>(mpPeer);
		pClient->SendPacket(apData, alDataLength);
	}

	void cSession::BroadcastPacketToClients(const void* apData, size_t alDataLength)
	{
		if (mSessionType != eSessionType_Host || !mpPeer) return;

		cServer* pServer = static_cast<cServer*>(mpPeer);
		pServer->BroadcastPacket(apData, alDataLength);
	}

	void cSession::SendPacketToPeer(_ENetPeer* apPeer, const void* apData, size_t alDataLength)
	{
		if (mSessionType != eSessionType_Host || !mpPeer) return;

		cServer* pServer = static_cast<cServer*>(mpPeer);
		pServer->SendPacketToPeer(apPeer, apData, alDataLength);
	}

	//-----------------------------------------------------------------------
}
