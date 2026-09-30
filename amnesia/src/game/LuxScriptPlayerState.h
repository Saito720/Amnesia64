#ifndef LUX_SCRIPT_PLAYER_STATE_H
#define LUX_SCRIPT_PLAYER_STATE_H

#include "LuxScriptExecution.h"
#include "LuxMultiplayerProtocol.h"
#include <tuple>

struct LuxScriptPublishedValue {
    std::string module,name,value;
};

namespace luxscript {
static const size_t MaxPlayerVariableNameBytes=128, MaxPlayerVariableValueBytes=4096;
static const size_t MaxPlayerVariables=4096, MaxPlayerVariableBytes=4*1024*1024;
static const size_t MaxPublishedValues=64, MaxPublishedBytes=60*1024;

inline bool ValidModule(const std::string& module) {
    return module=="map" || module=="global" || module=="inventory";
}
inline bool ValidVariable(const std::string& name,const std::string& value) {
    return !name.empty() && name.size()<=MaxPlayerVariableNameBytes &&
        value.size()<=MaxPlayerVariableValueBytes && name.find('\0')==std::string::npos &&
        value.find('\0')==std::string::npos;
}
inline bool ValidPublishedScriptValues(const std::vector<LuxScriptPublishedValue>& values) {
    if(values.size()>MaxPublishedValues) return false;
    size_t bytes=4;std::set<std::pair<std::string,std::string> > names;
    for(const auto& value:values) {
        if(!ValidModule(value.module) || !ValidVariable(value.name,value.value) ||
           !names.insert({value.module,value.name}).second) return false;
        bytes+=value.module.size()+value.name.size()+value.value.size()+12;
        if(bytes>MaxPublishedBytes) return false;
    }
    return true;
}
// The caller supplies the packet type and map epoch. The complete replacement
// body avoids exposing references into the authoritative variable store.
inline bool WritePublishedScriptValues(luxnet::Writer& writer,
    const std::vector<LuxScriptPublishedValue>& values) {
    if(!ValidPublishedScriptValues(values)) return false;
    writer.U32(static_cast<uint32_t>(values.size()));
    for(const auto& value:values) {writer.String(value.module);writer.String(value.name);writer.String(value.value);}
    return true;
}
inline bool ReadPublishedScriptValues(luxnet::Reader& reader,
    std::vector<LuxScriptPublishedValue>& values) {
    const uint32_t count=reader.U32();
    if(!reader.valid || count>MaxPublishedValues) return false;
    std::vector<LuxScriptPublishedValue> parsed;
    for(uint32_t i=0;i<count && reader.valid;++i)
        parsed.push_back({reader.String(16),reader.String(MaxPlayerVariableNameBytes),reader.String(MaxPlayerVariableValueBytes)});
    if(!reader.Done() || !ValidPublishedScriptValues(parsed)) return false;
    values.swap(parsed);return true;
}
}

// Session participant state, not an account save. Character respawn retains
// variables; disconnect removes them. No reconnect or disk persistence is
// inferred from a reused display name or transport peer number.
class cLuxScriptPlayerState {
public:
    void Reset() {mValues.clear();mPublished.clear();mBytes=0;mSession=0;mEpoch=0;mInitialized=false;mHasMap=false;}
    void BeginSession(uint64_t session) {
        if(!mInitialized || mSession!=session) {Reset();mSession=session;mInitialized=true;}
    }
    void BeginMap(uint64_t session,uint32_t epoch=0) {
        BeginSession(session);mEpoch=epoch;mHasMap=true;
        for(auto it=mValues.begin();it!=mValues.end();)
            if(!std::get<2>(it->first)) {mBytes-=EntryBytes(it->first,it->second);it=mValues.erase(it);} else ++it;
        for(auto it=mPublished.begin();it!=mPublished.end();)
            if(std::get<1>(it->first)=="map") it=mPublished.erase(it);else ++it;
    }
    void RebindMap(uint64_t session,uint32_t epoch) {
        BeginSession(session);mEpoch=epoch;mHasMap=true;
    }
    void RemovePlayer(uint64_t session,uint32_t player) {
        if(!mInitialized || session!=mSession) return;
        for(auto it=mValues.begin();it!=mValues.end();)
            if(std::get<0>(it->first)==player) {mBytes-=EntryBytes(it->first,it->second);it=mValues.erase(it);} else ++it;
        for(auto it=mPublished.begin();it!=mPublished.end();)
            if(std::get<0>(it->first)==player) it=mPublished.erase(it);else ++it;
    }
    bool Set(const LuxScriptExecutionContext& context,const std::string& name,
        const std::string& value,bool campaign,std::string& error) {
        error.clear();
        if(!Authority(context) || !luxscript::ValidVariable(name,value)) {
            error="Player variables require a current authority player and a bounded name/value.";return false;
        }
        const Key key={context.player,context.module,campaign,name};
        const auto old=mValues.find(key);
        const size_t oldBytes=old==mValues.end()?0:EntryBytes(key,old->second),newBytes=EntryBytes(key,value);
        if((old==mValues.end() && mValues.size()>=luxscript::MaxPlayerVariables) ||
           mBytes-oldBytes+newBytes>luxscript::MaxPlayerVariableBytes) {
            error="Player variable storage limit exceeded.";return false;
        }
        mValues[key]=value;mBytes=mBytes-oldBytes+newBytes;return true;
    }
    std::string Get(const LuxScriptExecutionContext& context,const std::string& name,bool campaign) const {
        if(!Authority(context) || !luxscript::ValidVariable(name,"")) return "";
        const auto found=mValues.find({context.player,context.module,campaign,name});
        return found==mValues.end()?std::string():found->second;
    }
    bool Publish(const LuxScriptExecutionContext& context,const std::string& name,
        const std::string& value,std::string& error) {
        error.clear();
        if(!Authority(context) || !luxscript::ValidVariable(name,value)) {
            error="Publishing requires a current authority player and a bounded name/value.";return false;
        }
        auto next=Published(context.session,context.player);bool replaced=false;
        for(auto& current:next) if(current.module==context.module && current.name==name) {
            current.value=value;replaced=true;break;
        }
        if(!replaced) next.push_back({context.module,name,value});
        return ApplyPublished(context.session,context.player,next,error);
    }
    std::vector<LuxScriptPublishedValue> Published(uint64_t session,uint32_t player) const {
        std::vector<LuxScriptPublishedValue> values;
        if(!mInitialized || session!=mSession || player==UINT32_MAX) return values;
        for(const auto& value:mPublished) if(std::get<0>(value.first)==player)
            values.push_back({std::get<1>(value.first),std::get<2>(value.first),value.second});
        return values;
    }
    bool ApplyPublished(uint64_t session,uint32_t player,
        const std::vector<LuxScriptPublishedValue>& values,std::string& error) {
        error.clear();
        if(!mInitialized || session!=mSession || player==UINT32_MAX || !luxscript::ValidPublishedScriptValues(values)) {
            error="Invalid published player variable snapshot.";return false;
        }
        // Validate the whole replacement before changing the local read view.
        for(auto it=mPublished.begin();it!=mPublished.end();)
            if(std::get<0>(it->first)==player) it=mPublished.erase(it);else ++it;
        for(const auto& value:values) mPublished[{player,value.module,value.name}]=value.value;
        return true;
    }
    std::string GetPublished(const LuxScriptExecutionContext& context,const std::string& name) const {
        if(!CurrentPlayer(context) || (context.domain!=LuxScriptDomain::Authority && context.domain!=LuxScriptDomain::Client) ||
           !luxscript::ValidVariable(name,"")) return "";
        const auto found=mPublished.find({context.player,context.module,name});
        return found==mPublished.end()?std::string():found->second;
    }
private:
    using Key=std::tuple<uint32_t,std::string,bool,std::string>;
    using PublishedKey=std::tuple<uint32_t,std::string,std::string>;
    bool CurrentPlayer(const LuxScriptExecutionContext& context) const {
        return mInitialized && context.revised && context.hasPlayer && context.player!=UINT32_MAX &&
            context.session==mSession && (!mHasMap || context.mapEpoch==mEpoch) && luxscript::ValidModule(context.module);
    }
    bool Authority(const LuxScriptExecutionContext& context) const {
        return CurrentPlayer(context) && context.domain==LuxScriptDomain::Authority;
    }
    static size_t EntryBytes(const Key& key,const std::string& value) {
        return std::get<1>(key).size()+std::get<3>(key).size()+value.size();
    }
    std::map<Key,std::string> mValues;
    std::map<PublishedKey,std::string> mPublished;
    uint64_t mSession=0;
    uint32_t mEpoch=0;
    size_t mBytes=0;
    bool mInitialized=false,mHasMap=false;
};

#endif
