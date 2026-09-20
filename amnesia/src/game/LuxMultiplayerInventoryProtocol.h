#ifndef LUX_MULTIPLAYER_INVENTORY_PROTOCOL_H
#define LUX_MULTIPLAYER_INVENTORY_PROTOCOL_H
#include "LuxMultiplayerProtocol.h"

namespace luxnet {
// Enough information to restore an inventory entry without replaying its world
// pickup callback or requiring the original map/entity to still exist.
struct InventoryItem {
    std::string name, subtype, image, value, extra;
    uint32_t type=0;
    float amount=0;
};
inline std::vector<uint8_t> WriteInventoryItem(uint32_t epoch,const InventoryItem& item) {
    Writer w(InventoryGrant);w.U32(epoch);w.String(item.name);w.U32(item.type);
    w.String(item.subtype);w.String(item.image);w.Float(item.amount);w.String(item.value);w.String(item.extra);
    return w.data;
}
inline bool ReadInventoryItem(Reader& r,InventoryItem& item,uint32_t typeCount) {
    item.name=r.String(256);item.type=r.U32();item.subtype=r.String(256);item.image=r.String(1024);
    item.amount=r.Float();item.value=r.String(4096);item.extra=r.String(4096);
    return r.Done() && !item.name.empty() && item.type<typeCount && std::fabs(item.amount)<=100000;
}
}
#endif
