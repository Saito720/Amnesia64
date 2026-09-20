#ifndef CODEX_INVENTORY_REMOVAL_REGRESSION_H
#define CODEX_INVENTORY_REMOVAL_REGRESSION_H
#include "LuxPlayerState_UseItem.h"
#include "ChestQuestionRegression.h"

static unsigned inventoryCraftCallbacks=0;
static void __stdcall CodexInventoryCraft(std::string& a,std::string& b) {
    ++inventoryCraftCallbacks;
    gpBase->mpMapHandler->GetCurrentMap()->RunScript(
        "if(HasItem(\"codex_craft_host_piece\") && HasItem(\"codex_craft_client_piece\")) {"
        "RemoveItem(\"codex_craft_host_piece\"); RemoveItem(\"codex_craft_client_piece\");"
        "GiveItem(\"codex_craft_output\",\"Puzzle\",\"KeyTower\",\"key_tower.tga\",1); }");
}

// Use the real inventory, player state and reliable network packet so a peer
// consuming an equipped crafting ingredient cannot leave a dangling crosshair.
class cInventoryRemovalRegression {
    unsigned phase=0;
    bool both(const char* suffix) const {
        return exists(tString("host-")+suffix) && exists(tString("client-")+suffix);
    }
    cLuxInventory_Item* add(const char* name) {
        return gpBase->mpInventory->AddItem(name,eLuxItemType_Puzzle,"KeyTower","key_tower.tga",1,"","",NULL,false);
    }
    int fail(tString& error,const char* message) { error=message;return -1; }
public:
    int Update(tString& error) {
        auto* player=gpBase->mpPlayer;auto* inventory=gpBase->mpInventory;
        auto* state=static_cast<cLuxPlayerState_UseItem*>(player->GetStateData(eLuxPlayerState_UseItem));
        const char* name="codex_inventory_equipped_removal";
        if(phase==0) {
            if(!RunChestQuestionRegression(error)) return -1;
            auto* item=add(name);auto* unrelated=add("codex_inventory_unrelated_removal");
            if(!item || !unrelated) return fail(error,"inventory removal fixtures failed to load");
            cLuxPlayerStateVars::SetupUseItem(item);player->ChangeState(eLuxPlayerState_UseItem);
            if(player->GetCurrentState()!=eLuxPlayerState_UseItem || state->GetCrosshair()!=item->GetImage())
                return fail(error,"inventory fixture did not equip its puzzle item");
            inventory->RemoveItem(unrelated);
            if(player->GetCurrentState()!=eLuxPlayerState_UseItem || state->GetCrosshair()!=item->GetImage())
                return fail(error,"removing an unrelated entry cancelled the equipped item");
            mark(role+"-inventory-removal-armed.txt","equipped piece survives unrelated removal");++phase;return 0;
        }
        if(phase==1) {
            if(!both("inventory-removal-armed.txt")) return 0;
            if(role=="host") {
                auto* session=gpBase->mpMultiplayer;
                if(session->mPeers.empty()) return fail(error,"inventory removal has no connected client");
                luxnet::Writer removal(luxnet::InventoryRemove);removal.U32(session->GetMapEpoch());removal.String(name);
                if(!session->Send(session->mPeers.begin()->first,removal.data,true)) return fail(error,"inventory removal packet failed");
                inventory->RemoveItem(name);
            }
            ++phase;return 0;
        }
        if(phase==2) {
            if(inventory->GetItem(name)) return 0;
            if(player->GetCurrentState()!=eLuxPlayerState_Normal || state->GetCrosshair()!=NULL)
                return fail(error,"network/name removal retained an equipped item reference");
            auto* item=add("codex_inventory_pointer_removal");
            if(!item) return fail(error,"pointer removal fixture failed to load");
            cLuxPlayerStateVars::SetupUseItem(item);player->ChangeState(eLuxPlayerState_UseItem);
            inventory->RemoveItem(item);
            if(player->GetCurrentState()!=eLuxPlayerState_Normal || state->GetCrosshair()!=NULL)
                return fail(error,"pointer removal retained an equipped item reference");
            mark(role+"-inventory-removal-passed.txt","network/name and pointer removals cleared equipped references");++phase;return 0;
        }
        if(phase==3) {
            if(!both("inventory-removal-passed.txt")) return 0;
            if(role=="host") {
                if(!gpBase->mpEngine->GetSystem()->GetLowLevel()->AddScriptFunc(
                    "void CodexInventoryCraft(string &in a, string &in b)",(void*)CodexInventoryCraft))
                    return fail(error,"inventory crafting callback registration failed");
                if(!add("codex_craft_host_piece")) return fail(error,"host crafting fixture failed to load");
                inventory->AddCombineCallback("codex_craft_recipe","codex_craft_host_piece","codex_craft_client_piece","CodexInventoryCraft",true);
                auto* session=gpBase->mpMultiplayer;
                luxnet::InventoryItem piece;piece.name="codex_craft_client_piece";piece.type=eLuxItemType_Puzzle;
                piece.subtype="KeyTower";piece.image="key_tower.tga";piece.amount=1;
                if(!session->GiveInventoryItem(session->mPeers.begin()->first,piece)) return fail(error,"client crafting fixture grant failed");
            }
            ++phase;return 0;
        }
        if(phase==4) {
            if(role=="client") {
                auto* item=inventory->GetItem("codex_craft_client_piece");if(!item) return 0;
                cLuxPlayerStateVars::SetupUseItem(item);player->ChangeState(eLuxPlayerState_UseItem);
            }
            mark(role+"-inventory-craft-armed.txt","host/client own separate ingredients; client piece equipped");++phase;return 0;
        }
        if(phase==5) {
            if(!both("inventory-craft-armed.txt")) return 0;
            if(role=="host") {
                auto* session=gpBase->mpMultiplayer;
                session->AutoCombineInventory(session->mPeers.begin()->first,"codex_craft_client_piece");
            }
            ++phase;return 0;
        }
        if(phase==6) {
            if(role=="host") {
                auto* session=gpBase->mpMultiplayer;
                const auto& remote=session->mRemoteItems[session->mPeers.begin()->first];
                if(inventoryCraftCallbacks!=1 || inventory->GetItem("codex_craft_host_piece") || inventory->GetItem("codex_craft_output") ||
                   remote.count("codex_craft_client_piece") || !remote.count("codex_craft_output"))
                    return fail(error,"split recipe did not consume group pieces and assign private output");
            } else {
                if(!inventory->GetItem("codex_craft_output")) return 0;
                if(inventory->GetItem("codex_craft_client_piece") || player->GetCurrentState()!=eLuxPlayerState_Normal || state->GetCrosshair()!=NULL)
                    return fail(error,"split recipe retained an equipped consumed ingredient");
            }
            mark(role+"-inventory-craft-passed.txt","real script combined split pieces into collector's private output");++phase;return 0;
        }
        if(phase==7) {
            if(!both("inventory-craft-passed.txt")) return 0;
            if(role=="host") {
                auto* session=gpBase->mpMultiplayer;
                gpBase->mpMapHandler->GetCurrentMap()->RunScript("RemoveItem(\"codex_craft_output\");");
                auto* first=add("codex_craft_host_piece");auto* second=add("codex_craft_client_piece");
                if(!first || !second) return fail(error,"manual host crafting fixtures failed to load");
                inventory->AddCombineCallback("codex_craft_recipe","codex_craft_host_piece","codex_craft_client_piece","CodexInventoryCraft",true);
                // Match the host GUI: these arguments borrow the item strings,
                // and the real callback destroys both items before returning.
                if(!session->CombineInventoryItems(session->GetLocalPeerId(),first->GetName(),second->GetName()) ||
                   inventoryCraftCallbacks!=2 || inventory->GetItem("codex_craft_host_piece") || inventory->GetItem("codex_craft_client_piece") ||
                   !inventory->GetItem("codex_craft_output") || inventory->GetCombineCallback("codex_craft_host_piece","codex_craft_client_piece") ||
                   session->mRemoteItems[session->mPeers.begin()->first].count("codex_craft_output"))
                    return fail(error,"manual host crafting lost its private output or retained a consumed recipe");
                mark(role+"-inventory-manual-craft-passed.txt","host GUI-style borrowed ingredient names survive consuming callback");
            }
            ++phase;return 0;
        }
        if(role=="client") {
            if(!exists("host-inventory-manual-craft-passed.txt") || inventory->GetItem("codex_craft_output")) return 0;
            mark(role+"-inventory-manual-craft-passed.txt","manual host crafting output stayed private");
        }
        if(!both("inventory-manual-craft-passed.txt")) return 0;
        if(role=="host") inventory->RemoveItem("codex_craft_output");
        return 1;
    }
};
#endif
