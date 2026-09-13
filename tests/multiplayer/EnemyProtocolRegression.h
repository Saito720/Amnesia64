#ifndef MULTIPLAYER_ENEMY_PROTOCOL_REGRESSION_H
#define MULTIPLAYER_ENEMY_PROTOCOL_REGRESSION_H
#include "../../amnesia/src/game/LuxMultiplayerEnemyProtocol.h"
#include <limits>

inline bool RunEnemyProtocolRegression(std::string& error)
{
    using namespace LuxEnemyWire;
    State state;
    state.epoch=7;state.sequence=0xfffffffeu;state.generation=0x123456789abcdef0ull;
    state.name="codex_enemy_codec";state.type=1;state.state=12;state.target=42;
    state.flags=Active|Visible|CharacterActive|PlayerDetected;state.health=73.5f;
    state.position[0]=-27.25f;state.position[1]=40.925f;state.position[2]=9;state.yaw=1.2f;
    state.mesh[3]=state.position[0];state.mesh[7]=40;state.mesh[11]=state.position[2];
    Animation animation;animation.name="SwingClaws01";animation.time=0.42f;animation.length=1.35f;
    animation.weight=0.7f;animation.fadeStep=0.3f;state.currentAnimation=animation.name;
    state.animations.push_back(animation);
    Light light;light.name="EnemyPointLight";light.flags=LightActive|LightVisible;light.radius=8;
    light.color[0]=0.75f;light.matrix[7]=41;state.lights.push_back(light);
    animation.name="Idle";animation.flags=Loop;animation.weight=0.3f;animation.fadeStep=-0.3f;
    state.animations.push_back(animation);
    State decoded;const auto bytes=EncodeState(state);
    auto fail=[&](const char* text) {error=std::string("enemy codec: ")+text;return false;};
    if(!DecodeState(bytes,decoded) || EncodeState(decoded)!=bytes)
        return fail("a mid-attack layered-animation baseline did not round-trip exactly");
    for(size_t length=0;length<bytes.size();++length) {
        State sentinel;sentinel.name="unchanged";
        if(DecodeState(std::vector<uint8_t>(bytes.begin(),bytes.begin()+length),sentinel) || sentinel.name!="unchanged")
            return fail("a truncated snapshot was accepted or partially changed output");
    }
    auto invalid=bytes;invalid.push_back(0);
    if(DecodeState(invalid,decoded)) return fail("snapshot trailing bytes were accepted");
    invalid=bytes;invalid[0]=luxnet::EnemyRemoved;
    if(DecodeState(invalid,decoded)) return fail("another message type was accepted as a snapshot");
    auto reject=[&](const State& value) {return !DecodeState(EncodeState(value),decoded);};
    State changed=state;changed.generation=0;
    if(!reject(changed)) return fail("zero identity generation was accepted");
    changed=state;changed.type=255;
    if(!reject(changed)) return fail("unknown enemy type was accepted");
    changed=state;changed.state=255;
    if(!reject(changed)) return fail("unknown AI state was accepted");
    changed=state;changed.flags|=0x80000000u;
    if(!reject(changed)) return fail("unknown gameplay flags were accepted");
    changed=state;changed.name="invalid\nentity";
    if(!reject(changed)) return fail("control characters in identity were accepted");
    changed=state;changed.animations.push_back(changed.animations.front());
    if(!reject(changed)) return fail("duplicate animation layers were accepted");
    changed=state;changed.animations[0].time=changed.animations[0].length+1;
    if(!reject(changed)) return fail("an animation phase past its length was accepted");
    changed=state;changed.animations[0].weight=-1;
    if(!reject(changed)) return fail("a negative animation weight was accepted");
    changed=state;changed.animations[0].time=changed.animations[0].length=0;
    if(!reject(changed)) return fail("a zero-length active animation was accepted");
    changed=state;changed.animations[0].flags=128;
    if(!reject(changed)) return fail("unknown animation flags were accepted");
    changed=state;changed.animations.resize(MaxAnimations+1,animation);
    if(!reject(changed)) return fail("excess animation layers were accepted");
    changed=state;changed.coverage=1.01f;
    if(!reject(changed)) return fail("mesh fade coverage outside its valid range was accepted");
    changed=state;changed.lights.push_back(light);
    if(!reject(changed)) return fail("duplicate child lights were accepted");
    changed=state;changed.lights[0].radius=-1;
    if(!reject(changed)) return fail("a negative light radius was accepted");
    changed=state;changed.lights[0].color[2]=-1;
    if(!reject(changed)) return fail("a negative light intensity was accepted");
    changed=state;changed.lights[0].flags=128;
    if(!reject(changed)) return fail("unknown child-light flags were accepted");
    changed=state;changed.lights.resize(MaxLights+1,light);
    if(!reject(changed)) return fail("excess child lights were accepted");
    for(float bad : {std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        changed=state;changed.position[0]=bad;if(!reject(changed)) return fail("non-finite position was accepted");
        changed=state;changed.mesh[0]=bad;if(!reject(changed)) return fail("non-finite mesh transform was accepted");
        changed=state;changed.animations[0].time=bad;if(!reject(changed)) return fail("non-finite animation phase was accepted");
        changed=state;changed.lights[0].color[0]=bad;if(!reject(changed)) return fail("non-finite light color was accepted");
    }
    Removed removed;removed.epoch=state.epoch;removed.sequence=1;removed.generation=state.generation;removed.name=state.name;
    Removed decodedRemoved;const auto removal=EncodeRemoved(removed);
    if(!DecodeRemoved(removal,decodedRemoved) || EncodeRemoved(decodedRemoved)!=removal)
        return fail("destruction identity did not round-trip");
    for(size_t length=0;length<removal.size();++length) {
        Removed sentinel;sentinel.name="unchanged";
        if(DecodeRemoved(std::vector<uint8_t>(removal.begin(),removal.begin()+length),sentinel) || sentinel.name!="unchanged")
            return fail("a truncated destruction was accepted or partially changed output");
    }
    if(!Newer(1,0xfffffffeu) || Newer(0xfffffffeu,1) || Newer(7,7) || Newer(0x80000000u,0))
        return fail("sequence wrap/reordering comparison is ambiguous");
    return true;
}
#endif
