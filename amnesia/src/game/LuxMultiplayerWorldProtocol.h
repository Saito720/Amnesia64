#ifndef LUX_MULTIPLAYER_WORLD_PROTOCOL_H
#define LUX_MULTIPLAYER_WORLD_PROTOCOL_H

// Engine-independent, explicit little-endian wire codec. Never deserialize a
// native struct: padding, endianness and malformed floats are not trustworthy.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace LuxWorldWire
{
    enum Type : uint8_t { Pose = 64, Bodies, LeaseRequest, LeaseGrant, LeaseRelease, LeaseDenied, PeerGone, ContactRequest };
    enum BodyFlags : uint8_t { Awake = 1, Active = 2, Gravity = 4, Collide = 8, CollideCharacter = 16 };
    const size_t MaxBodiesPerPacket = 12;
    const size_t MaxLeaseBodies = 64;
    const size_t MaxPacketBytes = 1200;

    inline bool Newer(uint32_t value, uint32_t previous)
    { return value != previous && uint32_t(value - previous) < 0x80000000u; }

    inline uint64_t BodyId(const std::string& name)
    {
        uint64_t hash = 14695981039346656037ull;
        for (size_t i = 0; i < name.size(); ++i) { hash ^= uint8_t(name[i]); hash *= 1099511628211ull; }
        return hash;
    }

    inline uint64_t BodyId(const std::string& name, int32_t authoredId)
    {
        if (authoredId < 0) return BodyId(name); // Procedural bodies without an XML ID.
        uint64_t hash = BodyId(name);
        // Runtime names already include the entity instance name. A NUL domain
        // separator and fixed-width ID distinguish repeated names inside an .ent
        // without depending on allocation order, position or mutable body mass.
        hash *= 1099511628211ull;
        for (unsigned i = 0; i < 4; ++i)
        {
            hash ^= uint8_t(uint32_t(authoredId) >> (i * 8));
            hash *= 1099511628211ull;
        }
        return hash;
    }

    struct Writer
    {
        std::vector<uint8_t> bytes;
        Writer(uint8_t type, uint32_t epoch) { U8(type); U32(epoch); }
        void U8(uint8_t value) { bytes.push_back(value); }
        void U32(uint32_t value) { for (int i = 0; i < 4; ++i) U8(uint8_t(value >> (8 * i))); }
        void U64(uint64_t value) { U32(uint32_t(value)); U32(uint32_t(value >> 32)); }
        void F32(float value) { uint32_t bits; std::memcpy(&bits, &value, 4); U32(bits); }
    };

    struct Reader
    {
        const std::vector<uint8_t>& bytes;
        size_t offset;
        bool valid;
        explicit Reader(const std::vector<uint8_t>& input) : bytes(input), offset(0), valid(input.size() <= MaxPacketBytes) {}
        uint8_t U8() { if (offset >= bytes.size()) { valid = false; return 0; } return bytes[offset++]; }
        uint32_t U32() { uint32_t value = 0; for (int i = 0; i < 4; ++i) value |= uint32_t(U8()) << (8 * i); return value; }
        uint64_t U64() { uint64_t lo = U32(); return lo | (uint64_t(U32()) << 32); }
        float F32(float limit)
        {
            uint32_t bits = U32(); float value; std::memcpy(&value, &bits, 4);
            if (!std::isfinite(value) || std::fabs(value) > limit) { valid = false; return 0; }
            return value;
        }
        bool Done() const { return valid && offset == bytes.size(); }
    };

    // Presentation sampled from the drawn hand light, relative to its player.
    struct Lantern
    {
        bool active = false;
        float offset[3] = {}, color[4] = {}, radius = 0;
    };
    inline void WriteLantern(Writer& writer, const Lantern& light)
    {
        writer.U8(light.active ? 1 : 0);
        if(!light.active) return;
        for(float value : light.offset) writer.F32(value);
        for(float value : light.color) writer.F32(value);
        writer.F32(light.radius);
    }
    inline Lantern ReadLantern(Reader& reader)
    {
        Lantern light;
        const uint8_t active = reader.U8();
        if(active > 1) reader.valid = false;
        light.active = active == 1;
        if(!light.active) return light;
        float distanceSquared = 0;
        for(float& value : light.offset) { value = reader.F32(4); distanceSquared += value * value; }
        for(float& value : light.color) { value = reader.F32(16); if(value < 0) reader.valid = false; }
        light.radius = reader.F32(64);
        if(distanceSquared > 16 || light.radius <= 0) reader.valid = false;
        return light;
    }

    // Gameplay information accompanies the owning player's pose. It never uses
    // interpolated draw transforms. Terror is computed by the host, not sent here.
    enum PlayerFlags : uint8_t { PlayerAlive=1, PlayerCrouching=2, PlayerLantern=4, PlayerProtected=8 };
    struct PlayerState
    {
        uint8_t flags = PlayerAlive;
        uint32_t life = 1;
        float eyeOffset[3] = {}, forward[3] = {0,0,-1}, velocity[3] = {};
        float pitch=0, fov=1.2f, aspect=4.0f/3.0f, speed=0, light=1, health=100;
    };
    inline void WritePlayerState(Writer& writer, const PlayerState& player)
    {
        writer.U8(player.flags);writer.U32(player.life);
        for(float v : player.eyeOffset) writer.F32(v);
        for(float v : player.forward) writer.F32(v);
        for(float v : player.velocity) writer.F32(v);
        writer.F32(player.pitch); writer.F32(player.fov); writer.F32(player.aspect);
        writer.F32(player.speed); writer.F32(player.light); writer.F32(player.health);
    }
    inline PlayerState ReadPlayerState(Reader& reader)
    {
        PlayerState player;
        player.flags=reader.U8();player.life=reader.U32();
        for(float& v : player.eyeOffset) v=reader.F32(8);
        float norm=0;
        for(float& v : player.forward) {v=reader.F32(1.01f);norm+=v*v;}
        for(float& v : player.velocity) v=reader.F32(100);
        player.pitch=reader.F32(100000); player.fov=reader.F32(3.14f); player.aspect=reader.F32(32);
        player.speed=reader.F32(100); player.light=reader.F32(1000); player.health=reader.F32(10000);
        if(!player.life || player.flags>15 || std::fabs(norm-1)>0.025f || player.fov<0.05f || player.aspect<0.02f ||
           player.speed<0 || player.light<0 || player.health<0) reader.valid=false;
        return player;
    }

    struct Body
    {
        uint64_t id;
        float matrix[12]; // 3 rows of a rigid affine transform; last row is 0,0,0,1.
        float linear[3], angular[3];
        uint8_t flags;
        uint8_t wheel = 0;
        float wheelAngle = 0;
        uint8_t wheelStuck = 1; // -1, 0, 1 encoded as 0, 1, 2.
    };

    inline void WriteBody(Writer& writer, const Body& body)
    {
        writer.U64(body.id);
        for (int i = 0; i < 12; ++i) writer.F32(body.matrix[i]);
        for (int i = 0; i < 3; ++i) writer.F32(body.linear[i]);
        for (int i = 0; i < 3; ++i) writer.F32(body.angular[i]);
        writer.U8(body.flags);
        writer.U8(body.wheel);
        if(body.wheel) {writer.F32(body.wheelAngle);writer.U8(body.wheelStuck);}
    }

    inline Body ReadBody(Reader& reader)
    {
        Body body = {}; body.id = reader.U64();
        for (int i = 0; i < 12; ++i) body.matrix[i] = reader.F32(i % 4 == 3 ? 100000.0f : 1.01f);
        for (int i = 0; i < 3; ++i) body.linear[i] = reader.F32(150.0f);
        for (int i = 0; i < 3; ++i) body.angular[i] = reader.F32(150.0f);
        body.flags = reader.U8();
        body.wheel = reader.U8();
        if(body.wheel>1) reader.valid=false;
        if(body.wheel) {
            body.wheelAngle=reader.F32(100000.0f);body.wheelStuck=reader.U8();
            if(body.wheelStuck>2) reader.valid=false;
        }
        if (body.flags & ~(Awake | Active | Gravity | Collide | CollideCharacter)) reader.valid = false;
        // Reject scaled, singular or reflected matrices before Newton sees them.
        for (int row = 0; row < 3; ++row)
        {
            float norm = 0;
            for (int col = 0; col < 3; ++col) norm += body.matrix[4 * row + col] * body.matrix[4 * row + col];
            if (std::fabs(norm - 1) > 0.025f) reader.valid = false;
            for (int other = row + 1; other < 3; ++other)
            {
                float dot = 0;
                for (int col = 0; col < 3; ++col) dot += body.matrix[4 * row + col] * body.matrix[4 * other + col];
                if (std::fabs(dot) > 0.025f) reader.valid = false;
            }
        }
        const float* m = body.matrix;
        float determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
        if (determinant < 0.97f || determinant > 1.03f) reader.valid = false;
        return body;
    }

    // Validate the whole packet before mutating any physics body. This prevents
    // a valid prefix followed by corrupt data from partially changing the world.
    inline bool ReadBodies(Reader& reader, std::vector<Body>& bodies)
    {
        uint8_t count = reader.U8();
        if (count == 0 || count > MaxBodiesPerPacket) return false;
        bodies.clear();
        for (uint8_t i = 0; i < count; ++i)
        {
            Body body = ReadBody(reader);
            for (size_t j = 0; j < bodies.size(); ++j) if (bodies[j].id == body.id) reader.valid = false;
            bodies.push_back(body);
        }
        return reader.Done();
    }
}

#endif
