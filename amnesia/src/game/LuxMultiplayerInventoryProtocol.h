#ifndef LUX_MULTIPLAYER_INVENTORY_PROTOCOL_H
#define LUX_MULTIPLAYER_INVENTORY_PROTOCOL_H
#include "LuxMultiplayerProtocol.h"
#include <set>

namespace luxnet {
// Enough information to restore an inventory entry without replaying its world
// pickup callback or requiring the original map/entity to still exist.
struct InventoryItem {
    std::string name, subtype, image, value, extra;
    uint32_t type=0;
    float amount=0;
};
inline void WriteInventoryItemFields(Writer& w,const InventoryItem& item) {
    w.String(item.name);w.U32(item.type);
    w.String(item.subtype);w.String(item.image);w.Float(item.amount);w.String(item.value);w.String(item.extra);
}
inline std::vector<uint8_t> WriteInventoryItem(uint32_t epoch,const InventoryItem& item) {
    Writer w(InventoryGrant);w.U32(epoch);WriteInventoryItemFields(w,item);
    return w.data;
}
inline bool ReadInventoryItemFields(Reader& r,InventoryItem& item,uint32_t typeCount) {
    item.name=r.String(256);item.type=r.U32();item.subtype=r.String(256);item.image=r.String(1024);
    item.amount=r.Float();item.value=r.String(4096);item.extra=r.String(4096);
    return r.valid && !item.name.empty() && item.type<typeCount && std::fabs(item.amount)<=100000;
}
inline bool ReadInventoryItem(Reader& r,InventoryItem& item,uint32_t typeCount) {
    return ReadInventoryItemFields(r,item,typeCount) && r.Done();
}
// A fresh peer replays this map's effects before reconciling retained shared
// rewards from every visited map. Exact stack counts make reconciliation
// idempotent even when this map's history already granted the same reward.
struct SharedInventoryEntry {
    InventoryItem item;
    uint32_t count=1;
};
// Native inventory merges counted grants with the first entry of any type
// sharing their subtype. A restorable roster retains the native insertion
// order: a counted stack may precede a later non-counted entry of that subtype.
template<class CountedType> inline bool ValidSharedInventoryRestoreOrder(
    const std::vector<SharedInventoryEntry>& entries,const CountedType& counted) {
    std::set<std::string> subtypes;
    for(const auto& entry:entries) {
        if(counted(entry.item.type) && subtypes.count(entry.item.subtype)) return false;
        subtypes.insert(entry.item.subtype);
    }
    return true;
}
static const size_t MaxSharedInventoryItems=1024;
static const size_t MaxSharedInventoryBytes=64*1024;
inline std::vector<uint8_t> WriteSharedInventoryState(uint32_t epoch,const std::vector<SharedInventoryEntry>& entries) {
    Writer w(SharedInventoryState);w.U32(epoch);w.U32(static_cast<uint32_t>(entries.size()));
    for(const auto& entry:entries) {WriteInventoryItemFields(w,entry.item);w.U32(entry.count);}
    return w.data;
}
inline bool ReadSharedInventoryState(Reader& r,std::vector<SharedInventoryEntry>& output,uint32_t typeCount) {
    if(r.data.empty() || r.data.front()!=SharedInventoryState || r.data.size()>MaxSharedInventoryBytes) return false;
    const uint32_t count=r.U32();if(!r.valid || count>MaxSharedInventoryItems) return false;
    std::vector<SharedInventoryEntry> entries;std::set<std::string> names;
    for(uint32_t i=0;i<count;++i) {
        SharedInventoryEntry entry;
        if(!ReadInventoryItemFields(r,entry.item,typeCount)) return false;
        entry.count=r.U32();
        if(!r.valid || !entry.count || entry.count>1024 || !names.insert(entry.item.name).second) return false;
        entries.push_back(entry);
    }
    if(!r.Done()) return false;
    output.swap(entries);return true;
}
}
#endif
