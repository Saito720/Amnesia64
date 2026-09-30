#ifndef MULTIPLAYER_SHARED_INVENTORY_REGRESSION_H
#define MULTIPLAYER_SHARED_INVENTORY_REGRESSION_H

// Run in Study after an ordinary transition from Back Hall, then reconnect as
// a fresh player. The production Ready path reconciles prior-map rewards with
// the current map's history, including merged stacks and locally consumed items.
class cSharedInventoryRegression {
    unsigned phase=0;
    Uint32 entered=0;
    bool acted=false;
    static bool both(const char* suffix) {
        return exists(tString("host-shared-")+suffix) && exists(tString("client-shared-")+suffix);
    }
    void next() {++phase;entered=SDL_GetTicks();acted=false;}
    int fail(tString& error,const char* message) const {
        error="shared inventory phase "+cString::ToString(int(phase))+": "+message;return -1;
    }
public:
    static void GiveEarlierMapRewards(cLuxMap* map) {
        map->RunScript("GiveItem(\"codex_shared_earlier\",\"Puzzle\",\"codex_shared_earlier\",\"key_tower.tga\",1);");
        for(unsigned i=0;i<3;++i) map->RunScript(
            "GiveItem(\"codex_shared_health\",\"Health\",\"codex_shared_health_type\",\"potion_health.tga\",25);");
        map->RunScript("GiveItem(\"z_codex_shared_subtype_health\",\"Health\",\"codex_shared_subtype_collision\",\"potion_health.tga\",25);");
    }
    static bool EarlierMapRewardsReady() {
        auto* inventory=gpBase->mpInventory;auto* health=inventory->GetItem("codex_shared_health");
        return inventory->GetItem("codex_shared_earlier") && health && health->GetCount()==3 &&
            inventory->GetItem("z_codex_shared_subtype_health");
    }
    int Update(tString& error) {
        auto* session=gpBase->mpMultiplayer;auto* inventory=gpBase->mpInventory;
        auto* map=gpBase->mpMapHandler->GetCurrentMap();const bool host=role=="host";
        if(entered && SDL_GetTicks()-entered>30000) return fail(error,"reconnect/reconciliation timed out");
        if(phase==0) {
            auto* health=inventory->GetItem("codex_shared_health");
            if(!session->IsReady() || !map || map->GetName()!="11_study" || !inventory->GetItem("codex_shared_earlier") ||
               !health || health->GetCount()!=(host?3:2)) return fail(error,"ordinary transition reset an existing player's individually consumed stack");
            mark(role+"-shared-retained.txt","ordinary map transition preserved per-player consumption");next();return 0;
        }
        if(phase==1) {
            if(!both("retained.txt")) return 0;
            if(host && !acted) {
                // A counted grant under another name merges with the earlier
                // map's stack on the host; a fresh history replay names it anew.
                map->RunScript("GiveItem(\"codex_shared_current_health\",\"Health\",\"codex_shared_health_type\",\"potion_health.tga\",25);");
                map->RunScript("GiveItem(\"codex_shared_consumed\",\"Puzzle\",\"codex_shared_consumed\",\"key_study.tga\",1);");
                inventory->RemoveItem("codex_shared_consumed");
                // Native AddItem permits this later non-counted entry to
                // coexist with its earlier stack. A fresh current-map history
                // replay holds only the puzzle before the snapshot arrives.
                map->RunScript("GiveItem(\"a_codex_shared_subtype_puzzle\",\"Puzzle\",\"codex_shared_subtype_collision\",\"key_study.tga\",1);");
                map->RunScript("GiveItem(\"b_codex_shared_subtype_puzzle\",\"Puzzle\",\"codex_shared_subtype_collision\",\"key_study.tga\",1);");
                map->RunScript("GiveItem(\"codex_shared_current\",\"Puzzle\",\"codex_shared_current\",\"key_study.tga\",1);");
                acted=true;
            }
            if(!inventory->GetItem("codex_shared_current")) return 0;
            auto* health=inventory->GetItem("codex_shared_health");
            if(!health || health->GetCount()!=(host?4:3)) return fail(error,"current-map shared grant did not merge exactly once");
            mark(role+"-shared-current.txt","current map history includes stack alias and consumed reward");next();return 0;
        }
        if(phase==2) {
            if(!both("current.txt")) return 0;
            if(!host) {
                if(!session->Send(0,std::vector<uint8_t>{luxnet::Hello},true)) return fail(error,"could not request isolated peer rejection");
                mark("client-shared-reject.txt","fresh peer will reconnect to Study");
            } else if(!exists("client-shared-reject.txt")) return 0;
            next();return 0;
        }
        if(phase==3) {
            if(host) {
                if(!session->IsHost() || !session->IsReady()) return fail(error,"host listener ended during isolated peer rejection");
                if(!session->mPeers.empty() || !session->GetWorld()->GetRemotePlayers().empty()) return 0;
                mark("host-shared-listening.txt","prior-map shared ledger remains in the live session");
            } else {
                if(!exists("host-shared-listening.txt") || session->IsActive() || map) return 0;
                if(!session->Join("127.0.0.1:"+cString::ToString(int(port)))) return fail(error,"fresh Study join failed");
            }
            next();return 0;
        }
        if(phase==4) {
            if(!session->IsReady() || !map || session->GetWorld()->GetRemotePlayers().empty()) return 0;
            auto* health=inventory->GetItem("codex_shared_health");
            auto* collisionHealth=inventory->GetItem("z_codex_shared_subtype_health");
            auto* collisionPuzzle=inventory->GetItem("a_codex_shared_subtype_puzzle");
            auto* secondCollisionPuzzle=inventory->GetItem("b_codex_shared_subtype_puzzle");
            if(!inventory->GetItem("codex_shared_earlier") || !inventory->GetItem("codex_shared_current") || !health ||
               health->GetCount()!=4 || inventory->GetItem("codex_shared_current_health") || inventory->GetItem("codex_shared_consumed") ||
               !collisionHealth || collisionHealth->GetType()!=eLuxItemType_Health || collisionHealth->GetCount()!=1 ||
               !collisionPuzzle || collisionPuzzle->GetType()!=eLuxItemType_Puzzle || collisionPuzzle->GetCount()!=1 ||
               !secondCollisionPuzzle || secondCollisionPuzzle->GetType()!=eLuxItemType_Puzzle || secondCollisionPuzzle->GetCount()!=1) return 0;
            if(host && !acted) {
                // Applying the same current snapshot twice must retain its
                // exact count, never add the replayed consumable again.
                std::vector<luxnet::SharedInventoryEntry> entries;
                for(int index=0;index<inventory->GetItemNum();++index) {
                    auto* held=inventory->GetItem(index);const tString name=held->GetName();
                    if(!session->mSharedScriptItems.count(name)) continue;
                    luxnet::SharedInventoryEntry entry;auto& item=entry.item;
                    item.name=name;item.type=held->GetType();item.subtype=held->GetSubType();item.image=held->GetImageName().substr(14);
                    item.amount=held->GetAmount();item.value=held->GetStringVal();item.extra=held->GetExtraStringVal();entry.count=held->GetCount();entries.push_back(entry);
                }
                const auto packet=luxnet::WriteSharedInventoryState(session->GetMapEpoch(),entries);
                const uint32_t peer=session->GetWorld()->GetRemotePlayers().begin()->first;
                if(!session->Send(peer,packet,true) || !session->Send(peer,packet,true)) return fail(error,"could not repeat shared snapshots");
                map->RunScript("SetPlayerSanity(73.125f);");
                acted=true;
            }
            // This reliable typed command is ordered after both real snapshot
            // sends; reaching it proves repeated production reconciliation ran.
            if(std::fabs(gpBase->mpPlayer->GetSanity()-73.125f)>0.25f) return 0;
            if(health->GetCount()!=4) return fail(error,"repeated shared snapshot added another consumable");
            mark(role+"-shared-reconciled.txt","fresh Study join restored earlier rewards and exact count, without retaining consumed history grants");
            if(!both("reconciled.txt")) return 0;
            printStatus("Fresh cross-map join reconciled shared rewards, stack aliases, consumed history and exact consumable counts");
            return 1;
        }
        return 0;
    }
};
#endif
