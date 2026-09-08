#ifndef LUX_MULTIPLAYER_CONTENT_H
#define LUX_MULTIPLAYER_CONTENT_H
#include "system/SystemTypes.h"
#include <cstdint>
#include <vector>
namespace hpl { class cResources; }
bool LuxReadMultiplayerMap(const hpl::tWString& path, std::vector<uint8_t>& bytes);
bool LuxValidateMultiplayerMap(const std::vector<uint8_t>& bytes, hpl::tString& error,
    hpl::cResources* resources = nullptr);
bool LuxValidateMultiplayerAsset(const hpl::tString& name, const hpl::tString& defaultExt,
    hpl::tString& error, hpl::cResources* resources = nullptr);
#endif
