#include "LuxMultiplayerCache.h"
#include "LuxMultiplayerContent.h"
#include "LuxMultiplayerMapHash.h"
#include "LuxMultiplayerProtocol.h"
#include "system/Platform.h"
#include "system/String.h"

#include <cstdio>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace hpl;

namespace {
enum PathKind { Missing, RegularFile, Directory, Other };

PathKind Kind(const tWString& path) {
#ifdef _WIN32
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        const DWORD error=GetLastError();
        return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND ? Missing : Other;
    }
    if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) return Other;
    return attributes&FILE_ATTRIBUTE_DIRECTORY ? Directory : RegularFile;
#else
    struct stat info;
    if(lstat(cString::To8Char(path).c_str(),&info)!=0) return Missing;
    if(S_ISDIR(info.st_mode)) return Directory;
    return S_ISREG(info.st_mode) ? RegularFile : Other;
#endif
}

bool RecognizedObjectName(const tWString& filename) {
    return filename.size()==68 && filename.substr(64)==_W(".map") &&
        luxnet::ValidMapHash(cString::To8Char(filename.substr(0,64)));
}

bool RegularFileSize(const tWString& path,uint64_t& bytes) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info) ||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))) return false;
    bytes=(uint64_t(info.nFileSizeHigh)<<32)|uint64_t(info.nFileSizeLow);
#else
    struct stat info;
    if(lstat(cString::To8Char(path).c_str(),&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<0) return false;
    bytes=static_cast<uint64_t>(info.st_size);
#endif
    return true;
}

tWString NormalizeDirectory(tWString path) {
    path=cString::ReplaceCharToW(path,_W("\\"),_W("/"));
    while(path.size()>1 && path.back()==_W('/')) path.pop_back();
    // Keep drive roots absolute when walking parents on Windows.
    if(path.size()==2 && path[1]==_W(':')) path+=_W('/');
    return path;
}

bool EnsureDirectory(tWString path) {
    path=NormalizeDirectory(path);
    if(path.empty()) return false;
    const PathKind kind=Kind(path);
    if(kind==Directory) return true;
    if(kind!=Missing) return false;
    // GetFilePathW treats names without a period as directories. Derive the
    // parent explicitly so this works for a completely fresh OS cache tree.
    const size_t separator=path.find_last_of(_W('/'));
    if(separator==tWString::npos) return false;
    const tWString parent=path.substr(0,separator+1);
    if(parent==path || !EnsureDirectory(parent)) return false;
    return cPlatform::CreateFolder(path) || Kind(path)==Directory;
}

tWString ObjectsDirectory(const tWString& root,bool create) {
    if(root.empty()) return _W("");
    const tWString normalizedRoot=NormalizeDirectory(root);
    const PathKind rootKind=Kind(normalizedRoot);
    if(rootKind!=Missing && rootKind!=Directory) return _W("");
    const tWString objects=normalizedRoot+_W("/objects");
    if(create) {
        if(!EnsureDirectory(objects)) return _W("");
    } else if(rootKind!=Directory || Kind(objects)!=Directory) return _W("");
    return objects+_W('/');
}

bool RemoveFile(const tWString& path) {
#ifdef _WIN32
    return _wremove(path.c_str())==0;
#else
    return unlink(cString::To8Char(path).c_str())==0;
#endif
}

void RemoveEmptyDirectory(const tWString& path) {
#ifdef _WIN32
    _wrmdir(path.c_str());
#else
    rmdir(cString::To8Char(path).c_str());
#endif
}

bool RenameFile(const tWString& source,const tWString& destination) {
#ifdef _WIN32
    return _wrename(source.c_str(),destination.c_str())==0;
#else
    return std::rename(cString::To8Char(source).c_str(),cString::To8Char(destination).c_str())==0;
#endif
}

struct TemporaryMap {
    tWString directory,file;
    ~TemporaryMap() {
        if(!file.empty()) RemoveFile(file);
        if(!directory.empty()) RemoveEmptyDirectory(directory);
    }
};
}

tWString LuxMultiplayerCacheRoot() {
    tWString systemCache=cPlatform::GetSystemSpecialPath(eSystemPath_Cache);
    if(systemCache.empty()) return _W("");
    systemCache=NormalizeDirectory(systemCache);
    if(systemCache.back()!=_W('/')) systemCache+=_W('/');
    return systemCache+_W("HPL2/Amnesia/MultiplayerCache/");
}

bool LuxReadCachedMultiplayerMap(const tString& hash,uint32_t expectedSize,std::vector<uint8_t>& bytes,const tWString& root) {
    bytes.clear();
    if(!luxnet::ValidMapHash(hash) || !expectedSize || expectedSize>luxnet::MaxMapBytes) return false;
    const tWString directory=ObjectsDirectory(root,false);
    if(directory.empty()) return false;
    const tWString path=directory+cString::To16Char(hash)+_W(".map");
    if(Kind(path)!=RegularFile) return false;
    std::vector<uint8_t> candidate;
    if(!LuxReadMultiplayerMap(path,candidate) || candidate.size()!=expectedSize || luxnet::MapHash(candidate)!=hash) return false;
    bytes.swap(candidate);
    return true;
}

bool LuxStoreCachedMultiplayerMap(const tString& hash,const std::vector<uint8_t>& bytes,const tWString& root) {
    if(!luxnet::ValidMapHash(hash) || bytes.empty() || bytes.size()>luxnet::MaxMapBytes || luxnet::MapHash(bytes)!=hash) return false;
    std::vector<uint8_t> existing;
    if(LuxReadCachedMultiplayerMap(hash,static_cast<uint32_t>(bytes.size()),existing,root)) return true;
    const tWString directory=ObjectsDirectory(root,true);
    if(directory.empty()) return false;
    const tWString destination=directory+cString::To16Char(hash)+_W(".map");
    const PathKind destinationKind=Kind(destination);
    if(destinationKind!=Missing && destinationKind!=RegularFile) return false;

    TemporaryMap temporary;
    const tWString prefix=directory+_W(".write_")+cString::To16Char(hash.substr(0,12))+_W('_')+
        cString::To16Char(cString::ToString(cPlatform::GetApplicationTime()))+_W('_');
    for(unsigned attempt=0;attempt<100;++attempt) {
        const tWString candidate=prefix+cString::To16Char(cString::ToString(attempt));
        if(cPlatform::CreateFolder(candidate)) {temporary.directory=candidate;break;}
    }
    if(temporary.directory.empty()) return false;
    temporary.file=temporary.directory+_W("/map.tmp");
    FILE* file=cPlatform::OpenFile(temporary.file,_W("wb"));
    if(!file) return false;
    bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
    if(std::fflush(file)!=0) written=false;
    if(std::fclose(file)!=0) written=false;
    if(!written) return false;

    // Another instance may have completed this same verified object while we
    // wrote the temporary file. Keep an already valid entry whenever observed.
    if(LuxReadCachedMultiplayerMap(hash,static_cast<uint32_t>(bytes.size()),existing,root)) return true;
    if(Kind(destination)==RegularFile) {
        if(!RemoveFile(destination)) return false;
    } else if(Kind(destination)!=Missing) return false;
    if(RenameFile(temporary.file,destination)) return true;
    // Windows refuses to rename over a concurrently published destination.
    // POSIX may atomically replace that entry with our byte-identical content.
    return LuxReadCachedMultiplayerMap(hash,static_cast<uint32_t>(bytes.size()),existing,root);
}

cLuxMultiplayerMapCacheStats LuxGetMultiplayerMapCacheStats(const tWString& root) {
    cLuxMultiplayerMapCacheStats result;
    const tWString directory=ObjectsDirectory(root,false);
    if(directory.empty()) return result;
    tWStringList files;
    cPlatform::FindFilesInDir(files,directory,_W("*.map"),true);
    for(const tWString& filename:files) {
        if(!RecognizedObjectName(filename)) continue;
        uint64_t bytes=0;
        if(RegularFileSize(directory+filename,bytes)) {result.bytes+=bytes;++result.maps;}
    }
    return result;
}

unsigned LuxClearMultiplayerMapCache(const tWString& root) {
    const tWString directory=ObjectsDirectory(root,false);
    if(directory.empty()) return 0;
    tWStringList files;
    cPlatform::FindFilesInDir(files,directory,_W("*.map"),true);
    unsigned removed=0;
    for(const tWString& filename:files) {
        if(!RecognizedObjectName(filename)) continue;
        const tWString path=directory+filename;
        if(Kind(path)==RegularFile && RemoveFile(path)) ++removed;
    }
    return removed;
}
