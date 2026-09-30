#ifndef MULTIPLAYER_SCRIPT_RUNTIME_REGRESSION_H
#define MULTIPLAYER_SCRIPT_RUNTIME_REGRESSION_H
#include "LuxScriptRuntime.h"
#include "LuxScriptExecution.h"
#include "LuxScriptPlayerState.h"
#include "LuxScriptHandler.h"
#include "LuxGlobalDataHandler.h"
#include "LuxMultiplayerProtocol.h"
#include "impl/SqScript.h"
#include "impl/scriptstring.h"

namespace ScriptRegression {
struct Probe {
    cLuxScriptRuntime* runtime=NULL;
    std::map<tString,unsigned> calls;
    std::vector<LuxScriptExecutionContext> contexts;
    float step=0;
    tString error;
} probe;
static void __stdcall Observe(tString& value) {
    const auto context=LuxCurrentScriptContext();
    ++probe.calls[context.module+":"+value];probe.contexts.push_back(context);
}
static void __stdcall ObserveStep(float value) {probe.step=value;}
static void __stdcall Timer(tString& name,float seconds,tString& function) {
    probe.runtime->AddClientTimer(name,seconds,function,probe.error);
}
static std::vector<uint8_t> Package(const std::vector<std::pair<tString,tString> >& sources) {
    luxnet::Writer packet(1);packet.U32(2);packet.U32(static_cast<uint32_t>(sources.size()));
    for(const auto& source:sources) {packet.String(source.first);packet.String(source.second);}
    return packet.data;
}
static bool Unit(tString& error) {
    cLuxScriptRuntime runtime;probe=Probe();probe.runtime=&runtime;
    runtime.RegisterNative("void Print(string &in)",(void*)Observe);
    runtime.RegisterNative("void FadeIn(float)",(void*)ObserveStep);
    runtime.RegisterNative("void AddTimer(string &in,float,string &in)",(void*)Timer);
    auto check=[&](bool condition,const tString& message) {if(!condition) {error=message+": "+error;return false;}return true;};
    for(const tString source:{
        "void OnStart() { SetEntityActive(\"door\",false); }",
        "class Forbidden {} Forbidden object;",
        "void OnStart() {} void ClientOnStart() {}",
        "void ClientOnUpdate(string &in value) {}"
    }) {
        if(!check(!runtime.InstallPackage(Package({{"map",source}}),error),"restricted source was accepted")) return false;
    }
    const tString nulSource=tString("void OnStart() {}")+tString(1,'\0')+"void Hidden() {}";
    if(!check(!runtime.InstallPackage(Package({{"map",nulSource}}),error),"embedded NUL source was accepted")) return false;
    const tString map=R"AS(
        string privateValue="map";
        void ClientOnStart() { Print("start"); AddTimer("same",0.25f,"Tick"); }
        void ClientOnEnter() { Print("enter"); }
        void ClientOnLeave() { Print("leave"); }
        void ClientOnUpdate(float dt) { FadeIn(dt); }
        void Tick(string &in name) { Print(privateValue+"timer"); }
        void Echo(string &in value) { Print(value); }
    )AS";
    const tString global=R"AS(
        string privateValue="global";
        int count=0;
        void ClientOnGameStart() { count++; Print("start"); AddTimer("same",0.25f,"Tick"); }
        void Tick(string &in name) { Print(privateValue+"timer"); }
        void Count(string &in unused) { if(count==1) Print("once"); }
    )AS";
    const auto bytes=Package({{"map",map},{"global",global}});
    error.clear();
    if(!runtime.InstallPackage(bytes,error) || !check(!runtime.IsClientReady() && probe.calls.empty(),"package executed before ready")) return false;
    if(!check(!runtime.RunClientEvent("map","Echo",{"early"},error),"event ran before ready")) return false;
    if(!runtime.InitializeClient(error) || !runtime.InitializeClient(error)) return false;
    if(!check(probe.calls["map:start"]==1 && probe.calls["map:enter"]==1 && probe.calls["global:start"]==1,"lifecycle dispatched twice")) return false;
    const tString literal="quoted\"; Print(\"injected\");//\nline";
    LuxScriptExecutionContext outer;outer.revised=true;outer.domain=LuxScriptDomain::Authority;outer.hasPlayer=true;outer.player=77;outer.module="outer";
    {
        cLuxScriptExecutionScope scope(outer);
        if(!runtime.RunClientEvent("map","Echo",{literal},error)) return false;
        if(!check(LuxCurrentScriptContext().player==77 && LuxCurrentScriptContext().module=="outer","client call leaked actor context")) return false;
    }
    if(!check(probe.calls["map:"+literal]==1,"event argument was not passed literally")) return false;
    if(!runtime.UpdateClient(0.5f,error)) return false;
    if(!check(probe.error.empty() && probe.calls["map:maptimer"]==1 && probe.calls["global:globaltimer"]==1 && probe.step==0.5f,"typed update or timer ownership failed")) return false;
    for(const auto& context:probe.contexts) {
        if(!check(context.revised && context.domain==LuxScriptDomain::Client && context.hasPlayer && context.player==gpBase->mpMultiplayer->GetLocalPeerId(),"client hook lacks local actor context")) return false;
    }
    if(!runtime.LeaveClient(error) || !runtime.InstallPackage(bytes,error) || !runtime.InitializeClient(error,false) ||
       !runtime.RunClientEvent("global","Count",{""},error)) return false;
    if(!check(probe.calls["map:start"]==1 && probe.calls["map:enter"]==2 && probe.calls["map:leave"]==1 &&
        probe.calls["global:start"]==1 && probe.calls["global:once"]==1,"map revisit lost persistent module state or replayed start")) return false;
    // The host's authority context has no implicit local actor, even when its
    // client VM has just executed local presentation.
    asIScriptEngine* engine=asCreateScriptEngine(ANGELSCRIPT_VERSION);
    if(!engine) {error="cannot create authority probe engine";return false;}
    bool authorityOk=false;
    {
        cScriptOutput output;engine->SetMessageCallback(asMETHOD(cScriptOutput,AddMessage),&output,asCALL_THISCALL);
        RegisterScriptString(engine);
        engine->RegisterGlobalFunction("void Print(string &in)",asFUNCTION(Observe),asCALL_STDCALL);
        cSqScript authority("authority-probe",engine,&output,1);
        authorityOk=authority.CreateFromSource("authority.hps","void OnStart() {} void ServerOnStart() {}",&error) &&
            !runtime.ValidateAuthorityScript(&authority,"map",error);
        authorityOk=authorityOk && authority.CreateFromSource("authority.hps","void ServerOnPlayerReady(float player) {}",&error) &&
            !runtime.ValidateAuthorityScript(&authority,"map",error);
        authorityOk=authorityOk && authority.CreateFromSource("authority.hps","void ServerOnUpdate(float dt) { Print(\"authority\"); }",&error) &&
            runtime.ValidateAuthorityScript(&authority,"map",error);
        const float step=0.02f;
        authorityOk=authorityOk && runtime.RunAuthorityHook(&authority,"OnUpdate",error,&step);
        if(authorityOk) {
            const auto context=probe.contexts.back();
            authorityOk=context.domain==LuxScriptDomain::Authority && !context.hasPlayer && context.module=="map";
            if(!authorityOk) error="authority hook inherited a client actor";
        }
        authorityOk=authorityOk && authority.CreateFromSource("authority.hps","void Loop() { while(true) {} }",&error) &&
            !runtime.RunAuthorityCommand(&authority,"Loop()",error) && !error.empty();
        const unsigned before=probe.calls["map:fault-once"];
        authorityOk=authorityOk && authority.CreateFromSource("authority.hps","void OnUpdate(float dt) { Print(\"fault-once\"); while(true) {} }",&error) &&
            runtime.ValidateAuthorityScript(&authority,"map",error) && !runtime.RunAuthorityHook(&authority,"OnUpdate",error,&step) &&
            runtime.RunAuthorityHook(&authority,"OnUpdate",error,&step) && probe.calls["map:fault-once"]==before+1;
        authorityOk=authorityOk && authority.CreateFromSource("authority.hps","void OnUpdate(float dt) { Print(\"recompiled\"); }",&error) &&
            runtime.ValidateAuthorityScript(&authority,"map",error) && runtime.RunAuthorityHook(&authority,"OnUpdate",error,&step) &&
            probe.calls["map:recompiled"]==1;
    }
    engine->Release();
    if(!authorityOk) return false;
    if(!runtime.InstallPackage(Package({{"map","void ClientOnUpdate(float dt) { while(true) {} }"}}),error) || !runtime.InitializeClient(error)) return false;
    if(!check(!runtime.UpdateClient(0.02f,error) && !runtime.IsClientReady(),"runaway hook did not disable client execution")) return false;
    if(!check(!runtime.InitializeClient(error),"faulted package reinitialized without reload")) return false;
    if(!runtime.InstallPackage(bytes,error) || !runtime.InitializeClient(error)) return false;
    LuxScriptExecutionContext participant;participant.revised=true;participant.domain=LuxScriptDomain::Authority;
    participant.hasPlayer=true;participant.player=0;participant.module="map";
    auto& state=runtime.GetPlayerState();
    if(!state.Set(participant,"removed","private",true,error) || !state.Publish(participant,"removed","published",error)) return false;
    runtime.BeginMapChange();runtime.ForgetPlayer(0,0);runtime.RollbackMapChange();
    if(!check(runtime.GetPlayerState().Get(participant,"removed",true).empty() &&
        runtime.GetPlayerState().Published(0,0).empty(),"map rollback revived a departed player's private state")) return false;
    runtime.Reset();probe.runtime=NULL;error.clear();return true;
}
}

class cScriptRuntimeRegression {
    unsigned phase=0;
    Uint32 entered=0;
    bool lanternTriggered=false;
    bool nativeRoutingChecked=false,entryBaselineChecked=false,initializerRecompiled=false;
    cLuxMap* rollbackMap=NULL;
    uint32_t rollbackEpoch=0;
    void next() {++phase;entered=SDL_GetTicks();lanternTriggered=false;}
    bool both(const char* suffix) const {return exists(tString("host-scripts-")+suffix) && exists(tString("client-scripts-")+suffix);}
    int fail(tString& error,const tString& message) const {error="script phase "+cString::ToString(int(phase))+": "+message;return -1;}
    bool CloseTo(float a,float b) const {return std::fabs(a-b)<0.005f;}
    int CheckPublication(cLuxScriptRuntime* runtime,const tString& stage,tString& error) {
        if(!runtime->RunClientEvent("map","InspectPublication",{stage},error)) return fail(error,error);
        const float result=gpBase->mpPlayer->mfRollGoal;
        if(result<0) return fail(error,"published player state leaked across actors or returned a mutable store reference");
        return CloseTo(result,cMath::ToRad(23.0f)) ? 1 : 0;
    }
public:
    int Update(tString& error) {
        auto* session=gpBase->mpMultiplayer;
        const bool host=role=="host";
        if(phase && SDL_GetTicks()-entered>(phase<=2?60000u:20000u)) {
            tString values=" health="+cString::ToString(gpBase->mpPlayer->GetHealth())+
                " fov="+cString::ToString(gpBase->mpPlayer->mfFOVMulGoal)+
                " aspect="+cString::ToString(gpBase->mpPlayer->mfAspectMulGoal)+
                " roll="+cString::ToString(gpBase->mpPlayer->mfRollGoal);
            if(host && gpBase->mpMapHandler->GetCurrentMap()) {
                auto* map=gpBase->mpMapHandler->GetCurrentMap();
                values+=" ready_host="+map->GetVar("ready_host")->msVal+" ready_client="+map->GetVar("ready_client")->msVal;
            }
            return fail(error,"timed out: "+session->GetStatus()+values);
        }
        if(phase==0) {
            if(!ScriptRegression::Unit(error)) return fail(error,error);
            auto* production=gpBase->mpScriptHandler->GetRuntime();
            const tString substringProbe=R"AS(
                string@ stored;
                void ClientOnStart() {
                    string source="abcdef";
                    @stored=StringSub(source,1,3);
                    string@ other=StringSub("xyz",1,1);
                    stored+="!";
                    bool ok=source=="abcdef" && stored=="bcd!" && other=="y" &&
                        StringSub(source,-1,3)=="" && StringSub(source,6,3)=="" &&
                        StringSub(source,2147483647,2147483647)=="" &&
                        StringSub(source,2,-1)=="cdef" && StringSub(source,1,2147483647)=="bcdef";
                    if(!ok) { while(true) {} }
                }
                void Check(string &in unused) {
                    if(stored!="bcd!") { while(true) {} }
                    string@ other=StringSub("another",0,3);
                    if(stored!="bcd!" || other!="ano") { while(true) {} }
                }
            )AS";
            if(!production->InstallPackage(ScriptRegression::Package({{"map",substringProbe}}),error) ||
               !production->InitializeClient(error) || !production->RunClientEvent("map","Check",{""},error))
                return fail(error,"production StringSub handle/bounds regression: "+error);
            production->Reset();
            const uint32_t completionSequence=session->mlScriptCompletionSequence;
            for(const tString command:{"__LuxPlayerCompletion_","__LuxPlayerCompletion_0()",
                "__LuxPlayerCompletion_4294967296()","__LuxPlayerCompletion_1(true);Injected()",
                "__LuxPlayerCompletion_1(false,true)","__LuxPlayerCompletion_1(1)"}) {
                if(!session->HandlePlayerCompletionCommand(command) || session->mlScriptCompletionSequence!=completionSequence)
                    return fail(error,"malformed completion token was accepted");
            }
            // Exercise the production registration list too, rather than only
            // the probe VM's deliberately small set of observation natives.
            for(const tString command:{
                "SetPlayerHealth(1)","SetEntityActive(\"door\",false)",
                "SelectScriptPlayer(0)","RunClientCallback(\"Personal\",\"\")",
                "GetScriptPlayerCount()","GetScriptPlayerIdAt(0)",
                "SetPlayerVarInt(\"x\",1)","GetPlayerVarInt(\"x\")",
                "SetPlayerVarFloat(\"x\",1)","GetPlayerVarFloat(\"x\")",
                "SetPlayerVarString(\"x\",\"secret\")","GetPlayerVarString(\"x\")",
                "PublishPlayerVar(\"x\",\"secret\")",
                "ChangeMap(\"other.map\",\"\",\"\",\"\")"
            }) {
                const auto denied=ScriptRegression::Package({{"map","void ClientOnStart() { "+command+"; }"}});
                if(gpBase->mpScriptHandler->GetRuntime()->InstallPackage(denied,error))
                    return fail(error,"authority native escaped into production client VM: "+command);
            }
            mark(role+"-scripts-runtime.txt","capability rejection, lifecycle aliases, typed calls, module/timer isolation, persistent globals, authority/client contexts, watchdog passed");
            next();return 0;
        }
        if(phase==1) {
            if(!both("runtime.txt")) return 0;
            if(host) {
                const char* fixture=std::getenv("CODEX_MP_SCRIPT_MAP");
                if(!fixture || !*fixture) return fail(error,"missing disposable script map");
                cLuxMultiplayerSettings settings;settings.useSteam=false;settings.port=port;settings.maxPlayers=2;
                settings.map=fixture;settings.startPos="PlayerStartArea_1";
                if(!session->Host(settings)) return fail(error,"host failed: "+session->GetStatus());
                auto* baseline=gpBase->mpMapHandler->GetCurrentMap()->GetEntityByName("chair_wood_1");
                if(!baseline || !baseline->GetBodyNum()) return fail(error,"initial body baseline fixture is absent");
                auto* baselineBody=baseline->GetBody(0);baselineBody->SetGravity(false);baselineBody->SetCollide(false);
                baselineBody->SetLinearVelocity(cVector3f(0));baselineBody->SetAngularVelocity(cVector3f(0));
                const cVector3f baselinePosition=baselineBody->GetWorldPosition()+cVector3f(1.25f,0,0);
                baselineBody->SetPosition(baselinePosition);
                mark("host-scripts-entry-baseline.txt",cString::ToString(baselinePosition.x)+" "+cString::ToString(baselinePosition.y)+" "+cString::ToString(baselinePosition.z));
                mark("host-scripts-listening.txt","listening");
            } else {
                if(!exists("host-scripts-listening.txt")) return 0;
                if(!session->Join("127.0.0.1:"+cString::ToString(int(port)))) return fail(error,"join failed: "+session->GetStatus());
            }
            next();return 0;
        }
        if(phase==14) return both("rollback.txt") ? 1 : 0;
        if(!session->IsActive()) return fail(error,"session stopped: "+session->GetStatus());
        auto* runtime=gpBase->mpScriptHandler->GetRuntime();
        auto* player=gpBase->mpPlayer;
        if(phase==2) {
            if(!host && !entryBaselineChecked && CloseTo(player->mfFOVMulGoal,0.95f)) {
                FILE* file=NULL;fopen_s(&file,(outputDir+"/host-scripts-entry-baseline.txt").c_str(),"rb");
                if(!file) return fail(error,"missing initial body baseline record");
                cVector3f expected;const int values=fscanf_s(file,"%f %f %f",&expected.x,&expected.y,&expected.z);fclose(file);
                auto* map=gpBase->mpMapHandler->GetCurrentMap();
                auto* chair=map?map->GetEntityByName("chair_wood_1"):NULL;
                if(values!=3 || !chair || !chair->GetBodyNum() || cMath::Vector3Dist(chair->GetBody(0)->GetWorldPosition(),expected)>0.01f)
                    return fail(error,"client initializer ran before the moved-body baseline was installed");
                entryBaselineChecked=true;
            }
            if(!session->IsReady() || session->GetWorld()->GetRemotePlayers().empty() || !runtime->IsClientReady()) return 0;
            if(host && (session->mPeers.empty() || !session->mPeers.begin()->second.ready)) return 0;
            if(!CloseTo(player->mfFOVMulGoal,0.95f) || !CloseTo(player->mfAspectMulGoal,0.91f)) return 0;
            if(!runtime->IsRevised()) return fail(error,"companion package not installed");
            auto* map=gpBase->mpMapHandler->GetCurrentMap();
            auto* moved=map->GetEntityByName("tinderbox_1");
            if(!moved || !moved->GetBodyNum()) return fail(error,"world mutation fixture is missing");
            const cVector3f position=moved->GetBody(0)->GetWorldPosition();
            if(host) {
                if(map->GetVar("world_setpos")->msVal!="1") return fail(error,"no-actor authority world setter failed");
                if(map->GetVar("ready_host")->msVal!="1" || map->GetVar("ready_client")->msVal!="1") return 0;
                if(map->GetVar("ready_context")->msVal!="1") return fail(error,"player-ready hook lost typed actor/context or player enumeration");
                if(map->GetVar("vars_ready_host")->msVal!="1" || map->GetVar("vars_ready_client")->msVal!="1")
                    return fail(error,"map/campaign player variables or copied native string getters failed");
                for(const auto& peer:session->mPeers) if(peer.second.scriptInitializationSent &&
                    (session->GetEntities()->HasPendingInitialState(peer.first) || session->GetWorld()->HasPendingInitialState(peer.first) ||
                     session->GetEnemies()->HasPendingInitialState(peer.first)))
                    return fail(error,"script initializer was sent with pending baseline queues");
                if(!nativeRoutingChecked) {
                    auto* body=player->GetCharacterBody();const cVector3f before=body->GetForce();
                    const cVector3f expected=body->GetRight()*3+body->GetUp()*5+body->GetForward()*7;
                    map->RunScript("SelectScriptPlayer(0); AddPlayerBodyForce(3,5,7,true)");
                    const cVector3f applied=body->GetForce()-before;body->AddForce(applied*-1);
                    if(cMath::Vector3Dist(applied,expected)>0.001f) return fail(error,"local player force lost its X component");
                    const size_t callbacks=player->GetCollideCallbackList()->size();
                    const cMatrixf oldMatrix=moved->GetBody(0)->GetLocalMatrix();
                    moved->GetBody(0)->SetPosition(body->GetPosition());
                    map->RunScript("AddPlayerCollideCallback(\"tinderbox_1\",\"ReplacementCollision\",false,1)");
                    player->CheckCollisionCallback("Player",map);
                    moved->GetBody(0)->SetMatrix(oldMatrix);
                    if(map->GetVar("collision_replacement")->msVal!="1" || player->GetCollideCallbackList()->size()!=callbacks)
                        return fail(error,"per-player remove/readd/remove retained the replacement registration");
                    nativeRoutingChecked=true;
                }
                if(std::atoi(map->GetVar("wrong_module_timer")->msVal.c_str())>0 || map->GetVar("timer_owner_error")->msVal=="1")
                    return fail(error,"authority timer ran in the wrong module or lost its captured actor");
                for(const tString name:{"global_timer_host","global_timer_client","inventory_timer_host","inventory_timer_client"})
                    if(map->GetVar(name)->msVal!="1") return 0;
                if(!initializerRecompiled) {
                    if(!gpBase->mpGlobalDataHandler->RecompileScript(&error))
                        return fail(error,"online authority initializer lost its current session/epoch: "+error);
                    initializerRecompiled=true;
                }
                if(map->GetVar("global_initializer_owner")->msVal=="-1")
                    return fail(error,"global initializer inherited an actor or its delayed owner lost player variables");
                if(map->GetVar("global_initializer_owner")->msVal!="1") return 0;
                if(map->GetVar("world_timer_actor")->msVal!="-1") return fail(error,"unattributed timer acquired a player");
                {
                    LuxScriptExecutionContext timerOwner;timerOwner.revised=true;timerOwner.domain=LuxScriptDomain::Authority;
                    timerOwner.module="map";timerOwner.session=session->GetSessionSerial();timerOwner.mapEpoch=session->GetMapEpoch();
                    cLuxScriptExecutionScope timerScope(timerOwner);
                    const auto* timer=map->GetTimer("unattributed-pending");
                    if(!timer) return fail(error,"unattributed pending timer is missing");
                    if(timer->mScriptContext.hasPlayer || timer->mScriptPlayerTrigger.remote)
                        return fail(error,"unattributed timer acquired a remote permission scope");
                }
                moved->RunCallbackFunc("CodexWorldProbe");
                if(map->GetVar("world_actor")->msVal!="-1")
                    return fail(error,"unattributed world callback inherited the host character");
                auto* interaction=map->GetEntityByName("chair_wood_1");
                if(!interaction) return fail(error,"host interaction fixture is missing");
                interaction->RunInteractCallbackFunc();
                if(map->GetVar("interaction_actor")->msVal!="0")
                    return fail(error,"host player interaction lost its local actor attribution");
                mark("host-scripts-world.txt",cString::ToString(position.x)+" "+cString::ToString(position.y)+" "+cString::ToString(position.z));
            } else {
                FILE* file=NULL;fopen_s(&file,(outputDir+"/host-scripts-world.txt").c_str(),"rb");
                if(!file) return 0;
                cVector3f expected;const int values=fscanf_s(file,"%f %f %f",&expected.x,&expected.y,&expected.z);fclose(file);
                if(values!=3 || cMath::Vector3Dist(position,expected)>0.01f) return 0;
            }
            const int publication=CheckPublication(runtime,"ready",error);
            if(publication<=0) return publication;
            player->SetHealth(100);
            mark(role+"-scripts-ready.txt","package initialized after join with local start and timer");next();return 0;
        }
        if(phase==3) {
            if(!both("ready.txt")) return 0;
            if(host) {
                const uint32_t peer=session->mPeers.begin()->first;
                gpBase->mpMapHandler->GetCurrentMap()->RunScript("TargetPeer("+cString::ToString(int(peer))+")");
                if(gpBase->mpMapHandler->GetCurrentMap()->GetVar("position_immediate")->msVal!="1")
                    return fail(error,"remote position query did not observe the same-callback setter");
                if(gpBase->mpMapHandler->GetCurrentMap()->GetVar("vars_target")->msVal!="1")
                    return fail(error,"remote authority player variables changed after the host initialized its own state");
            }
            next();return 0;
        }
        if(phase==4) {
            if(host) {
                if(!CloseTo(player->GetHealth(),100) || !CloseTo(player->mfFOVMulGoal,0.95f) || !CloseTo(player->mfAspectMulGoal,0.91f))
                    return fail(error,"remote selected-player work ran on host character");
            } else if(!CloseTo(player->GetHealth(),73) || !CloseTo(player->mfFOVMulGoal,0.7f) || !CloseTo(player->mfAspectMulGoal,0.81f)) return 0;
            const int publication=CheckPublication(runtime,host?"ready":"targeted",error);
            if(publication<=0) return publication;
            mark(role+"-scripts-remote.txt","B received one selected-player callback and health mutation; host unchanged");next();return 0;
        }
        if(phase==5) {
            if(!both("remote.txt")) return 0;
            if(host) {
                gpBase->mpMapHandler->GetCurrentMap()->RunScript("TargetPeer(0)");
                if(gpBase->mpMapHandler->GetCurrentMap()->GetVar("position_immediate")->msVal!="1")
                    return fail(error,"host position query did not observe the same-callback setter");
                if(gpBase->mpMapHandler->GetCurrentMap()->GetVar("vars_target")->msVal!="1")
                    return fail(error,"host authority player variables changed after the remote actor was selected");
            }
            next();return 0;
        }
        if(phase==6) {
            if(!CloseTo(player->GetHealth(),host?63.0f:73.0f) || !CloseTo(player->mfFOVMulGoal,host?0.8f:0.7f) || !CloseTo(player->mfAspectMulGoal,host?0.71f:0.81f)) return 0;
            const int publication=CheckPublication(runtime,"targeted",error);
            if(publication<=0) return publication;
            // Revealing the private update accumulator proves the client hook
            // was receiving typed, positive game steps on both participants.
            if(!runtime->RunClientEvent("map","Reveal",{""},error)) return fail(error,error);
            if(!(player->mfRollGoal>0)) return fail(error,"client OnUpdate did not accumulate positive steps");
            mark(role+"-scripts-personal.txt","host-local and remote callbacks ran once in separate client contexts");next();return 0;
        }
        if(phase==7) {
            if(!both("personal.txt")) return 0;
            if(host) {
                const uint32_t peer=session->mPeers.begin()->first;
                gpBase->mpMapHandler->GetCurrentMap()->RunScript("BeginCompletion("+cString::ToString(int(peer))+")");
            }
            next();return 0;
        }
        if(phase==8) {
            auto* map=gpBase->mpMapHandler->GetCurrentMap();
            if(!host && !lanternTriggered) {
                if(map->GetLanternLitCallback().find("__LuxPlayerCompletion_")!=0) return 0;
                // Exercise the native lantern completion path without requiring
                // an inventory pickup; the real hand/light helper still runs.
                player->GetHelperLantern()->SetActive(true,false,false);lanternTriggered=true;
            }
            if(host) {
                if(map->GetVar("completion_count")->msVal!="1") return 0;
                if(map->GetVar("completion_player")->msVal!=cString::ToString(int(session->mPeers.begin()->first)) || !CloseTo(player->GetHealth(),63))
                    return fail(error,"remote completion lost its authority actor context");
                for(const auto& completion:session->mScriptCompletions) {
                    if(completion.second.context.player==session->mPeers.begin()->first && completion.second.kind=="lantern") {
                        session->HandlePlayerCompletionCommand("__LuxPlayerCompletion_"+cString::ToString(int(completion.first))+"(true)");
                        if(map->GetVar("completion_count")->msVal!="1") return fail(error,"host could claim B's completion token");
                    }
                }
            } else if(!CloseTo(player->GetHealth(),71)) return 0;
            mark(role+"-scripts-completion-remote.txt","host-issued remote lantern completion returned to authority with B actor");next();return 0;
        }
        if(phase==9) {
            if(!both("completion-remote.txt")) return 0;
            if(host) gpBase->mpMapHandler->GetCurrentMap()->RunScript("BeginCompletion(0)");
            next();return 0;
        }
        if(phase==10) {
            auto* map=gpBase->mpMapHandler->GetCurrentMap();
            if(host) {
                if(!lanternTriggered) {
                    if(map->GetLanternLitCallback().find("__LuxPlayerCompletion_")!=0) return 0;
                    player->GetHelperLantern()->SetActive(true,false,false);lanternTriggered=true;
                }
                if(map->GetVar("completion_count")->msVal!="2" || map->GetVar("completion_player")->msVal!="0" || !CloseTo(player->GetHealth(),61))
                    return fail(error,"host-local lantern completion did not run exactly once in authority");
            } else if(!CloseTo(player->GetHealth(),71)) return fail(error,"host-local completion mutated remote player");
            mark(role+"-scripts-completion-local.txt","host-local completion returned to authority without leaking to B");next();return 0;
        }
        if(phase==11) {
            if(!both("completion-local.txt")) return 0;
            rollbackMap=gpBase->mpMapHandler->GetCurrentMap();rollbackEpoch=session->GetMapEpoch();
            if(!runtime->RunClientEvent("map","ArmRollback",{""},error)) return fail(error,error);
            if(host) {
                // This probe bypasses the lantern inventory requirement when
                // driving the helper, so reset through the same native path.
                player->GetHelperLantern()->SetActive(false,false,false);
                rollbackMap->RunScript("BeginCompletion(0)");
            }
            mark(role+"-scripts-rollback-armed.txt","private client counter, pending timer and completion token retained before failing map change");
            next();return 0;
        }
        if(phase==12) {
            if(!both("rollback-armed.txt")) return 0;
            if(host) {
                gpBase->mpMapHandler->ChangeMap("codex_script_broken.map","PlayerStartArea_1","","");
                if(!gpBase->mpMapHandler->mMapChangeData.mbActive) return fail(error,"invalid authority map change was not accepted");
                mark("host-scripts-rollback-requested.txt","normal map change will replace the client package before rejecting the authority signature");
            }
            next();return 0;
        }
        if(phase==13) {
            if(!exists("host-scripts-rollback-requested.txt")) return 0;
            auto* maps=gpBase->mpMapHandler;
            if(maps->GetCurrentMap()!=rollbackMap || session->GetMapEpoch()!=rollbackEpoch)
                return fail(error,"failed authority map load replaced the old map or epoch");
            if(host) {
                if(maps->mMapChangeData.mbActive || session->mbMapPreparing) return 0;
                if(!runtime->IsClientReady()) return fail(error,"failed map change did not restore the old client runtime");
                if(!lanternTriggered) {
                    session->NotifyScriptPlayerReady(0);
                    session->NotifyScriptPlayerReady(session->mPeers.begin()->first);
                    if(rollbackMap->GetVar("ready_host")->msVal!="1" || rollbackMap->GetVar("ready_client")->msVal!="1")
                        return fail(error,"failed map change lost player-ready deduplication");
                    player->GetHelperLantern()->SetActive(true,false,false);lanternTriggered=true;
                    if(rollbackMap->GetVar("completion_count")->msVal!="3" || rollbackMap->GetVar("completion_player")->msVal!="0")
                        return fail(error,"failed map change lost an outstanding authority completion token: count="+
                            rollbackMap->GetVar("completion_count")->msVal+" actor="+rollbackMap->GetVar("completion_player")->msVal+
                            " callback="+rollbackMap->GetLanternLitCallback()+" tokens="+cString::ToString(int(session->mScriptCompletions.size()))+
                            " life="+cString::ToString(int(session->GetWorld()->GetLocalPlayerLife())));
                    for(const uint32_t peer:{0u,session->mPeers.begin()->first}) {
                        rollbackMap->RunScript("CheckAfterRollback("+cString::ToString(int(peer))+")");
                        if(rollbackMap->GetVar("vars_rollback")->msVal!="1") return fail(error,"failed map change discarded actor map/campaign variables");
                    }
                }
                mark("host-scripts-rollback-cancelled.txt","old world, completion token, ready actors and private authority stores restored");
            }
            if(!exists("host-scripts-rollback-cancelled.txt") || !session->IsReady()) return 0;
            if(!runtime->IsClientReady()) return fail(error,"client runtime remained stopped after cancellation");
            if(!runtime->RunClientEvent("map","VerifyRollback",{""},error)) return fail(error,error);
            if(player->mfRollGoal<0) return fail(error,"failed map change lost client globals/published state or replayed lifecycle");
            if(!CloseTo(player->mfRollGoal,cMath::ToRad(31.0f))) return 0;
            mark(role+"-scripts-rollback.txt","failed authority load restored client VM globals, pending timer, update hook and published player state");
            next();return 0;
        }
        return 0;
    }
};
#endif
