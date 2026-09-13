#ifndef LUX_MULTIPLAYER_ENEMY_PROTOCOL_H
#define LUX_MULTIPLAYER_ENEMY_PROTOCOL_H

#include "LuxMultiplayerProtocol.h"
#include <set>

// Enemy replicas contain presentation and authoritative gameplay results, never
// state-machine messages or native pointers. Each packet is independently valid.
namespace LuxEnemyWire {
const size_t MaxPacketBytes = 16384;
const size_t MaxAnimations = 32;
const size_t MaxLights = 32;
const size_t MaxEnemies = 2048;
enum Flags : uint32_t {
    Active = 1, Disabled = 2, Visible = 4, MeshActive = 8,
    CharacterActive = 16, Collide = 32, CollideCharacter = 64,
    DisableTriggers = 128, Hallucination = 256, SanityDecrease = 512,
    PlayerDetected = 1024, PlayerInRange = 2048, CanSeePlayer = 4096,
    TargetTerror = 8192, AttackMusic = 16384, SearchMusic = 32768,
    // Reliable lifecycle snapshots follow the reliable script creation stream.
    // This is ordering metadata, not a persistent property of the enemy.
    Baseline = 65536
};
const uint32_t AllFlags = 131071;
enum AnimationFlags : uint8_t { Loop = 1, Paused = 2 };

struct Animation {
    std::string name;
    uint8_t flags = 0;
    float time = 0, length = 0, speed = 1, baseSpeed = 1, weight = 1, fadeStep = 0;
};
enum LightFlags : uint8_t { LightActive = 1, LightVisible = 2 };
struct Light {
    std::string name;
    uint8_t flags = 0;
    float color[4] = {1,1,1,1}, radius = 1;
    float matrix[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
};
struct State {
    uint32_t epoch = 0, sequence = 0;
    uint64_t generation = 0;
    std::string name;
    uint8_t type = 0, state = 0;
    uint32_t target = UINT32_MAX, flags = 0;
    float health = 0, position[3] = {}, yaw = 0;
    float mesh[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
    float illumination = 1, coverage = 1;
    std::string currentAnimation;
    std::vector<Animation> animations;
    std::vector<Light> lights;
};
struct Removed {
    uint32_t epoch = 0, sequence = 0;
    uint64_t generation = 0;
    std::string name;
};
inline bool Newer(uint32_t value, uint32_t previous) {
    return value != previous && uint32_t(value - previous) < 0x80000000u;
}
inline void U64(luxnet::Writer& w, uint64_t value) {
    w.U32(uint32_t(value)); w.U32(uint32_t(value >> 32));
}
inline uint64_t U64(luxnet::Reader& r) {
    const uint64_t low = r.U32(); return low | (uint64_t(r.U32()) << 32);
}
inline float Float(luxnet::Reader& r, float limit, bool nonnegative = false) {
    const float value = r.Float();
    if(std::fabs(value) > limit || (nonnegative && value < 0)) r.valid = false;
    return value;
}
inline bool Name(const std::string& name) {
    if(name.empty()) return false;
    for(unsigned char c : name) if(c < 32 || c == 127) return false;
    return true;
}
inline std::vector<uint8_t> EncodeState(const State& s) {
    luxnet::Writer w(luxnet::EnemyState);
    w.U32(s.epoch); w.U32(s.sequence); U64(w,s.generation); w.String(s.name);
    w.U8(s.type); w.U8(s.state); w.U32(s.target); w.U32(s.flags); w.Float(s.health);
    for(float f : s.position) w.Float(f);
    w.Float(s.yaw);
    for(float f : s.mesh) w.Float(f);
    w.Float(s.illumination); w.Float(s.coverage);
    w.String(s.currentAnimation); w.U32(uint32_t(s.animations.size()));
    for(const auto& a : s.animations) {
        w.String(a.name); w.U8(a.flags); w.Float(a.time); w.Float(a.length);
        w.Float(a.speed); w.Float(a.baseSpeed); w.Float(a.weight); w.Float(a.fadeStep);
    }
    w.U32(uint32_t(s.lights.size()));
    for(const auto& light : s.lights) {
        w.String(light.name); w.U8(light.flags);
        for(float f : light.color) w.Float(f);
        w.Float(light.radius);
        for(float f : light.matrix) w.Float(f);
    }
    return w.data;
}
inline bool DecodeState(const std::vector<uint8_t>& bytes, State& output) {
    if(bytes.empty() || bytes[0] != luxnet::EnemyState || bytes.size() > MaxPacketBytes) return false;
    luxnet::Reader r(bytes); State s;
    s.epoch = r.U32(); s.sequence = r.U32(); s.generation = U64(r); s.name = r.String(256);
    s.type = r.U8(); s.state = r.U8(); s.target = r.U32(); s.flags = r.U32();
    s.health = Float(r,1000000);
    for(float& f : s.position) f = Float(r,100000);
    s.yaw = Float(r,1000000);
    for(int i=0;i<12;++i) s.mesh[i] = Float(r,i%4==3 ? 100000.0f : 1000.0f);
    s.illumination = Float(r,100,true); s.coverage = Float(r,1,true);
    s.currentAnimation = r.String(128);
    const uint32_t count = r.U32();
    // These enum counts are protocol constants, checked against game enums by
    // the adapter. Never allocate according to an untrusted layer count.
    if(!r.valid || count > MaxAnimations || s.type >= 3 || s.state >= 21 ||
       (s.flags & ~AllFlags) || !s.generation || !Name(s.name) ||
       (!s.currentAnimation.empty() && !Name(s.currentAnimation))) return false;
    std::set<std::string> names;
    for(uint32_t i=0;i<count;++i) {
        Animation a;
        a.name = r.String(128); a.flags = r.U8();
        a.time = Float(r,86400,true); a.length = Float(r,86400,true);
        a.speed = Float(r,1000); a.baseSpeed = Float(r,1000);
        a.weight = Float(r,100,true); a.fadeStep = Float(r,10000);
        if(!Name(a.name) || !names.insert(a.name).second || (a.flags & ~(Loop|Paused)) ||
           a.length <= 0 || a.time > a.length + 0.001f) r.valid = false;
        s.animations.push_back(a);
    }
    const uint32_t lights = r.U32();
    if(!r.valid || lights > MaxLights) return false;
    names.clear();
    for(uint32_t i=0;i<lights;++i) {
        Light light;
        light.name = r.String(256); light.flags = r.U8();
        for(float& f : light.color) f = Float(r,100,true);
        light.radius = Float(r,10000,true);
        for(int j=0;j<12;++j) light.matrix[j] = Float(r,j%4==3 ? 100000.0f : 1000.0f);
        if(!Name(light.name) || !names.insert(light.name).second || (light.flags & ~(LightActive|LightVisible))) r.valid = false;
        s.lights.push_back(light);
    }
    if(!r.Done()) return false;
    output = s; return true;
}
inline std::vector<uint8_t> EncodeRemoved(const Removed& s) {
    luxnet::Writer w(luxnet::EnemyRemoved);
    w.U32(s.epoch); w.U32(s.sequence); U64(w,s.generation); w.String(s.name);
    return w.data;
}
inline bool DecodeRemoved(const std::vector<uint8_t>& bytes, Removed& output) {
    if(bytes.empty() || bytes[0] != luxnet::EnemyRemoved || bytes.size() > 512) return false;
    luxnet::Reader r(bytes); Removed s;
    s.epoch = r.U32(); s.sequence = r.U32(); s.generation = U64(r); s.name = r.String(256);
    if(!r.Done() || !s.generation || !Name(s.name)) return false;
    output = s; return true;
}
// Phase/time, motion, and blend weights are continuous. Changes to the layer
// set, state, visibility or identity require reliable delivery.
inline bool SameDiscreteState(const State& a, const State& b) {
    if(a.generation != b.generation || a.type != b.type || a.state != b.state ||
       a.target != b.target || (a.flags & ~Baseline) != (b.flags & ~Baseline) || a.health != b.health ||
       a.currentAnimation != b.currentAnimation || a.animations.size() != b.animations.size() || a.lights.size() != b.lights.size()) return false;
    for(size_t i=0;i<a.animations.size();++i)
        if(a.animations[i].name != b.animations[i].name || a.animations[i].flags != b.animations[i].flags ||
           (!(b.animations[i].flags & Loop) && b.animations[i].time + 0.001f < a.animations[i].time)) return false;
    for(size_t i=0;i<a.lights.size();++i)
        if(a.lights[i].name != b.lights[i].name || a.lights[i].flags != b.lights[i].flags) return false;
    return true;
}
}
#endif
