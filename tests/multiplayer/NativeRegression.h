// Real game instances exercise the production request/grant/result and snapshot
// paths. File markers coordinate the harnesses; no gameplay result is mocked.
#ifndef MULTIPLAYER_NATIVE_REGRESSION_H
#define MULTIPLAYER_NATIVE_REGRESSION_H
#include "LuxProp_Item.h"
#include "LuxProp_Lamp.h"
#include "LuxProp_SwingDoor.h"
#include "LuxProp_MoveObject.h"
#include "system/LowLevelSystem.h"
#define private public
#include "LuxMultiplayerEntities.h"
#undef private
#include "DiaryRegression.h"

static std::map<std::string,unsigned> nativeCallbacks;
static void __stdcall CodexNativeCallback(std::string& entity,std::string& event) {
    ++nativeCallbacks[entity+":"+event];
}
static void __stdcall CodexNativeInteractCallback(std::string& entity) {
    ++nativeCallbacks[entity+":OnInteract"];
}
class cNativeRegression {
    unsigned phase=0;
    Uint32 entered=0;
    Uint32 diagnosticAt=0;
    bool callbackRequested=false;
    bool lampEffectsBaselineRequested=false;
    bool recreationRequested=false;
    int recreationEntityID=-1, recreationTinderboxes=0;
    uint64_t recreationRuntimeID=0;
    cMatrixf tinderTransform;
    cNativeDiaryRegression diaryRegression;
    void next() { ++phase;entered=SDL_GetTicks(); }
    bool both(const char* suffix) { return exists(tString("host-")+suffix) && exists(tString("client-")+suffix); }
    bool near(iLuxEntity* entity) {
        if(!entity || !entity->GetBodyNum()) return false;
        gpBase->mpPlayer->GetCharacterBody()->SetPosition(entity->GetBody(0)->GetWorldPosition()+cVector3f(0,0,1));
        return true;
    }
    bool absent(cLuxMap* map,const char* name) {
        auto* entity=map->GetEntityByName(name);return !entity || entity->GetDestroyMe();
    }
    int fail(tString& error,const char* message) {
        error="native phase "+cString::ToString(static_cast<int>(phase))+": "+message;return -1;
    }
    bool callbackCount(const char* name,const char* event,unsigned count) {
        return nativeCallbacks[tString(name)+":"+event]==count;
    }
public:
    // 0 pending, 1 complete, -1 failed.
    int Update(tString& error) {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
        if(!mp->IsActive() || !map) return fail(error,"session/map disappeared");
        const bool host=role=="host";
        const Uint32 age=SDL_GetTicks()-entered;
        if(phase && phase!=21 && age>15000) return fail(error,"timed out waiting for native replication");
        auto* lamp=static_cast<cLuxProp_Lamp*>(map->GetEntityByName("torch_static01_1",eLuxEntityType_Prop,eLuxPropType_Lamp));
        auto* door=static_cast<cLuxProp_SwingDoor*>(map->GetEntityByName("Door_1",eLuxEntityType_Prop,eLuxPropType_SwingDoor));
        auto* shelf=static_cast<cLuxProp_MoveObject*>(map->GetEntityByName("secret_shelf",eLuxEntityType_Prop,eLuxPropType_MoveObject));
        if(!lamp || !door || !shelf) return fail(error,"Old Archives native fixtures missing");
        if(phase==0) {
            auto* clientItem=map->GetEntityByName("tinderbox_1");
            auto* hostItem=map->GetEntityByName("tinderbox_2");
            if(!clientItem || !hostItem) return fail(error,"pickup fixtures missing");
            tinderTransform=clientItem->GetBody(0)->GetWorldMatrix();
            if(!gpBase->mpEngine->GetSystem()->GetLowLevel()->AddScriptFunc(
                "void CodexNativeCallback(string &in asEntity, string &in asEvent)",(void*)CodexNativeCallback))
                return fail(error,"native callback observer registration failed");
            if(!gpBase->mpEngine->GetSystem()->GetLowLevel()->AddScriptFunc(
                "void CodexNativeInteractCallback(string &in asEntity)",(void*)CodexNativeInteractCallback))
                return fail(error,"interaction callback observer registration failed");
            gpBase->mpPlayer->SetTinderboxes(host?7:2);
            if(host) {
                clientItem->SetCallbackFunc("CodexNativeCallback");
                hostItem->SetCallbackFunc("CodexNativeCallback");
                lamp->SetCallbackFunc("CodexNativeCallback");lamp->SetLit(false,false);
                door->SetLocked(true,false);door->SetClosed(true,false);
                door->SetDisableAutoClose(true);
                if(shelf->GetMainBody()->GetMass()!=0) return fail(error,"shelf fixture is not a static mover");
            }
            mark(role+"-native-setup.txt","ready");next();return 0;
        }
        if(phase==1) {
            if(!both("native-setup.txt")) return 0;
            auto* item=map->GetEntityByName("tinderbox_1");
            if(!host && !near(item)) return fail(error,"client pickup disappeared before request");
            if(age<700) return 0; // Publish the client's real pose before host reach validation.
            if(!host) {
                item->OnInteract(item->GetBody(0),item->GetBody(0)->GetWorldPosition());
                item->OnInteract(item->GetBody(0),item->GetBody(0)->GetWorldPosition());
            }
            next();return 0;
        }
        if(phase==2) {
            if(!absent(map,"tinderbox_1")) return 0;
            if(gpBase->mpPlayer->GetTinderboxes()!=(host?7:3)) return fail(error,"client pickup was duplicated or credited to another player");
            if(!callbackCount("tinderbox_1","OnPickup",host?1:0)) return fail(error,"client pickup callback did not run exactly once on host");
            mark(role+"-native-client-pickup.txt","passed");next();return 0;
        }
        if(phase==3) {
            if(!both("native-client-pickup.txt")) return 0;
            if(host) {
                auto* item=map->GetEntityByName("tinderbox_2");
                if(!item) return fail(error,"host pickup fixture missing");
                item->OnInteract(item->GetBody(0),item->GetBody(0)->GetWorldPosition());
                item->OnInteract(item->GetBody(0),item->GetBody(0)->GetWorldPosition());
            }
            next();return 0;
        }
        if(phase==4) {
            if(!absent(map,"tinderbox_2")) return 0;
            if(gpBase->mpPlayer->GetTinderboxes()!=(host?8:3)) return fail(error,"host pickup credit is not exclusive");
            if(!callbackCount("tinderbox_2","OnPickup",host?1:0)) return fail(error,"host pickup callback count incorrect");
            mark(role+"-native-host-pickup.txt","passed");next();return 0;
        }
        if(phase==5) {
            if(!both("native-host-pickup.txt")) return 0;
            if(!host) {gpBase->mpPlayer->SetTinderboxes(0);near(lamp);}
            if(age<700) return 0;
            if(!host) lamp->OnInteract(lamp->GetBody(0),lamp->GetBody(0)->GetWorldPosition());
            next();return 0;
        }
        if(phase==6) {
            if(age<1000 || !mp->mpEntities->msPending.empty() || !mp->mpEntities->mClaims.empty()) return 0;
            if(lamp->GetLit() || gpBase->mpPlayer->GetTinderboxes()!=(host?8:0)) return fail(error,"failed ignition changed lamp or charged a player");
            if(!callbackCount("torch_static01_1","OnIgnite",0)) return fail(error,"failed ignition ran callback");
            mark(role+"-native-no-fuel.txt","passed");next();return 0;
        }
        if(phase==7) {
            if(!both("native-no-fuel.txt")) return 0;
            if(!host) {
                gpBase->mpPlayer->SetTinderboxes(2);near(lamp);
                lamp->OnInteract(lamp->GetBody(0),lamp->GetBody(0)->GetWorldPosition());
                lamp->OnInteract(lamp->GetBody(0),lamp->GetBody(0)->GetWorldPosition());
            }
            next();return 0;
        }
        if(phase==8) {
            if(!lamp->GetLit() || !mp->mpEntities->msPending.empty() || !mp->mpEntities->mClaims.empty()) return 0;
            if(gpBase->mpPlayer->GetTinderboxes()!=(host?8:1)) return fail(error,"successful ignition cost was not paid exactly once by client");
            if(!callbackCount("torch_static01_1","OnIgnite",host?1:0)) return fail(error,"lamp callback did not run exactly once on host");
            if(!door->GetLocked() || !door->GetClosed()) return fail(error,"closed/locked door state did not replicate");
            if(host) map->RunScript("SetEntityPlayerInteractCallback(\"torch_static01_1\",\"CodexNativeInteractCallback\",false);");
            mark(role+"-native-ignition.txt","passed");next();return 0;
        }
        if(phase==9) {
            if(!both("native-ignition.txt")) return 0;
            if(!host && !callbackRequested) {
                if(age<700) return 0;
                near(lamp);lamp->OnInteract(lamp->GetBody(0),lamp->GetBody(0)->GetWorldPosition());
                lamp->OnInteract(lamp->GetBody(0),lamp->GetBody(0)->GetWorldPosition());
                callbackRequested=true;
            }
            if(host) {
                if(!callbackCount("torch_static01_1","OnInteract",1)) return 0;
                mark("host-native-lit-callback.txt","passed");
            }
            if(!exists("host-native-lit-callback.txt") || !mp->mpEntities->msPending.empty() || !mp->mpEntities->mClaims.empty()) return 0;
            if(!callbackCount("torch_static01_1","OnIgnite",host?1:0) || !callbackCount("torch_static01_1","OnInteract",host?1:0) ||
               gpBase->mpPlayer->GetTinderboxes()!=(host?8:1)) return fail(error,"lit-lamp callback consumed fuel or executed on client");
            if(host) {
                door->SetLocked(false,false);door->SetClosed(false,false);door->SetDisableAutoClose(true);
                // Real script execution tests the typed movement-start registry.
                map->RunScript("SetMoveObjectStateExt(\"secret_shelf\",1,5,3,0.05f,true);");
            }
            next();return 0;
        }
        if(phase==10) {
            if(SDL_GetTicks()-diagnosticAt>2000) {
                diagnosticAt=SDL_GetTicks();
                const cMatrixf& current=shelf->GetMainBody()->GetLocalMatrix();
                const cMatrixf& open=shelf->GetOpenTransform();
                const cVector3f angle=cMath::MatrixEulerAngleDistance(current.GetRotation(),open.GetRotation());
                std::printf("%s native mover: locked=%d closed=%d disable-auto=%d active=%d moving=%d state=%.9g angle-to-open=%.9g pos=(%.6g %.6g %.6g)\n",
                    role.c_str(),door->GetLocked(),door->GetClosed(),door->GetDisableAutoClose(),shelf->IsActive(),shelf->IsMoving(),
                    shelf->GetMoveState(),angle.Length(),current.m[0][3],current.m[1][3],current.m[2][3]);
                std::fflush(stdout);
                tString matrices;
                for(int row=0;row<4;++row) for(int column=0;column<4;++column)
                    matrices+=cString::ToString(current.m[row][column])+" ";
                matrices+="\nopen: ";
                for(int row=0;row<4;++row) for(int column=0;column<4;++column)
                    matrices+=cString::ToString(open.m[row][column])+" ";
                mark(role+"-native-shelf-diagnostic.txt",matrices);
            }
            if(door->GetLocked() || door->GetClosed() || !door->GetDisableAutoClose()) return 0;
            if(host && !exists("host-native-shelf-target.txt")) {
                const cMatrixf& current=shelf->GetMainBody()->GetLocalMatrix();
                float openError=0,closedDifference=0;
                for(int row=0;row<3;++row) for(int column=0;column<3;++column) {
                    openError=std::max(openError,std::fabs(current.m[row][column]-shelf->GetOpenTransform().m[row][column]));
                    closedDifference=std::max(closedDifference,std::fabs(current.m[row][column]-shelf->GetClosedTransform().m[row][column]));
                }
                // This retail prop starts with tiny off-axis rotations.
                // GetMoveState divides the first nonzero Euler component and
                // reports ~24.19 at the actual open matrix. Assert real motion
                // and the configured rotation directly, then test the stop event.
                if(age<1000 || shelf->IsMoving() || openError>0.005f || closedDifference<0.1f) return 0;
                map->RunScript("StopPropMovement(\"secret_shelf\");");
                FILE* file=NULL;fopen_s(&file,(outputDir+"/host-native-shelf-target.txt").c_str(),"wb");
                if(!file) return fail(error,"could not record shelf target");
                const cMatrixf& matrix=shelf->GetMainBody()->GetWorldMatrix();
                for(int row=0;row<4;++row) for(int column=0;column<4;++column) std::fprintf(file,"%.9g ",matrix.m[row][column]);
                std::fclose(file);
            }
            if(!exists("host-native-shelf-target.txt")) return 0;
            FILE* file=NULL;fopen_s(&file,(outputDir+"/host-native-shelf-target.txt").c_str(),"rb");
            if(!file) return 0;
            float maximumError=0;bool complete=true;
            const cMatrixf& matrix=shelf->GetMainBody()->GetWorldMatrix();
            for(int row=0;row<4;++row) for(int column=0;column<4;++column) {
                float value=0;if(std::fscanf(file,"%f",&value)!=1) complete=false;
                maximumError=std::max(maximumError,std::fabs(matrix.m[row][column]-value));
            }
            std::fclose(file);if(!complete || maximumError>0.06f || shelf->IsMoving()) return 0;
            mark(role+"-native-world.txt","passed: discrete door flags and static moving bookshelf transform");next();return 0;
        }
        if(phase==11) {
            if(!both("native-world.txt")) return 0;
            if(!host) {
                // Deliberately stale client replica: the late-join baseline path
                // must remove the already collected item and restore live state.
                map->CreateEntity("tinderbox_1","tinderbox.ent",tinderTransform,1);
                lamp->SetLit(false,false);door->SetLocked(true,false);door->SetClosed(true,false);
                mark("client-native-stale.txt","ready for baseline replay");
            } else {
                if(!exists("client-native-stale.txt")) return 0;
                if(mp->mPeers.empty() || !mp->mpEntities->SendInitialState(mp->mPeers.begin()->first)) return fail(error,"late-join native baseline rejected");
            }
            next();return 0;
        }
        if(phase==12) {
            if(!absent(map,"tinderbox_1") || !lamp->GetLit() || door->GetLocked() || door->GetClosed()) return 0;
            if(gpBase->mpPlayer->GetTinderboxes()!=(host?8:1)) return fail(error,"baseline replay changed inventory");
            if(!callbackCount("tinderbox_1","OnPickup",host?1:0) || !callbackCount("tinderbox_2","OnPickup",host?1:0) ||
               !callbackCount("torch_static01_1","OnIgnite",host?1:0)) return fail(error,"baseline replay repeated native callbacks");
            if(!lampEffectsBaselineRequested) {
                if(!host) {
                    // Keep Lit correct, but lose the light/particle effects.
                    // SetLit alone returns early and cannot repair this state.
                    lamp->SetEffectsActive(false,false);
                    mark("client-native-stale-lamp-effects.txt","lit flag matches host but effects are disabled");
                } else {
                    if(!exists("client-native-stale-lamp-effects.txt")) return 0;
                    if(!mp->mpEntities->SendInitialState(mp->mPeers.begin()->first)) return fail(error,"lamp effects baseline rejected");
                }
                lampEffectsBaselineRequested=true;return 0;
            }
            if(lamp->GetEffectsAlpha()<0.99f) return 0;
            mark(role+"-native-baseline.txt","collected original stays removed after baseline replay");next();return 0;
        }
        const tString recreatedName="codex_native_recreated",claimName="codex_native_replaced_claim";
        const uint32_t claimToken=0x1234abcd;
        if(phase==13) {
            if(!both("native-baseline.txt")) return 0;
            map->CreateEntity(recreatedName,"tinderbox.ent",tinderTransform,1);
            auto* item=map->GetEntityByName(recreatedName);
            if(!item) return fail(error,"recreated pickup fixture could not be loaded");
            item->SetCallbackFunc("CodexNativeCallback");
            recreationEntityID=item->GetID();recreationRuntimeID=item->GetRuntimeID();
            recreationTinderboxes=gpBase->mpPlayer->GetTinderboxes();recreationRequested=false;
            mark(role+"-native-reuse-created.txt","first fixture instance ready");next();return 0;
        }
        if(phase==14) {
            if(!both("native-reuse-created.txt")) return 0;
            if(host && !recreationRequested) {
                auto* item=map->GetEntityByName(recreatedName);
                item->OnInteract(item->GetBody(0),item->GetBody(0)->GetWorldPosition());recreationRequested=true;
            }
            if(!absent(map,recreatedName.c_str())) return 0;
            mark(role+"-native-reuse-collected.txt","first instance collected and removed");next();return 0;
        }
        if(phase==15) {
            if(!both("native-reuse-collected.txt") || map->GetEntityByName(recreatedName)) return 0;
            // CreateEntity deliberately reuses the lowest free authored ID.
            map->CreateEntity(recreatedName,"tinderbox.ent",tinderTransform,1);
            auto* item=map->GetEntityByName(recreatedName);
            if(!item || item->GetID()!=recreationEntityID || item->GetRuntimeID()==recreationRuntimeID)
                return fail(error,"replacement did not reuse the authored ID with a new lifetime identity");
            item->SetCallbackFunc("CodexNativeCallback");
            if(!host && !near(item)) return fail(error,"could not reach replacement pickup");
            recreationRequested=false;
            mark(role+"-native-reuse-recreated.txt","same name and authored ID; new collectible instance");next();return 0;
        }
        if(phase==16) {
            if(!both("native-reuse-recreated.txt") || age<700) return 0;
            if(!host && !recreationRequested) {
                auto* item=map->GetEntityByName(recreatedName);
                item->OnInteract(item->GetBody(0),item->GetBody(0)->GetWorldPosition());recreationRequested=true;
            }
            if(!absent(map,recreatedName.c_str()) || !mp->mpEntities->msPending.empty() || !mp->mpEntities->mClaims.empty()) return 0;
            if(gpBase->mpPlayer->GetTinderboxes()!=recreationTinderboxes+1 ||
               !callbackCount(recreatedName.c_str(),"OnPickup",host?2:0))
                return fail(error,"replacement pickup was duplicated, lost, or repeated its callback");
            mark(role+"-native-reuse-passed.txt","host collected original; client collected same-ID replacement exactly once");next();return 0;
        }
        if(phase==17) {
            if(!both("native-reuse-passed.txt") || map->GetEntityByName(recreatedName)) return 0;
            map->CreateEntity(claimName,"tinderbox.ent",tinderTransform,1);
            auto* item=map->GetEntityByName(claimName);
            if(!item) return fail(error,"stale-claim fixture could not be loaded");
            recreationEntityID=item->GetID();recreationRuntimeID=item->GetRuntimeID();
            recreationTinderboxes=gpBase->mpPlayer->GetTinderboxes();recreationRequested=false;
            // Hold an approved transaction across destruction. Its grant is
            // delivered below through the production decoder after replacement.
            if(host) mp->mpEntities->mClaims[claimName]={mp->mPeers.begin()->first,claimToken,0,recreationRuntimeID,false};
            else {mp->mpEntities->msPending=claimName;mp->mpEntities->mlPendingRuntimeID=recreationRuntimeID;}
            map->DestroyEntity(item);
            mark(role+"-native-claim-held.txt","transaction refers to original lifetime");next();return 0;
        }
        if(phase==18) {
            if(!both("native-claim-held.txt") || map->GetEntityByName(claimName)) return 0;
            map->CreateEntity(claimName,"tinderbox.ent",tinderTransform,1);
            auto* item=map->GetEntityByName(claimName);
            if(!item || item->GetID()!=recreationEntityID || item->GetRuntimeID()==recreationRuntimeID)
                return fail(error,"stale-claim replacement did not reuse its authored ID");
            if(host) {
                // Check forged/stale success results without consuming the live
                // claim needed for the client's real failure acknowledgement.
                cLuxMultiplayerEntities isolated(mp);
                const uint32_t peer=mp->mPeers.begin()->first;
                isolated.mClaims[claimName]={peer,claimToken,0,recreationRuntimeID,false};
                luxnet::Writer wrong(luxnet::NativeResult);wrong.U32(mp->GetMapEpoch());wrong.String(claimName);
                wrong.U32(claimToken+1);wrong.U8(1);wrong.U32(0);
                if(isolated.HandleMessage(peer,wrong.data) || !isolated.mClaims.count(claimName) || item->GetDestroyMe())
                    return fail(error,"forged native result consumed the outstanding claim");
                luxnet::Writer stale(luxnet::NativeResult);stale.U32(mp->GetMapEpoch());stale.String(claimName);
                stale.U32(claimToken);stale.U8(1);stale.U32(0);
                if(!isolated.HandleMessage(peer,stale.data) || !isolated.mClaims.empty() || item->GetDestroyMe())
                    return fail(error,"old successful claim collected the replacement instance");
            }
            mark(role+"-native-claim-replaced.txt","replacement survives stale successful result");next();return 0;
        }
        if(phase==19) {
            if(!both("native-claim-replaced.txt")) return 0;
            auto* item=map->GetEntityByName(claimName);
            if(!host && !recreationRequested) {
                luxnet::Writer grant(luxnet::NativeGrant);grant.U32(mp->GetMapEpoch());grant.String(claimName);
                grant.U32(claimToken);grant.U8(0);
                if(!mp->mpEntities->HandleMessage(0,grant.data)) return fail(error,"replaced client grant was treated as malformed");
                recreationRequested=true;
            }
            if(!item || item->GetDestroyMe() || gpBase->mpPlayer->GetTinderboxes()!=recreationTinderboxes)
                return fail(error,"old native grant collected a new client instance");
            if(!mp->mpEntities->msPending.empty() || !mp->mpEntities->mClaims.empty()) return 0;
            mark(role+"-native-claim-passed.txt","stale grant rejected locally; host received failure without losing replacement");next();return 0;
        }
        if(phase==20) {
            if(!both("native-claim-passed.txt")) return 0;
            if(auto* item=map->GetEntityByName(claimName)) map->DestroyEntity(item);
            next();return 0;
        }
        if(phase==21) return diaryRegression.Update(tinderTransform,error);
        return 0;
    }
};
#endif
