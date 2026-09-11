#ifndef MULTIPLAYER_CACHE_PATH_TESTS_H
#define MULTIPLAYER_CACHE_PATH_TESTS_H

// Include after Require()/Bytes() in multiplayer_content_tests.cpp. These
// filesystem trials stay under the test log directory, outside the OS cache.
static void TestMapCachePaths(const tWString& logPath) {
    Require(cPlatform::GetFullFilePath(_W("")).empty(),"empty full-path query fails without returning uninitialized bytes");
#ifdef _WIN32
    Require(cPlatform::GetFullFilePath(_W("C:/")+tWString(4096,_W('x'))).empty(),
        "oversized Windows full-path result is rejected rather than truncated");
#endif
    const auto parent=std::filesystem::path(logPath).parent_path();
    std::filesystem::path root;
    for(unsigned attempt=0;attempt<100;++attempt) {
        const auto candidate=parent/(_W("cache-paths_")+cString::To16Char(cString::ToString(cPlatform::GetApplicationTime()))+
            _W('_')+cString::To16Char(cString::ToString(attempt)));
        if(cPlatform::CreateFolder(candidate.wstring())) {root=candidate;break;}
    }
    Require(!root.empty(),"reserve isolated cache-path directory");
    const auto nested=root/"nested";
    Require(cPlatform::CreateFolder(nested.wstring()),"create cache-path parent fixture");
    const auto expected=cPlatform::GetFullFilePath(nested.wstring());
    Require(!expected.empty() && cPlatform::GetFullFilePath((nested/".."/"nested").wstring())==expected,
        "directory full-path query resolves parent components consistently");
#ifndef _WIN32
    Require(cPlatform::GetFullFilePath((root/"absent.map").wstring()).empty(),
        "failed Unix realpath returns an empty result");
    // An XDG_CACHE_HOME symlink has this same parent-link shape. Resolve the
    // directory and its child through the platform API used by map matching.
    const auto alias=root/"alias";
    std::error_code linkError;
    std::filesystem::create_directory_symlink(nested,alias,linkError);
    Require(!linkError,"create isolated Unix cache-parent symlink");
    const auto child=nested/"HPL2";
    Require(cPlatform::CreateFolder(child.wstring()),"create child below symlinked cache parent");
    Require(cPlatform::GetFullFilePath(alias.wstring())==cPlatform::GetFullFilePath(nested.wstring()) &&
        cPlatform::GetFullFilePath((alias/"HPL2").wstring())==cPlatform::GetFullFilePath(child.wstring()),
        "cache-root and child aliases resolve to the same physical paths");
    Require(std::filesystem::remove(alias) && std::filesystem::remove(child),"remove exact cache-alias fixtures");
#endif
    Require(cPlatform::RemoveFolder(nested.wstring(),false,false) && cPlatform::RemoveFolder(root.wstring(),false,false),
        "remove empty isolated cache-path directories");
    std::puts("PASS: platform full-path failure handling and isolated cache-path normalization");
}

#endif
