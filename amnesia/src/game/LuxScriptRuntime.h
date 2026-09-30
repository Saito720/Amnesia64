#ifndef LUX_SCRIPT_RUNTIME_H
#define LUX_SCRIPT_RUNTIME_H

#include "LuxBase.h"
#include <cstdint>
#include <memory>
#include <vector>

class cLuxScriptPlayerState;

// A separately bound VM for explicitly declared client presentation modules.
// Source is transferred in memory; it is never discovered from client disk.
class cLuxScriptRuntime {
public:
    static const unsigned ExecutionLineBudget=10000;
    cLuxScriptRuntime();
    ~cLuxScriptRuntime();
    void RegisterNative(const tString& declaration, void* function);
    bool LoadPackage(const tString& mapFile, tString& error);
    bool ExportPackage(std::vector<uint8_t>& bytes) const;
    bool InstallPackage(const std::vector<uint8_t>& bytes, tString& error);
    bool IsRevised() const;
    bool IsClientReady() const;
    cLuxScriptPlayerState& GetPlayerState();
    void ForgetPlayer(uint64_t session, uint32_t peer);
    void BeginMapChange();
    void CommitMapChange();
    void RollbackMapChange();
    bool ValidateAuthorityScript(iScript* script,const tString& module,tString& error);
    bool RunAuthorityHook(iScript* script, const tString& hook, tString& error, const float* step=NULL,
        const tString& module="map");
    bool RunAuthorityCommand(iScript* script, const tString& command, tString& error);
    bool RunAuthorityPlayerReady(iScript* script,const tString& module,uint32_t peer,tString& error);
    bool InitializeClient(tString& error, bool firstVisit=true);
    bool LeaveClient(tString& error);
    bool UpdateClient(float step, tString& error);
    bool RunClientEvent(const tString& module, const tString& function,
        const std::vector<tString>& arguments, tString& error);
    bool AddClientTimer(const tString& name,float seconds,const tString& function,tString& error);
    void RemoveClientTimer(const tString& name);
    float GetClientTimerTimeLeft(const tString& name) const;
    void Reset();
    static bool IsClientNativeAllowed(const tString& name);
    static bool IsIdentifier(const tString& name);
private:
    void BeginPlayerStateMap();
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

#endif
