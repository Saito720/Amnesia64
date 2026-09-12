#ifndef LUX_MULTIPLAYER_CONTENT_H
#define LUX_MULTIPLAYER_CONTENT_H
#include "system/SystemTypes.h"
#include <cstdint>
#include <vector>
namespace hpl { class cResources; class cWorld; }
bool LuxValidateMultiplayerCurrentMapSource(hpl::cWorld* world, const std::vector<uint8_t>& bytes,
    hpl::tString& error, hpl::cResources* resources = nullptr);
bool LuxReadMultiplayerMap(const hpl::tWString& path, std::vector<uint8_t>& bytes);
bool LuxCollectMultiplayerStartPositions(const std::vector<uint8_t>& bytes, std::vector<hpl::tString>& starts,
    hpl::tString& error, hpl::cResources* resources = nullptr);
bool LuxValidateMultiplayerMap(const std::vector<uint8_t>& bytes, hpl::tString& error,
    hpl::cResources* resources = nullptr);
bool LuxCollectMultiplayerMapItems(const std::vector<uint8_t>& bytes, std::vector<hpl::tString>& items,
    hpl::tString& error, hpl::cResources* resources = nullptr);
bool LuxValidateMultiplayerAsset(const hpl::tString& name, const hpl::tString& defaultExt,
    hpl::tString& error, hpl::cResources* resources = nullptr);
#endif
