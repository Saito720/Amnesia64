#ifndef LUX_MULTIPLAYER_SCRIPT_H
#define LUX_MULTIPLAYER_SCRIPT_H
#include "LuxMultiplayer.h"
#include "LuxMultiplayerProtocol.h"
#include "system/Script.h"

// A flagged command ID is followed by a mask of unavailable host resources.
// Its native fallback does not waive any other resource in the same command.
static const uint32_t LuxScriptOptionalResources=0x80000000u;
bool LuxValidateMultiplayerScriptEffect(luxnet::Reader& r, std::string& error,uint32_t* unavailableResources=NULL);

// A fixed, typed registry; clients never compile script text from the network.
// The scope suppresses duplicate broadcasts from nested native helper calls.
class cLuxMultiplayerScriptScope {
public:
    template<class... Args> cLuxMultiplayerScriptScope(uint32_t id,const Args&... args) {
        bool outer=smDepth++==0;
        if(outer && gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsHost() && hpl::IsScriptExecuting()) {
            luxnet::Writer w(luxnet::ScriptEffect);w.U32(gpBase->mpMultiplayer->GetMapEpoch());
            const size_t commandOffset=w.data.size();w.U32(id);
            const size_t argumentsOffset=w.data.size();
            int unused[]={0,(Write(w,args),0)...};(void)unused;
            luxnet::Reader check(w.data);check.U32();
            std::string error;uint32_t unavailable=0;
            if(LuxValidateMultiplayerScriptEffect(check,error,&unavailable)) {
                if(unavailable) {
                    w.data[commandOffset+3]|=0x80;
                    uint8_t mask[4];for(unsigned i=0;i<4;++i) mask[i]=uint8_t(unavailable>>(i*8));
                    w.data.insert(w.data.begin()+argumentsOffset,mask,mask+4);
                }
                gpBase->mpMultiplayer->BroadcastScriptEffect(w.data);
            }
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
