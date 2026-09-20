#ifndef LUX_MULTIPLAYER_INVENTORY_POLICY_H
#define LUX_MULTIPLAYER_INVENTORY_POLICY_H
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace luxnet {
struct InventoryRecipe { std::string name, function, a, b; };
using InventoryOwners=std::map<std::string,std::set<uint32_t> >;
using InventoryRecipeAttempts=std::map<std::string,uint64_t>;
// Only declared ingredients establish recipe membership. One generic callback
// may implement several unrelated recipes, so sharing a function is not enough
// to make the most recent collector their owner.
inline const InventoryRecipe* NextSplitRecipe(const std::vector<InventoryRecipe>& recipes,
    const InventoryOwners& owners,const std::set<std::string>& acquired,const InventoryRecipeAttempts& attempted,
    uint64_t inventoryVersion=0) {
    for(const auto& recipe:recipes) {
        auto previous=attempted.find(recipe.name);
        if((previous!=attempted.end() && previous->second==inventoryVersion) ||
           (!acquired.count(recipe.a) && !acquired.count(recipe.b))) continue;
        auto a=owners.find(recipe.a),b=owners.find(recipe.b);
        if(a==owners.end() || b==owners.end() || a->second.empty() || b->second.empty()) continue;
        bool together=false;
        for(uint32_t owner:a->second) if(b->second.count(owner)) together=true;
        if(!together) return &recipe;
    }
    return NULL;
}
}
#endif
