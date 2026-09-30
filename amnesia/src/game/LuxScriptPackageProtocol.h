#ifndef LUX_SCRIPT_PACKAGE_PROTOCOL_H
#define LUX_SCRIPT_PACKAGE_PROTOCOL_H
#include "LuxMultiplayerProtocol.h"
#include "LuxMultiplayerMapHash.h"
#include <algorithm>

namespace luxnet {
static const uint32_t MaxScriptPackageBytes=256*1024;
static const uint32_t ScriptPackageChunkBytes=32*1024;

// The map generation binds every fragment. An empty package explicitly selects
// legacy scripting; it is still acknowledged before map initialization.
inline std::vector<uint8_t> ScriptPackageFragment(uint32_t epoch,const std::vector<uint8_t>& package,
    uint32_t offset) {
    if(!epoch || package.size()>MaxScriptPackageBytes || offset>package.size()) return {};
    Writer w(ScriptPackage);w.U32(epoch);w.U32(static_cast<uint32_t>(package.size()));w.U32(offset);
    w.String(MapHash(package));
    const size_t count=std::min(size_t(ScriptPackageChunkBytes),package.size()-offset);
    if(count) w.Bytes(package.data()+offset,count);
    return w.data;
}

struct ScriptPackageReceiver {
    uint32_t epoch=0,total=0;
    std::string hash;
    std::vector<uint8_t> bytes;
    bool begun=false,complete=false;
    void Reset() {*this=ScriptPackageReceiver();}
    bool Accept(const std::vector<uint8_t>& packet,uint32_t expectedEpoch,std::string& error) {
        Reader r(packet);const uint32_t generation=r.U32(),size=r.U32(),offset=r.U32();
        const std::string digest=r.String(64);
        if(packet.empty() || packet[0]!=ScriptPackage || !r.valid || !generation ||
           size>MaxScriptPackageBytes || !ValidMapHash(digest) || offset>size ||
           packet.size()-r.pos>ScriptPackageChunkBytes || packet.size()-r.pos>size-offset) {
            error="Malformed client script package fragment.";return false;
        }
        // Old-generation traffic cannot initialize a replacement world.
        if(generation!=expectedEpoch) return true;
        if(complete || (!begun && offset!=0)) {error="Unexpected client script package fragment.";return false;}
        if(!begun) {epoch=generation;total=size;hash=digest;begun=true;bytes.reserve(size);}
        if(epoch!=generation || total!=size || hash!=digest || bytes.size()!=offset ||
           (size && packet.size()==r.pos)) {error="Out-of-order client script package fragment.";return false;}
        bytes.insert(bytes.end(),packet.begin()+r.pos,packet.end());
        if(bytes.size()==total) {
            if(MapHash(bytes)!=hash) {error="Client script package hash mismatch.";return false;}
            complete=true;
        }
        return true;
    }
};
}
#endif
