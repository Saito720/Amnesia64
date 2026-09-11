#include "../amnesia/src/game/LuxMultiplayerProtocol.h"
#include "../amnesia/src/game/LuxMultiplayerEntityProtocol.h"
#include "../amnesia/src/game/LuxMultiplayerTriggerPolicy.h"
#include "../amnesia/src/game/LuxMultiplayerMapHash.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace luxnet;
static std::vector<uint8_t> NativePacket(const NativeState& state) {
    Writer w(EntityState);w.U32(state.epoch);w.String(state.name);
    w.U8(state.kind);w.U8(state.flags);w.U8(state.detail);
    w.U32(static_cast<uint32_t>(state.joints.size()));
    for(const auto& j:state.joints) {
        w.U32(j.index);w.U8(j.kind);w.U8(j.flags);w.Float(j.min);w.Float(j.max);
    }
    return w.data;
}
static bool DecodeNative(const std::vector<uint8_t>& bytes) {
    Reader reader(bytes);NativeState result;
    return ReadNativeState(reader,result);
}
static void CheckNativeEntityStates() {
    NativeState basic={42,"bookshelf",PropState,EntityActive|EffectsActive,0,{}};
    assert(DecodeNative(NativePacket(basic)));
    for(uint8_t kind=PropState;kind<=DoorState;++kind) {
        NativeState state=basic;state.kind=kind;
        state.detail=kind==DoorState ? 7 : kind==LampState ? 1 : 0;
        state.flags=15;assert(DecodeNative(NativePacket(state)));
        ++state.detail;assert(!DecodeNative(NativePacket(state)));
    }
    NativeState jointed=basic;
    jointed.joints={{0,1,0,-1.5f,1.5f},{3,2,7,-100000,100000}};
    const std::vector<uint8_t> valid=NativePacket(jointed);
    Reader roundTrip(valid);NativeState decoded;
    assert(ReadNativeState(roundTrip,decoded) && roundTrip.Done());
    assert(decoded.epoch==42 && decoded.name=="bookshelf" && decoded.joints.size()==2);
    assert(decoded.joints[1].index==3 && decoded.joints[1].max==100000);
    // A reliable state must be complete before any of its discrete settings apply.
    for(size_t n=0;n<valid.size();++n)
        assert(!DecodeNative(std::vector<uint8_t>(valid.begin(),valid.begin()+n)));
    std::vector<uint8_t> trailing=valid;trailing.push_back(0);assert(!DecodeNative(trailing));

    NativeState invalid=jointed;invalid.kind=3;assert(!DecodeNative(NativePacket(invalid)));
    invalid=jointed;invalid.flags=16;assert(!DecodeNative(NativePacket(invalid)));
    for(const std::string& name:{std::string(),std::string(257,'x'),std::string("door\0hidden",11)}) {
        invalid=jointed;invalid.name=name;assert(!DecodeNative(NativePacket(invalid)));
    }
    invalid=basic;invalid.name=std::string(256,'x');assert(DecodeNative(NativePacket(invalid)));
    for(uint8_t kind:{uint8_t(0),uint8_t(3),uint8_t(255)}) {
        invalid=jointed;invalid.joints[0].kind=kind;assert(!DecodeNative(NativePacket(invalid)));
    }
    invalid=jointed;invalid.joints[0].flags=8;assert(!DecodeNative(NativePacket(invalid)));
    for(uint32_t index:{0u,128u,0xffffffffu}) {
        invalid=jointed;invalid.joints[1].index=index;assert(!DecodeNative(NativePacket(invalid)));
    }
    invalid=jointed;invalid.joints[0].index=4;assert(!DecodeNative(NativePacket(invalid)));
    for(float value:{100001.0f,-100001.0f,std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        invalid=jointed;invalid.joints[0].min=value;assert(!DecodeNative(NativePacket(invalid)));
        invalid=jointed;invalid.joints[0].max=value;assert(!DecodeNative(NativePacket(invalid)));
    }
    NativeState maximum=basic;
    for(uint32_t i=0;i<128;++i) maximum.joints.push_back({i,1,7,-1,1});
    assert(DecodeNative(NativePacket(maximum)));
    maximum.joints.push_back({128,1,0,0,1});assert(!DecodeNative(NativePacket(maximum)));
    Writer hugeCount(EntityState);hugeCount.U32(42);hugeCount.String("door");
    hugeCount.U8(DoorState);hugeCount.U8(15);hugeCount.U8(7);hugeCount.U32(0xffffffffu);
    assert(!DecodeNative(hugeCount.data));

    // Mutating valid states reaches joint decoding as well as header validation.
    uint32_t random=813;
    for(unsigned i=0;i<10000;++i) {
        auto bytes=valid;
        random=1664525*random+1013904223;const size_t at=1+random%(bytes.size()-1);
        random=1664525*random+1013904223;bytes[at]=static_cast<uint8_t>(random>>24);
        Reader reader(bytes);NativeState state;
        if(ReadNativeState(reader,state)) {
            assert(reader.Done() && state.joints.size()<=128 && !state.name.empty() && state.name.size()<=256);
            uint32_t previous=0;
            for(size_t n=0;n<state.joints.size();++n) {
                const auto& j=state.joints[n];
                assert(j.index<128 && (n==0 || j.index>previous));
                assert(std::isfinite(j.min) && std::isfinite(j.max));previous=j.index;
            }
        }
        assert(reader.pos<=bytes.size());
    }
}
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
static void CheckMapHashes() {
    const auto textHash=[](const std::string& text) {
        return MapHash(std::vector<uint8_t>(text.begin(),text.end()));
    };
    // Standard SHA-256 known-answer vectors cover empty input, padding that
    // requires another block, and messages spanning many compression blocks.
    assert(textHash("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(textHash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(textHash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    assert(textHash("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                    "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")==
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    assert(textHash(std::string(1000000,'a'))==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // Byte sequences 0..length-1 exercise embedded NUL/high-bit bytes and both
    // sides of SHA-256's padding/block boundaries. Expected hashes were also
    // checked independently with System.Security.Cryptography.SHA256.
    struct BinaryVector {size_t length;const char* hash;};
    const BinaryVector samples[]={
        {55,"463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"},
        {56,"da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"},
        {63,"29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"},
        {64,"fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"},
        {65,"4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"},
        {256,"40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880"}
    };
    for(const auto& sample:samples) {
        std::vector<uint8_t> bytes(sample.length);
        for(size_t i=0;i<bytes.size();++i) bytes[i]=uint8_t(i);
        const std::string digest=MapHash(bytes);
        assert(digest==sample.hash && ValidMapHash(digest));
        bytes.back()^=1;assert(MapHash(bytes)!=digest);
    }
    assert(ValidMapHash(std::string(64,'0')) && ValidMapHash(std::string(64,'f')));
    for(const std::string& bad:{std::string(),std::string(63,'0'),std::string(65,'0'),
        std::string(64,'A'),std::string(64,'g'),std::string(64,'/'),std::string(64,'\0')})
        assert(!ValidMapHash(bad));
    std::string embedded=textHash("abc");embedded[32]='\0';assert(!ValidMapHash(embedded));
}
int main() {
    CheckMapHashes();
    CheckPlayerTriggerOrigins();
    CheckNativeEntityStates();
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
    std::cout<<"Session protocol: SHA-256 known vectors, Player trigger attribution, native entity/joint states (10000 mutations), bounds, truncation, strings, finite floats, paths, CRC and 30000 malformed packets passed.\n";
}
