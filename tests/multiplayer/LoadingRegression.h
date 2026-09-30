#ifndef CODEX_LOADING_REGRESSION_H
#define CODEX_LOADING_REGRESSION_H

class cLoadingPhaseObserver {
    uint32_t progressEpoch=0,lastProgress=0;
    uint32_t heldEnemyPeer=0,heldEnemyEpoch=0;
    Uint32 enemyHeldAt=0;
    std::set<tString> checkedEnemyTransfers;
    static tString enemyMarker(uint32_t peer,uint32_t epoch,const char* suffix) {
        return "enemy-loading-"+cString::ToString(int(peer))+"-"+cString::ToString(int(epoch))+"-"+suffix;
    }
    bool ObserveHostEnemyLoading(tString& error) {
        auto* mp=gpBase->mpMultiplayer;
        if(!heldEnemyPeer) for(auto& peer:mp->mPeers) {
            auto& state=peer.second;const tString key=enemyMarker(peer.first,mp->GetMapEpoch(),"checked.txt");
            if(state.beginSent && state.transferRequested && !state.endSent && !state.ready && state.offset<mp->mvMapBytes.size() &&
               !checkedEnemyTransfers.count(key) && exists("client-"+enemyMarker(peer.first,mp->GetMapEpoch(),"waiting.txt"))) {
                heldEnemyPeer=peer.first;heldEnemyEpoch=mp->GetMapEpoch();enemyHeldAt=SDL_GetTicks();break;
            }
        }
        if(!heldEnemyPeer) return true;
        auto found=mp->mPeers.find(heldEnemyPeer);
        if(found==mp->mPeers.end() || mp->GetMapEpoch()!=heldEnemyEpoch) {error="enemy loading test lost its held transfer";return false;}
        // Hold MapChunk/MapEnd long enough for the client to acknowledge actual
        // production dispatch of the delayed unreliable packets while unready.
        found->second.transferRequested=false;
        const tString sent="host-"+enemyMarker(heldEnemyPeer,heldEnemyEpoch,"sent.txt");
        if(!exists(sent)) {
            for(uint32_t epoch:{heldEnemyEpoch-1,heldEnemyEpoch}) {
                LuxEnemyWire::State state;state.epoch=epoch;state.sequence=1;state.generation=1;state.name="codex_delayed_enemy";
                luxnet::Writer terror(luxnet::EnemyTerror);terror.U32(epoch);terror.U32(1);terror.U32(1);terror.Float(0.75f);
                if(!mp->Send(heldEnemyPeer,LuxEnemyWire::EncodeState(state),false) || !mp->Send(heldEnemyPeer,terror.data,false)) {
                    error="could not send delayed enemy packets during loading";return false;
                }
            }
            mark(sent,"real delayed enemy packets sent while map transfer is held");
        }
        if(exists("client-"+enemyMarker(heldEnemyPeer,heldEnemyEpoch,"checked.txt"))) {
            found->second.transferRequested=true;
            checkedEnemyTransfers.insert(enemyMarker(heldEnemyPeer,heldEnemyEpoch,"checked.txt"));heldEnemyPeer=0;
        } else if(SDL_GetTicks()-enemyHeldAt>5000) {error="client did not dispatch delayed enemy packets while loading";return false;}
        return true;
    }
    bool screenshot[6]={},progressScreenshot=false;
    bool saveFrame(const tString& name,tString& error) {
        cBitmap* bitmap=gpBase->mpEngine->GetGraphics()->GetLowLevel()->CopyFrameBufferToBitmap();
        if(!bitmap) {error="loading phase screenshot readback failed";return false;}
        const bool saved=gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->SaveBitmap(
            bitmap,cString::To16Char(outputDir+"/"+name),0);
        hplDelete(bitmap);
        if(!saved) error="loading phase screenshot could not be saved";
        return saved;
    }
public:
    unsigned samples[6]={},frames[6]={},progressSamples=0,progressFrames=0;
    bool Observe(tString& error) {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        if(!mp->IsActive()) return true;
        if(role=="host") return ObserveHostEnemyLoading(error);
        if(role!="client") return true;
        const int phase=int(mp->GetLoadPhase());
        if(phase<0 || phase>=6) {error="invalid multiplayer loading phase";return false;}
        ++samples[phase];
        if(phase==eLuxMultiplayerLoadPhase_None) return true;
        if(phase==eLuxMultiplayerLoadPhase_Downloading) {
            const uint32_t peer=mp->GetLocalPeerId(),epoch=mp->GetMapEpoch();
            mark("client-"+enemyMarker(peer,epoch,"waiting.txt"),"client downloading with no ready map replicas");
            if(exists("host-"+enemyMarker(peer,epoch,"sent.txt")) &&
               (mp->mlLastPacketType==luxnet::EnemyState || mp->mlLastPacketType==luxnet::EnemyTerror)) {
                if(mp->IsReady() || !mp->GetEnemies()->mReplicas.empty() || mp->GetWorld()->mlLastTerrorSequence) {
                    error="delayed enemy packet mutated an unready replica";return false;
                }
                mark("client-"+enemyMarker(peer,epoch,"checked.txt"),"real delayed enemy packets dispatched harmlessly before MapEnd");
                mark("client-loading-enemy-discard.txt","validated delayed enemy state/terror packets discarded before map readiness");
            }
        }
        cGuiSet* gui=gpBase->mpEngine->GetGui()->GetSetFromName("LoadScreen");
        cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
        if(mp->IsReady() || gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!="MultiplayerLoading" ||
           gpBase->mpInputHandler->GetState()!=eLuxInputState_LoadScreen || !gui || !gui->IsActive() ||
           gpBase->mpMapHandler->GetViewport()->IsActive() || gpBase->mpMapHandler->GetViewport()->IsVisible() ||
           mp->GetLoadScreenStatus().empty() || (map && map->GetWorld()->IsActive())) {
            error="loading phase left gameplay ready, interactive, visible, or without loading status";return false;
        }
        if(phase==eLuxMultiplayerLoadPhase_Downloading) {
            const uint32_t received=mp->GetDownloadReceivedBytes(),total=mp->GetDownloadTotalBytes();
            if(!total || received>total || (progressEpoch==mp->GetMapEpoch() && received<lastProgress)) {
                error="download loading phase has invalid or decreasing byte progress";return false;
            }
            progressEpoch=mp->GetMapEpoch();lastProgress=received;
            if(received>0 && received<total) ++progressSamples;
        }
        return true;
    }
    bool OnPostRender(tString& error) {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        if(role!="client" || !mp->IsActive()) return true;
        const int phase=int(mp->GetLoadPhase());
        if(phase<=eLuxMultiplayerLoadPhase_None || phase>=6) return true;
        ++frames[phase];
        static const char* names[]={"none","connecting","preparing","checking","downloading","loading"};
        if(!screenshot[phase]) {
            if(!saveFrame(tString("client-phase-")+names[phase]+".png",error)) return false;
            screenshot[phase]=true;
        }
        if(phase==eLuxMultiplayerLoadPhase_Downloading && mp->GetDownloadReceivedBytes()>0 &&
           mp->GetDownloadReceivedBytes()<mp->GetDownloadTotalBytes()) {
            ++progressFrames;
            if(!progressScreenshot) {
                if(!saveFrame("client-phase-download-progress.png",error)) return false;
                progressScreenshot=true;
            }
        }
        return true;
    }
};

// Exercise both production map-change entry points while the network remains
// live. The harness briefly holds the accepted request, never production code,
// so the client must render preparation before synchronous loading may begin.
class cLoadingRegression {
    unsigned trial=0,phase=0,preparingFrames=0;
    Uint32 entered=0;
    cLuxMap* originalMap=NULL;
    uint32_t originalEpoch=0;
    tString heldDebugMap;
    tString marker(const char* suffix) const {
        return "loading-"+cString::ToString(static_cast<int>(trial))+"-"+suffix;
    }
    static bool both(const tString& suffix) {return exists("host-"+suffix) && exists("client-"+suffix);}
    void next(unsigned value) {phase=value;entered=SDL_GetTicks();}
    int fail(tString& error,const char* message) const {
        error="loading trial "+cString::ToString(static_cast<int>(trial))+" phase "+
            cString::ToString(static_cast<int>(phase))+": "+message;return -1;
    }
public:
    int Update(cLoadingPhaseObserver& observer,tString& error) {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        cLuxMapHandler* maps=gpBase->mpMapHandler;
        const bool host=role=="host";
        if(!mp->IsActive()) return fail(error,"session stopped");
        if(entered && SDL_GetTicks()-entered>10000) return fail(error,"preparation/cancellation timed out");
        if(phase==0) {
            originalMap=maps->GetCurrentMap();originalEpoch=mp->GetMapEpoch();
            preparingFrames=observer.frames[eLuxMultiplayerLoadPhase_Preparing];
            if(!originalMap || !mp->IsReady()) return fail(error,"old map is not ready");
            if(mp->IsWindowVisible()) mp->ToggleWindow();
            gpBase->mpEngine->GetUpdater()->SetContainer("Default");
            gpBase->mpInputHandler->ChangeState(eLuxInputState_Game);
            if(!host && trial>0) {
                const char* menus[]={"Default","Inventory","Journal","MainMenu"};
                gpBase->mpEngine->GetUpdater()->SetContainer(menus[trial]);
                if(!maps->GetViewport()->IsVisible() || !maps->GetViewport()->IsActive())
                    return fail(error,"live-menu fixture did not retain its gameplay viewport");
            }
            mark(role+"-"+marker("armed.txt"),"ready");next(1);return 0;
        }
        if(maps->GetCurrentMap()!=originalMap || mp->GetMapEpoch()!=originalEpoch)
            return fail(error,"teleport or failed map load replaced the old map/epoch");
        if(trial==0 && !host && (!mp->IsReady() || mp->GetLoadPhase()!=eLuxMultiplayerLoadPhase_None ||
           gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!="Default" || !originalMap->GetWorld()->IsActive()))
            return fail(error,"host-only same-map teleport froze the client");
        if(phase==1) {
            if(!both(marker("armed.txt"))) return 0;
            if(host) {
                if(trial==0) maps->ChangeMap(mp->msMapName,"PlayerStartArea_1","","");
                else {
                    const tString missing="codex_missing_"+std::filesystem::path(outputDir).filename().string()+".map";
                    if(cPlatform::FileExists(cString::To16Char(maps->GetMapFolder()+missing)) ||
                       !gpBase->mpEngine->GetResources()->GetFileSearcher()->GetFilePath(missing).empty())
                        return fail(error,"missing-map fixture unexpectedly exists");
                    if(trial==1) {
                        maps->ChangeMap(missing,"PlayerStartArea_1","","");
                        if(!maps->mMapChangeData.mbActive) return fail(error,"normal map change was not accepted");
                        maps->mMapChangeData.mbActive=false;
                    } else {
                        if(!mp->HostChangeMap(missing,"PlayerStartArea_1")) return fail(error,"debug map change was not accepted");
                        heldDebugMap=mp->msPendingHostMap;mp->msPendingHostMap.clear();
                    }
                }
                mark("host-"+marker("requested.txt"),"accepted before synchronous loading");
            }
            next(2);return 0;
        }
        if(phase==2) {
            if(trial==0) {
                if(host) {
                    if(maps->mMapChangeData.mbActive || !gpBase->mpPlayer->IsActive()) return 0;
                    mark("host-"+marker("teleported.txt"),"same-map teleport completed");
                }
                if(!exists("host-"+marker("teleported.txt"))) return 0;
                mark(role+"-"+marker("passed.txt"),"same-map teleport preserved client gameplay");next(5);return 0;
            }
            if(!host) {
                if(mp->GetLoadPhase()!=eLuxMultiplayerLoadPhase_Preparing ||
                   observer.frames[eLuxMultiplayerLoadPhase_Preparing]<=preparingFrames) return 0;
                if(mp->IsReady() || originalMap->GetWorld()->IsActive()) return fail(error,"preparation did not suspend old world");
                mark("client-"+marker("preparing-rendered.txt"),"preparation rendered before host load was released");
            } else {
                if(!exists("client-"+marker("preparing-rendered.txt"))) return 0;
                if(trial==1) maps->mMapChangeData.mbActive=true;
                else mp->msPendingHostMap=heldDebugMap;
                mark("host-"+marker("load-released.txt"),"client preparation rendered before host synchronous load");
            }
            next(4);return 0;
        }
        if(phase==4) {
            if(!exists("host-"+marker("load-released.txt"))) return 0;
            if(host && (maps->mMapChangeData.mbActive || !mp->msPendingHostMap.empty())) return 0;
            if(!mp->IsReady() || mp->GetLoadPhase()!=eLuxMultiplayerLoadPhase_None || !originalMap->GetWorld()->IsActive()) return 0;
            if(!maps->GetViewport()->IsVisible() || !maps->GetViewport()->IsActive())
                return fail(error,"cancelled loading did not restore the gameplay viewport");
            if(!host && (gpBase->mpEngine->GetUpdater()->GetCurrentContainerName()!="Default" ||
               gpBase->mpInputHandler->GetState()!=eLuxInputState_Game)) return fail(error,"cancelled loading did not restore gameplay input");
            mark(role+"-"+marker("passed.txt"),"missing-map cancellation restored the original ready world");next(5);return 0;
        }
        if(phase==5) {
            if(!both(marker("passed.txt"))) return 0;
            if(trial==3) return 1;
            ++trial;next(0);return 0;
        }
        return 0;
    }
};

#endif
