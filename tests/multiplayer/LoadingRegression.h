#ifndef CODEX_LOADING_REGRESSION_H
#define CODEX_LOADING_REGRESSION_H

class cLoadingPhaseObserver {
    uint32_t progressEpoch=0,lastProgress=0;
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
        if(role!="client" || !mp->IsActive()) return true;
        const int phase=int(mp->GetLoadPhase());
        if(phase<0 || phase>=6) {error="invalid multiplayer loading phase";return false;}
        ++samples[phase];
        if(phase==eLuxMultiplayerLoadPhase_None) return true;
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
