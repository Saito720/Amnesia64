#ifndef LUX_MULTIPLAYER_CACHE_H
#define LUX_MULTIPLAYER_CACHE_H

#include "system/SystemTypes.h"
#include <cstdint>
#include <vector>

// Persistent, content-addressed map objects. Active map copies have a separate
// lifetime managed by the multiplayer session and are never cleared here.
struct cLuxMultiplayerMapCacheStats {
    uint64_t bytes=0;
    uint64_t maps=0;
};
hpl::tWString LuxMultiplayerCacheRoot();
cLuxMultiplayerMapCacheStats LuxGetMultiplayerMapCacheStats(const hpl::tWString& root=LuxMultiplayerCacheRoot());
bool LuxReadCachedMultiplayerMap(const hpl::tString& hash, uint32_t expectedSize,
    std::vector<uint8_t>& bytes, const hpl::tWString& root=LuxMultiplayerCacheRoot());
bool LuxStoreCachedMultiplayerMap(const hpl::tString& hash, const std::vector<uint8_t>& bytes,
    const hpl::tWString& root=LuxMultiplayerCacheRoot());
unsigned LuxClearMultiplayerMapCache(const hpl::tWString& root=LuxMultiplayerCacheRoot());

#endif
