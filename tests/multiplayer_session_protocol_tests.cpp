#include "../amnesia/src/game/LuxMultiplayerProtocol.h"
#include "../amnesia/src/game/LuxMultiplayerTriggerPolicy.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace luxnet;
static void CheckPlayerTriggerOrigins() {
    struct Sample { bool host,remote,remoteOrigin; };
    const std::vector<std::vector<Sample>> scenarios={
        {{false,true,true},{false,false,true},{false,false,false}}, // remote enter, leave, still empty
        {{true,false,false},{false,false,false}}, // host enter and leave
        {{true,false,false},{true,true,false},{false,true,true},{false,false,true}}, // host hands occupancy to remote
        {{false,true,true},{true,true,false},{true,false,false},{false,false,false}}, // remote hands occupancy to host
        {{true,true,false},{false,false,false}}, // both leave together: preserve host participation
        {{false,true,true},{false,true,true},{false,false,true}} // multiple remote samples remain remote
    };
    for(const auto& scenario:scenarios) {
        bool previousRemoteOnly=false;
        for(const auto& sample:scenario)
            assert(UpdatePlayerTriggerOrigin(sample.host,sample.remote,previousRemoteOnly)==sample.remoteOrigin);
    }
}
int main() {
    CheckPlayerTriggerOrigins();
    Writer w(MapBegin);w.U32(ProtocolVersion);w.String("00_rainy_hall.map");w.Float(3.5f);w.U8(1);
    Reader r(w.data);assert(r.U32()==ProtocolVersion);assert(r.String()=="00_rainy_hall.map");
    assert(r.Float()==3.5f && r.U8()==1 && r.Done());
    for(size_t n=0;n<w.data.size();++n) {
        std::vector<uint8_t> shortData(w.data.begin(),w.data.begin()+n);
        Reader s(shortData);s.U32();s.String();s.Float();s.U8();assert(!s.Done());
    }
    Writer oversized(Hello);oversized.U32(0xffffffffu);
    Reader length(oversized.data);assert(length.String().empty() && !length.valid);
    Writer embeddedNul(Hello);embeddedNul.String(std::string("a\0b",3));
    Reader nul(embeddedNul.data);nul.String();assert(!nul.valid);
    Writer badFloat(Hello);badFloat.Float(std::numeric_limits<float>::infinity());
    Reader inf(badFloat.data);inf.Float();assert(!inf.valid);
    badFloat.data.resize(1);badFloat.Float(std::numeric_limits<float>::quiet_NaN());
    Reader nan(badFloat.data);nan.Float();assert(!nan.valid);
    for(const char* path:{"../escape.map","maps/../../escape.map","C:/escape.map","/etc/file","\\\\server\\map","a//b","a/./b","a/..","map. ","maps/a:stream.map"})
        assert(!SafeRelativePath(path));
    for(const char* path:{"00_rainy_hall.map","main/00_rainy_hall.map","main\\00_rainy_hall.map"}) assert(SafeRelativePath(path));
    std::string crcText="123456789";
    assert(Checksum(std::vector<uint8_t>(crcText.begin(),crcText.end()))==0xcbf43926u);
    // Exercise hostile lengths and float bit patterns without allocating from the wire.
    uint32_t seed=991;
    for(int i=0;i<30000;++i) {
        std::vector<uint8_t> bytes(1+(i%128));
        for(auto& b:bytes) {seed=1664525*seed+1013904223;b=uint8_t(seed>>24);}
        Reader fuzz(bytes);fuzz.U32();fuzz.String(512);fuzz.Float();fuzz.U8();
        assert(fuzz.pos<=bytes.size());
    }
    std::cout<<"Session protocol: Player trigger enter/leave attribution, bounds, truncation, strings, finite floats, paths, CRC and 30000 malformed packets passed.\n";
}
