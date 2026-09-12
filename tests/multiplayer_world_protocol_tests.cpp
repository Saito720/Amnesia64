#include "../amnesia/src/game/LuxMultiplayerWorldProtocol.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace LuxWorldWire;

static Body Sample(uint64_t id = 123)
{
    Body body = {};
    body.id = id;
    body.matrix[0] = body.matrix[5] = body.matrix[10] = 1;
    body.matrix[3] = 4; body.matrix[7] = -2; body.matrix[11] = 1;
    body.linear[0] = 2.5f; body.angular[2] = -0.25f;
    body.flags = Awake | Active | Gravity | Collide | CollideCharacter;
    return body;
}

static std::vector<uint8_t> Encode(const Body& body)
{
    Writer writer(Bodies, 0x12345678); writer.U32(0xffffffff); writer.U32(0); writer.U8(0); writer.U8(1);
    WriteBody(writer, body); return writer.bytes;
}

static bool Decode(const std::vector<uint8_t>& bytes, std::vector<Body>& bodies)
{
    Reader reader(bytes);
    if (reader.U8() != Bodies || reader.U32() != 0x12345678) return false;
    reader.U32(); reader.U32(); if (reader.U8() > 1) return false;
    return ReadBodies(reader, bodies);
}

int main()
{
    const auto encodeLantern=[](const Lantern& light) {
        Writer writer(Pose,7);WriteLantern(writer,light);return writer.bytes;
    };
    const auto decodeLantern=[](const std::vector<uint8_t>& bytes,Lantern& light) {
        Reader reader(bytes);if(reader.U8()!=Pose || reader.U32()!=7) return false;
        light=ReadLantern(reader);return reader.Done();
    };
    Lantern lantern, decoded;
    assert(decodeLantern(encodeLantern(lantern),decoded) && !decoded.active);
    lantern.active=true;lantern.offset[1]=0.75f;lantern.color[0]=0.845f;
    lantern.color[1]=0.69f;lantern.color[2]=0.155f;lantern.color[3]=0.689f;lantern.radius=10;
    auto lanternBytes=encodeLantern(lantern);
    assert(decodeLantern(lanternBytes,decoded) && decoded.active && decoded.radius==10 && decoded.color[0]==lantern.color[0]);
    for(size_t n=0;n<lanternBytes.size();++n)
        assert(!decodeLantern(std::vector<uint8_t>(lanternBytes.begin(),lanternBytes.begin()+n),decoded));
    auto extraLantern=lanternBytes;extraLantern.push_back(0);assert(!decodeLantern(extraLantern,decoded));
    extraLantern=lanternBytes;extraLantern[5]=2;assert(!decodeLantern(extraLantern,decoded));
    Lantern invalidLight=lantern;invalidLight.radius=0;assert(!decodeLantern(encodeLantern(invalidLight),decoded));
    invalidLight=lantern;invalidLight.radius=65;assert(!decodeLantern(encodeLantern(invalidLight),decoded));
    invalidLight=lantern;invalidLight.color[1]=-0.01f;assert(!decodeLantern(encodeLantern(invalidLight),decoded));
    invalidLight=lantern;invalidLight.offset[0]=4;invalidLight.offset[1]=4;assert(!decodeLantern(encodeLantern(invalidLight),decoded));
    invalidLight=lantern;invalidLight.color[0]=std::numeric_limits<float>::quiet_NaN();assert(!decodeLantern(encodeLantern(invalidLight),decoded));

    assert(BodyId("Player") == 3692324345213718176ull);
    assert(BodyId("crate_1") != BodyId("crate_2"));
    assert(BodyId("cabinet_Body", -1) == BodyId("cabinet_Body"));
    assert(BodyId("cabinet_Body", 0) != BodyId("cabinet_Body", -1));
    for (int id : {17, 32, 33, 34})
    {
        assert(BodyId("cabinet_Body", id) != BodyId("other_cabinet_Body", id));
        assert(BodyId("cabinet_Body", id) != BodyId("cabinet_Body" + std::to_string(id)));
        for (int other : {17, 32, 33, 34})
            if (id != other) assert(BodyId("cabinet_Body", id) != BodyId("cabinet_Body", other));
    }
    assert(Newer(0, 0xffffffff));
    assert(!Newer(0xffffffff, 0));
    assert(!Newer(42, 42));
    assert(!Newer(0x80000000, 0));
    std::vector<Body> bodies;
    std::vector<uint8_t> bytes = Encode(Sample());
    assert(bytes[1] == 0x78 && bytes[2] == 0x56 && bytes[3] == 0x34 && bytes[4] == 0x12);
    assert(Decode(bytes, bodies) && bodies.size() == 1);
    assert(bodies[0].id == 123 && bodies[0].matrix[7] == -2 && bodies[0].linear[0] == 2.5f);
    assert(bodies[0].flags == (Awake | Active | Gravity | Collide | CollideCharacter));
    for (size_t length = 0; length < bytes.size(); ++length)
        assert(!Decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + length), bodies));
    std::vector<uint8_t> trailing = bytes; trailing.push_back(0);
    assert(!Decode(trailing, bodies));
    std::vector<uint8_t> tooLarge(MaxPacketBytes + 1, 0);
    assert(!Decode(tooLarge, bodies));

    const float invalid[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), 151.0f, -151.0f };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        Body body = Sample(); body.linear[0] = invalid[i]; assert(!Decode(Encode(body), bodies));
        body = Sample(); body.angular[2] = invalid[i]; assert(!Decode(Encode(body), bodies));
    }
    Body malformed = Sample(); malformed.matrix[0] = 0; assert(!Decode(Encode(malformed), bodies));
    malformed = Sample(); malformed.matrix[0] = -1; assert(!Decode(Encode(malformed), bodies));
    malformed = Sample(); malformed.matrix[4] = 1; assert(!Decode(Encode(malformed), bodies));
    malformed = Sample(); malformed.matrix[3] = 100001; assert(!Decode(Encode(malformed), bodies));
    malformed = Sample(); malformed.flags = 0x80; assert(!Decode(Encode(malformed), bodies));

    Writer packet(Bodies, 0x12345678); packet.U32(1); packet.U32(0); packet.U8(0); packet.U8(MaxBodiesPerPacket);
    for (size_t i = 0; i < MaxBodiesPerPacket; ++i) WriteBody(packet, Sample(i));
    assert(packet.bytes.size() <= MaxPacketBytes);
    assert(Decode(packet.bytes, bodies) && bodies.size() == MaxBodiesPerPacket);
    packet.bytes[14] = MaxBodiesPerPacket + 1;
    assert(!Decode(packet.bytes, bodies));
    packet.bytes[14] = 0;
    assert(!Decode(packet.bytes, bodies));
    Writer duplicate(Bodies, 0x12345678); duplicate.U32(1); duplicate.U32(0); duplicate.U8(0); duplicate.U8(2);
    WriteBody(duplicate, Sample()); WriteBody(duplicate, Sample()); assert(!Decode(duplicate.bytes, bodies));

    // Exercise the bounded decoder with deterministic malformed packets and
    // mutations. Under ASan/UBSan this also checks all out-of-bounds reads.
    uint32_t random = 0x12345678;
    for (int attempt = 0; attempt < 20000; ++attempt)
    {
        std::vector<uint8_t> fuzz = bytes;
        random = random * 1664525u + 1013904223u;
        size_t offset = random % fuzz.size();
        random = random * 1664525u + 1013904223u;
        fuzz[offset] ^= uint8_t(random >> 24);
        Decode(fuzz, bodies);
    }
    std::cout << "Multiplayer world protocol tests passed.\n";
}
