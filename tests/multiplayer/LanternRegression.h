#ifndef MULTIPLAYER_LANTERN_REGRESSION_H
#define MULTIPLAYER_LANTERN_REGRESSION_H
#include "LuxPlayerHands.h"
#include "LuxHandObject.h"

class cLanternRegression {
    unsigned phase=0,menu=0;
    Uint32 entered=0;
    float oldOil=0,oldYaw=0,oldPitch=0,oldRoll=0,oldBodyYaw=0,menuOil=0;
    bool oldActive=false,oldDisabled=false,oldGravity=true;
    bool addedLantern=false;
    cVector3f oldPosition;
    tString oldHand;
    static bool both(const tString& suffix) {return exists("host-"+suffix) && exists("client-"+suffix);}
    void next(unsigned value) {phase=value;entered=SDL_GetTicks();}
    int fail(tString& error,const tString& message) {error="lantern phase "+cString::ToString((int)phase)+": "+message;return -1;}
    static unsigned lights(cWorld* world) {
        unsigned count=0;auto it=world->GetLightIterator();
        while(it.HasNext()) if(it.Next()->GetName().find("MultiplayerLantern_")==0) ++count;
        return count;
    }
    static bool remoteLight(cLuxMultiplayerWorld* replication,cWorld* world) {
        if(replication->GetRemotePlayers().empty() || lights(world)!=replication->GetRemotePlayers().size()) return false;
        for(const auto& entry:replication->GetRemotePlayers()) {
            const auto& source=entry.second;
            iLight* light=world->GetLight("MultiplayerLantern_"+cString::ToString((int)entry.first));
            if(!source.lantern.active || !light || light->GetLightType()!=eLightType_Point ||
               std::fabs(light->GetRadius()-source.lantern.radius)>0.02f ||
               cMath::Vector3Dist(light->GetWorldPosition(),source.renderPosition+source.renderLanternOffset)>0.15f) return false;
            const auto color=light->GetDiffuseColor();
            if(std::fabs(color.r-source.lantern.color[0])>0.05f || std::fabs(color.g-source.lantern.color[1])>0.05f ||
               std::fabs(color.b-source.lantern.color[2])>0.05f) return false;
        }
        return true;
    }
public:
    int Update(tString& error,float dt) {
        auto* session=gpBase->mpMultiplayer;auto* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!session->IsReady() || !map) return fail(error,"session stopped");
        auto* player=gpBase->mpPlayer;auto* lantern=player->GetHelperLantern();auto* camera=player->GetCamera();
        auto* character=player->GetCharacterBody();auto* hands=player->GetHands();auto* world=map->GetWorld();
        auto* replication=session->GetWorld();const Uint32 age=entered?SDL_GetTicks()-entered:0;
        if(age>10000) return fail(error,"phase timed out");
        if(phase==0) {
            oldOil=player->GetLampOil();oldActive=lantern->IsActive();oldDisabled=lantern->GetDisabled();
            oldPosition=character->GetPosition();oldGravity=character->GravityIsActive();oldBodyYaw=character->GetYaw();
            oldYaw=camera->GetYaw();oldPitch=camera->GetPitch();oldRoll=camera->GetRoll();
            if(auto* object=hands->GetCurrentHandObject()) oldHand=object->GetName();
            // The native out-of-oil path checks inventory even when turning
            // the lantern off. Give this isolated fixture a real lantern item.
            if(!gpBase->mpInventory->HasItemOfType(eLuxItemType_Lantern)) {
                if(!gpBase->mpInventory->AddItem("codex_lantern_fixture",eLuxItemType_Lantern,"Lantern","lantern.tga",1,"",""))
                    return fail(error,"could not add the native lantern inventory item");
                addedLantern=true;
            }
            character->SetGravityActive(false);character->SetForceVelocity(0);
            lantern->SetDisabled(false);player->SetLampOil(80);lantern->SetActive(true,false,false,false);
            next(1);return 0;
        }
        if(phase==1) {
            auto* light=player->GetVisibleLanternLight();
            if(age<400 || !light || hands->GetState()!=eLuxHandsState_Idle || !remoteLight(replication,world)) return 0;
            if(light==lantern->GetLight() || light->GetName().find("_PointLight_1")==tString::npos)
                return fail(error,"source is the helper fill light instead of the visible lantern's main light");
            auto meshes=world->GetDynamicMeshEntityIterator();
            while(meshes.HasNext()) if(meshes.Next()->GetName().find("MultiplayerLantern_")==0)
                return fail(error,"remote lantern created a mesh instead of only a point light");
            mark(role+"-lantern-drawn.txt","visible main light shared once with native color, radius and position");
            next(2);return 0;
        }
        if(phase==2) {
            if(!both("lantern-drawn.txt")) return 0;
            static const char* menus[]={"MainMenu","Inventory","Journal"};
            gpBase->mpEngine->GetUpdater()->SetContainer("Default");
            const float yaw=oldYaw+0.5f+0.4f*menu;
            character->SetPosition(oldPosition+cVector3f(0.1f*(menu+1),0,0));character->SetYaw(yaw);
            camera->SetYaw(yaw);camera->SetPitch(0.1f*(menu+1));camera->SetRoll(0);
            // Seed a visible mismatch before entering the menu. Only a normal
            // subsequent native PostUpdate is allowed to repair this matrix.
            hands->GetHandsEntity()->SetMatrix(cMath::MatrixTranslate(camera->GetPosition()+cVector3f(4,0,0)));
            menuOil=player->GetLampOil();
            if(menu==2) {gpBase->mpEngine->GetUpdater()->SetContainer("Inventory");gpBase->mpJournal->SetOpenedFromInventory(true);}
            gpBase->mpEngine->GetUpdater()->SetContainer(menus[menu]);next(3);return 0;
        }
        if(phase==3) {
            const tString marker="lantern-menu-"+cString::ToString((int)menu)+".txt";
            if(!exists(role+"-"+marker)) {
                if(age<700) return 0;
                static const char* menus[]={"MainMenu","Inventory","Journal"};
                static const eLuxInputState input[]={eLuxInputState_MainMenu,eLuxInputState_Inventory,eLuxInputState_Journal};
                if(gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!=menus[menu] || gpBase->mpInputHandler->GetState()!=input[menu])
                    return fail(error,"background player updates changed the active menu input");
                const cMatrixf actual=hands->GetHandsEntity()->GetWorldMatrix();
                const cMatrixf expected=cMath::MatrixRotate(cVector3f(camera->GetPitch(),camera->GetYaw(),camera->GetRoll()),eEulerRotationOrder_ZXY);
                if(cMath::Vector3Dist(actual.GetTranslation(),camera->GetPosition())>0.5f)
                    return fail(error,"native hands position froze while menu was open");
                for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c) if(std::fabs(actual.m[r][c]-expected.m[r][c])>0.04f)
                    return fail(error,"native camera-follow smoothing froze while menu was open");
                if(player->GetLampOil()>=menuOil-0.001f)
                    return fail(error,"lantern oil consumption stopped in menu "+cString::ToString((int)menu));
                if(!player->GetVisibleLanternLight())
                    return fail(error,"local lantern main light disappeared in menu "+cString::ToString((int)menu));
                if(!remoteLight(replication,world))
                    return fail(error,"remote lantern light did not match its received state in menu "+cString::ToString((int)menu));
                mark(role+"-"+marker,"native hands matrix, oil and remote light continue with menu input preserved");
            }
            if(!both(marker)) return 0;
            gpBase->mpEngine->GetUpdater()->SetContainer("Default");
            if(++menu<3) {next(2);return 0;}
            lantern->SetActive(false,false,false,false);next(4);return 0;
        }
        if(phase==4) {
            if(!exists(role+"-lantern-holstered.txt")) {
                if(age<200 || player->GetVisibleLanternLight() || lights(world)!=0 || !replication->mPlayerLights.empty()) return 0;
                mark(role+"-lantern-holstered.txt","holstering removes the remote main light");
            }
            if(!both("lantern-holstered.txt")) return 0;
            player->SetLampOil(80);lantern->SetActive(true,false,false,false);next(5);return 0;
        }
        if(phase==5) {
            if(!exists(role+"-lantern-oil-armed.txt")) {
                if(!player->GetVisibleLanternLight() || !remoteLight(replication,world)) return 0;
                mark(role+"-lantern-oil-armed.txt","lantern redrawn before exhaustion");
            }
            if(!both("lantern-oil-armed.txt")) return 0;
            player->SetLampOil(0);next(6);return 0;
        }
        if(phase==6) {
            if(!exists(role+"-lantern-oil-empty.txt")) {
                if(age<200 || lantern->IsActive() || player->GetVisibleLanternLight() || lights(world)!=0 || !replication->mPlayerLights.empty()) return 0;
                mark(role+"-lantern-oil-empty.txt","native oil exhaustion removes the remote main light");
            }
            if(!both("lantern-oil-empty.txt")) return 0;
            gpBase->mpEngine->GetUpdater()->SetContainer("Default");
            character->SetPosition(oldPosition);character->SetYaw(oldBodyYaw);character->SetForceVelocity(0);character->SetGravityActive(oldGravity);
            camera->SetYaw(oldYaw);camera->SetPitch(oldPitch);camera->SetRoll(oldRoll);
            player->SetLampOil(oldOil);lantern->SetDisabled(oldDisabled);lantern->SetActive(oldActive,false,false,false);
            if(!oldActive && !oldHand.empty()) hands->SetActiveHandObject(oldHand);
            if(addedLantern) gpBase->mpInventory->RemoveItem("codex_lantern_fixture");
            mark(role+"-lantern-finished.txt","remote main light and all three menu PostUpdate cases passed");next(7);return 0;
        }
        return both("lantern-finished.txt")?1:0;
    }
};
#endif
