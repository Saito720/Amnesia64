#ifndef LUX_MULTIPLAYER_ENTITY_PROTOCOL_H
#define LUX_MULTIPLAYER_ENTITY_PROTOCOL_H
#include "LuxMultiplayerProtocol.h"

namespace luxnet {
enum EntityKind : uint8_t { PropState=0, LampState, DoorState };
enum EntityFlags : uint8_t { EntityActive=1, InteractionDisabled=2, EffectsActive=4, StaticPhysics=8 };
// kind 0 is a deleted authored joint slot; live hinge/slider states are 1/2.
struct JointState { uint32_t index; uint8_t kind, flags; float min, max; };
struct JointBreakState { uint32_t epoch, index, token; uint64_t body; std::string name; };
inline std::vector<uint8_t> WriteJointBreak(const JointBreakState& state) {
    Writer w(JointBreakRequest);w.U32(state.epoch);w.String(state.name);w.U32(state.index);
    w.U32(static_cast<uint32_t>(state.body));w.U32(static_cast<uint32_t>(state.body>>32));w.U32(state.token);
    return w.data;
}
inline bool ReadJointBreak(Reader& r,JointBreakState& state) {
    if(r.data.size()>512) return false;
    state.epoch=r.U32();state.name=r.String(256);state.index=r.U32();
    state.body=r.U32();state.body|=uint64_t(r.U32())<<32;state.token=r.U32();
    return r.Done() && !state.name.empty() && state.index<128 && state.token!=0;
}
struct NativeState {
    uint32_t epoch;
    std::string name;
    uint8_t kind, flags, detail;
    std::vector<JointState> joints;
};
inline bool ReadNativeState(Reader& r, NativeState& state) {
    state.epoch=r.U32();state.name=r.String(256);
    state.kind=r.U8();state.flags=r.U8();state.detail=r.U8();
    uint32_t count=r.U32();
    if(!r.valid || state.name.empty() || state.kind>DoorState || state.flags>15 ||
       (state.kind==PropState && state.detail!=0) || (state.kind==LampState && state.detail>1) ||
       (state.kind==DoorState && state.detail>7) || count>128) return false;
    state.joints.clear();
    for(uint32_t i=0;i<count;++i) {
        JointState j;j.index=r.U32();j.kind=r.U8();j.flags=r.U8();j.min=r.Float();j.max=r.Float();
        if(!r.valid || j.index>=128 || j.kind>2 || j.flags>7 ||
           (j.kind==0 && (j.flags!=0 || j.min!=0 || j.max!=0)) ||
           std::fabs(j.min)>100000 || std::fabs(j.max)>100000 ||
           (!state.joints.empty() && j.index<=state.joints.back().index)) return false;
        state.joints.push_back(j);
    }
    return r.Done();
}
}
#endif
