#ifndef LUX_MULTIPLAYER_SCRIPT_H
#define LUX_MULTIPLAYER_SCRIPT_H
#include "LuxMultiplayer.h"
#include "LuxMultiplayerProtocol.h"
#include "system/Script.h"

// A fixed, typed registry; clients never compile script text from the network.
// The scope suppresses duplicate broadcasts from nested native helper calls.
class cLuxMultiplayerScriptScope {
public:
    template<class... Args> cLuxMultiplayerScriptScope(uint32_t id,const Args&... args) {
        bool outer=smDepth++==0;
        if(outer && gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsHost() && hpl::IsScriptExecuting()) {
            luxnet::Writer w(luxnet::ScriptEffect);w.U32(gpBase->mpMultiplayer->GetMapEpoch());w.U32(id);
            int unused[]={0,(Write(w,args),0)...};(void)unused;
            gpBase->mpMultiplayer->BroadcastScriptEffect(w.data);
        }
    }
    ~cLuxMultiplayerScriptScope() {--smDepth;}
private:
    static unsigned smDepth;
    static void Write(luxnet::Writer& w,const std::string& v) {w.String(v);}
    static void Write(luxnet::Writer& w,float v) {w.Float(v);}
    static void Write(luxnet::Writer& w,int v) {w.U32(static_cast<uint32_t>(v));}
    static void Write(luxnet::Writer& w,bool v) {w.U8(v?1:0);}
};
bool LuxApplyMultiplayerScriptEffect(luxnet::Reader& r, std::string& error);
#endif
