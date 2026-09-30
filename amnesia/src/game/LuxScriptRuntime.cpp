#include "LuxScriptRuntime.h"
#include "LuxScriptExecution.h"
#include "LuxScriptPlayerState.h"
#include "LuxMultiplayer.h"
#include "LuxMultiplayerWorld.h"
#include "LuxMultiplayerProtocol.h"
#include "LuxMapHandler.h"
#include "impl/SqScript.h"
#include "impl/scriptstring.h"
#include "system/Platform.h"
#include "system/String.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

namespace {
const size_t MaxPackageBytes=256*1024, MaxSourceBytes=96*1024;
const unsigned MaxLineCallbacks=cLuxScriptRuntime::ExecutionLineBudget, MaxTimers=256, MaxTimerCallbacks=64;

tString Trim(const tString& s) {
    const size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");
    return a==tString::npos?tString():s.substr(a,b-a+1);
}
bool ReadSource(const tString& path,tString& source,tString& error,size_t limit) {
    FILE* file=cPlatform::OpenFile(cString::To16Char(path),_W("rb"));
    if(!file) {error="Cannot read script package file: "+path;return false;}
    if(fseek(file,0,SEEK_END)) {fclose(file);error="Cannot size script package file.";return false;}
    const long size=ftell(file);
    if(size<0 || size>static_cast<long>(limit) || fseek(file,0,SEEK_SET)) {
        fclose(file);error="Script package file exceeds its size limit: "+path;return false;
    }
    source.resize(size);
    const bool ok=source.empty() || fread(&source[0],1,source.size(),file)==source.size();
    fclose(file);
    if(!ok || source.find('\0')!=tString::npos) {error="Invalid script package text: "+path;return false;}
    if(source.compare(0,3,"\xEF\xBB\xBF")==0) source.erase(0,3);
    return true;
}
bool ReadManifest(const tString& path,tString& error) {
    tString source;if(!ReadSource(path,source,error,4096)) return false;
    std::istringstream input(source);tString line,section;bool version=false;
    while(std::getline(input,line)) {
        line=Trim(line);if(line.empty() || line[0]==';' || line[0]=='#') continue;
        if(line.front()=='[' && line.back()==']') {section=line.substr(1,line.size()-2);continue;}
        const size_t eq=line.find('=');
        if(section!="Scripting" || eq==tString::npos || Trim(line.substr(0,eq))!="Version" ||
           Trim(line.substr(eq+1))!="2" || version) {
            error="Expected [Scripting] and exactly one Version=2 in "+path;return false;
        }
        version=true;
    }
    if(!version) error="Missing [Scripting] Version=2 in "+path;
    return version;
}
tString FunctionName(const tString& declaration) {
    const size_t end=declaration.find('(');if(end==tString::npos) return "";
    size_t start=end;while(start && ((declaration[start-1]>='A' && declaration[start-1]<='Z') ||
        (declaration[start-1]>='a' && declaration[start-1]<='z') ||
        (declaration[start-1]>='0' && declaration[start-1]<='9') || declaration[start-1]=='_')) --start;
    return declaration.substr(start,end-start);
}
tString Declaration(const tString& name,size_t strings,bool step=false) {
    tString result="void "+name+"(";
    if(step) result+="float";
    else for(size_t i=0;i<strings;++i) {if(i) result+=",";result+="string &in";}
    return result+")";
}
bool HookHandle(iScript* script,const tString& hook,const tString& prefix,bool step,int& handle,tString& error,const char* argumentType=NULL) {
    handle=-1;if(!script) return true;
    const tString alias=prefix+hook;
    const bool plain=script->HasFunctionNamed(hook),prefixed=script->HasFunctionNamed(alias);
    if(plain && prefixed) {error="Ambiguous lifecycle aliases: "+hook+" and "+alias;return false;}
    if(!plain && !prefixed) return true;
    const tString declaration=argumentType?"void "+(prefixed?alias:hook)+"("+argumentType+")":Declaration(prefixed?alias:hook,0,step);
    handle=script->GetFuncHandleByDecl(declaration);
    if(handle<0) {error="Lifecycle hook must have declaration: "+declaration;return false;}
    return true;
}
LuxScriptExecutionContext ClientContext(const tString& module) {
    LuxScriptExecutionContext context;context.revised=true;context.domain=LuxScriptDomain::Client;
    context.hasPlayer=true;context.module=module;
    cLuxMultiplayer* session=gpBase->mpMultiplayer;
    const bool active=session && session->IsActive();
    context.player=active?session->GetLocalPeerId():0;
    context.mapEpoch=active?session->GetMapEpoch():0;
    context.session=active?session->GetSessionSerial():0;
    context.event=LuxNextScriptEventId();
    return context;
}
}

struct cLuxScriptRuntime::Impl {
    struct Native {tString declaration;void* function;};
    struct Module {
        tString source;
        std::unique_ptr<cSqScript> script;
        bool initialized=false,started=false;
    };
    struct Timer {tString module,name,function;float remaining;uint64_t serial;};
    struct MapChange {
        std::map<tString,std::shared_ptr<Module> > modules;
        std::vector<Timer> timers;
        std::set<std::pair<iScript*,tString> > failedHooks;
        cLuxScriptPlayerState playerState;
        tString loadedMapFile;
        bool revised=false,ready=false,clientDisabled=false,playerStateMapLoaded=false;
        tString clientFailure;
        uint64_t playerStateMapSession=0;
        uint32_t playerStateMapEpoch=0;
    };
    std::vector<Native> natives;
    asIScriptEngine* engine=NULL;
    cScriptOutput output;
    std::map<tString,std::shared_ptr<Module> > modules;
    std::unique_ptr<MapChange> mapChange;
    std::vector<Timer> timers;
    std::set<std::pair<iScript*,tString> > failedHooks;
    cLuxScriptPlayerState playerState;
    bool playerStateMapLoaded=false;
    uint64_t playerStateMapSession=0;
    uint32_t playerStateMapEpoch=0;
    bool revised=false,ready=false,clientDisabled=false;
    tString clientFailure;
    tString loadedMapFile;
    int nextModule=0;
    uint64_t nextTimer=0;
    ~Impl() {mapChange.reset();modules.clear();if(engine) engine->Release();}

    void ClearPackage() {
        timers.clear();modules.clear();ready=false;revised=false;clientDisabled=false;clientFailure.clear();
        failedHooks.clear();loadedMapFile.clear();
    }

    bool EnsureEngine(tString& error) {
        if(engine) return true;
        engine=asCreateScriptEngine(ANGELSCRIPT_VERSION);
        if(!engine) {error="Cannot create client script VM.";return false;}
        engine->SetMessageCallback(asMETHOD(cScriptOutput,AddMessage),&output,asCALL_THISCALL);
        RegisterScriptString(engine);
        for(const auto& native:natives) {
            if(engine->RegisterGlobalFunction(native.declaration.c_str(),asFUNCTION(native.function),asCALL_STDCALL)<0) {
                error="Cannot register client API: "+native.declaration;engine->Release();engine=NULL;return false;
            }
        }
        return true;
    }
    bool Hook(const tString& name,const tString& hook,tString& error,const float* step=NULL) {
        auto it=modules.find(name);if(it==modules.end()) return true;
        const auto module=it->second;
        int handle;
        if(!HookHandle(module->script.get(),hook,"Client",step!=NULL,handle,error)) return false;
        if(handle<0) return true;
        cLuxScriptExecutionScope context(ClientContext(name));
        return module->script->RunTyped(handle,std::vector<tString>(),step,&error,MaxLineCallbacks);
    }

    bool DisableClient(tString& error) {
        ready=false;clientDisabled=true;clientFailure=error;return false;
    }
};

cLuxScriptRuntime::cLuxScriptRuntime() : mpImpl(new Impl) {}
cLuxScriptRuntime::~cLuxScriptRuntime() {}

bool cLuxScriptRuntime::IsIdentifier(const tString& name) {
    if(name.empty() || name.size()>128) return false;
    for(size_t i=0;i<name.size();++i) {
        const char c=name[i];
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || c=='_' || (i && c>='0'&&c<='9'))) return false;
    }
    return true;
}
bool cLuxScriptRuntime::IsClientNativeAllowed(const tString& name) {
    // Register a positive capability list in a different engine. World,
    // inventory, AI, save, permission and authoritative player mutations are
    // intentionally absent, rather than guarded after arbitrary script calls.
    static const std::set<tString> allowed={
        "Print","AddDebugMessage","ScriptDebugOn","StringContains","StringSub",
        "MathSin","MathCos","MathTan","MathAsin","MathAcos","MathAtan","MathAtan2",
        "MathSqrt","MathPow","MathMin","MathMax","MathClamp","MathAbs",
        "StringToInt","StringToFloat","StringToBool",
        "GetScriptPlayerId","GetPublishedScriptVar","AddTimer","RemoveTimer","GetTimerTimeLeft",
        "FadeIn","FadeOut","FadeImageTrailTo","FadeSepiaColorTo","FadeRadialBlurTo",
        "SetRadialBlurStartDist","StartEffectFlash","StartScreenShake",
        "PlayGuiSound","SetMessage","GiveHint","RemoveHint","BlockHint","UnBlockHint",
        "SetInventoryMessage","FadePlayerFOVMulTo","FadePlayerAspectMulTo","FadePlayerRollTo",
        "MovePlayerHeadPos","ShowPlayerCrossHairIcons",
        "GetPlayerPosX","GetPlayerPosY","GetPlayerPosZ","GetPlayerSpeed","GetPlayerYSpeed",
        "GetPlayerHealth","GetPlayerSanity","GetPlayerLampOil","GetLanternActive",
        "GetEffectVoiceActive","GetFlashbackIsActive","InsanityEventIsActive",
        "GetEntityExists","GetEntityPosX","GetEntityPosY","GetEntityPosZ",
        "PreloadParticleSystem","PreloadSound"
    };
    return allowed.count(name)!=0;
}
void cLuxScriptRuntime::RegisterNative(const tString& declaration,void* function) {
    if(IsClientNativeAllowed(FunctionName(declaration))) mpImpl->natives.push_back({declaration,function});
}
bool cLuxScriptRuntime::IsRevised() const {return mpImpl->revised;}
bool cLuxScriptRuntime::IsClientReady() const {return mpImpl->ready;}
cLuxScriptPlayerState& cLuxScriptRuntime::GetPlayerState() {
    const cLuxMultiplayer* session=gpBase->mpMultiplayer;
    const bool active=session && session->IsActive();
    mpImpl->playerState.BeginSession(active?session->GetSessionSerial():0);
    return mpImpl->playerState;
}
void cLuxScriptRuntime::ForgetPlayer(uint64_t session,uint32_t peer) {
    mpImpl->playerState.RemovePlayer(session,peer);
    if(mpImpl->mapChange) mpImpl->mapChange->playerState.RemovePlayer(session,peer);
}
void cLuxScriptRuntime::BeginPlayerStateMap() {
    const cLuxMultiplayer* session=gpBase->mpMultiplayer;
    const bool active=session && session->IsActive();
    mpImpl->playerStateMapSession=active?session->GetSessionSerial():0;
    mpImpl->playerStateMapEpoch=active?session->GetMapEpoch():0;
    mpImpl->playerState.BeginMap(mpImpl->playerStateMapSession,mpImpl->playerStateMapEpoch);
    mpImpl->playerStateMapLoaded=true;
}
void cLuxScriptRuntime::Reset() {
    CommitMapChange();mpImpl->ClearPackage();mpImpl->playerState.Reset();mpImpl->playerStateMapLoaded=false;
}
void cLuxScriptRuntime::BeginMapChange() {
    if(mpImpl->mapChange) return;
    std::unique_ptr<Impl::MapChange> previous(new Impl::MapChange);
    previous->modules=mpImpl->modules;previous->timers=mpImpl->timers;
    previous->failedHooks=mpImpl->failedHooks;previous->playerState=mpImpl->playerState;
    previous->loadedMapFile=mpImpl->loadedMapFile;
    previous->revised=mpImpl->revised;previous->ready=mpImpl->ready;
    previous->clientDisabled=mpImpl->clientDisabled;previous->clientFailure=mpImpl->clientFailure;
    previous->playerStateMapLoaded=mpImpl->playerStateMapLoaded;
    previous->playerStateMapSession=mpImpl->playerStateMapSession;
    previous->playerStateMapEpoch=mpImpl->playerStateMapEpoch;
    mpImpl->mapChange=std::move(previous);
    if(gpBase->mpMultiplayer) gpBase->mpMultiplayer->BeginScriptMapChange();
}
void cLuxScriptRuntime::CommitMapChange() {
    if(!mpImpl->mapChange) return;
    mpImpl->mapChange.reset();
    if(gpBase->mpMultiplayer) gpBase->mpMultiplayer->CommitScriptMapChange();
}
void cLuxScriptRuntime::RollbackMapChange() {
    if(!mpImpl->mapChange) return;
    Impl::MapChange& previous=*mpImpl->mapChange;
    mpImpl->modules.swap(previous.modules);mpImpl->timers.swap(previous.timers);
    mpImpl->failedHooks.swap(previous.failedHooks);mpImpl->playerState=previous.playerState;
    mpImpl->loadedMapFile=previous.loadedMapFile;
    mpImpl->revised=previous.revised;mpImpl->ready=previous.ready;
    mpImpl->clientDisabled=previous.clientDisabled;mpImpl->clientFailure=previous.clientFailure;
    mpImpl->playerStateMapLoaded=previous.playerStateMapLoaded;
    mpImpl->playerStateMapSession=previous.playerStateMapSession;
    mpImpl->playerStateMapEpoch=previous.playerStateMapEpoch;
    mpImpl->mapChange.reset();
    if(gpBase->mpMultiplayer) gpBase->mpMultiplayer->RollbackScriptMapChange();
}
bool cLuxScriptRuntime::LoadPackage(const tString& mapFile,tString& error) {
    if(mpImpl->loadedMapFile==mapFile && mpImpl->revised) {error.clear();return true;}
    error.clear();const tString manifest=cString::SetFileExt(mapFile,"scriptcfg");
    if(!cPlatform::FileExists(cString::To16Char(manifest))) {mpImpl->ClearPackage();BeginPlayerStateMap();return true;}
    if(!ReadManifest(manifest,error)) return false;
    const tString folder=cString::GetFilePath(mapFile);
    std::vector<std::pair<tString,tString> > sources;
    const std::pair<tString,tString> files[]={
        {"global",folder+"global.client.hps"},{"inventory",folder+"inventory.client.hps"},
        {"map",cString::SetFileExt(mapFile,"client.hps")}
    };
    for(const auto& file:files) {
        if(!cPlatform::FileExists(cString::To16Char(file.second))) continue;
        tString source;if(!ReadSource(file.second,source,error,MaxSourceBytes)) return false;
        sources.push_back({file.first,source});
    }
    luxnet::Writer packet(1);packet.U32(2);packet.U32(static_cast<uint32_t>(sources.size()));
    for(const auto& source:sources) {packet.String(source.first);packet.String(source.second);}
    if(!InstallPackage(packet.data,error)) return false;
    BeginPlayerStateMap();mpImpl->loadedMapFile=mapFile;return true;
}
bool cLuxScriptRuntime::ExportPackage(std::vector<uint8_t>& bytes) const {
    bytes.clear();if(!mpImpl->revised) return true;
    luxnet::Writer packet(1);packet.U32(2);packet.U32(static_cast<uint32_t>(mpImpl->modules.size()));
    for(const auto& module:mpImpl->modules) {packet.String(module.first);packet.String(module.second->source);}
    if(packet.data.size()>MaxPackageBytes) return false;
    bytes.swap(packet.data);return true;
}
bool cLuxScriptRuntime::InstallPackage(const std::vector<uint8_t>& bytes,tString& error) {
    error.clear();if(bytes.empty()) {mpImpl->ClearPackage();BeginPlayerStateMap();return true;}
    if(bytes.size()>MaxPackageBytes || bytes.front()!=1) {error="Invalid client script package size or format.";return false;}
    luxnet::Reader reader(bytes);const uint32_t api=reader.U32(),count=reader.U32();
    if(api!=2 || count>3) {error="Unsupported client script package version or module count.";return false;}
    std::map<tString,tString> sources;
    for(uint32_t i=0;i<count && reader.valid;++i) {
        const tString name=reader.String(16),source=reader.String(MaxSourceBytes);
        if((name!="map" && name!="global" && name!="inventory") || sources.count(name)) {
            error="Invalid or repeated client module name.";return false;
        }
        if(source.find('\0')!=tString::npos) {error="Client script package source contains an embedded NUL.";return false;}
        sources[name]=source;
    }
    if(!reader.Done()) {error="Malformed client script package.";return false;}
    if(!mpImpl->EnsureEngine(error)) return false;
    std::map<tString,std::shared_ptr<Impl::Module> > compiled;
    for(const auto& source:sources) {
        auto previous=mpImpl->modules.find(source.first);
        if(source.first!="map" && previous!=mpImpl->modules.end() && previous->second->source==source.second) continue;
        std::shared_ptr<Impl::Module> module(new Impl::Module);module->source=source.second;
        module->script.reset(new cSqScript(source.first,mpImpl->engine,&mpImpl->output,++mpImpl->nextModule));
        if(!module->script->CreateFromSource(source.first+".client.hps",source.second,&error,false)) return false;
        // AS 2.19 invokes script destructors and array element factories using
        // private contexts without our line watchdog. Reject these object types
        // before any globals run; strings and native value types remain usable.
        if(module->script->HasScriptDefinedObjectTypes()) {
            error="Client modules cannot define script classes or interfaces.";return false;
        }
        for(const tString hook:{"OnGameStart","OnStart","OnEnter","OnLeave","OnUpdate"}) {
            int handle;if(!HookHandle(module->script.get(),hook,"Client",hook=="OnUpdate",handle,error)) return false;
        }
        compiled[source.first]=std::move(module);
    }
    for(const auto& source:sources) if(!compiled.count(source.first))
        compiled[source.first]=std::move(mpImpl->modules[source.first]);
    mpImpl->modules.swap(compiled);
    // Map modules and their timers never survive a generation. Retained
    // global/inventory instances keep their private globals and timers.
    mpImpl->timers.erase(std::remove_if(mpImpl->timers.begin(),mpImpl->timers.end(),[&](const Impl::Timer& timer) {
        auto old=compiled.find(timer.module),current=mpImpl->modules.find(timer.module);
        return timer.module=="map" || current==mpImpl->modules.end() || (old!=compiled.end() && old->second);
    }),mpImpl->timers.end());
    mpImpl->failedHooks.clear();
    mpImpl->revised=true;mpImpl->ready=false;mpImpl->clientDisabled=false;mpImpl->clientFailure.clear();mpImpl->loadedMapFile.clear();
    const cLuxMultiplayer* session=gpBase->mpMultiplayer;
    if(session && session->IsClient() && (!mpImpl->playerStateMapLoaded ||
       mpImpl->playerStateMapSession!=session->GetSessionSerial() || mpImpl->playerStateMapEpoch!=session->GetMapEpoch()))
        BeginPlayerStateMap();
    return true;
}
bool cLuxScriptRuntime::ValidateAuthorityScript(iScript* script,const tString& module,tString& error) {
    error.clear();if(!mpImpl->revised || !script) return true;
    for(const tString hook:{"OnGameStart","OnStart","OnEnter","OnLeave","OnUpdate"}) {
        int handle;if(!HookHandle(script,hook,"Server",hook=="OnUpdate",handle,error)) {
            error=module+": "+error;return false;
        }
    }
    int handle;if(!HookHandle(script,"OnPlayerReady","Server",false,handle,error,"int")) {
        error=module+": "+error;return false;
    }
    // A successfully loaded/recompiled authority instance gets a fresh hook
    // lifetime even when its allocation reuses a retired script's address.
    for(auto it=mpImpl->failedHooks.begin();it!=mpImpl->failedHooks.end();)
        if(it->first==script) it=mpImpl->failedHooks.erase(it);else ++it;
    return true;
}
bool cLuxScriptRuntime::RunAuthorityPlayerReady(iScript* script,const tString& module,uint32_t peer,tString& error) {
    error.clear();if(!mpImpl->revised || !script) return true;
    int handle;if(!HookHandle(script,"OnPlayerReady","Server",false,handle,error,"int")) return false;
    if(handle<0) return true;
    cLuxMultiplayer* session=gpBase->mpMultiplayer;
    const bool active=session && session->IsActive();
    const uint32_t local=active?session->GetLocalPeerId():0;
    if((active && !session->IsHost()) || peer>INT32_MAX ||
       (peer!=local && (!session || !session->IsHost() || !session->GetWorld()->GetRemotePlayers().count(peer)))) {
        error="OnPlayerReady requires an available authoritative player.";return false;
    }
    LuxScriptExecutionContext context;context.revised=true;context.domain=LuxScriptDomain::Authority;
    context.hasPlayer=true;context.player=peer;context.module=module;
    context.session=active?session->GetSessionSerial():0;context.mapEpoch=active?session->GetMapEpoch():0;
    context.event=LuxNextScriptEventId();cLuxScriptExecutionScope scope(context);
    return script->RunTypedInt(handle,static_cast<int>(peer),&error,MaxLineCallbacks);
}
bool cLuxScriptRuntime::RunAuthorityHook(iScript* script,const tString& hook,tString& error,const float* step,const tString& module) {
    error.clear();if(!script) return true;
    if(!mpImpl->revised) return step ? true : script->Run(hook+"()");
    const auto key=std::make_pair(script,hook);
    if(mpImpl->failedHooks.count(key)) return true;
    int handle;if(!HookHandle(script,hook,"Server",step!=NULL,handle,error)) {
        if(hook=="OnUpdate") mpImpl->failedHooks.insert(key);
        return false;
    }
    if(handle<0) return true;
    LuxScriptExecutionContext context;context.revised=true;context.domain=LuxScriptDomain::Authority;context.module=module;
    if(gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsActive()) {context.session=gpBase->mpMultiplayer->GetSessionSerial();context.mapEpoch=gpBase->mpMultiplayer->GetMapEpoch();}
    context.event=LuxNextScriptEventId();
    cLuxScriptExecutionScope scope(context);
    const bool ok=script->RunTyped(handle,std::vector<tString>(),step,&error,MaxLineCallbacks);
    if(!ok && hook=="OnUpdate") mpImpl->failedHooks.insert(key);
    return ok;
}
bool cLuxScriptRuntime::RunAuthorityCommand(iScript* script,const tString& command,tString& error) {
    error.clear();if(!script) return true;
    if(!mpImpl->revised) return script->Run(command);
    auto context=LuxCurrentScriptContext();context.revised=true;context.domain=LuxScriptDomain::Authority;
    if(context.module.empty()) {
        const tString name=cString::GetFileName(cString::SetFileExt(script->GetName(),""));
        context.module=name=="global" || name=="inventory" ? name : "map";
    }
    if(gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsActive()) {context.session=gpBase->mpMultiplayer->GetSessionSerial();context.mapEpoch=gpBase->mpMultiplayer->GetMapEpoch();}
    else {context.session=0;context.mapEpoch=0;}
    cLuxScriptExecutionScope scope(context);
    if(!script->Run(command,&error,MaxLineCallbacks)) {
        error="Authority callback failed: "+command.substr(0,512)+": "+error;return false;
    }
    return true;
}
bool cLuxScriptRuntime::InitializeClient(tString& error,bool firstVisit) {
    error.clear();if(!mpImpl->revised || mpImpl->ready) return true;
    if(mpImpl->clientDisabled) {error="Client script package is disabled until reload: "+mpImpl->clientFailure;return false;}
    // All globals initialize only after map and authoritative state are ready.
    for(const tString name:{"global","inventory","map"}) {
        auto module=mpImpl->modules.find(name);if(module==mpImpl->modules.end()) continue;
        if(!module->second->initialized) {
            cLuxScriptExecutionScope context(ClientContext(name));
            if(!module->second->script->InitializeGlobals(&error,MaxLineCallbacks)) return mpImpl->DisableClient(error);
            module->second->initialized=true;
        }
        if(name!="map" && !module->second->started) {
            if(!mpImpl->Hook(name,"OnGameStart",error)) return mpImpl->DisableClient(error);
            module->second->started=true;
        }
    }
    if(firstVisit && !mpImpl->Hook("map","OnStart",error)) return mpImpl->DisableClient(error);
    if(!mpImpl->Hook("map","OnEnter",error)) return mpImpl->DisableClient(error);
    mpImpl->ready=true;return true;
}
bool cLuxScriptRuntime::LeaveClient(tString& error) {
    mpImpl->loadedMapFile.clear();
    error.clear();if(!mpImpl->ready) return true;
    const bool ok=mpImpl->Hook("map","OnLeave",error);mpImpl->ready=false;
    mpImpl->timers.erase(std::remove_if(mpImpl->timers.begin(),mpImpl->timers.end(),[](const Impl::Timer& timer) {return timer.module=="map";}),mpImpl->timers.end());
    return ok;
}
bool cLuxScriptRuntime::RunClientEvent(const tString& module,const tString& function,const std::vector<tString>& arguments,tString& error) {
    error.clear();
    if(!mpImpl->revised || !mpImpl->ready) {
        error="RunClientCallback requires initialized client modules; put initial presentation in ClientOnEnter.";return false;
    }
    if(!IsIdentifier(function) || arguments.size()>4) {
        error="Client event has an invalid signature.";return false;
    }
    auto found=mpImpl->modules.find(module);if(found==mpImpl->modules.end()) {error="Client event module is absent: "+module;return false;}
    for(const auto& argument:arguments) if(argument.size()>4096 || argument.find('\0')!=tString::npos) {error="Client event argument exceeds limit.";return false;}
    const tString declaration=Declaration(function,arguments.size());
    const int handle=found->second->script->GetFuncHandleByDecl(declaration);
    if(handle<0) {error="Missing typed client event: "+declaration;return false;}
    cLuxScriptExecutionScope context(ClientContext(module));
    return found->second->script->RunTyped(handle,arguments,NULL,&error,MaxLineCallbacks);
}
bool cLuxScriptRuntime::AddClientTimer(const tString& name,float seconds,const tString& function,tString& error) {
    error.clear();
    const auto context=LuxCurrentScriptContext();
    if(context.domain!=LuxScriptDomain::Client || !IsIdentifier(function) || name.empty() || name.size()>128 || name.find('\0')!=tString::npos ||
       !std::isfinite(seconds) || seconds<0 || seconds>86400) {
        error="Invalid client timer or timer budget exceeded.";return false;
    }
    auto module=mpImpl->modules.find(context.module);
    if(module==mpImpl->modules.end() || module->second->script->GetFuncHandleByDecl(Declaration(function,1))<0) {
        error="Client timer requires void "+function+"(string &in).";return false;
    }
    RemoveClientTimer(name);
    if(mpImpl->timers.size()>=MaxTimers) {error="Client timer budget exceeded.";return false;}
    mpImpl->timers.push_back({context.module,name,function,seconds,++mpImpl->nextTimer});return true;
}
void cLuxScriptRuntime::RemoveClientTimer(const tString& name) {
    const tString module=LuxCurrentScriptContext().module;
    mpImpl->timers.erase(std::remove_if(mpImpl->timers.begin(),mpImpl->timers.end(),[&](const Impl::Timer& timer) {return timer.name==name && timer.module==module;}),mpImpl->timers.end());
}
float cLuxScriptRuntime::GetClientTimerTimeLeft(const tString& name) const {
    const tString module=LuxCurrentScriptContext().module;
    for(const auto& timer:mpImpl->timers) if(timer.module==module && timer.name==name) return timer.remaining;
    return 0;
}
bool cLuxScriptRuntime::UpdateClient(float step,tString& error) {
    error.clear();if(!mpImpl->ready) return true;
    if(!std::isfinite(step) || step<0 || step>1) {error="Invalid client script update step.";return false;}
    std::vector<uint64_t> due;
    for(auto& timer:mpImpl->timers) {timer.remaining-=step;if(timer.remaining<=0) due.push_back(timer.serial);}
    unsigned fired=0;
    for(uint64_t serial:due) {
        if(fired++>=MaxTimerCallbacks) break;
        auto timer=std::find_if(mpImpl->timers.begin(),mpImpl->timers.end(),[&](const Impl::Timer& candidate) {return candidate.serial==serial;});
        if(timer==mpImpl->timers.end()) continue;
        const Impl::Timer value=*timer;mpImpl->timers.erase(timer);
        if(!RunClientEvent(value.module,value.function,std::vector<tString>{value.name},error)) return mpImpl->DisableClient(error);
    }
    if(!mpImpl->Hook("map","OnUpdate",error,&step)) return mpImpl->DisableClient(error);
    return true;
}
