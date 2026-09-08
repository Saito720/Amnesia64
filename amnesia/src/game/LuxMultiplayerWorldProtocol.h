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
    enum Type : uint8_t { Pose = 64, Bodies, LeaseRequest, LeaseGrant, LeaseRelease, LeaseDenied, PeerGone };
    enum BodyFlags : uint8_t { Awake = 1, Active = 2, Gravity = 4 };
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

    struct Body
    {
        uint64_t id;
        float matrix[12]; // 3 rows of a rigid affine transform; last row is 0,0,0,1.
        float linear[3], angular[3];
        uint8_t flags;
    };

    inline void WriteBody(Writer& writer, const Body& body)
    {
        writer.U64(body.id);
        for (int i = 0; i < 12; ++i) writer.F32(body.matrix[i]);
        for (int i = 0; i < 3; ++i) writer.F32(body.linear[i]);
        for (int i = 0; i < 3; ++i) writer.F32(body.angular[i]);
        writer.U8(body.flags);
    }

    inline Body ReadBody(Reader& reader)
    {
        Body body = {}; body.id = reader.U64();
        for (int i = 0; i < 12; ++i) body.matrix[i] = reader.F32(i % 4 == 3 ? 100000.0f : 1.01f);
        for (int i = 0; i < 3; ++i) body.linear[i] = reader.F32(150.0f);
        for (int i = 0; i < 3; ++i) body.angular[i] = reader.F32(150.0f);
        body.flags = reader.U8();
        if (body.flags & ~(Awake | Active | Gravity)) reader.valid = false;
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
