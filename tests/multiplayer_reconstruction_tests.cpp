#include "../amnesia/src/game/LuxMultiplayerEntityDefinition.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace luxnet;
static bool Decode(const std::vector<uint8_t>& bytes) {
    Reader reader(bytes);PropDefinition definition;return ReadPropDefinition(reader,definition);
}
int main() {
    PropDefinition source;source.epoch=7;source.id=415;source.incarnation=0x123456789ull;source.name="restored_puzzle";
    source.file="entities/item/puzzle.ent";source.matrix[3]=7;source.scale[1]=2;
    source.bodies.push_back({0x123456789abcdef0ull,4});
    PropDefinition::StaticBodyPose pose;pose.id=99;pose.matrix[3]=14;pose.matrix[7]=3;
    pose.flags=LuxWorldWire::Active|LuxWorldWire::Collide;
    source.staticBodies.push_back(pose);
    auto bytes=WritePropDefinition(source);assert(Decode(bytes));
    Reader reader(bytes);PropDefinition target;assert(ReadPropDefinition(reader,target));
    assert(target.id==415 && target.epoch==7 && target.name==source.name && target.file==source.file);
    assert(target.matrix[3]==7 && target.scale[1]==2);
    assert(target.incarnation==source.incarnation && target.staticBodies.size()==1 && target.staticBodies[0].matrix[3]==14);
    assert(target.staticBodies[0].flags==pose.flags);
    assert(target.bodies.size()==1 && target.bodies[0].id==source.bodies[0].id && target.bodies[0].generation==4);
    for(size_t length=0;length<bytes.size();++length)
        assert(!Decode(std::vector<uint8_t>(bytes.begin(),bytes.begin()+length)));
    bytes.push_back(0);assert(!Decode(bytes));
    auto bad=source;bad.file="../untrusted.ent";assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.file="D:/external.ent";assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.id=0xffffffffu;assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.incarnation=0;assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.matrix[3]=std::numeric_limits<float>::quiet_NaN();assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.scale[0]=std::numeric_limits<float>::infinity();assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.bodies[0].generation=0;assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.bodies.push_back(bad.bodies.front());assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.staticBodies.push_back(bad.staticBodies.front());assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.staticBodies[0].id=bad.bodies[0].id;assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.staticBodies[0].matrix[3]=std::numeric_limits<float>::quiet_NaN();assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.staticBodies[0].matrix[0]=0;assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.staticBodies[0].matrix[0]=-1;assert(!Decode(WritePropDefinition(bad)));
    bad=source;bad.staticBodies[0].flags=32;assert(!Decode(WritePropDefinition(bad)));
    auto attached=source;attached.parent="rope_parent";attached.attachment[3]=2.5f;attached.attachment[7]=-0.1f;
    const auto attachedBytes=WritePropDefinition(attached);assert(Decode(attachedBytes));
    Reader attachedReader(attachedBytes);PropDefinition child;assert(ReadPropDefinition(attachedReader,child));
    assert(child.parent=="rope_parent" && child.attachment[3]==2.5f && child.attachment[7]==-0.1f);
    for(size_t length=0;length<attachedBytes.size();++length)
        assert(!Decode(std::vector<uint8_t>(attachedBytes.begin(),attachedBytes.begin()+length)));
    bad=attached;bad.parent=bad.name;assert(!Decode(WritePropDefinition(bad)));
    bad=attached;bad.attachment[7]=std::numeric_limits<float>::infinity();assert(!Decode(WritePropDefinition(bad)));
    bad=attached;bad.parent=std::string(257,'x');assert(!Decode(WritePropDefinition(bad)));
    PropDefinition parent=source;parent.name="rope_parent";
    PropDefinition grandchild=source;grandchild.name="second_attachment";grandchild.parent=attached.name;
    std::vector<PropDefinition> definitions={grandchild,attached,parent};
    assert(OrderPropDefinitions(definitions));
    assert(definitions[0].name==parent.name && definitions[1].name==attached.name && definitions[2].name==grandchild.name);
    definitions={attached};assert(!OrderPropDefinitions(definitions));
    parent.parent=attached.name;definitions={attached,parent};assert(!OrderPropDefinitions(definitions));
    definitions={source,source};assert(!OrderPropDefinitions(definitions));
    assert(!DefinitionChangesBoundIncarnation({},2,5));
    assert(!DefinitionChangesBoundIncarnation({1,5},1,5));
    assert(DefinitionChangesBoundIncarnation({1,5},2,5));
    assert(!DefinitionChangesBoundIncarnation({1,5},2,6));
    assert(IsSameMapDestination("D:\\maps\\PORTAL.map","portal"));
    assert(IsSameMapDestination("portal","portal.map"));
    assert(!IsSameMapDestination("portal.map","other.map"));
    assert(!IsSameMapDestination("","portal.map"));
    std::cout << "Multiplayer entity reconstruction and same-map destination tests passed.\n";
}
