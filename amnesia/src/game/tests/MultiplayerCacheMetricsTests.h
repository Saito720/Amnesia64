#ifndef MULTIPLAYER_CACHE_METRICS_TESTS_H
#define MULTIPLAYER_CACHE_METRICS_TESTS_H

// Include after Require()/Bytes() in multiplayer_content_tests.cpp. All
// metadata and deletion trials use this run's reserved workspace directory.
static void TestMapCacheMetrics(const tWString& logPath) {
    const auto parent=std::filesystem::path(logPath).parent_path();
    tWString root;
    for(unsigned attempt=0;attempt<100;++attempt) {
        const tWString name=_W("cache-metrics_")+cString::To16Char(cString::ToString(cPlatform::GetApplicationTime()))+
            _W('_')+cString::To16Char(cString::ToString(attempt));
        const tWString candidate=(parent/name).wstring();
        if(cPlatform::CreateFolder(candidate)) {root=candidate;break;}
    }
    Require(!root.empty(),"reserve isolated cache metrics directory");
    const tWString absent=root+_W("/absent");
    auto stats=LuxGetMultiplayerMapCacheStats(absent);
    Require(stats.maps==0 && stats.bytes==0 && !cPlatform::FolderExists(absent),
        "querying missing cache reports zero without creating directories");

    const auto first=Bytes("<Level>metrics first</Level>"),second=Bytes("<Level>metrics second</Level>");
    const tString firstHash=luxnet::MapHash(first),secondHash=luxnet::MapHash(second);
    Require(LuxStoreCachedMultiplayerMap(firstHash,first,root) && LuxStoreCachedMultiplayerMap(secondHash,second,root),
        "create two recognized stored maps for metrics");
    stats=LuxGetMultiplayerMapCacheStats(root);
    Require(stats.maps==2 && stats.bytes==first.size()+second.size(),"metrics sum actual stored XML bytes and count");

    const tWString objects=root+_W("/objects"),active=root+_W("/123_1");
    const tWString staging=objects+_W("/.write_fixture");
    const tWString firstPath=objects+_W('/')+cString::To16Char(firstHash)+_W(".map");
    const tWString zeroPath=objects+_W('/')+tWString(64,_W('0'))+_W(".map");
    const tWString directoryObject=objects+_W('/')+tWString(64,_W('1'))+_W(".map");
    const tWString unknown=objects+_W("/unrelated.map"),uppercase=objects+_W('/')+tWString(64,_W('a'))+_W(".MAP");
    const auto write=[](const tWString& path,const std::vector<uint8_t>& bytes) {
        FILE* file=cPlatform::OpenFile(path,_W("wb"));Require(file!=NULL,"open isolated metrics fixture");
        const bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
        const bool closed=std::fclose(file)==0;Require(written && closed,"write isolated metrics fixture");
    };
    Require(cPlatform::CreateFolder(active) && cPlatform::CreateFolder(staging) && cPlatform::CreateFolder(directoryObject),
        "create active, incomplete-store, and non-file metrics fixtures");
    write(active+_W("/active.map"),first);write(staging+_W('/')+cString::To16Char(firstHash)+_W(".map"),first);
    write(unknown,first);write(uppercase,first);write(zeroPath,{});
    // Corrupt entries still consume disk space and Delete downloaded maps can
    // remove them, so the size must include their actual bytes without hashing.
    const auto corrupt=Bytes("corrupt");write(firstPath,corrupt);
    const auto firstTimestamp=std::filesystem::last_write_time(std::filesystem::path(firstPath));
    stats=LuxGetMultiplayerMapCacheStats(root);
    Require(stats.maps==3 && stats.bytes==corrupt.size()+second.size(),
        "metrics include corrupt/empty recognized objects but exclude private, staged, unknown and non-file entries");
    Require(std::filesystem::last_write_time(std::filesystem::path(firstPath))==firstTimestamp,
        "metrics do not rewrite or repair cached maps");

    const tWString symlink=objects+_W('/')+tWString(64,_W('2'))+_W(".map");
    std::error_code linkError;
    std::filesystem::create_symlink(std::filesystem::path(unknown),std::filesystem::path(symlink),linkError);
    if(!linkError) {
        const auto linkedStats=LuxGetMultiplayerMapCacheStats(root);
        Require(linkedStats.maps==stats.maps && linkedStats.bytes==stats.bytes,"cache metrics never follow symbolic links");
    }
    Require(LuxClearMultiplayerMapCache(root)==stats.maps,"cache size and deletion cover exactly the same stored objects");
    stats=LuxGetMultiplayerMapCacheStats(root);
    Require(stats.maps==0 && stats.bytes==0,"cache metrics become empty after scoped deletion");
    Require(cPlatform::FileExists(unknown) && cPlatform::FileExists(uppercase) && cPlatform::FileExists(active+_W("/active.map")),
        "metrics/deletion leave unrelated and active files intact");
    if(!linkError) std::filesystem::remove(std::filesystem::path(symlink));
    cPlatform::RemoveFile(active+_W("/active.map"));
    cPlatform::RemoveFile(staging+_W('/')+cString::To16Char(firstHash)+_W(".map"));
    cPlatform::RemoveFile(unknown);cPlatform::RemoveFile(uppercase);
    Require(cPlatform::RemoveFolder(active,false,false) && cPlatform::RemoveFolder(staging,false,false) &&
        cPlatform::RemoveFolder(directoryObject,false,false) && cPlatform::RemoveFolder(objects,false,false) &&
        cPlatform::RemoveFolder(root,false,false),"clean exact metrics fixtures and empty directories");
    std::puts("PASS: cache size/count scope, metadata-only reads, missing-cache queries, corruption and scoped deletion");
}

#endif
