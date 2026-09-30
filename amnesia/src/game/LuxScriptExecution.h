#ifndef LUX_SCRIPT_EXECUTION_H
#define LUX_SCRIPT_EXECUTION_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class LuxScriptDomain { Legacy, Authority, Client, Replication };

struct LuxScriptExecutionContext {
    LuxScriptDomain domain=LuxScriptDomain::Legacy;
    bool revised=false, hasPlayer=false;
    uint32_t player=UINT32_MAX;
    uint64_t session=0, event=0;
    uint32_t mapEpoch=0;
    std::string module;
};

inline LuxScriptExecutionContext& LuxCurrentScriptContext() {
    static thread_local LuxScriptExecutionContext context;
    return context;
}

class cLuxScriptExecutionScope {
public:
    explicit cLuxScriptExecutionScope(const LuxScriptExecutionContext& context)
        : mPrevious(LuxCurrentScriptContext()) { LuxCurrentScriptContext()=context; }
    ~cLuxScriptExecutionScope() { LuxCurrentScriptContext()=mPrevious; }
    cLuxScriptExecutionScope(const cLuxScriptExecutionScope&)=delete;
    cLuxScriptExecutionScope& operator=(const cLuxScriptExecutionScope&)=delete;
private:
    LuxScriptExecutionContext mPrevious;
};

// Global constructors have module ownership but no implicit player. The scope
// also covers hot recompilation, before any lifecycle callback can run.
class cLuxScriptAuthorityInitializationScope {
public:
    cLuxScriptAuthorityInitializationScope(bool revised,const std::string& module)
        : mScope(Context(revised,module)) {}
    cLuxScriptAuthorityInitializationScope(bool revised,const std::string& module,uint64_t session,uint32_t mapEpoch)
        : mScope(Context(revised,module,session,mapEpoch)) {}
private:
    static LuxScriptExecutionContext Context(bool revised,const std::string& module) {
        auto context=LuxCurrentScriptContext();context.revised=revised;
        context.domain=revised?LuxScriptDomain::Authority:LuxScriptDomain::Legacy;
        context.module=module;context.hasPlayer=false;context.player=UINT32_MAX;
        return context;
    }
    static LuxScriptExecutionContext Context(bool revised,const std::string& module,uint64_t session,uint32_t mapEpoch) {
        auto context=Context(revised,module);context.session=session;context.mapEpoch=mapEpoch;return context;
    }
    cLuxScriptExecutionScope mScope;
};

// Actor-bound delayed work belongs to a particular character incarnation.
// Domain/module rebinding on a legitimate saved-map revisit is independent of
// actor validity; neither a reconnect nor a respawn may inherit stale work.
inline bool LuxScriptActorMatches(const LuxScriptExecutionContext& context, uint32_t life,
    uint64_t session, uint32_t player, uint32_t currentLife, bool alive) {
    return !context.hasPlayer || (alive && context.player!=UINT32_MAX &&
        context.session==session && context.player==player && life && life==currentLife);
}

inline bool LuxScriptSameOwner(const LuxScriptExecutionContext& first,
    const LuxScriptExecutionContext& second) {
    if(!first.revised || !second.revised) return first.revised==second.revised;
    return first.domain==second.domain && first.module==second.module &&
        first.session==second.session && first.hasPlayer==second.hasPlayer &&
        (!first.hasPlayer || first.player==second.player);
}

inline uint64_t LuxNextScriptEventId() {
    static thread_local uint64_t next=0;
    return ++next;
}

struct LuxScriptPlayerOverlap {
    uint32_t player=UINT32_MAX, life=0;
    bool inside=false;
};
struct LuxScriptPlayerCollisionEvent {
    uint32_t player=UINT32_MAX, life=0;
    int state=0;
};

// Session-private observer state. Omitted participants have disconnected or
// died: discard their old overlap without manufacturing a personal leave.
// One-shot consumption belongs to the participant, not to its current life.
class cLuxScriptPlayerCollisionState {
public:
    bool OfflineInside() const {
        const auto player=mPrevious.find(0);
        return mSession==0 && player!=mPrevious.end() && player->second.inside;
    }
    bool OfflineConsumed() const { return mSession==0 && mConsumed.count(0)!=0; }
    void RestoreOffline(bool inside, bool consumed) {
        mSession=0;mPrevious.clear();mConsumed.clear();
        mPrevious[0]={0,1,inside};
        if(consumed) mConsumed.insert(0);
    }
    void Consume(uint32_t player) { mConsumed.insert(player); }
    std::vector<LuxScriptPlayerCollisionEvent> Sample(uint64_t session,
        const std::vector<LuxScriptPlayerOverlap>& players, int states, bool oncePerPlayer,
        bool consumeEvents=true) {
        if(mSession!=session) { mPrevious.clear();mConsumed.clear();mSession=session; }
        std::map<uint32_t,LuxScriptPlayerOverlap> current;
        std::vector<LuxScriptPlayerCollisionEvent> events;
        for(const auto& player:players) {
            if(player.player==UINT32_MAX || !player.life || current.count(player.player)) continue;
            current[player.player]=player;
            const auto previous=mPrevious.find(player.player);
            const bool wasInside=previous!=mPrevious.end() &&
                previous->second.life==player.life && previous->second.inside;
            if(wasInside==player.inside || (oncePerPlayer && mConsumed.count(player.player))) continue;
            const int state=player.inside ? 1 : -1;
            if(states && states!=state) continue;
            if(oncePerPlayer && consumeEvents) mConsumed.insert(player.player);
            events.push_back({player.player,player.life,state});
        }
        mPrevious.swap(current);
        return events;
    }
private:
    uint64_t mSession=0;
    std::map<uint32_t,LuxScriptPlayerOverlap> mPrevious;
    std::set<uint32_t> mConsumed;
};

#endif
