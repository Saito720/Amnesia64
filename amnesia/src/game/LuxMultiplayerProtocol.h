// Multiplayer wire primitives. No engine types or native struct layouts on the wire.
#ifndef LUX_MULTIPLAYER_PROTOCOL_H
#define LUX_MULTIPLAYER_PROTOCOL_H
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace luxnet {
static const uint32_t ProtocolVersion = 5;
static const uint32_t MaxMapBytes = 16 * 1024 * 1024;
static const uint32_t MapChunkBytes = 32 * 1024;
enum Packet : uint8_t { Hello=1, MapBegin, MapChunk, MapEnd, Ready, Reject,
    MapChangeRequest, ScriptEffect, EntityInteract, ObjectBreak,
    NativeRequest, NativeGrant, NativeResult, EntityState, ItemRemoved, MapRequest,
    MapPreparing, MapCancelled, NativeDiaryResult };

struct Writer {
    std::vector<uint8_t> data;
    explicit Writer(uint8_t type) { U8(type); }
    void U8(uint8_t v) { data.push_back(v); }
    void U32(uint32_t v) { for(int i=0;i<4;++i) U8(uint8_t(v>>(i*8))); }
    void Float(float v) { uint32_t bits; std::memcpy(&bits,&v,4); U32(bits); }
    void String(const std::string& v) { U32(static_cast<uint32_t>(v.size())); data.insert(data.end(),v.begin(),v.end()); }
    void Bytes(const uint8_t* p,size_t n) { if(n) data.insert(data.end(),p,p+n); }
};
struct Reader {
    const std::vector<uint8_t>& data; size_t pos; bool valid;
    explicit Reader(const std::vector<uint8_t>& d):data(d),pos(1),valid(!d.empty()){}
    uint8_t U8() { if(pos>=data.size()) { valid=false; return 0; } return data[pos++]; }
    uint32_t U32() { uint32_t v=0; for(int i=0;i<4;++i) v|=uint32_t(U8())<<(i*8); return v; }
    float Float() { uint32_t bits=U32(); float v; std::memcpy(&v,&bits,4); if(!std::isfinite(v)) valid=false; return v; }
    std::string String(size_t max=1024) {
        uint32_t n=U32(); if(!valid || n>max || n>data.size()-pos) {valid=false;return "";}
        std::string s(data.begin()+pos,data.begin()+pos+n); pos+=n;
        if(s.find('\0')!=std::string::npos) valid=false;
        return s;
    }
    bool Done() const { return valid && pos==data.size(); }
};
inline uint32_t Checksum(const std::vector<uint8_t>& data) {
    uint32_t crc=0xffffffffu;
    for(uint8_t b:data) { crc^=b; for(int i=0;i<8;++i) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u))); }
    return ~crc;
}
inline bool SafeRelativePath(const std::string& path) {
    if(path.empty() || path.size()>512 || path[0]=='/' || path[0]=='\\') return false;
    if(path.find(':')!=std::string::npos || path.find('\0')!=std::string::npos) return false;
    size_t start=0;
    for(size_t i=0;i<=path.size();++i) {
        if(i<path.size() && static_cast<unsigned char>(path[i])<32) return false;
        if(i==path.size() || path[i]=='/' || path[i]=='\\') {
            std::string part=path.substr(start,i-start);
            if(part.empty() || part==".." || part=="." || part.back()=='.' || part.back()==' ') return false;
            start=i+1;
        }
    }
    return true;
}
}
#endif
