#ifndef LUX_MULTIPLAYER_ENTITY_DEFINITION_H
#define LUX_MULTIPLAYER_ENTITY_DEFINITION_H
#include "LuxMultiplayerProtocol.h"
#include "LuxMultiplayerWorldProtocol.h"
#include <map>
#include <set>

namespace luxnet {
// Removal is tied to the host instance, so a delayed tombstone cannot remove
// a replacement that happens to reuse the same authored ID and name.
struct PropRemoval {
    uint32_t epoch=0, id=0;
    uint64_t incarnation=0;
    std::string name;
};
inline std::vector<uint8_t> WritePropRemoval(const PropRemoval& s) {
    Writer w(EntityRemoved);w.U32(s.epoch);w.U32(s.id);
    w.U32(static_cast<uint32_t>(s.incarnation));w.U32(static_cast<uint32_t>(s.incarnation>>32));
    w.String(s.name);return w.data;
}
inline bool ReadPropRemoval(Reader& r,PropRemoval& s) {
    s.epoch=r.U32();s.id=r.U32();s.incarnation=r.U32();s.incarnation|=uint64_t(r.U32())<<32;
    s.name=r.String(256);
    return r.Done() && s.epoch && s.id<=0x7fffffffu && s.incarnation && !s.name.empty();
}
// A reconstruction recipe, sent before native flags and physics state. The
// source is an installed, validated entity resource, never executable script.
struct PropDefinition {
    uint32_t epoch=0, id=0, parentId=UINT32_MAX;
    uint64_t incarnation=0;
    std::string name, file, parent;
    float matrix[12]={1,0,0,0,0,1,0,0,0,0,1,0};
    float scale[3]={1,1,1};
    float attachment[12]={1,0,0,0,0,1,0,0,0,0,1,0};
    struct BodyIdentity { uint64_t id=0;uint32_t generation=0; };
    std::vector<BodyIdentity> bodies;
    struct StaticBodyPose {
        uint64_t id=0;
        uint8_t flags=0;
        float matrix[12]={1,0,0,0,0,1,0,0,0,0,1,0};
    };
    std::vector<StaticBodyPose> staticBodies;
};
struct PropIncarnationBinding { uint64_t host=0,local=0; };
inline bool DefinitionChangesBoundIncarnation(const PropIncarnationBinding& bound,uint64_t host,uint64_t local) {
    // Script replay may already have created the local successor. Only force
    // rebuilding when the old bound local instance is still present.
    return bound.host && bound.local==local && bound.host!=host;
}
inline std::vector<uint8_t> WritePropDefinition(const PropDefinition& s) {
    Writer w(EntityDefinition);w.U32(s.epoch);w.U32(s.id);
    w.U32(static_cast<uint32_t>(s.incarnation));w.U32(static_cast<uint32_t>(s.incarnation>>32));
    w.String(s.name);w.String(s.file);
    for(float value:s.matrix) w.Float(value);
    for(float value:s.scale) w.Float(value);
    w.String(s.parent);
    if(!s.parent.empty()) {w.U32(s.parentId);for(float value:s.attachment) w.Float(value);}
    w.U32(static_cast<uint32_t>(s.bodies.size()));
    for(const auto& body:s.bodies) {w.U32(static_cast<uint32_t>(body.id));w.U32(static_cast<uint32_t>(body.id>>32));w.U32(body.generation);}
    w.U32(static_cast<uint32_t>(s.staticBodies.size()));
    for(const auto& body:s.staticBodies) {
        w.U32(static_cast<uint32_t>(body.id));w.U32(static_cast<uint32_t>(body.id>>32));
        w.U8(body.flags);
        for(float value:body.matrix) w.Float(value);
    }
    return w.data;
}
inline bool ReadPropDefinition(Reader& r,PropDefinition& s) {
    s.epoch=r.U32();s.id=r.U32();s.incarnation=r.U32();s.incarnation|=uint64_t(r.U32())<<32;
    s.name=r.String(256);s.file=r.String(512);
    for(float& value:s.matrix) value=r.Float();
    for(float& value:s.scale) value=r.Float();
    s.parent=r.String(256);
    if(!s.parent.empty()) {s.parentId=r.U32();for(float& value:s.attachment) value=r.Float();}
    else s.parentId=UINT32_MAX;
    const uint32_t count=r.U32();if(!r.valid || count>1024) return false;
    s.bodies.clear();
    for(uint32_t i=0;i<count;++i) {
        PropDefinition::BodyIdentity body;body.id=r.U32();body.id|=uint64_t(r.U32())<<32;body.generation=r.U32();
        if(!r.valid || !body.generation) return false;
        for(const auto& previous:s.bodies) if(previous.id==body.id) return false;
        s.bodies.push_back(body);
    }
    const uint32_t staticCount=r.U32();if(!r.valid || staticCount>1024-count) return false;
    s.staticBodies.clear();
    for(uint32_t i=0;i<staticCount;++i) {
        PropDefinition::StaticBodyPose body;body.id=r.U32();body.id|=uint64_t(r.U32())<<32;
        body.flags=r.U8();if(body.flags>31) return false;
        for(float& value:body.matrix) {value=r.Float();if(std::fabs(value)>1000000) return false;}
        if(!r.valid || !LuxWorldWire::ValidRigidBodyMatrix(body.matrix)) return false;
        for(const auto& previous:s.bodies) if(previous.id==body.id) return false;
        for(const auto& previous:s.staticBodies) if(previous.id==body.id) return false;
        s.staticBodies.push_back(body);
    }
    if(!r.Done() || !s.epoch || !s.incarnation || s.id>0x7fffffffu || s.name.empty() || !SafeRelativePath(s.file)) return false;
    if(!s.parent.empty() && (s.parentId>0x7fffffffu || s.parentId==s.id)) return false;
    for(float value:s.matrix) if(std::fabs(value)>1000000) return false;
    for(float value:s.scale) if(value==0 || std::fabs(value)>10000) return false;
    if(!s.parent.empty()) for(float value:s.attachment) if(std::fabs(value)>1000000) return false;
    return true;
}
// Definitions can replace an authored parent (which destroys its attached
// children). Always finish that reconstruction before binding any child.
inline std::string DescribePropDefinition(const PropDefinition& definition) {
    return "entity '"+definition.name+"' (ID "+std::to_string(definition.id)+", source '"+definition.file+"')";
}
inline bool OrderPropDefinitions(std::vector<PropDefinition>& definitions,std::string* error=NULL) {
    const auto fail=[&](const std::string& reason) {if(error) *error=reason;return false;};
    if(error) error->clear();
    if(definitions.size()>8192) return fail("Prop reconstruction roster exceeds 8192 entities.");
    std::map<uint32_t,const PropDefinition*> identities;
    for(const auto& definition:definitions)
        if(!identities.emplace(definition.id,&definition).second)
            return fail("Duplicate authored ID for "+DescribePropDefinition(definition)+".");
    for(const auto& definition:definitions) if(!definition.parent.empty()) {
        const auto parent=identities.find(definition.parentId);
        if(parent==identities.end() || parent->second->name!=definition.parent)
            return fail("Missing attachment parent '"+definition.parent+"' (ID "+std::to_string(definition.parentId)+") for "+DescribePropDefinition(definition)+".");
    }
    std::set<uint32_t> emitted;
    std::vector<PropDefinition> ordered;ordered.reserve(definitions.size());
    while(ordered.size()<definitions.size()) {
        const size_t before=ordered.size();
        for(auto& definition:definitions) {
            if(emitted.count(definition.id) || (!definition.parent.empty() && !emitted.count(definition.parentId))) continue;
            emitted.insert(definition.id);ordered.push_back(definition);
        }
        if(before==ordered.size()) {
            for(const auto& definition:definitions) if(!emitted.count(definition.id))
                return fail("Attachment cycle involving "+DescribePropDefinition(definition)+".");
        }
    }
    definitions.swap(ordered);return true;
}
inline bool IsSameMapDestination(const std::string& destination,const std::string& current) {
    const auto name=AuthoredMapFilename(destination);
    return !name.empty() && name==AuthoredMapFilename(current);
}
}
#endif
