#ifndef LUX_MULTIPLAYER_EFFECTS_PROTOCOL_H
#define LUX_MULTIPLAYER_EFFECTS_PROTOCOL_H
#include "LuxMultiplayerProtocol.h"

namespace luxfx {
enum Kind : uint8_t { Sound=1, Particle, PlayerSound };
enum Operation : uint8_t { Create=1, State, Remove, DeclareSound };
enum Origin : uint8_t { World=0, Player };
enum Flags : uint8_t { Active=1, Visible=2, Stopped=4, Dying=8, Loop=16, AutoRemove=32, SoundStart=64, SoundEnd=128 };
struct Effect {
    uint32_t epoch=0, peer=0, id=0;
    uint8_t operation=Create, kind=Sound, origin=World, flags=Active|Visible|AutoRemove;
    std::string asset, name;
    float matrix[12]={1,0,0,0,0,1,0,0,0,0,1,0};
    float size[3]={1,1,1}, color[4]={1,1,1,1};
    float volume=1, minimum=1, maximum=20;
};
inline void Write(luxnet::Writer& w,const Effect& e) {
    w.U32(e.epoch);w.U32(e.peer);w.U32(e.id);w.U8(e.operation);w.U8(e.kind);w.U8(e.origin);
    if(e.operation==Remove) return;
    if(e.operation==Create || e.operation==DeclareSound) {w.String(e.asset);w.String(e.name);}
    w.U8(e.flags);
    for(float f:e.matrix) w.Float(f);
    for(float f:e.size) w.Float(f);
    for(float f:e.color) w.Float(f);
    w.Float(e.volume);w.Float(e.minimum);w.Float(e.maximum);
}
inline bool Read(luxnet::Reader& r,Effect& e) {
    e.epoch=r.U32();e.peer=r.U32();e.id=r.U32();e.operation=r.U8();e.kind=r.U8();e.origin=r.U8();
    if(!r.valid || !e.id || e.operation<Create || e.operation>DeclareSound || e.kind<Sound || e.kind>PlayerSound ||
       e.origin>Player ||
       (e.operation==DeclareSound && e.kind!=Sound) ||
       (e.kind==PlayerSound && (e.operation!=Create || e.origin!=Player))) return false;
    if(e.operation==Remove) return r.Done();
    if(e.operation==Create || e.operation==DeclareSound) {
        e.asset=r.String(512);e.name=r.String(256);
        if(e.asset.empty() || !luxnet::SafeRelativePath(e.asset)) return false;
    }
    e.flags=r.U8();
    if((e.kind!=Sound && (e.flags&(SoundStart|SoundEnd))) ||
       ((e.flags&SoundStart) && (e.flags&(Stopped|SoundEnd))) ||
       ((e.flags&SoundEnd) && !(e.flags&Stopped))) return false;
    for(unsigned i=0;i<12;++i) {e.matrix[i]=r.Float();if(std::fabs(e.matrix[i])>(i%4==3?100000:100)) return false;}
    for(float& f:e.size) {f=r.Float();if(f<=0 || f>1000) return false;}
    for(float& f:e.color) {f=r.Float();if(f<0 || f>16) return false;}
    e.volume=r.Float();e.minimum=r.Float();e.maximum=r.Float();
    return r.Done() && e.volume>=0 && e.volume<=16 && e.minimum>=0 && e.maximum>=e.minimum && e.maximum<=10000;
}
inline std::vector<uint8_t> Packet(const Effect& e) {luxnet::Writer w(luxnet::WorldEffect);Write(w,e);return w.data;}
}
#endif
