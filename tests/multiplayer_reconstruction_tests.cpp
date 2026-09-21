#include "../amnesia/src/game/LuxMultiplayerEntityDefinition.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace luxnet;
static bool Decode(const std::vector<uint8_t>& bytes) {
    Reader reader(bytes);PropDefinition definition;return ReadPropDefinition(reader,definition);
}
static void CheckPropRemovals() {
    PropRemoval source;source.epoch=7;source.id=512;source.incarnation=0x123456789abcdef0ull;source.name="door_broken";
    const auto bytes=WritePropRemoval(source);assert(bytes[0]==EntityRemoved);
    Reader reader(bytes);PropRemoval target;assert(ReadPropRemoval(reader,target));
    assert(target.epoch==source.epoch && target.id==source.id && target.incarnation==source.incarnation && target.name==source.name);
    assert(WritePropRemoval(target)==bytes);
    auto decode=[](const std::vector<uint8_t>& data) {Reader reader(data);PropRemoval removal;return ReadPropRemoval(reader,removal);};
    for(size_t length=0;length<bytes.size();++length) assert(!decode({bytes.begin(),bytes.begin()+length}));
    auto trailing=bytes;trailing.push_back(0);assert(!decode(trailing));
    auto bad=source;bad.epoch=0;assert(!decode(WritePropRemoval(bad)));
    bad=source;bad.id=UINT32_MAX;assert(!decode(WritePropRemoval(bad)));
    bad=source;bad.id=0x80000000u;assert(!decode(WritePropRemoval(bad)));
    bad=source;bad.incarnation=0;assert(!decode(WritePropRemoval(bad)));
    for(const auto& name:{std::string(),std::string(257,'x'),std::string("door\0hidden",11)}) {
        bad=source;bad.name=name;assert(!decode(WritePropRemoval(bad)));
    }
    auto limit=source;limit.name=std::string(256,'x');limit.id=0x7fffffffu;assert(decode(WritePropRemoval(limit)));
    limit.id=0;assert(decode(WritePropRemoval(limit)));
    auto successor=source;++successor.incarnation;
    assert(decode(WritePropRemoval(successor)) && WritePropRemoval(successor)!=bytes);
}
int main() {
    CheckPropRemovals();
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
    auto attached=source;attached.parent="rope_parent";attached.parentId=414;attached.attachment[3]=2.5f;attached.attachment[7]=-0.1f;
    const auto attachedBytes=WritePropDefinition(attached);assert(Decode(attachedBytes));
    Reader attachedReader(attachedBytes);PropDefinition child;assert(ReadPropDefinition(attachedReader,child));
    assert(child.parent=="rope_parent" && child.parentId==414 && child.attachment[3]==2.5f && child.attachment[7]==-0.1f);
    for(size_t length=0;length<attachedBytes.size();++length)
        assert(!Decode(std::vector<uint8_t>(attachedBytes.begin(),attachedBytes.begin()+length)));
    bad=attached;bad.parentId=bad.id;assert(!Decode(WritePropDefinition(bad)));
    bad=attached;bad.parentId=UINT32_MAX;assert(!Decode(WritePropDefinition(bad)));
    bad=attached;bad.attachment[7]=std::numeric_limits<float>::infinity();assert(!Decode(WritePropDefinition(bad)));
    bad=attached;bad.parent=std::string(257,'x');assert(!Decode(WritePropDefinition(bad)));
    PropDefinition parent=source;parent.name="rope_parent";parent.id=414;
    PropDefinition grandchild=source;grandchild.id=416;grandchild.name="second_attachment";grandchild.parent=attached.name;grandchild.parentId=attached.id;
    std::vector<PropDefinition> definitions={grandchild,attached,parent};
    assert(OrderPropDefinitions(definitions));
    assert(definitions[0].name==parent.name && definitions[1].name==attached.name && definitions[2].name==grandchild.name);
    std::string error;
    definitions={attached};assert(!OrderPropDefinitions(definitions,&error));
    assert(error.find("414")!=std::string::npos && error.find(attached.name)!=std::string::npos);
    parent.parent=attached.name;parent.parentId=attached.id;
    definitions={attached,parent};assert(!OrderPropDefinitions(definitions,&error));assert(error.find("cycle")!=std::string::npos);
    definitions={source,source};assert(!OrderPropDefinitions(definitions,&error));assert(error.find("Duplicate authored ID")!=std::string::npos);
    // Back Hall contains two same-name slime props with distinct authored IDs.
    // The parent lookup must also distinguish same-name attachment siblings.
    auto first=source;first.name="Even01Slime04";first.id=1030;
    auto second=first;second.id=1040;second.matrix[3]=42;
    auto sameNameChild=first;sameNameChild.id=1050;sameNameChild.parent=second.name;sameNameChild.parentId=second.id;
    assert(Decode(WritePropDefinition(sameNameChild)));
    definitions={sameNameChild,first,second};assert(OrderPropDefinitions(definitions,&error) && error.empty());
    assert(definitions.size()==3 && definitions[0].id==1030 && definitions[1].id==1040 && definitions[2].id==1050);
    sameNameChild.parentId=1041;definitions={first,second,sameNameChild};assert(!OrderPropDefinitions(definitions,&error));
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
