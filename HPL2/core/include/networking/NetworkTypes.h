namespace hpl {

	enum eSessionType {
        eSessionType_Host,
        eSessionType_Client
	};

    enum eSessionState
    {
        eSessionState_Idle,         // Not yet initialized or fully cleaned up
        eSessionState_Connecting,   // Client is trying to connect
        eSessionState_Connected,    // Host is running or client is connected
        eSessionState_Disconnecting // Graceful disconnect is in progress
    };
}