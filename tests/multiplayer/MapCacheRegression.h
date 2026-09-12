#ifndef CODEX_MAP_CACHE_REGRESSION_H
#define CODEX_MAP_CACHE_REGRESSION_H

#include "LuxMultiplayerCache.h"
#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerMapHash.h"
#include "LuxMultiplayerProtocol.h"

class cMapCacheRegression {
    unsigned trial=0,phase=0;
    Uint32 entered=0;
    uint32_t targetEpoch=0,expectedSize=0;
    tString fixtureName,fixturePath,expectedHash,firstHash;
    std::vector<uint8_t> originalMap;
    std::set<tString> ownedHashes;
    tWString previousWorkingMap;
    bool cleaned=false;
    unsigned downloadSamplesBefore=0,progressFramesBefore=0;

    static void armMapLifecycle() {
        cLuxPlayer* player=gpBase->mpPlayer;
        player->SetActive(false);
        player->SetJumpDisabled(true);
        player->SetScriptMoveSpeedMul(0.35f);
        player->SetScriptRunSpeedMul(0.45f);
        player->SetScriptJumpForceMul(0.25f);
        player->SetLookSpeedMul(0.25f);
        player->SetTinderboxes(13);
        player->GetCamera()->SetRoll(0.3f);
        // Keep an active flashback before its audio starts. This isolates its
        // native lifetime policy from recording duration and download timing.
        cLuxPlayerFlashback* flashback=player->GetHelperFlashback();
        flashback->mbActive=true;flashback->mfFlashDelay=0;
        flashback->mfFlashbackStartCount=1000;
        cLuxEffectHandler* effects=gpBase->mpEffectHandler;
        effects->GetFade()->FadeOut(0);
        effects->GetImageTrail()->FadeTo(0.7f,100);effects->GetImageTrail()->Update(1);
        effects->GetSepiaColor()->FadeTo(0.8f,100);effects->GetSepiaColor()->Update(1);
        effects->GetRadialBlur()->FadeTo(0.6f,100);effects->GetRadialBlur()->Update(1);
    }
    bool checkMapLifecycle(tString& error) const {
        cLuxPlayer* player=gpBase->mpPlayer;
        cLuxEffectHandler* effects=gpBase->mpEffectHandler;
        const bool normal=trial==0;
        if(!player->IsActive() || std::fabs(player->GetCamera()->GetRoll())>0.001f ||
           effects->GetFade()->mfGoalAlpha!=0 || effects->GetSepiaColor()->mfAmountGoal!=0 ||
           effects->GetRadialBlur()->mfSizeGoal!=0) {
            error="map transition retained disabled player, camera roll, fade-out or map-local post effects";return false;
        }
        if(player->GetScriptJumpForceMul()!=1 || player->GetLookSpeedMul()!=1) {
            error="map transition lost the host's script cleanup sent after preparation";return false;
        }
        if(player->GetJumpDisabled()!=normal || player->GetTinderboxes()!=(normal?13:0) ||
           std::fabs(player->GetScriptMoveSpeedMul()-(normal?0.35f:1.0f))>0.001f ||
           std::fabs(player->GetScriptRunSpeedMul()-(normal?0.45f:1.0f))>0.001f ||
           player->GetHelperFlashback()->IsActive()!=normal ||
           gpBase->mpMapHandler->GetPostEffect_ImageTrail()->IsActive()!=normal) {
            error=normal?"ordinary map change cleared native persistent player/flashback/image-trail state":
                "debug map restart retained player/flashback/image-trail state that native StartGame resets";return false;
        }
        return true;
    }

    tString marker(const char* suffix) const {
        return "map-cache-"+cString::ToString(static_cast<int>(trial))+"-"+suffix;
    }
    static bool both(const tString& suffix) {
        return exists("host-"+suffix) && exists("client-"+suffix);
    }
    void next(unsigned value) {phase=value;entered=SDL_GetTicks();}
    int fail(tString& error,const char* message) const {
        error="map cache trial "+cString::ToString(static_cast<int>(trial))+" phase "+
            cString::ToString(static_cast<int>(phase))+": "+message;return -1;
    }
    static tWString normalized(const tWString& path) {
        std::error_code error;
        const auto result=std::filesystem::weakly_canonical(std::filesystem::path(path),error);
        if(error) return _W("");
        tWString pathString=cString::ToLowerCaseW(result.generic_wstring());
        while(pathString.size()>3 && pathString.back()==_W('/')) pathString.pop_back();
        return pathString;
    }
    static tWString objectPath(const tString& hash) {
        return LuxMultiplayerCacheRoot()+_W("objects/")+cString::To16Char(hash)+_W(".map");
    }
    static bool writeBytes(const tWString& path,const std::vector<uint8_t>& bytes) {
        FILE* file=cPlatform::OpenFile(path,_W("wb"));
        if(!file) return false;
        bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
        if(std::fclose(file)!=0) written=false;
        return written;
    }
    bool workingCopyIsValid(cLuxMultiplayer* mp) const {
        const tWString path=cString::To16Char(mp->msReceivedMapPath);
        if(path.empty() || !cPlatform::FileExists(path) || mp->msLoadedMapPath!=mp->msReceivedMapPath ||
           !mp->msExistingMapPath.empty()) return false;
        const auto parent=std::filesystem::path(path).parent_path();
        const tWString root=normalized(LuxMultiplayerCacheRoot());
        const tWString profile=normalized(gpBase->msBaseSavePath);
        const tWString actual=normalized(path);
        if(root.empty() || profile.empty() || actual.empty() ||
           normalized(parent.parent_path().wstring())!=root ||
           actual.compare(0,profile.size()+1,profile+_W("/"))==0) return false;
        if(!previousWorkingMap.empty() &&
           (actual==normalized(previousWorkingMap) || cPlatform::FileExists(previousWorkingMap) ||
            cPlatform::FolderExists(std::filesystem::path(previousWorkingMap).parent_path().wstring()))) return false;
        std::vector<uint8_t> bytes;
        return LuxReadMultiplayerMap(path,bytes) && bytes.size()==expectedSize && luxnet::MapHash(bytes)==expectedHash;
    }
public:
    const tWString& CurrentWorkingMap() const {return previousWorkingMap;}
    // Call only after both test sessions have stopped. Hashes contain this
    // run's unique XML comment; no retail/shared cache clear operation is used.
    bool Cleanup(tString& error) {
        if(cleaned || role!="client") return true;
        if(gpBase->mpMultiplayer->IsActive()) {error="cache fixture cleanup attempted before disconnect";return false;}
        for(const auto& hash:ownedHashes) {
            const tWString path=objectPath(hash);
            cPlatform::RemoveFile(path);
            if(cPlatform::FileExists(path)) {error="could not remove this run's persistent cache fixture";return false;}
        }
        cleaned=true;return true;
    }
    int Update(cLoadingPhaseObserver& observer,tString& error) {
        cLuxMultiplayer* mp=gpBase->mpMultiplayer;
        if(!mp->IsActive()) return fail(error,"session stopped");
        if(entered && SDL_GetTicks()-entered>18000) return fail(error,"map negotiation/transition timed out");
        const bool host=role=="host";
        if(fixtureName.empty()) {
            const tString runId=std::filesystem::path(outputDir).filename().string();
            fixtureName="codex_cache_"+runId+".map";
            fixturePath=outputDir+"/"+fixtureName;
            entered=SDL_GetTicks();
        }
        if(phase==0) {
            armMapLifecycle();
            if(host) {
                if(trial==0) {
                    cLuxMap* map=gpBase->mpMapHandler->GetCurrentMap();
                    if(!map || !LuxReadMultiplayerMap(map->GetWorld()->GetFilePath(),originalMap))
                        return fail(error,"could not read the loaded Old Archives XML");
                    if(cPlatform::FileExists(cString::To16Char(fixturePath)))
                        return fail(error,"refusing to overwrite an existing fixture map");
                }
                if(trial==0 || trial==3) {
                    std::vector<uint8_t> bytes=originalMap;
                    const tString comment="\n<!-- codex multiplayer map cache "+fixtureName+
                        " revision "+cString::ToString(trial==0?0:1)+" -->\n";
                    bytes.insert(bytes.end(),comment.begin(),comment.end());
                    expectedHash=luxnet::MapHash(bytes);
                    expectedSize=static_cast<uint32_t>(bytes.size());
                    if(cPlatform::FileExists(objectPath(expectedHash)))
                        return fail(error,"unique fixture hash already exists in persistent cache");
                    if(!writeBytes(cString::To16Char(fixturePath),bytes))
                        return fail(error,"could not write the host-only fixture XML");
                    if(trial==0) firstHash=expectedHash;
                    else if(expectedHash==firstHash) return fail(error,"host edit did not change map hash");
                }
                targetEpoch=mp->GetMapEpoch()+1;
                mark("host-"+marker("prepared.txt"),cString::ToString(targetEpoch)+" "+
                    cString::ToString(expectedSize)+" "+expectedHash);
            } else {
                FILE* manifest=cPlatform::OpenFile(cString::To16Char(outputDir+"/host-"+marker("prepared.txt")),_W("rb"));
                if(!manifest) return 0;
                unsigned epoch=0,size=0;char hash[65]={};
                const bool complete=std::fscanf(manifest,"%u %u %64s",&epoch,&size,hash)==3;
                std::fclose(manifest);
                if(!complete) return 0;
                downloadSamplesBefore=observer.samples[eLuxMultiplayerLoadPhase_Downloading];
                progressFramesBefore=observer.progressFrames;
                targetEpoch=epoch;expectedSize=size;expectedHash=hash;
                if(!luxnet::ValidMapHash(expectedHash) || !expectedSize || expectedSize>luxnet::MaxMapBytes ||
                   targetEpoch!=mp->GetMapEpoch()+1) return fail(error,"invalid fixture manifest");
                if(trial==0 || trial==3) {
                    if(cPlatform::FileExists(objectPath(expectedHash)))
                        return fail(error,"cold fixture unexpectedly has a persistent cache entry");
                    if(trial==0) firstHash=expectedHash;
                    else if(expectedHash==firstHash) return fail(error,"same-name host edit kept old map hash");
                    ownedHashes.insert(expectedHash);
                } else if(expectedHash!=firstHash) return fail(error,"unchanged host map acquired another hash");
                if(trial==2) {
                    // Modify only the exact entry produced by this run. Keep
                    // its size unchanged so integrity, not size, rejects it.
                    std::vector<uint8_t> bytes;
                    if(!LuxReadCachedMultiplayerMap(expectedHash,expectedSize,bytes) || bytes.size()<8)
                        return fail(error,"cannot read this run's cache entry before corruption");
                    bytes[bytes.size()-6]^=1;
                    if(!writeBytes(objectPath(expectedHash),bytes)) return fail(error,"could not corrupt fixture cache entry");
                    if(LuxReadCachedMultiplayerMap(expectedHash,expectedSize,bytes))
                        return fail(error,"same-size corrupt cache passed hash verification");
                }
                mark("client-"+marker("armed.txt"),"fixture prepared");
            }
            next(1);return 0;
        }
        if(phase==1) {
            if(host) {
                if(!exists("client-"+marker("armed.txt"))) return 0;
                if(trial==0) {
                    // Cover the native level-door path as well as the debug
                    // restarts used by the remaining cache-reload trials.
                    // Native map loads resolve through the host's resource
                    // index, as retail map folders already do at startup.
                    gpBase->mpEngine->GetResources()->AddResourceDir(cString::To16Char(outputDir),false);
                    gpBase->mpMapHandler->SetMapFolder(outputDir+"/");
                    gpBase->mpMapHandler->ChangeMap(fixtureName,"PlayerStartArea_1","","");
                    gpBase->mpMapHandler->GetCurrentMap()->RunScript(
                        "SetPlayerJumpForceMul(1);SetPlayerLookSpeedMul(1);");
                } else if(!mp->HostChangeMap(fixturePath,"PlayerStartArea_1")) return fail(error,"host map change refused");
            }
            next(2);return 0;
        }
        if(phase==2) {
            if(mp->GetMapEpoch()!=targetEpoch || !mp->IsReady() || mp->msMapName!=fixtureName ||
               mp->GetWorld()->GetRemotePlayers().empty()) return 0;
            if(host) {
                if(mp->mPeers.empty() || !mp->mPeers.begin()->second.ready) return 0;
            } else {
                bool received=false;
                for(const auto& body:mp->GetWorld()->mBodies) if(body.second.received) {received=true;break;}
                if(!received) return 0;
            }
            if(mp->msMapHash!=expectedHash || mp->mvMapBytes.size()!=expectedSize)
                return fail(error,"loaded map differs from host fixture bytes");
            if(mp->mbMapResetsGame!=(trial!=0)) return fail(error,"map manifest changed native transition semantics");
            if(!checkMapLifecycle(error)) return -1;
            if(!host) {
                const bool shouldReuse=trial==1;
                if(mp->mbReusingMap!=shouldReuse || mp->mlDownloadedMapBytes!=(shouldReuse?0:expectedSize))
                    return fail(error,"unexpected cache reuse or XML download byte count");
                if(mp->GetLoadPhase()!=eLuxMultiplayerLoadPhase_None)
                    return fail(error,"completed map retained a loading phase");
                if(shouldReuse) {
                    if(observer.samples[eLuxMultiplayerLoadPhase_Downloading]!=downloadSamplesBefore)
                        return fail(error,"cached map displayed a download phase");
                } else if(observer.samples[eLuxMultiplayerLoadPhase_Downloading]<=downloadSamplesBefore ||
                          observer.progressFrames<=progressFramesBefore) {
                    return fail(error,"cold map did not render an intermediate download progress screen");
                }
                if(!workingCopyIsValid(mp)) return fail(error,"working copy placement/content or prior-copy cleanup failed");
                std::vector<uint8_t> persistent;
                if(!LuxReadCachedMultiplayerMap(expectedHash,expectedSize,persistent) ||
                   luxnet::MapHash(persistent)!=expectedHash)
                    return fail(error,"download/cache reuse did not retain or repair the persistent object");
                previousWorkingMap=cString::To16Char(mp->msReceivedMapPath);
                std::printf("client map-cache trial=%u reused=%d downloaded=%u hash=%s\n",trial,
                    int(mp->mbReusingMap),mp->mlDownloadedMapBytes,mp->msMapHash.c_str());
                std::fflush(stdout);
            } else if(!mp->msReceivedMapPath.empty()) return fail(error,"host owns a downloaded working copy");
            mark(role+"-"+marker("passed.txt"),"map source, byte counts, content hash and cleanup verified");
            next(3);return 0;
        }
        if(phase==3) {
            if(!both(marker("passed.txt"))) return 0;
            if(trial==3) return 1;
            ++trial;next(0);return 0;
        }
        return 0;
    }
};

#endif
