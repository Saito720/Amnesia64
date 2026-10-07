// Native integration tests against the real game objects and synthetic bodies,
// meshes and clips. Retail files are read only; all output stays in scratch.
#include "hpl.h"
#include "graphics/Bitmap.h"
#include "graphics/SceneObservation.h"
#include "graphics/SubMesh.h"
#include "resources/BitmapLoaderHandler.h"
#include "LuxLlamaController.h" // Preserve private-member mangling for the friend adapter.
#include "../../dependencies/sources/llama.cpp/vendor/nlohmann/json.hpp"
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <crtdbg.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <SDL2/SDL.h>
#undef main

// A test adapter observes controller state without widening the game API.
// Engine and standard library declarations have already been included above.
#define private public
#define protected public
#include "LuxBase.h"
#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxHelpFuncs.h"
#include "LuxMusicHandler.h"
#include "LuxConfigHandler.h"
#include "LuxDebugHandler.h"
#include "LuxEnemy_Llama.h"
#include "LuxEnemyMover.h"
#include "LuxEnemyPathfinder.h"
#include "LuxInputHandler.h"
#include "LuxMapHelper.h"
#include "LuxProp_SwingDoor.h"
#undef protected
#undef private

namespace fs = std::filesystem;
cLuxBase* gpBase = NULL;
static int checks = 0;
static FILE* gpCheckLog = NULL;

static void Require(bool abResult, const char* asLabel)
{
    ++checks;
    std::fprintf(gpCheckLog, "%s: %s\n", abResult ? "PASS" : "FAIL", asLabel);
    std::fflush(gpCheckLog);
    if(abResult) return;
    std::fprintf(stderr, "FAIL: %s\n", asLabel);
    std::exit(2);
}

static bool Near(float a, float b) { return std::fabs(a-b)<0.0001f; }

class cLuxLlamaControllerTestAdapter
{
public:
    static void Reply(cLuxLlamaController* apController, cLuxEnemy_Llama* apOwner,
        const std::string& asReply, float afLatency=0.1f, bool abWrongGeneration=false,
        uint64_t alRequestId=101,bool abCancelled=false)
    {
        apController->mbEnabled=true;
        apController->mpOwner=apOwner;
        apController->mpOwnerMap=apOwner->mpMap;
        apController->mlPendingRequest=alRequestId;
        apController->mlPendingGeneration=apController->mlGeneration+(abWrongGeneration ? 1 : 0);
        apController->mfSubmittedAt=apController->mfClock-afLatency;
        cLlamaResult result;
        result.mlRequestId=alRequestId;
        result.msText=asReply;
        result.mbCancelled=abCancelled;
        apController->HandleResult(result);
    }
    static void Pending(cLuxLlamaController* apController, cLuxEnemy_Llama* apOwner)
    {
        apController->mpOwner=apOwner;
        apController->mpOwnerMap=apOwner->mpMap;
        apController->mlPendingRequest=102;
        apController->mlPendingGeneration=apController->mlGeneration;
    }
    static unsigned long long Generation(cLuxLlamaController* apController) { return apController->mlGeneration; }
    static void Submit(cLuxLlamaController* apController, cLuxEnemy_Llama* apOwner)
    { apController->SubmitObservation(apOwner); }
    static bool EnsureLoaded(cLuxLlamaController* apController)
    { return apController->EnsureLoaded(); }
    static void PerceptionPending(cLuxLlamaController* apController,cLuxEnemy_Llama* apOwner,
        const cLuxLlamaObservation& aImage,uint64_t alRequestId=101)
    {
        apController->mpOwner=apOwner;
        apController->mpOwnerMap=apOwner->mpMap;
        apController->mlPendingRequest=alRequestId;
        apController->mlPendingGeneration=apController->mlGeneration;
        apController->mPerceptionFrame=cLuxLlamaPerceptionFrame();
        apController->mPerceptionFrame.mlRequestId=alRequestId;
        apController->mPerceptionFrame.mlFrameId=aImage.mlFrameId;
        apController->mPerceptionFrame.mImage.mlWidth=aImage.mvSize.x;
        apController->mPerceptionFrame.mImage.mlHeight=aImage.mvSize.y;
        apController->mPerceptionFrame.mImage.mvRGB=aImage.mvRGB;
    }
    static void ExportPerception(cLuxLlamaController* apController)
    { apController->SavePerceptionReport(true); }
    static void AutonomousRecord(cLuxLlamaController* apController,cLuxEnemy_Llama* apOwner,
        const cLuxLlamaObservation& aImage,const cLlamaRequest& aRequest,uint64_t alRequestId=101)
    {
        apController->mpOwner=apOwner;
        apController->mpOwnerMap=apOwner->mpMap;
        apController->mPendingDecision=cLuxLlamaDecisionRecord();
        apController->mPendingDecision.mpOwner=apOwner;
        apController->mPendingDecision.mpMap=apOwner->mpMap;
        apController->mPendingDecision.mlRequestId=alRequestId;
        apController->mPendingDecision.mlFrameId=aImage.mlFrameId;
        apController->mPendingDecision.mfTime=aImage.mfTime;
        apController->mPendingDecision.mRequest=aRequest;
    }
    static cLuxLlamaDecisionRecord LastDecision(cLuxLlamaController* apController)
    { return apController->mLastDecision; }
    static void LastDecision(cLuxLlamaController* apController,const cLuxLlamaDecisionRecord& aRecord)
    { apController->mLastDecision=aRecord; }
    static bool HasPendingDecision(cLuxLlamaController* apController)
    { return apController->mPendingDecision.mlRequestId!=0; }
    static void DiagnosticPending(cLuxLlamaController* apController,uint64_t alID=101)
    {
        apController->mlPendingRequest=alID;
        apController->mlPendingGeneration=apController->mlGeneration;
        apController->mfSubmittedAt=apController->mfClock;
        apController->mPerceptionFrame.mlRequestId=alID;
    }
    static void Deliver(cLuxLlamaController* apController,const cLlamaResult& aResult)
    { apController->HandleResult(aResult); }
};

static std::string Decision(const char* asAction, const char* asTarget="none", float afForward=0,
    float afTurn=0, float afDuration=0.2f, int alX=0, int alY=0)
{
    return "{\"behavior\":\"patrol\",\"action\":\""+std::string(asAction)+"\",\"target\":\""+asTarget+
        "\",\"forward\":"+std::to_string(afForward)+",\"turn_degrees\":"+std::to_string(afTurn)+
        ",\"duration\":"+std::to_string(afDuration)+",\"run\":false,\"target_x\":"+std::to_string(alX)+
        ",\"target_y\":"+std::to_string(alY)+",\"memory\":\"visible evidence\"}";
}

static void Write(const fs::path& aPath, const std::string& asText)
{
    std::ofstream file(aPath, std::ios::binary | std::ios::trunc);
    file << asText;
    Require(file.good(), "write scratch fixture");
}

static cConfigFile* Config(const fs::path& aPath)
{
    cConfigFile* pConfig = hplNew(cConfigFile, (cString::To16Char(aPath.generic_string())));
    Require(pConfig->Load(), "load read-only game config");
    return pConfig;
}

static cLuxEnemy_Llama* Enemy(cLuxMap* apMap, const char* asName, int alID, bool abAnimation,
    const char* asFOV = NULL, cVector2l avObservationSize = cVector2l(64,64))
{
    cLuxEnemy_Llama* pEnemy = hplNew(cLuxEnemy_Llama, (asName, alID, apMap));
    cResources* pResources = gpBase->mpEngine->GetResources();
    cMesh* pMesh = hplNew(cMesh, (asName, _W(""), pResources->GetMaterialManager(), pResources->GetAnimationManager()));
    pMesh->IncUserCount();
    if(abAnimation)
    {
        cAnimation* pClip = hplNew(cAnimation, ("Idle", _W(""), "synthetic"));
        pClip->SetLength(1);
        pMesh->AddAnimation(pClip);
    }
    pEnemy->mpWorld = apMap->GetWorld();
    pEnemy->mpMeshEntity = apMap->GetWorld()->CreateMeshEntity(asName, pMesh, false);
    pEnemy->mpCharBody = apMap->GetPhysicsWorld()->CreateCharacterBody(asName, cVector3f(0.8f, 1.8f, 0.8f));
    pEnemy->mpCharBody->SetMass(50);
    pEnemy->mpCharBody->SetPosition(cVector3f(float(alID), 2, 0));
    pEnemy->mpCharBody->SetYaw(0);
    pEnemy->mpCharBody->SetEntity(pEnemy->mpMeshEntity);
    pEnemy->mpCharBody->SetUserData(pEnemy);
    pEnemy->mfHealth = 100;
    pEnemy->mlToughness = 0;
    pEnemy->mfMaxRegenHealth = 100;
    pEnemy->mfRegenHealthSpeed = 0;
    pEnemy->mfTurnSpeedMul = 3;
    pEnemy->mfTurnMaxSpeed = 2;
    pEnemy->mfTurnMinBreakAngle = cMath::ToRad(20);
    pEnemy->mfTurnBreakMul = 0;
    pEnemy->mfMoveSpeedAnimMul = 1;
    for(int pose = 0; pose < eLuxEnemyPoseType_LastEnum; ++pose)
    {
        pEnemy->mfStoppedToWalkSpeed[pose] = 0.2f;
        pEnemy->mfWalkToStoppedSpeed[pose] = 0.1f;
        pEnemy->mfWalkToRunSpeed[pose] = 2;
        pEnemy->mfRunToWalkSpeed[pose] = 1.5f;
        for(int speed = 0; speed < eLuxEnemyMoveSpeed_LastEnum; ++speed)
        {
            pEnemy->mfDefaultForwardSpeed[pose][speed] = 1;
            pEnemy->mfDefaultBackwardSpeed[pose][speed] = 1;
            pEnemy->mfDefaultForwardAcc[pose][speed] = 4;
            pEnemy->mfDefaultForwardDeacc[pose][speed] = 4;
        }
    }
    // Initialize fields normally supplied by an asset before save/restore.
    pEnemy->m_mtxOnLoadTransform = cMatrixf::Identity;
    pEnemy->mvOnLoadScale = 1;
    pEnemy->mvStartPosition = pEnemy->mpCharBody->GetFeetPosition();
    pEnemy->mlStuckDoorID = -1;
    pEnemy->mfDarknessGlowAlphaGoal = 0;
    pEnemy->mvTempPos = 0;
    pEnemy->mfTempVal = pEnemy->mlTempVal = 0;
    // Load the real observation settings without a retail entity/rig fixture.
    cLuxEnemyLoader_Llama loader("Enemy_Llama");
    if(asFOV) loader.SetUserVariable("FOV",asFOV);
    loader.SetUserVariable("LlamaObservationWidth",cString::ToString(avObservationSize.x));
    loader.SetUserVariable("LlamaObservationHeight",cString::ToString(avObservationSize.y));
    // A legacy FOV and multiplier must not override the raw observation setting.
    pEnemy->mfFOV = cMath::ToRad(45.0f*1.1f);
    pEnemy->mfFOVXMul = 3;
    loader.LoadVariables(pEnemy,NULL);
    // The synthetic world has no game MapHelper for ground-alignment rays.
    pEnemy->mbAlignEntityWithGroundRay = false;
    pEnemy->SetupAfterLoad(apMap->GetWorld());
    if(abAnimation) pEnemy->mpMeshEntity->GetAnimationState(0)->SetBaseSpeed(0.7f);
    apMap->AddEntity(pEnemy);
    return pEnemy;
}

static void CheckFOV(cLuxMap* apMap)
{
    struct cFOVCase
    {
        const char* msInput;
        float mfDegrees;
        cVector2l mvSize;
        const char* msLabel;
    };
    const cFOVCase cases[] = {
        {NULL,120,cVector2l(64,64),"absent FOV retains the 120 degree horizontal default"},
        {"90",90,cVector2l(128,64),"raw entity FOV applies to a wide camera independently of legacy hard-mode settings"},
        {"90",90,cVector2l(64,128),"raw entity FOV retains its horizontal angle on a portrait camera"},
        {"0",1,cVector2l(64,64),"zero FOV clamps to one degree"},
        {"360",179,cVector2l(64,64),"oversized FOV clamps below 180 degrees"},
        {"nan",120,cVector2l(64,64),"NaN FOV falls back to 120 degrees"},
        {"inf",120,cVector2l(64,64),"positive infinite FOV falls back to 120 degrees"},
        {"-inf",120,cVector2l(64,64),"negative infinite FOV falls back to 120 degrees"}
    };
    const bool bPreviousHardMode = gpBase->mbHardMode;
    gpBase->mbHardMode = true;
    for(size_t i=0; i<sizeof(cases)/sizeof(cases[0]); ++i)
    {
        const cFOVCase& test = cases[i];
        const tString sName = "FOV"+cString::ToString(int(i));
        cLuxEnemy_Llama* pEnemy = Enemy(apMap,sName.c_str(),30+int(i),false,test.msInput,test.mvSize);
        const float fExpected = cMath::ToRad(test.mfDegrees);
        Require(Near(pEnemy->GetObservationFOV(),fExpected),test.msLabel);
        const cMatrixf& projection = pEnemy->mpObservationCamera->GetProjectionMatrix();
        const float fAspect = float(test.mvSize.x)/float(test.mvSize.y);
        Require(std::isfinite(projection.m[0][0]) && projection.m[0][0]>0 &&
            Near(2.0f*std::atan(1.0f/projection.m[0][0]),fExpected) &&
            Near(projection.m[1][1]/projection.m[0][0],fAspect),
            "actual camera projection preserves the configured horizontal FOV and image aspect");
        pEnemy->SetDisabled(true); // FOV fixtures never compete for the live controller.
    }
    gpBase->mbHardMode = bPreviousHardMode;
}

static void CheckObservationResolution(cLuxMap* apMap)
{
    cLuxEnemy_Llama* enemy=Enemy(apMap,"ResolutionDefaults",39,false);
    cLuxEnemyLoader_Llama loader("Enemy_Llama");
    loader.LoadVariables(enemy,NULL);
    Require(enemy->GetObservationSize()==cVector2l(1280,864),
        "missing authored resolution selects the full-detail spatial-grounding default");
    loader.SetUserVariable("LlamaObservationWidth","99999");
    loader.SetUserVariable("LlamaObservationHeight","1");
    loader.LoadVariables(enemy,NULL);
    Require(enemy->GetObservationSize()==cVector2l(2048,64),
        "loader bounds excessive width and undersized height to supported capture axes");
    loader.SetUserVariable("LlamaObservationWidth","64");
    loader.SetUserVariable("LlamaObservationHeight","2048");
    loader.LoadVariables(enemy,NULL);
    Require(enemy->GetObservationSize()==cVector2l(64,2048),
        "explicit small observations remain allowed alongside the higher capture ceiling");
    enemy->SetDisabled(true);
}

static void Hear(cLuxEnemy_Llama* apEnemy, float afBearing, float afDistance = 6, float afVolume = 0.8f)
{
    const float fWorldBearing = afBearing-apEnemy->mpCharBody->GetYaw();
    const cVector3f vOffset(std::sin(fWorldBearing)*afDistance,0,-std::cos(fWorldBearing)*afDistance);
    apEnemy->SendMessage(eLuxEnemyMessage_SoundHeard, 0, false, apEnemy->mpCharBody->GetPosition()+vOffset, afVolume);
}

static void Capture(cLuxEnemy_Llama* apEnemy)
{
    apEnemy->mfLastObservationAttempt = apEnemy->mfElapsedTime - apEnemy->mfObservationInterval - 0.01f;
    apEnemy->CaptureObservation();
    Require(apEnemy->GetObservationTexture() != NULL && apEnemy->GetObservationRGB().size() == 64*64*3,
        "controller captures a real RGB observation of the synthetic world");
}

static cVector2l SnapshotPoint(cLuxEnemy_Llama* apEnemy, unsigned char ar, unsigned char ag, unsigned char ab)
{
    Capture(apEnemy);
    Require(apEnemy->SnapshotModelObservation(),"copy the exact RGB/camera frame used to ground a model reply");
    const cLuxLlamaObservation& image=apEnemy->mModelObservation;
    double xSum=0,ySum=0;
    int count=0;
    for(int y=0; y<image.mvSize.y; ++y)
    for(int x=0; x<image.mvSize.x; ++x)
    {
        const size_t i=(size_t(y)*image.mvSize.x+x)*3;
        if(image.mvRGB[i]==ar && image.mvRGB[i+1]==ag && image.mvRGB[i+2]==ab)
        { xSum+=x+0.5; ySum+=y+0.5; ++count; }
    }
    Require(count>0,"grounding fixture has a visible target in the actual observation");
    return cVector2l(int(xSum/count*1000/image.mvSize.x),int(ySum/count*1000/image.mvSize.y));
}

static cMeshEntity* VisibleBox(cLuxMap* apMap, const char* asName, const cVector3f& avSize, const cVector3f& avPosition)
{
    cGraphics* pGraphics=gpBase->mpEngine->GetGraphics();
    cResources* pResources=gpBase->mpEngine->GetResources();
    cMaterial* pMaterial=pResources->GetMaterialManager()->CreateCustomMaterial(asName,pGraphics->GetMaterialType("SolidDiffuse"));
    cMesh* pMesh=hplNew(cMesh,(asName,_W(""),pResources->GetMaterialManager(),pResources->GetAnimationManager()));
    pMesh->IncUserCount();
    cSubMesh* pSubMesh=pMesh->CreateSubMesh("Box");
    pSubMesh->SetMaterial(pMaterial);
    pSubMesh->SetVertexBuffer(pGraphics->GetMeshCreator()->CreateBoxVertexBuffer(avSize));
    pSubMesh->Compile();
    cMeshEntity* pEntity=apMap->GetWorld()->CreateMeshEntity(asName,pMesh,false);
    pEntity->SetPosition(avPosition);
    return pEntity;
}

class cAttackTestProp : public iLuxProp
{
public:
    cAttackTestProp(const char* asName,int alID,cLuxMap* apMap) : iLuxProp(asName,alID,apMap,eLuxPropType_Object),mlHits(0) {}
    bool CanInteract(iPhysicsBody*) { return false; }
    bool OnInteract(iPhysicsBody*,const cVector3f&) { return false; }
    eLuxFocusCrosshair GetFocusCrosshair(iPhysicsBody*,const cVector3f&) { return eLuxFocusCrosshair_Default; }
    void OnConnectionStateChange(iLuxEntity*,int) {}
    iLuxEntity_SaveData* CreateSaveData() { return NULL; }
    void GiveDamage(float afAmount,int) { ++mlHits; mfHealth-=afAmount; }
    // This minimal fixture has no lights/effects. Override the protected virtual
    // instead of referencing its access-dependent MSVC mangling through the adapter.
    void OnSetActive(bool abActive)
    {
        for(size_t i=0; i<mvBodies.size(); ++i) mvBodies[i]->SetActive(abActive);
        mpMeshEntity->SetActive(abActive);
        mpMeshEntity->SetVisible(abActive);
    }
    void OnResetProperties() {}
    void OnSetupAfterLoad(cWorld*) {}
    void UpdatePropSpecific(float) {}
    int mlHits;
};

static void PropBody(cLuxMap* apMap,iLuxProp* apProp,const cVector3f& avPosition)
{
    apProp->mpWorld=apMap->GetWorld();
    apProp->mpMeshEntity=VisibleBox(apMap,apProp->GetName().c_str(),cVector3f(0.8f,1.8f,0.25f),avPosition);
    apProp->mfHealth=100;
    apProp->mlToughness=0;
    apProp->mbDissolveOnDestruction=false;
    iPhysicsBody* pBody=apMap->GetPhysicsWorld()->CreateBody(apProp->GetName(),
        apMap->GetPhysicsWorld()->CreateBoxShape(cVector3f(0.8f,1.8f,0.25f),NULL));
    pBody->SetMass(10);
    pBody->SetPosition(avPosition);
    pBody->SetUserData(apProp);
    apProp->mvBodies.push_back(pBody);
    apMap->AddEntity(apProp);
}

static void CheckModelDecisions(cLuxMap* apMap,cLuxEnemy_Llama* apOwner,cLuxEnemy_Llama* apExtra)
{
    apOwner->SetDebugOverride(false);
    apOwner->SetObservationEnabled(true);
    apOwner->mpCharBody->SetYaw(0);
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"model snapshot is available for deterministic controller result tests");
    const cLuxLlamaObservation snapshot=apOwner->mModelObservation;
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-4));
    Capture(apOwner);
    Require(apOwner->mModelObservation.mlFrameId==snapshot.mlFrameId && apOwner->mModelObservation.mvRGB==snapshot.mvRGB &&
        apOwner->GetObservationFrameId()!=snapshot.mlFrameId,"new debug captures leave the submitted RGB and camera snapshot immutable");
    cLuxLlamaController* pController=gpBase->mpMapHandler->GetLlamaController();
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("move","none",1,30));
    Require(apOwner->mModelAction==eLuxLlamaAction_Move && Near(apOwner->mfModelForward,1) &&
        Near(apOwner->mfModelTurnGoal,-cMath::ToRad(30)),"strict model reply creates bounded forward movement and a camera-relative right turn");
    apOwner->OnUpdate(0.3f);
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait && apOwner->mfModelActionRemaining==0 &&
        apOwner->mpCharBody->GetMoveSpeed(eCharDir_Forward)==0,"model movement expires and stops without receiving another reply");
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("turn","none",0,-45));
    Require(apOwner->mModelAction==eLuxLlamaAction_Turn && Near(apOwner->mfModelTurnGoal,cMath::ToRad(45)),
        "turn action uses the submitted camera heading and configured mover limits");
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("wait"));
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait && Near(apOwner->mfModelActionRemaining,0.2f),"wait replaces an outstanding turn with a bounded idle action");
    apOwner->OnUpdate(0.3f);
    Require(apOwner->mfModelActionRemaining==0,"wait expires through the ordinary enemy update path");
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,"{\"action\":\"teleport\"}");
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait && pController->GetStatus().find("Rejected")!=tString::npos,
        "invalid JSON/action replies are rejected without mechanical side effects");
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("move","none",1),7);
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait && pController->GetStatus().find("stale")!=tString::npos,
        "expired inference latency cannot start an action");
    apOwner->mModelObservation.mfTime=apOwner->mfElapsedTime-7;
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("move","none",1));
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait,"old source images are rejected even when the result arrives quickly");
    apOwner->mModelObservation.mfTime=apOwner->mfElapsedTime;
    apOwner->mpObservationCamera->SetPosition(snapshot.mvPosition+cVector3f(3,0,0));
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("move","none",1));
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait,"camera displacement invalidates an old grounded reply");
    apOwner->mpObservationCamera->SetPosition(snapshot.mvPosition);
    apOwner->mpCharBody->SetYaw(cMath::ToRad(46));
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("move","none",1));
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait,"heading changes beyond the accepted arc invalidate an old reply");
    apOwner->mpCharBody->SetYaw(0);
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("move","none",1),0.1f,true);
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait,"a cancelled controller generation cannot apply a delayed reply");
    cLuxLlamaControllerTestAdapter::Pending(pController,apOwner);
    const unsigned long long generation=cLuxLlamaControllerTestAdapter::Generation(pController);
    apOwner->SetDebugOverride(true);
    Require(!pController->IsBusy() && cLuxLlamaControllerTestAdapter::Generation(pController)>generation,
        "manual override cancels the pending request and invalidates its generation");
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("move","none",1));
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait,"a reply cannot override manual debug controls");
    apOwner->SetDebugOverride(false);
    cLuxLlamaControllerTestAdapter::Pending(pController,apOwner);
    apOwner->SetDisabled(true);
    Require(!pController->IsBusy() && cLuxEnemy_Llama::GetControllerOwner(apMap)==apExtra,"owner handoff cancels pending inference before another enemy can act");
    cLuxLlamaControllerTestAdapter::Reply(pController,apOwner,Decision("move","none",1));
    Require(apOwner->mModelAction==eLuxLlamaAction_Wait,"a delayed reply cannot drive an unavailable previous owner");
    apOwner->SetDisabled(false);
    pController->SetEnabled(false);
}

static void CheckTargetSteeringLifecycle(cLuxMap* apMap,cLuxEnemy_Llama* apOwner)
{
    cLuxLlamaController* controller=gpBase->mpMapHandler->GetLlamaController();
    controller->SetEnabled(false);
    controller->SetTargetSteeringOnly(false);
    apOwner->SetDebugOverride(false);
    apOwner->ResetModelAction();
    apOwner->mpCharBody->SetPosition(cVector3f(10,2,0));
    apOwner->mpCharBody->SetYaw(0);
    apOwner->mpCharBody->SetGravityActive(false);
    apOwner->mpCharBody->SetForceVelocity(cVector3f(0)); // This stationary fixture must not inherit falling velocity.
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"steering lifecycle starts from a real camera frame");
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,Decision("move","none",1,0,1));
    Require(apOwner->IsModelActionActive(),"a full-protocol movement is active before changing steering mode");
    cLuxLlamaControllerTestAdapter::Pending(controller,apOwner);
    const unsigned long long generation=cLuxLlamaControllerTestAdapter::Generation(controller);
    controller->SetTargetSteeringOnly(true);
    Require(controller->IsTargetSteeringOnly() && !controller->IsBusy() && !apOwner->IsModelActionActive() &&
        cLuxLlamaControllerTestAdapter::Generation(controller)>generation,
        "changing the action protocol cancels pending inference and stops an old-format action");
    const std::string previousReply=apOwner->GetLastModelReply();
    cLlamaResult delayed;
    delayed.mlRequestId=102;
    delayed.msText=Decision("move","none",1);
    cLuxLlamaControllerTestAdapter::Deliver(controller,delayed);
    Require(!apOwner->IsModelActionActive() && apOwner->GetLastModelReply()==previousReply,
        "an actual delayed old-format result cannot revive an action after a mode change");

    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"the new steering protocol starts from a fresh camera snapshot");
    const float startYaw=apOwner->mpCharBody->GetYaw();
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,
        "{\"target_visible\":true,\"target_side\":\"right\",\"action\":\"turn_right\"}");
    Require(apOwner->mModelBehavior==eLuxLlamaBehavior_Chase && apOwner->mModelAction==eLuxLlamaAction_Turn &&
        Near(apOwner->mfModelForward,0) && apOwner->mfModelActionRequestedTurn>0,
        "a compact visible-target reply becomes a bounded stationary chase turn");
    for(int i=0;i<60 && apOwner->IsModelActionActive();++i)
    {
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
    }
    Require(!apOwner->IsModelActionActive() &&
        -cMath::ToDeg(cMath::GetAngleDistanceRad(startYaw,apOwner->mpCharBody->GetYaw()))>1 &&
        apOwner->GetLastActionFeedback().find("turn completed")==0,
        "the translated compact reply actually turns the body right and records measured completion");

    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"an absent-target reply uses a fresh post-turn snapshot");
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,
        "{\"target_visible\":false,\"target_side\":\"none\",\"action\":\"wait\"}");
    const cVector3f idlePosition=apOwner->mpCharBody->GetPosition();
    const float idleYaw=apOwner->mpCharBody->GetYaw();
    Require(apOwner->mModelBehavior==eLuxLlamaBehavior_Patrol && apOwner->mModelAction==eLuxLlamaAction_Wait &&
        Near(apOwner->mfModelForward,0) && !apOwner->IsModelAttacking(),
        "an absent-target report idles without movement, pursuit or attack");
    for(int i=0;i<60 && apOwner->IsModelActionActive();++i)
    {
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
    }
    Require(!apOwner->IsModelActionActive() && cMath::Vector3Dist(idlePosition,apOwner->mpCharBody->GetPosition())<0.01f &&
        Near(idleYaw,apOwner->mpCharBody->GetYaw()) && apOwner->GetLastActionFeedback().find("wait completed")==0,
        "the absent-target wait completes without changing the actual body position or heading");
    const std::string idleFeedback=apOwner->GetLastActionFeedback();
    const char* contradictions[]={
        "{\"target_visible\":true,\"target_side\":\"none\",\"action\":\"turn_right\"}",
        "{\"target_visible\":false,\"target_side\":\"none\",\"action\":\"move_forward\"}"
    };
    for(const char* reply:contradictions)
    {
        Capture(apOwner);
        Require(apOwner->SnapshotModelObservation(),"contradictory steering reply uses a current snapshot");
        cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,reply);
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
        Require(!apOwner->IsModelActionActive() && controller->GetStatus().find("contradict")!=std::string::npos &&
            Near(idleYaw,apOwner->mpCharBody->GetYaw()) &&
            cMath::Vector3Dist(idlePosition,apOwner->mpCharBody->GetPosition())<0.01f &&
            apOwner->GetLastActionFeedback()==idleFeedback,
            "contradictory visual steering is rejected without inventing motion or overwriting completed feedback");
    }
    controller->SetTargetSteeringOnly(false);
    controller->SetEnabled(false);
    apOwner->ResetModelAction();
    apOwner->mpCharBody->SetGravityActive(true);
    apOwner->mpCharBody->SetYaw(0);
    apOwner->mpMover->TurnToAngle(0);
}

static void CheckActionFeedback(cLuxMap* apMap,cLuxEnemy_Llama* apOwner)
{
    apOwner->SetDebugOverride(false);
    apOwner->ResetModelAction();
    apOwner->SetObservationEnabled(true);
    apOwner->mpCharBody->SetYaw(0);
    apOwner->mpCharBody->SetPosition(cVector3f(10,2,0));
    apOwner->mpCharBody->SetGravityActive(false);
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-6));
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"new action history starts from actual submitted perception");
    cLuxLlamaDecision decision;
    std::string error;
    Require(ParseLuxLlamaDecision(Decision("move","none",1,0,0.4f),decision,error),"parse progress-feedback move fixture");
    apOwner->ApplyModelDecision(decision);
    Require(apOwner->IsModelActionActive(),"move remains active until its bounded action completes");
    apOwner->OnUpdate(1.0f/60);
    apMap->GetPhysicsWorld()->Update(1.0f/60);
    apOwner->SnapshotModelActionFeedback();
    Require(apOwner->GetLastActionFeedback().find("in progress")!=tString::npos &&
        apOwner->GetLastActionFeedback().find("horizontal_distance=")!=tString::npos,
        "a running move reports measured progress rather than only an executing label");
    const unsigned int submittedFrame=apOwner->mModelObservation.mlFrameId;
    Capture(apOwner);
    cLuxLlamaController* controller=gpBase->mpMapHandler->GetLlamaController();
    cLuxLlamaControllerTestAdapter::Submit(controller,apOwner);
    Require(apOwner->mModelObservation.mlFrameId==submittedFrame && apOwner->IsModelActionActive() && !controller->IsBusy(),
        "planning cannot replace an unfinished move even when a newer image is available");
    for(int i=0; i<30 && apOwner->IsModelActionActive(); ++i)
    {
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
    }
    Require(!apOwner->IsModelActionActive() && apOwner->mfModelDistanceMoved>0.05f &&
        apOwner->GetLastActionFeedback().find("move completed")==0 && apOwner->mvModelActionHistory.size()==1,
        "actual physics movement finishes with a measured nonzero horizontal distance");
    const tString completed=apOwner->GetLastActionFeedback();
    apOwner->SnapshotModelActionFeedback();
    apOwner->StopModelAction("discarded stale reply");
    apOwner->RecordRejectedModelDecision("discarded stale reply");
    Require(apOwner->GetLastActionFeedback()==completed && apOwner->GetModelContextSummary().find(completed)!=tString::npos &&
        apOwner->GetModelContextSummary().find("Decision not applied: discarded stale reply")!=tString::npos &&
        apOwner->mvModelActionHistory.size()==2,
        "rejected replies preserve completed movement evidence and are recorded as unapplied decisions");

    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"fractional-input fixture starts from a current observation");
    Require(ParseLuxLlamaDecision(Decision("move","none",0.5f,0,1),decision,error),"parse fractional acceleration input fixture");
    apOwner->ApplyModelDecision(decision);
    for(int i=0; i<70 && apOwner->IsModelActionActive(); ++i)
    {
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
    }
    Require(!apOwner->IsModelActionActive() && apOwner->mfModelDistanceMoved>0.5f &&
        apOwner->GetLastActionFeedback().find("forward_input=0.50")!=tString::npos &&
        apOwner->GetLastActionFeedback().find("nominal_travel_budget=1.00")!=tString::npos,
        "fractional character input scales acceleration while feedback keeps the actual authored speed-limit budget");

    apOwner->mpCharBody->SetPosition(cVector3f(10,2,0));
    apOwner->mpCharBody->SetYaw(0);
    iPhysicsBody* wall=apMap->GetPhysicsWorld()->CreateBody("FeedbackWall",
        apMap->GetPhysicsWorld()->CreateBoxShape(cVector3f(4,4,0.3f),NULL));
    wall->SetPosition(cVector3f(10,2,-0.58f));
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"blocked-action fixture uses a fresh submitted image");
    Require(ParseLuxLlamaDecision(Decision("move","none",1,0,0.4f),decision,error),"parse fully blocked move fixture");
    apOwner->ApplyModelDecision(decision);
    for(int i=0; i<30 && apOwner->IsModelActionActive(); ++i)
    {
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
    }
    Require(!apOwner->IsModelActionActive() && apOwner->mfModelDistanceMoved<0.05f &&
        apOwner->GetLastActionFeedback().find("blocked/no movement progress")!=tString::npos,
        "a real colliding wall produces completed blocked-movement evidence for the next decision");
    apOwner->mpCharBody->SetPosition(cVector3f(10,2,0));
    wall->SetPosition(cVector3f(10,2,-0.8f));
    Require(ParseLuxLlamaDecision(Decision("move","none",1,0,1),decision,error),"parse partly blocked move fixture");
    apOwner->ApplyModelDecision(decision);
    for(int i=0; i<70 && apOwner->IsModelActionActive(); ++i)
    {
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
    }
    Require(!apOwner->IsModelActionActive() && apOwner->mfModelDistanceMoved>0.05f &&
        apOwner->GetLastActionFeedback().find("limited movement progress")!=tString::npos,
        "movement that makes partial progress before a wall reports its measured distance and limited result");
    wall->SetActive(false);
    apMap->GetPhysicsWorld()->DestroyBody(wall);

    apOwner->mpCharBody->SetYaw(0);
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"turn-feedback fixture uses current camera heading");
    Require(ParseLuxLlamaDecision(Decision("turn","none",0,90,0.1f),decision,error),"parse short turn fixture");
    apOwner->ApplyModelDecision(decision);
    apOwner->OnUpdate(1.0f/60);
    apOwner->OnUpdate(0.1f);
    Require(!apOwner->IsModelActionActive() && apOwner->GetLastActionFeedback().find("turn incomplete")!=tString::npos &&
        apOwner->GetLastActionFeedback().find("remaining_turn=")!=tString::npos,
        "a duration-limited turn reports achieved rotation and remaining angle rather than false success");
    apOwner->mpCharBody->SetYaw(0);
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"successful-turn fixture starts from a current observation");
    Require(ParseLuxLlamaDecision(Decision("turn","none",0,20,1),decision,error),"parse attainable turn fixture");
    apOwner->ApplyModelDecision(decision);
    for(int i=0; i<70 && apOwner->IsModelActionActive(); ++i) apOwner->OnUpdate(1.0f/60);
    Require(!apOwner->IsModelActionActive() && apOwner->GetLastActionFeedback().find("turn achieved")!=tString::npos,
        "a completed attainable turn reports measured achievement");

    const float previousObservationInterval=apOwner->mfObservationInterval;
    apOwner->mfObservationInterval=5;
    apOwner->mpCharBody->SetYaw(0);
    apOwner->mpMover->TurnToAngle(0);
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"post-turn freshness fixture starts from an actual submitted frame");
    const unsigned int beforeTurnFrame=apOwner->mModelObservation.mlFrameId;
    Require(ParseLuxLlamaDecision(Decision("turn","none",0,20,1),decision,error),"parse post-turn freshness fixture");
    apOwner->ApplyModelDecision(decision);
    for(int i=0;i<2;++i)
    {
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
    }
    Capture(apOwner);
    const unsigned int duringTurnFrame=apOwner->GetObservationFrameId();
    const float duringTurnYaw=apOwner->mfObservationYaw;
    Require(apOwner->IsModelActionActive() && duringTurnFrame!=beforeTurnFrame,
        "a newer camera capture can still precede the end of a tracked turn");
    for(int i=0;i<70 && apOwner->IsModelActionActive();++i)
    {
        apOwner->OnUpdate(1.0f/60);
        apMap->GetPhysicsWorld()->Update(1.0f/60);
    }
    Require(!apOwner->IsModelActionActive() && apOwner->GetLastActionFeedback().find("turn achieved")!=tString::npos &&
        std::fabs(cMath::GetAngleDistanceRad(duringTurnYaw,apOwner->mpCharBody->GetYaw()))>cMath::ToRad(1),
        "the real mover finishes at a different heading from its last in-action capture");
    cLuxLlamaControllerTestAdapter::Submit(controller,apOwner);
    Require(apOwner->GetObservationFrameId()==duringTurnFrame && apOwner->mModelObservation.mlFrameId==beforeTurnFrame &&
        !controller->IsBusy(),"planning rejects a newer frame captured before the previous turn finished");
    apOwner->CaptureObservation(); // Completion should make this due even with the authored five-second interval.
    Require(apOwner->GetObservationFrameId()!=duringTurnFrame && Near(apOwner->mfObservationYaw,apOwner->mpCharBody->GetYaw()),
        "turn completion schedules an immediate camera capture at the resulting heading");
    cLuxLlamaControllerTestAdapter::Submit(controller,apOwner);
    Require(apOwner->mModelObservation.mlFrameId==apOwner->GetObservationFrameId() &&
        Near(apOwner->mModelObservation.mfYaw,apOwner->mpCharBody->GetYaw()) && !controller->IsBusy(),
        "a post-turn frame becomes the next submitted snapshot even while inference is unloaded");
    apOwner->mfObservationInterval=previousObservationInterval;

    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"interrupted action starts with a current perception snapshot");
    Require(ParseLuxLlamaDecision(Decision("move","none",1,0,1),decision,error),"parse interruptible move fixture");
    apOwner->ApplyModelDecision(decision);
    apOwner->OnUpdate(0.1f);
    apOwner->StopModelAction("test interruption");
    Require(!apOwner->IsModelActionActive() && apOwner->GetLastActionFeedback().find("interrupted")!=tString::npos &&
        apOwner->GetModelContextSummary().find("reason=test interruption")!=tString::npos,
        "interruptions record actual partial progress and their reason before stopping motion");
    const tString interrupted=apOwner->GetLastActionFeedback();
    for(int i=0; i<10; ++i) apOwner->RecordRejectedModelDecision("bounded history "+cString::ToString(i));
    Require(apOwner->mvModelActionHistory.size()==8 && apOwner->GetModelContextSummary().size()<4608 &&
        apOwner->GetModelContextSummary().find("bounded history 0")==tString::npos &&
        apOwner->GetModelContextSummary().find("bounded history 9")!=tString::npos &&
        apOwner->GetLastActionFeedback()==interrupted,
        "engine factual memory is bounded to eight newest outcomes without fabricating action success");
    Require(apOwner->GetModelContextSummary().find("visible player-mask pixels=")!=tString::npos &&
        apOwner->GetModelContextSummary().find("Your remembered evidence")==tString::npos &&
        apOwner->GetModelContextSummary().find("visible evidence")==tString::npos,
        "context-compaction summary contains submitted perception facts separately from model-written memory");
    apOwner->SetDebugOverride(true);
    Require(apOwner->mvModelActionHistory.empty() && apOwner->mModelObservation.mlFrameId==0 && !apOwner->IsModelActionActive(),
        "manual override starts a fresh factual session and cancels prior action context");
    apOwner->SetDebugOverride(false);
    apOwner->RecordRejectedModelDecision("old save session");
    iLuxEntity_SaveData* saved=apOwner->CreateSaveData();
    apOwner->SaveToSaveData(saved);
    apOwner->SetupSaveData(saved);
    hplDelete(saved);
    Require(apOwner->mvModelActionHistory.empty() && apOwner->GetLastActionFeedback()=="No previous action",
        "save setup does not retain unfinished action or stale context history");
    apOwner->RecordRejectedModelDecision("old owner session");
    apOwner->SetDisabled(true);
    Require(apOwner->mvModelActionHistory.empty(),"owner unavailability clears factual memory before another entity can take control");
    apOwner->SetDisabled(false);
    apOwner->mpCharBody->SetGravityActive(true);
    apOwner->mpCharBody->SetYaw(0);
    controller->SetEnabled(false);
}

static void CheckFrozenDebugPreview(const cLuxLlamaObservation& aImage)
{
    cLuxDebugHandler* debug=gpBase->mpDebugHandler;
    cGraphics* graphics=gpBase->mpEngine->GetGraphics();
    iLowLevelGraphics* lowLevel=graphics->GetLowLevel();
    cGui* gui=gpBase->mpEngine->GetGui();
    debug->mbShowLlamaObservation=true;
    gpBase->mpConfigHandler->mbLoadDebugMenu=true;
    debug->OnDraw(1.0f/60);
    gpBase->mpGameDebugSet->ClearRenderObjects();
    Require(debug->mpLlamaDiagnosticTexture && debug->mpLlamaObservationGfx &&
        debug->mpLlamaObservationGfx->GetTexture(0)==debug->mpLlamaDiagnosticTexture,
        "production debug panel uses its own immutable diagnostic texture after later live captures");
    iTexture* target=graphics->CreateTexture("FrozenPreviewTest",eTextureType_2D,eTextureUsage_RenderTarget);
    Require(target && target->CreateFromRawData(cVector3l(aImage.mvSize.x,aImage.mvSize.y,1),ePixelFormat_RGBA,NULL),
        "allocate framebuffer for frozen diagnostic GUI verification");
    iFrameBuffer* buffer=graphics->CreateFrameBuffer("FrozenPreviewTest");
    buffer->SetTexture2D(0,target);
    Require(buffer->CompileAndValidate(),"compile frozen diagnostic GUI framebuffer");
    cGuiSet* set=gui->CreateSet("FrozenPreviewTest",NULL);
    set->SetVirtualSize(cVector2f(float(aImage.mvSize.x),float(aImage.mvSize.y)),-100,100);
    iFrameBuffer* previous=lowLevel->GetCurrentFrameBuffer();
    lowLevel->PushMatrix(eMatrix_Projection);
    lowLevel->PushMatrix(eMatrix_ModelView);
    lowLevel->SetCurrentFrameBuffer(buffer);
    lowLevel->SetColorWriteActive(true,true,true,true);
    lowLevel->SetScissorActive(false);
    lowLevel->SetClearColor(cColor(0,1));
    lowLevel->ClearFrameBuffer(eClearFrameBufferFlag_Color);
    set->DrawGfx(debug->mpLlamaObservationGfx,0,cVector2f(float(aImage.mvSize.x),float(aImage.mvSize.y)));
    set->Render(NULL);
    cBitmap* bitmap=lowLevel->CopyFrameBufferToBitmap(cVector2l(0),aImage.mvSize);
    Require(bitmap && bitmap->GetPixelFormat()==ePixelFormat_RGBA && bitmap->GetData(0,0),
        "read the rendered production frozen diagnostic preview");
    const unsigned char* pixels=bitmap->GetData(0,0)->mpData;
    bool exact=true;
    for(int y=0;y<aImage.mvSize.y;++y)
    for(int x=0;x<aImage.mvSize.x;++x)
    for(int channel=0;channel<3;++channel)
    {
        const size_t displayed=(size_t(aImage.mvSize.y-1-y)*aImage.mvSize.x+x)*4+channel;
        const size_t submitted=(size_t(y)*aImage.mvSize.x+x)*3+channel;
        exact=exact && std::abs(int(pixels[displayed])-int(aImage.mvRGB[submitted]))<=1;
    }
    hplDelete(bitmap);
    Require(exact,"production frozen preview displays exact submitted RGB upright through the real GUI");
    lowLevel->SetTexture(0,NULL);
    lowLevel->SetBlendActive(false);
    lowLevel->SetDepthTestActive(true);
    lowLevel->SetDepthWriteActive(true);
    lowLevel->PopMatrix(eMatrix_ModelView);
    lowLevel->PopMatrix(eMatrix_Projection);
    lowLevel->SetCurrentFrameBuffer(previous);
    gui->DestroySet(set);
    graphics->DestroyFrameBuffer(buffer);
    graphics->DestroyTexture(target);
    debug->mbShowLlamaObservation=false;
}

static void CheckPerceptionLifecycle(cLuxMap* apMap,cLuxEnemy_Llama* apOwner,cLuxEnemy_Llama* apExtra)
{
    cLuxLlamaController* controller=gpBase->mpMapHandler->GetLlamaController();
    apOwner->SetDebugOverride(false);
    apOwner->SetObservationEnabled(true);
    apOwner->mpCharBody->SetYaw(0);
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"perception lifecycle fixture has a real camera snapshot");
    const cLuxLlamaObservation image=apOwner->mModelObservation;
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,Decision("move","none",1));
    Require(apOwner->IsModelActionActive(),"perception-mode transition starts from a real pending movement action");
    const tString previousActionReply=apOwner->GetLastModelReply();
    controller->SetPerceptionOnly(true);
    Require(controller->IsPerceptionOnly() && !controller->IsBusy() && !apOwner->IsModelActionActive() &&
        apOwner->mvModelActionHistory.empty(),"entering perception-only mode clears action history and stops autonomous movement");
    cLuxLlamaControllerTestAdapter::PerceptionPending(controller,apOwner,image);
    const unsigned long long generation=cLuxLlamaControllerTestAdapter::Generation(controller);
    const cVector3f previousPlayer=gpBase->mpPlayer->mpCharBody->GetPosition();
    gpBase->mpPlayer->mpCharBody->SetPosition(previousPlayer+cVector3f(1,0,0));
    Capture(apOwner);
    Require(controller->GetPerceptionFrame().mlFrameId==image.mlFrameId &&
        controller->GetPerceptionFrame().mImage.mvRGB==image.mvRGB &&
        apOwner->GetObservationFrameId()!=image.mlFrameId,"later camera frames leave the pending perception image immutable");
    cSceneObservation oddCapture(gpBase->mpEngine->GetGraphics());
    Require(oddCapture.Initialize(cVector2l(65,64)) &&
        oddCapture.Capture(apMap->GetWorld(),apOwner->mpObservationCamera,apOwner->mpMeshEntity,
            gpBase->mpPlayer->mpCharBody,std::vector<cMeshEntity*>()),
        "capture real odd-width packed RGB to exercise diagnostic texture row alignment");
    cLuxLlamaObservation oddImage=image;
    oddImage.mvSize=cVector2l(65,64);
    oddImage.mvRGB=oddCapture.GetRGBPixels();
    cLuxLlamaControllerTestAdapter::PerceptionPending(controller,apOwner,oddImage,102);
    CheckFrozenDebugPreview(oddImage);
    cLuxLlamaControllerTestAdapter::PerceptionPending(controller,apOwner,image);
    cLuxLlamaControllerTestAdapter::ExportPerception(controller);
    cBitmap* exported=gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler()->LoadBitmap(
        controller->GetPerceptionFrame().msExportPath+_W(".png"),0);
    Require(exported && exported->GetWidth()==image.mvSize.x && exported->GetHeight()==image.mvSize.y &&
        (exported->GetPixelFormat()==ePixelFormat_RGB || exported->GetPixelFormat()==ePixelFormat_RGBA),
        "production diagnostic exports a reloadable PNG at its frozen capture dimensions");
    const unsigned char* pixels=exported->GetData(0,0)->mpData;
    const size_t stride=exported->GetBytesPerPixel();
    bool exact=true;
    for(size_t i=0;i<image.mvRGB.size()/3;++i)
        exact=exact && pixels[i*stride]==image.mvRGB[i*3] && pixels[i*stride+1]==image.mvRGB[i*3+1] &&
            pixels[i*stride+2]==image.mvRGB[i*3+2];
    hplDelete(exported);
    Require(exact,"production-exported PNG matches frozen top-down RGB pixel for pixel without vertical inversion");
    apOwner->SetDebugOverride(true);
    Require(controller->IsBusy() && cLuxLlamaControllerTestAdapter::Generation(controller)==generation &&
        controller->GetPerceptionFrame().mImage.mvRGB==image.mvRGB,
        "opening manual F1 controls preserves the frozen perception diagnostic and its generation");
    const std::string actionReply=Decision("move","none",1);
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,actionReply);
    Require(controller->GetPerceptionFrame().msReply==actionReply && !controller->IsBusy() &&
        !apOwner->IsModelActionActive() && apOwner->GetLastModelReply()==previousActionReply && apOwner->mvModelActionHistory.empty(),
        "perception results remain diagnostic text even when their contents resemble an executable action");
    apOwner->SetDebugOverride(false);
    Require(controller->GetPerceptionFrame().msReply==actionReply &&
        cLuxLlamaControllerTestAdapter::Generation(controller)==generation,
        "closing manual F1 controls retains the completed frozen perception report");
    cLuxLlamaControllerTestAdapter::PerceptionPending(controller,apOwner,image);
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,"old perception",0.1f,true);
    Require(controller->GetPerceptionFrame().msReply.empty() && !apOwner->IsModelActionActive(),
        "cancelled generations cannot publish a perception answer into a replacement diagnostic");
    cLuxLlamaControllerTestAdapter::PerceptionPending(controller,apOwner,image);
    apOwner->SetDisabled(true);
    Require(!controller->IsBusy() && controller->GetPerceptionFrame().mImage.mvRGB.empty() &&
        cLuxEnemy_Llama::GetControllerOwner(apMap)==apExtra,
        "owner handoff cancels the diagnostic and releases its old frozen frame");
    apOwner->SetDisabled(false);
    gpBase->mpPlayer->mpCharBody->SetPosition(previousPlayer);
    controller->SetPerceptionOnly(false);
    controller->SetEnabled(false);
    Require(!controller->IsPerceptionOnly() && !controller->IsBusy() &&
        controller->GetPerceptionFrame().mImage.mvRGB.empty(),"leaving diagnostic mode restores a clean autonomous session boundary");
}

static void CheckDecisionExplanation(cLuxMap* apMap,cLuxEnemy_Llama* apOwner,cLuxEnemy_Llama* apExtra)
{
    cLuxLlamaController* controller=gpBase->mpMapHandler->GetLlamaController();
    controller->Invalidate();
    controller->SetPerceptionOnly(false);
    controller->SetEnabled(false);
    apOwner->SetDebugOverride(false);
    apOwner->ResetModelAction();
    apOwner->SetObservationEnabled(true);
    apOwner->mpCharBody->SetYaw(0);
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"explanation fixture has an exact autonomous camera snapshot");
    const cLuxLlamaObservation image=apOwner->mModelObservation;
    Require(!controller->RequestDecisionExplanation() && !controller->IsPerceptionOnly() &&
        !apOwner->IsModelActionActive() && controller->GetStatus().find("No completed autonomous decision")!=tString::npos,
        "an explanation without a completed decision fails clearly without entering diagnostic mode");

    cLlamaRequest request;
    request.msSystemPrompt="Original enemy instructions\nA quoted \"objective\" stays historical data.";
    request.msPrompt="Recorded old frame input\nPrevious behavior=patrol; sound bearing=45 degrees.";
    request.msContextSummary="Recorded engine summary\nmove failed: blocked/no movement progress";
    request.mlSessionId=51;
    request.msGrammar=GetLuxLlamaDecisionGrammar();
    request.mImage.mlWidth=image.mvSize.x;
    request.mImage.mlHeight=image.mvSize.y;
    request.mImage.mvRGB=image.mvRGB;
    const std::string raw=Decision("wait");
    cLuxLlamaControllerTestAdapter::AutonomousRecord(controller,apOwner,image,request);
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,raw);
    cLuxLlamaDecisionRecord saved=cLuxLlamaControllerTestAdapter::LastDecision(controller);
    Require(saved.mpOwner==apOwner && saved.mpMap==apMap && saved.mlRequestId==101 &&
        saved.mlFrameId==image.mlFrameId && saved.mRequest.mImage.mvRGB==image.mvRGB && saved.msReply==raw &&
        saved.msEngineStatus=="Decision accepted for mechanical execution" && !cLuxLlamaControllerTestAdapter::HasPendingDecision(controller),
        "a matching autonomous result retains its exact request, frame, raw reply and engine application status");

    apOwner->SetDebugOverride(true);
    controller->Update(0); // F1 suppresses autonomous control before any model load.
    controller->SetEnabled(false);
    controller->Update(0);
    controller->SetEnabled(true);
    controller->Update(0); // Manual override remains active, so this also cannot load a model.
    saved=cLuxLlamaControllerTestAdapter::LastDecision(controller);
    Require(saved.mlRequestId==101 && saved.mRequest.msPrompt==request.msPrompt && saved.msReply==raw &&
        saved.msActionFeedback.find("interrupted")!=tString::npos,
        "F1 and control suppression retain completed decision evidence and actual interrupted-action feedback");
    apOwner->SetDebugOverride(false);
    controller->SetEnabled(false);
    const cVector3f previousPlayer=gpBase->mpPlayer->mpCharBody->GetPosition();
    gpBase->mpPlayer->mpCharBody->SetPosition(previousPlayer+cVector3f(1,0,0));
    Capture(apOwner);
    Require(apOwner->GetObservationFrameId()!=image.mlFrameId,
        "explanation fixture advances the live camera beyond the recorded decision frame");
    const tString previousActionReply=apOwner->GetLastModelReply();
    Require(controller->RequestDecisionExplanation() && controller->IsPerceptionOnly() &&
        !apOwner->IsModelActionActive(),"requesting a decision explanation enters diagnostic mode and stops commands");
    const cLuxLlamaPerceptionFrame frame=controller->GetPerceptionFrame();
    Require(frame.mbDecisionExplanation && frame.mlFrameId==image.mlFrameId && Near(frame.mfTime,image.mfTime) &&
        frame.mImage.mlWidth==image.mvSize.x && frame.mImage.mlHeight==image.mvSize.y && frame.mImage.mvRGB==image.mvRGB &&
        frame.msSystemPrompt==GetLuxLlamaDecisionExplanationSystemPrompt(),
        "explanations freeze the historical submitted RGB and frame metadata rather than a later live capture");
    const std::string marker="OBSERVED_DECISION_RECORD:\n";
    const size_t recordStart=frame.msPrompt.find(marker);
    Require(recordStart!=std::string::npos,"explanation question contains a delimited historical decision record");
    const nlohmann::json record=nlohmann::json::parse(frame.msPrompt.substr(recordStart+marker.size()));
    Require(record.at("original_system_prompt")==request.msSystemPrompt &&
        record.at("original_user_prompt")==request.msPrompt &&
        record.at("conversation_context_summary")==request.msContextSummary && record.at("raw_decision_reply")==raw &&
        record.at("engine_application_status").get<std::string>().find(saved.msEngineStatus)!=std::string::npos &&
        record.at("engine_application_status").get<std::string>().find(saved.msActionFeedback)!=std::string::npos,
        "the question preserves original system/user inputs, summary, raw reply and real engine feedback as data");

    cLuxLlamaControllerTestAdapter::DiagnosticPending(controller);
    const unsigned long long generation=cLuxLlamaControllerTestAdapter::Generation(controller);
    Require(!controller->RequestDecisionExplanation() && controller->IsBusy() &&
        controller->GetPerceptionFrame().msPrompt==frame.msPrompt,
        "a second explanation cannot replace an outstanding diagnostic request");
    apOwner->SetDebugOverride(true);
    Require(controller->IsBusy() && cLuxLlamaControllerTestAdapter::Generation(controller)==generation &&
        controller->GetPerceptionFrame().mImage.mvRGB==image.mvRGB,
        "opening F1 preserves the pending explanation and its exact frozen frame");
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,"cancelled old explanation",0.1f,true);
    Require(controller->GetPerceptionFrame().msReply.empty() && !apOwner->IsModelActionActive(),
        "an explanation from a cancelled generation cannot publish a reply or execute an action");
    cLuxLlamaControllerTestAdapter::DiagnosticPending(controller);
    const std::string actionReply=Decision("move","none",1);
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,actionReply);
    Require(controller->GetPerceptionFrame().msReply==actionReply && !controller->IsBusy() &&
        !apOwner->IsModelActionActive() && apOwner->GetLastModelReply()==previousActionReply &&
        apOwner->mvModelActionHistory.empty() && cLuxLlamaControllerTestAdapter::LastDecision(controller).msReply==raw,
        "action-shaped explanation text cannot drive movement, replace the original reply or enter action history");
    apOwner->SetDebugOverride(false);
    Require(controller->GetPerceptionFrame().mImage.mvRGB==image.mvRGB &&
        controller->GetPerceptionFrame().msReply==actionReply,
        "closing F1 retains the completed decision explanation");
    cLuxLlamaControllerTestAdapter::ExportPerception(controller);
    std::ifstream report(fs::path(controller->GetPerceptionFrame().msExportPath+_W(".txt")),std::ios::binary);
    std::ostringstream reportText;
    reportText << report.rdbuf();
    Require(report.good() && reportText.str().find(frame.msSystemPrompt)!=std::string::npos &&
        reportText.str().find(frame.msPrompt)!=std::string::npos && reportText.str().find(actionReply)!=std::string::npos,
        "production explanation export retains the actual question, original evidence and full diagnostic reply");
    controller->RequestPerceptionDiagnostic();
    Require(!controller->GetPerceptionFrame().mbDecisionExplanation &&
        cLuxLlamaControllerTestAdapter::LastDecision(controller).msReply==raw,
        "inspecting a fresh perception frame leaves the completed autonomous decision available for later explanation");
    controller->SetPerceptionOnly(false);
    controller->SetEnabled(false);
    Require(cLuxLlamaControllerTestAdapter::LastDecision(controller).mlRequestId==101,
        "leaving diagnostic mode retains completed decision evidence while resetting its transient frame");

    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"a rejected-decision fixture has a current submitted frame");
    cLlamaRequest rejectedRequest=request;
    const cLuxLlamaObservation rejectedImage=apOwner->mModelObservation;
    rejectedRequest.mImage.mvRGB=rejectedImage.mvRGB;
    const std::string rejectedReply="not an action JSON\n"+std::string(2600,'x')+"\noriginal reply tail";
    cLuxLlamaControllerTestAdapter::AutonomousRecord(controller,apOwner,rejectedImage,rejectedRequest,201);
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,rejectedReply,0.1f,false,201);
    Require(cLuxLlamaControllerTestAdapter::LastDecision(controller).msReply==rejectedReply &&
        apOwner->GetLastModelReply().size()==2048 &&
        cLuxLlamaControllerTestAdapter::LastDecision(controller).msEngineStatus.find("Rejected model action")!=tString::npos,
        "a rejected reply retains the full bounded diagnostic record beyond the shorter action-overlay text");
    cLuxLlamaControllerTestAdapter::AutonomousRecord(controller,apOwner,rejectedImage,rejectedRequest,202);
    cLuxLlamaControllerTestAdapter::Reply(controller,apOwner,"cancelled replacement",0.1f,false,202,true);
    Require(cLuxLlamaControllerTestAdapter::LastDecision(controller).mlRequestId==201 &&
        cLuxLlamaControllerTestAdapter::LastDecision(controller).msReply==rejectedReply,
        "a cancelled autonomous request cannot replace the last completed decision available for explanation");
    Require(controller->RequestDecisionExplanation(),"engine-rejected autonomous replies can also be inspected");
    const std::string rejectedPrompt=controller->GetPerceptionFrame().msPrompt;
    const nlohmann::json rejectedRecord=nlohmann::json::parse(
        rejectedPrompt.substr(rejectedPrompt.find(marker)+marker.size()));
    Require(rejectedRecord.at("raw_decision_reply")==rejectedReply &&
        rejectedRecord.at("engine_application_status").get<std::string>().find("Rejected model action")!=std::string::npos,
        "the explanation question includes the original rejected command and its engine rejection reason");
    controller->SetPerceptionOnly(false);
    controller->SetEnabled(false);

    apOwner->SetDisabled(true);
    Require(cLuxLlamaControllerTestAdapter::LastDecision(controller).mlRequestId==0 &&
        cLuxEnemy_Llama::GetControllerOwner(apMap)==apExtra && !controller->RequestDecisionExplanation() &&
        !controller->IsPerceptionOnly(),"owner handoff discards saved decision evidence and cannot explain it for the replacement owner");
    cLuxLlamaControllerTestAdapter::LastDecision(controller,saved);
    Require(!controller->RequestDecisionExplanation(),"even an injected old record cannot explain an unavailable owner");
    controller->Update(0); // Disabled VLM control prevents loading while ownership is revalidated.
    Require(cLuxLlamaControllerTestAdapter::LastDecision(controller).mlRequestId==0,
        "the update loop rejects stale owner records independently of transient control state");
    apOwner->SetDisabled(false);
    cLuxLlamaControllerTestAdapter::LastDecision(controller,saved);
    apOwner->SetActive(false);
    Require(cLuxLlamaControllerTestAdapter::LastDecision(controller).mlRequestId==0,
        "entity deactivation releases saved autonomous decision evidence");
    apOwner->SetActive(true);
    cLuxLlamaControllerTestAdapter::LastDecision(controller,saved);
    gpBase->mpMapHandler->mpCurrentMap=NULL;
    Require(!controller->RequestDecisionExplanation(),"a decision from a different current world cannot be explained");
    controller->Update(0);
    Require(cLuxLlamaControllerTestAdapter::LastDecision(controller).mlRequestId==0,
        "world transitions discard historical decision pointers before they can be reused");
    gpBase->mpMapHandler->mpCurrentMap=apMap;
    gpBase->mpPlayer->mpCharBody->SetPosition(previousPlayer);
    controller->Invalidate();
    controller->SetEnabled(false);
}

static void CheckModelAttacks(cLuxMap* apMap,cLuxEnemy_Llama* apOwner)
{
    apOwner->SetDebugOverride(false);
    apOwner->SetObservationEnabled(true);
    apOwner->mpCharBody->SetPosition(cVector3f(10,2,0));
    apOwner->mpCharBody->SetYaw(0);
    apOwner->mvCameraOffset=cVector3f(0,-0.1f,0);
    apOwner->mfNormalAttackDistance=1.9f;
    apOwner->mNormalAttackSize.mlShapeIdx=int(apOwner->mvAttackShapes.size());
    apOwner->mvAttackShapes.push_back(apMap->GetPhysicsWorld()->CreateBoxShape(cVector3f(1,1.6f,1.75f),NULL));
    apOwner->mNormalAttackSize.mvOffset=cVector3f(0,0,1.2f);
    apOwner->mNormalAttackDamage.mfMinDamage=apOwner->mNormalAttackDamage.mfMaxDamage=10;
    apOwner->mNormalAttackDamage.mlStrength=1;
    apOwner->mBreakDoorAttackDamage=apOwner->mNormalAttackDamage;
    apOwner->mBreakDoorAttackDamage.mfMinDamage=apOwner->mBreakDoorAttackDamage.mfMaxDamage=30;
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-1.5f));
    cVector2l point=SnapshotPoint(apOwner,255,0,255);
    tString error;
    Require(!apOwner->BeginModelAttack(eLuxLlamaTarget_Player,0,0,error),"attack without a visible mask at its requested pixel is rejected");
    const cLuxLlamaObservation nearImage=apOwner->mModelObservation;
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-5));
    point=SnapshotPoint(apOwner,255,0,255);
    Require(!apOwner->BeginModelAttack(eLuxLlamaTarget_Player,point.x,point.y,error),"visible targets outside configured melee reach cannot be damaged");
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-1.5f));
    apOwner->mModelObservation=nearImage;
    iPhysicsBody* pWall=apMap->GetPhysicsWorld()->CreateBody("AttackWall",apMap->GetPhysicsWorld()->CreateBoxShape(cVector3f(2,3,0.2f),NULL));
    pWall->SetPosition(cVector3f(10,2,-0.8f));
    point=SnapshotPoint(apOwner,255,0,255); // Physics-only wall keeps old visible pixels, exercising physical grounding.
    Require(!apOwner->BeginModelAttack(eLuxLlamaTarget_Player,point.x,point.y,error),"physics ray rejects a masked player behind a solid obstruction");
    pWall->SetActive(false);
    point=SnapshotPoint(apOwner,255,0,255);
    Require(apOwner->BeginModelAttack(eLuxLlamaTarget_Player,point.x,point.y,error),"visible reachable player starts one mechanical swing with a missing-clip timed fallback");
    Require(!apOwner->BeginModelAttack(eLuxLlamaTarget_Player,point.x,point.y,error),"another request cannot interrupt an active swing");
    const float health=gpBase->mpPlayer->GetHealth();
    apOwner->UpdateModelAttack(0.2f);
    Require(Near(gpBase->mpPlayer->GetHealth(),health),"attack damage waits for its configured impact time");
    apOwner->UpdateModelAttack(0.2f);
    Require(Near(gpBase->mpPlayer->GetHealth(),health-10),"grounded impact uses the shared melee volume and configured player damage");
    apOwner->UpdateModelAttack(0.45f);
    Require(Near(gpBase->mpPlayer->GetHealth(),health-10) && !apOwner->IsModelAttacking(),"one swing applies damage only once and completes within its bounded fallback duration");
    cLuxLlamaDecision attack;
    std::string parseError;
    Require(ParseLuxLlamaDecision(Decision("attack","player",0,0,0.2f,point.x,point.y),attack,parseError),"parse grounded attack protocol fixture");
    apOwner->ApplyModelDecision(attack);
    Require(!apOwner->IsModelAttacking() && apOwner->mfModelAttackCooldown>0,"a replacement model decision preserves the attack cooldown");
    apOwner->UpdateModelAttack(0.2f);
    apOwner->ApplyModelDecision(attack);
    Require(apOwner->IsModelAttacking(),"a new grounded swing is accepted after the configured cooldown expires");
    pWall->SetActive(true);
    apOwner->UpdateModelAttack(0.4f);
    Require(Near(gpBase->mpPlayer->GetHealth(),health-10) && apOwner->GetLastActionFeedback().find("obstructed")!=tString::npos,
        "an obstruction introduced during windup prevents damage at impact");
    pWall->SetActive(false);
    apOwner->ResetModelAction();
    Require(!apOwner->IsModelAttacking() && !apOwner->mpMover->GetOverideMoveState(),"lifecycle reset cancels an unfinished attack and releases locomotion override");
    point=SnapshotPoint(apOwner,255,0,255);
    Require(apOwner->BeginModelAttack(eLuxLlamaTarget_Player,point.x,point.y,error),"fresh grounded swing starts for impact facing-arc validation");
    apOwner->mpCharBody->SetYaw(cMath::ToRad(90));
    apOwner->UpdateModelAttack(0.4f);
    Require(Near(gpBase->mpPlayer->GetHealth(),health-10),"turning away during windup prevents an impact outside the live facing arc");
    apOwner->ResetModelAction();
    apOwner->mpCharBody->SetYaw(cMath::ToRad(90));
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(8.5f,2,0));
    point=SnapshotPoint(apOwner,255,0,255);
    Require(apOwner->BeginModelAttack(eLuxLlamaTarget_Player,point.x,point.y,error),"a fresh observation immediately after a forced turn grounds the target without waiting for physics");
    apOwner->UpdateModelAttack(0.4f);
    Require(Near(gpBase->mpPlayer->GetHealth(),health-20),"shared melee impact uses the current heading even while character direction vectors retain the previous physics heading");
    apOwner->ResetModelAction();
    apOwner->mpCharBody->SetYaw(0);
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-1.5f));
    apOwner->msAttackAnimation="Idle";
    cAnimationState* pClip=apOwner->mpMeshEntity->GetAnimationStateFromName("Idle");
    pClip->SetActive(true); pClip->SetLoop(true); pClip->SetTimePosition(0.8f); pClip->SetSpecialEventTime(0.2f);
    point=SnapshotPoint(apOwner,255,0,255);
    Require(apOwner->BeginModelAttack(eLuxLlamaTarget_Player,point.x,point.y,error) && !pClip->IsLooping() &&
        Near(pClip->GetTimePosition(),0) && Near(apOwner->mfModelAttackImpactDelay,0.2f/0.7f),
        "configured attack reusing the current clip restarts nonlooping and honors its event time/base speed");
    iLuxEntity_SaveData* pSaved=apOwner->CreateSaveData();
    apOwner->SaveToSaveData(pSaved);
    cLuxEnemy_Llama* pRestored=Enemy(apMap,"RestoredAttack",80,true);
    pRestored->LoadFromSaveData(pSaved);
    pRestored->SetupSaveData(pSaved);
    Require(!pRestored->IsModelAttacking() && !pRestored->mpMover->GetOverideMoveState(),"a freshly created living enemy does not restore an unfinished attack animation override");
    pRestored->SetDisabled(true);
    pRestored->SetActive(false);
    hplDelete(pSaved);
    apOwner->SetActive(false);
    Require(!apOwner->IsModelAttacking(),"deactivation cancels a swing before it can apply damage");
    apOwner->SetActive(true);
    apOwner->msAttackAnimation.clear();
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-10));
    cAttackTestProp* pObstacle=hplNew(cAttackTestProp,("AttackObstacle",90,apMap));
    PropBody(apMap,pObstacle,cVector3f(10,2,-1.5f));
    point=SnapshotPoint(apOwner,255,255,255);
    Require(apOwner->BeginModelAttack(eLuxLlamaTarget_Obstacle,point.x,point.y,error),"a visible registered dynamic obstacle can be selected through its image point");
    apOwner->UpdateModelAttack(0.4f);
    Require(pObstacle->mlHits==1 && Near(pObstacle->GetHealth(),90),"obstacle impact uses shared prop damage exactly once");
    apOwner->ResetModelAction();
    pObstacle->SetActive(false);
    cLuxProp_SwingDoor* pDoor=hplNew(cLuxProp_SwingDoor,("AttackDoor",91,apMap));
    PropBody(apMap,pDoor,cVector3f(10,2,-1.5f));
    pDoor->mbBreakable=true; pDoor->mbDisableBreakable=false;
    pDoor->mpDamageMeshEntity[0]=pDoor->mpMeshEntity;
    pDoor->mfHealthDamage[0]=pDoor->mfHealthDamage[1]=0;
    pDoor->mfHealth=20;
    point=SnapshotPoint(apOwner,0,255,255);
    Require(!apOwner->BeginModelAttack(eLuxLlamaTarget_Obstacle,point.x,point.y,error),"obstacle requests cannot bypass a door's explicit breakable contract");
    Require(apOwner->BeginModelAttack(eLuxLlamaTarget_Door,point.x,point.y,error),"cyan mask and matching physical breakable door authorize a door strike");
    apOwner->UpdateModelAttack(0.4f);
    Require(pDoor->IsBroken() && !pDoor->GetBody(0)->IsActive(),"door strike uses configured break damage and the stock door destruction mechanics");
    apOwner->ResetModelAction();
    pDoor->SetActive(false);
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-1.5f));
    point=SnapshotPoint(apOwner,255,0,255);
    Require(ParseLuxLlamaDecision(Decision("attack","player",0,0,0.2f,point.x,point.y),attack,parseError),
        "parse final tracked attack-history fixture");
    apOwner->ApplyModelDecision(attack);
    Require(apOwner->IsModelActionActive(),"tracked attack remains active for its mechanical impact and animation");
    apOwner->UpdateEnemySpecific(0.4f);
    const tString impactFeedback=apOwner->GetLastActionFeedback();
    Require(apOwner->IsModelActionActive() && impactFeedback.find("Attack hit")!=tString::npos,
        "tracked attack retains exact mechanical impact feedback while its animation is still active");
    apOwner->SnapshotModelActionFeedback();
    Require(apOwner->GetLastActionFeedback()==impactFeedback,"progress snapshots do not overwrite a real attack hit result");
    apOwner->UpdateEnemySpecific(0.5f);
    Require(!apOwner->IsModelActionActive() && apOwner->GetModelContextSummary().find(impactFeedback)!=tString::npos &&
        apOwner->mvModelActionHistory.size()==1,"completed tracked attack records its real hit outcome once in factual history");
    apOwner->ResetModelAction(false);
    Require(apOwner->mvModelActionHistory.size()==1,"ordinary idle resets do not duplicate terminal attack history");
    apOwner->ResetModelAction();
    apMap->GetPhysicsWorld()->DestroyBody(pWall);
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-6));
}

static void CheckLiveModel(cLuxEnemy_Llama* apOwner,const std::string& asModel,const std::string& asProjector)
{
    gpBase->mpGameCfg->SetString("Llama","ModelPath",asModel);
    gpBase->mpGameCfg->SetString("Llama","ProjectorPath",asProjector);
    const float configuredAge=gpBase->mpGameCfg->GetFloat("Llama","MaxResultAge",6);
    const float maxResultAge=std::isfinite(configuredAge) ? std::max(0.25f,std::min(configuredAge,600.0f)) : 6.0f;
    const int timeoutSeconds=90+3*int(std::ceil(maxResultAge));
    gpBase->mpGameCfg->SetInt("Llama","MaxTokens",192);
    gpBase->mpGameCfg->SetInt("Llama","ContextSize",16384);
    apOwner->SetDebugOverride(false);
    apOwner->SetObservationEnabled(true);
    apOwner->mpCharBody->SetPosition(cVector3f(10,2,0));
    apOwner->mpCharBody->SetYaw(0);
    apOwner->mpCharBody->SetForceVelocity(cVector3f(0));
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-6));
    gpBase->mpPlayer->mpCharBody->SetForceVelocity(cVector3f(0));
    iPhysicsBody* floor=apOwner->mpMap->GetPhysicsWorld()->CreateBody("LiveModelFloor",
        apOwner->mpMap->GetPhysicsWorld()->CreateBoxShape(cVector3f(40,0.5f,40),NULL));
    floor->SetPosition(cVector3f(10,0.85f,-2));
    apOwner->msLastModelReply.clear();
    apOwner->msLastActionFeedback.clear();
    cLuxLlamaController* pController=gpBase->mpMapHandler->GetLlamaController();
    pController->SetEnabled(true);
    auto previous=std::chrono::steady_clock::now();
    const auto deadline=previous+std::chrono::seconds(timeoutSeconds);
    std::fprintf(gpCheckLog,"LIVE MODEL BUDGET: configured result age=%.2fs, total deadline=%ds, synthetic image=%dx%d\n",
        maxResultAge,timeoutSeconds,apOwner->GetObservationSize().x,apOwner->GetObservationSize().y);
    std::fflush(gpCheckLog);
    bool applied=false,reused=false;
    unsigned acceptedTurns=0;
    cLlamaContextStats context;
    while(std::chrono::steady_clock::now()<deadline)
    {
        const auto now=std::chrono::steady_clock::now();
        const float dt=std::chrono::duration<float>(now-previous).count();
        previous=now;
        pController->Update(dt);
        apOwner->OnUpdate(dt);
        apOwner->mpMap->GetPhysicsWorld()->Update(cMath::Min(dt,0.05f));
        gpBase->mpMapHandler->OnDraw(dt);
        context=pController->GetContextStats();
        reused=reused || context.mlReusedTokens>0;
        if(!apOwner->GetLastModelReply().empty())
        {
            std::string error;
            cLuxLlamaDecision decision;
            applied=ParseLuxLlamaDecision(apOwner->GetLastModelReply(),decision,error) &&
                apOwner->msModelMemory==decision.msMemory;
            if(applied && !context.mbAwaitingResolution)
            {
                if(context.mlTurns>acceptedTurns)
                {
                    std::fprintf(gpCheckLog,"LIVE ACCEPTED TURN %u (latency %.2fs): %s\n",context.mlTurns,
                        pController->GetLastLatency(),apOwner->GetLastModelReply().c_str());
                    std::fflush(gpCheckLog);
                }
                acceptedTurns=std::max(acceptedTurns,context.mlTurns);
            }
            if((acceptedTurns>=3 && !apOwner->IsModelActionActive()) ||
                gpBase->mpEngine->GetLlamaInference()->GetState()==eLlamaState_Failed) break;
        }
        if(gpBase->mpEngine->GetLlamaInference()->GetState()==eLlamaState_Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    std::fprintf(gpCheckLog,"LIVE MODEL STATUS: %s\nLIVE MODEL RESULT AGE LIMIT: %.2fs\nLIVE MODEL REPLY: %s\n",
        pController->GetStatus().c_str(),pController->GetMaxResultAge(),apOwner->GetLastModelReply().c_str());
    std::fprintf(gpCheckLog,"LIVE MODEL CONTEXT: turns=%u, used=%u/%u, text=%u, images=%u, output=%u, reused=%u, refreshes=%u\nLIVE ACTION FEEDBACK: %s\n",
        context.mlTurns,context.mlUsedTokens,context.mlCapacityTokens,context.mlTextTokens,context.mlImageTokens,
        context.mlOutputTokens,context.mlReusedTokens,context.mlRefreshCount,apOwner->GetLastActionFeedback().c_str());
    std::fprintf(gpCheckLog,"LIVE ENGINE FACTUAL HISTORY:\n%s\n",apOwner->GetModelContextSummary().c_str());
    std::fflush(gpCheckLog);
    Require(applied && acceptedTurns>=3,"real local VLM consumes sequential enemy images and three accepted strict replies reach the game action controller");
    Require(reused && context.mlCapacityTokens==16384 && context.mlUsedTokens>0 &&
        context.mlUsedTokens<=context.mlCapacityTokens && context.mlImageTokens>0 && context.mlOutputTokens>0,
        "actual controller conversation retains image/output context and reuses accepted history within its 16K budget");
    Require(apOwner->mvModelActionHistory.size()>=3 && apOwner->GetLastActionFeedback().find("Executing ")!=0,
        "real sequential planning observes completed or mechanically rejected actions rather than repeated unfinished moves");
    pController->SetEnabled(false);
    gpBase->mpEngine->GetLlamaInference()->Unload();
    apOwner->mpMap->GetPhysicsWorld()->DestroyBody(floor);
}

struct cPerceptionMask
{
    cPerceptionMask() : mlCount(0),mlMinX(1000),mlMinY(1000),mlMaxX(0),mlMaxY(0) {}
    int mlCount,mlMinX,mlMinY,mlMaxX,mlMaxY;
};

static cPerceptionMask PerceptionMask(const cLuxLlamaObservation& aImage,
    unsigned char ar,unsigned char ag,unsigned char ab)
{
    cPerceptionMask mask;
    for(int y=0;y<aImage.mvSize.y;++y)
    for(int x=0;x<aImage.mvSize.x;++x)
    {
        const size_t i=(size_t(y)*aImage.mvSize.x+x)*3;
        if(aImage.mvRGB[i]!=ar || aImage.mvRGB[i+1]!=ag || aImage.mvRGB[i+2]!=ab) continue;
        ++mask.mlCount;
        const int nx=int((x+0.5)*1000/aImage.mvSize.x),ny=int((y+0.5)*1000/aImage.mvSize.y);
        mask.mlMinX=std::min(mask.mlMinX,nx); mask.mlMaxX=std::max(mask.mlMaxX,nx);
        mask.mlMinY=std::min(mask.mlMinY,ny); mask.mlMaxY=std::max(mask.mlMaxY,ny);
    }
    return mask;
}

static void ExportPerceptionImage(const fs::path& aPath,const cLuxLlamaObservation& aImage)
{
    std::ofstream file(aPath,std::ios::binary | std::ios::trunc);
    file << "P6\n" << aImage.mvSize.x << " " << aImage.mvSize.y << "\n255\n";
    file.write(reinterpret_cast<const char*>(aImage.mvRGB.data()),std::streamsize(aImage.mvRGB.size()));
    Require(file.good(),"export exact submitted RGB without scaling or recoloring");
}

static std::string PerceptionField(const std::string& asText,const char* asKey)
{
    std::string lower=asText;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    const size_t position=lower.find(std::string(asKey)+":");
    if(position==std::string::npos) return "";
    const size_t start=position+std::string(asKey).size()+1;
    const size_t end=lower.find('\n',start);
    std::string value=lower.substr(start,end==std::string::npos ? end : end-start);
    const size_t first=value.find_first_not_of(" \t\r*`");
    return first==std::string::npos ? "" : value.substr(first);
}

static cMeshEntity* PerceptionBox(cLuxMap* apMap,const char* asName,
    const cVector3f& avSize,const cVector3f& avPosition,const cColor& aColor)
{
    cMeshEntity* mesh=VisibleBox(apMap,asName,avSize,avPosition);
    unsigned char rgb[]={static_cast<unsigned char>(aColor.r*255),static_cast<unsigned char>(aColor.g*255),
        static_cast<unsigned char>(aColor.b*255),255};
    iTexture* texture=gpBase->mpEngine->GetGraphics()->CreateTexture(asName,eTextureType_2D,eTextureUsage_Normal);
    Require(texture->CreateFromRawData(cVector3l(1,1,1),ePixelFormat_RGBA,rgb),"create visible perception scene material");
    cMaterial* material=mesh->GetSubMeshEntity(0)->GetMaterial();
    material->SetAutoDestroyTextures(false); // Graphics owns this directly-created texture.
    material->SetTexture(eMaterialTexture_Diffuse,texture);
    return mesh;
}

static void CheckRealDecisionExplanation(cLuxEnemy_Llama* apOwner,const fs::path& aScratch)
{
    cLuxLlamaController* controller=gpBase->mpMapHandler->GetLlamaController();
    cLlamaInference* inference=gpBase->mpEngine->GetLlamaInference();
    controller->SetEnabled(true);
    apOwner->mfLastObservationAttempt=apOwner->mfElapsedTime-apOwner->mfObservationInterval-0.01f;
    apOwner->CaptureObservation();
    Require(apOwner->SnapshotModelObservation(),"capture the real autonomous decision's full-detail image");
    const cLuxLlamaObservation image=apOwner->mModelObservation;
    const cPerceptionMask player=PerceptionMask(image,255,0,255);
    Require(image.mvSize==cVector2l(1280,864) && player.mlCount>100,
        "the autonomous decision fixture contains a genuinely visible player in a full-detail image");
    ExportPerceptionImage(aScratch/"decision-frame.ppm",image);
    const auto started=std::chrono::steady_clock::now();
    const auto decisionDeadline=started+std::chrono::seconds(600);
    cLuxLlamaDecisionRecord saved;
    while(std::chrono::steady_clock::now()<decisionDeadline)
    {
        controller->Update(1.0f/60);
        saved=cLuxLlamaControllerTestAdapter::LastDecision(controller);
        if(saved.mlRequestId || inference->GetState()==eLlamaState_Failed) break;
        apOwner->UpdateEnemySpecific(1.0f/60);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    const float decisionSeconds=std::chrono::duration<float>(std::chrono::steady_clock::now()-started).count();
    Write(aScratch/"decision-prompt.txt",saved.mRequest.msSystemPrompt+"\n\n"+saved.mRequest.msPrompt+
        "\n\nCONTEXT SUMMARY:\n"+saved.mRequest.msContextSummary);
    Write(aScratch/"decision-reply.txt",saved.msReply);
    const cLlamaContextStats decisionContext=controller->GetContextStats();
    std::fprintf(gpCheckLog,"AUTONOMOUS DECISION (%.2fs): used=%u, images=%u, output=%u\n%s\nENGINE: %s\nFEEDBACK: %s\n",
        decisionSeconds,decisionContext.mlUsedTokens,decisionContext.mlImageTokens,decisionContext.mlOutputTokens,
        saved.msReply.c_str(),saved.msEngineStatus.c_str(),saved.msActionFeedback.c_str());
    std::fflush(gpCheckLog);
    Require(saved.mlRequestId && saved.mpOwner==apOwner && saved.mRequest.mImage.mvRGB==image.mvRGB &&
        saved.mlFrameId==image.mlFrameId && !saved.msReply.empty(),
        "the real controller records an autonomous reply with its exact submitted request and image");
    Require(saved.mRequest.mlSessionId!=0 && !saved.mRequest.msGrammar.empty() &&
        decisionContext.mlImageTokens>=1024 && decisionContext.mlImageTokens<=2048,
        "the autonomous request uses its normal action protocol with the full spatial-grounding image budget");

    // Advance only the live capture; the diagnostic must retain the older
    // decision image and must not leak this newer player position into its input.
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(11.5f,2,-6));
    apOwner->mfLastObservationAttempt=apOwner->mfElapsedTime-apOwner->mfObservationInterval-0.01f;
    apOwner->CaptureObservation();
    Require(apOwner->GetObservationFrameId()!=image.mlFrameId && controller->RequestDecisionExplanation(),
        "the public explanation control accepts the completed decision after a newer live camera frame");
    Require(controller->GetPerceptionFrame().mbDecisionExplanation &&
        controller->GetPerceptionFrame().mlFrameId==saved.mlFrameId &&
        controller->GetPerceptionFrame().mImage.mvRGB==saved.mRequest.mImage.mvRGB && !apOwner->IsModelActionActive(),
        "the production explanation route freezes the original image and stops the actual autonomous command");
    const std::string marker="OBSERVED_DECISION_RECORD:\n";
    const std::string prompt=controller->GetPerceptionFrame().msPrompt;
    const nlohmann::json quoted=nlohmann::json::parse(prompt.substr(prompt.find(marker)+marker.size()));
    Require(quoted.at("original_system_prompt")==saved.mRequest.msSystemPrompt &&
        quoted.at("original_user_prompt")==saved.mRequest.msPrompt &&
        quoted.at("conversation_context_summary")==saved.mRequest.msContextSummary &&
        quoted.at("raw_decision_reply")==saved.msReply,
        "the real explanation question preserves every original input and the actual model command as historical data");
    Write(aScratch/"explanation-prompt.txt",controller->GetPerceptionFrame().msSystemPrompt+"\n\n"+prompt);
    const tString actionReply=apOwner->GetLastModelReply();
    const std::vector<tString> history=apOwner->mvModelActionHistory;
    const auto explanationStarted=std::chrono::steady_clock::now();
    const auto explanationDeadline=explanationStarted+std::chrono::seconds(600);
    while(std::chrono::steady_clock::now()<explanationDeadline)
    {
        controller->Update(1.0f/60);
        const cLuxLlamaPerceptionFrame& frame=controller->GetPerceptionFrame();
        if((frame.mlRequestId && !controller->IsBusy()) || inference->GetState()==eLlamaState_Failed) break;
        apOwner->UpdateEnemySpecific(1.0f/60);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    const cLuxLlamaPerceptionFrame explanation=controller->GetPerceptionFrame();
    const float explanationSeconds=std::chrono::duration<float>(std::chrono::steady_clock::now()-explanationStarted).count();
    Write(aScratch/"explanation-reply.txt",explanation.msReply);
    std::fprintf(gpCheckLog,"DECISION EXPLANATION (%.2fs): session=%llu, used=%u, images=%u, output=%u\n%s\nSTATUS: %s\n",
        explanationSeconds,static_cast<unsigned long long>(explanation.mContextStats.mlSessionId),
        explanation.mContextStats.mlUsedTokens,explanation.mContextStats.mlImageTokens,
        explanation.mContextStats.mlOutputTokens,explanation.msReply.c_str(),controller->GetStatus().c_str());
    std::fprintf(gpCheckLog,"DECISION GROUND TRUTH: player pixels=%d; bbox=%d,%d..%d,%d\n",
        player.mlCount,player.mlMinX,player.mlMinY,player.mlMaxX,player.mlMaxY);
    std::fflush(gpCheckLog);
    Require(!explanation.msReply.empty() && !controller->IsBusy() &&
        controller->GetStatus().find("Explanation complete")==0,
        "the real controller finishes an unconstrained decision explanation");
    cLuxLlamaDecision ignored;
    std::string error;
    Require(explanation.mContextStats.mlSessionId==0 && explanation.mContextStats.mlImageTokens>=1024 &&
        explanation.mContextStats.mlImageTokens<=2048 && !ParseLuxLlamaDecision(explanation.msReply,ignored,error),
        "the explanation uses a fresh full-detail conversation and returns diagnostic prose instead of action JSON");
    Require(!apOwner->IsModelActionActive() && apOwner->GetLastModelReply()==actionReply &&
        apOwner->mvModelActionHistory==history && cLuxLlamaControllerTestAdapter::LastDecision(controller).msReply==saved.msReply,
        "a real model explanation cannot execute actions or contaminate the original command and action history");
    controller->SetPerceptionOnly(false);
    controller->SetEnabled(false);
    inference->Unload();

    // Preserve the diagnostic even if the action still lacks direction: the
    // explanation is useful evidence for that failure and must be exported first.
    cLuxLlamaDecision decision;
    const bool parsed=ParseLuxLlamaDecision(saved.msReply,decision,error);
    const bool pursues=parsed && decision.mTarget==eLuxLlamaTarget_Player &&
        ((decision.mBehavior==eLuxLlamaBehavior_Chase &&
            ((decision.mAction==eLuxLlamaAction_Move && decision.mfForward>0) ||
             (decision.mAction==eLuxLlamaAction_Turn && decision.mfTurnDegrees>0))) ||
         (decision.mBehavior==eLuxLlamaBehavior_Attack && decision.mAction==eLuxLlamaAction_Attack));
    Require(pursues,"the clarified autonomous objective produces a chase or attack command directed at the visible player");
}

static void CheckRealSteering(cLuxEnemy_Llama* apOwner,const fs::path& aScratch)
{
    cLuxLlamaController* controller=gpBase->mpMapHandler->GetLlamaController();
    cLlamaInference* inference=gpBase->mpEngine->GetLlamaInference();
    const char* names[]={"target-right","target-left","target-center"};
    const float targetX[]={12.5f,7.5f,10.0f};
    bool inputs[3]={false,false,false},commands[3]={false,false,false},motion[3]={false,false,false};
    for(int scenario=0;scenario<3;++scenario)
    {
        // Reset factual/model-written memory before the stateless visual query.
        // Only the camera sees the target position; no test coordinates enter a prompt.
        controller->SetEnabled(false);
        controller->Invalidate();
        apOwner->ResetModelAction();
        apOwner->mpCharBody->SetPosition(cVector3f(10,2,0));
        apOwner->mpCharBody->SetYaw(0);
        apOwner->mpCharBody->SetForceVelocity(cVector3f(0));
        apOwner->mpMover->TurnToAngle(0);
        gpBase->mpPlayer->mpCharBody->StopMovement();
        gpBase->mpPlayer->mpCharBody->SetForceVelocity(cVector3f(0));
        gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(targetX[scenario],2,-6));
        controller->SetEnabled(true);
        apOwner->mfLastObservationAttempt=apOwner->mfElapsedTime-apOwner->mfObservationInterval-0.01f;
        apOwner->CaptureObservation();
        Require(apOwner->SnapshotModelObservation(),"capture a fresh full-detail steering frame");
        const cLuxLlamaObservation image=apOwner->mModelObservation;
        const cPerceptionMask player=PerceptionMask(image,255,0,255);
        const std::string name=names[scenario];
        ExportPerceptionImage(aScratch/(name+".ppm"),image);
        const float startYaw=apOwner->mpCharBody->GetYaw();
        const cVector3f startPosition=apOwner->mpCharBody->GetPosition();
        const auto started=std::chrono::steady_clock::now();
        const auto deadline=started+std::chrono::seconds(600);
        cLuxLlamaDecisionRecord saved;
        while(std::chrono::steady_clock::now()<deadline)
        {
            controller->Update(1.0f/60);
            saved=cLuxLlamaControllerTestAdapter::LastDecision(controller);
            if(saved.mlRequestId || inference->GetState()==eLlamaState_Failed) break;
            apOwner->UpdateEnemySpecific(1.0f/60);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        const float seconds=std::chrono::duration<float>(std::chrono::steady_clock::now()-started).count();
        const cLlamaContextStats context=controller->GetContextStats();
        Write(aScratch/(name+"-prompt.txt"),saved.mRequest.msSystemPrompt+"\n\n"+saved.mRequest.msPrompt+
            "\n\nCONTEXT SUMMARY:\n"+saved.mRequest.msContextSummary+"\n\nGRAMMAR:\n"+saved.mRequest.msGrammar);
        Write(aScratch/(name+"-reply.txt"),saved.msReply);
        const eLuxLlamaBehavior appliedBehavior=apOwner->mModelBehavior;
        const eLuxLlamaAction appliedAction=apOwner->mModelAction;
        const float appliedForward=apOwner->mfModelForward;
        const float requestedTurn=apOwner->mfModelActionRequestedTurn;

        // The controller has already applied the real reply. Run its bounded
        // action through the normal enemy mover and actual physics, without
        // scheduling another model request that could replace the evidence.
        for(int tick=0;tick<600 && apOwner->IsModelActionActive();++tick)
        {
            apOwner->OnUpdate(1.0f/60);
            apOwner->mpMap->GetPhysicsWorld()->Update(1.0f/60);
        }
        apOwner->SnapshotModelActionFeedback();
        const float turned=-cMath::ToDeg(cMath::GetAngleDistanceRad(startYaw,apOwner->mpCharBody->GetYaw()));
        cVector3f moved=apOwner->mpCharBody->GetPosition()-startPosition;
        moved.y=0;
        const std::string feedback=apOwner->GetLastActionFeedback();
        cLuxLlamaSteeringDecision decision;
        std::string parseError;
        const bool parsed=ParseLuxLlamaSteeringDecision(saved.msReply,decision,parseError);
        const bool turn=scenario<2;
        const float direction=scenario==0 ? 1.0f : -1.0f;
        const int centerX=(player.mlMinX+player.mlMaxX)/2;
        const bool placement=scenario==0 ? centerX>550 : scenario==1 ? centerX<450 : std::abs(centerX-500)<=30;
        inputs[scenario]=saved.mlRequestId && saved.mpOwner==apOwner &&
            saved.mRequest.mImage.mvRGB==image.mvRGB && saved.mlFrameId==image.mlFrameId &&
            image.mvSize==cVector2l(1280,864) && player.mlCount>100 && placement &&
            saved.mRequest.mlSessionId==0 && saved.mRequest.msSystemPrompt.empty() && saved.mRequest.msContextSummary.empty() &&
            saved.mRequest.msPrompt==GetLuxLlamaSteeringSystemPrompt() && saved.mRequest.msGrammar==GetLuxLlamaSteeringGrammar() &&
            context.mlImageTokens>=1024 && context.mlImageTokens<=2048 && context.mlReusedTokens==0 && context.mlTurns==0;
        const eLuxLlamaSteeringSide expectedSide=scenario==0 ? eLuxLlamaSteeringSide_Right :
            scenario==1 ? eLuxLlamaSteeringSide_Left : eLuxLlamaSteeringSide_Center;
        const eLuxLlamaSteeringAction expectedAction=scenario==0 ? eLuxLlamaSteeringAction_TurnRight :
            scenario==1 ? eLuxLlamaSteeringAction_TurnLeft : eLuxLlamaSteeringAction_MoveForward;
        commands[scenario]=parsed && decision.mbTargetVisible && decision.mSide==expectedSide && decision.mAction==expectedAction &&
            appliedBehavior==eLuxLlamaBehavior_Chase &&
            (turn ? appliedAction==eLuxLlamaAction_Turn && Near(appliedForward,0) &&
                Near(requestedTurn,direction*cMath::ToDeg(apOwner->GetObservationFOV())*0.05f) :
                appliedAction==eLuxLlamaAction_Move && Near(appliedForward,1) && Near(requestedTurn,0));
        motion[scenario]=!apOwner->IsModelActionActive() && saved.msEngineStatus=="Decision accepted for mechanical execution" &&
            (turn ? turned*direction>1 && moved.Length()<0.05f &&
                feedback.find("turn completed")==0 && feedback.find("turn_right=")!=std::string::npos :
                moved.Length()>0.05f && apOwner->mpCharBody->GetPosition().z<startPosition.z &&
                feedback.find("move completed")==0 && feedback.find("horizontal_distance=")!=std::string::npos);
        nlohmann::json report={
            {"scenario",name},{"elapsed_seconds",seconds},{"frame_id",saved.mlFrameId},
            {"request_id",saved.mlRequestId},{"session_id",saved.mRequest.mlSessionId},
            {"image_width",saved.mRequest.mImage.mlWidth},{"image_height",saved.mRequest.mImage.mlHeight},
            {"context_used_tokens",context.mlUsedTokens},{"image_tokens",context.mlImageTokens},
            {"output_tokens",context.mlOutputTokens},{"reused_tokens",context.mlReusedTokens},{"retained_turns",context.mlTurns},
            {"engine_status",saved.msEngineStatus},{"initial_feedback",saved.msActionFeedback},{"completed_feedback",feedback},
            {"applied_behavior",GetLuxLlamaBehaviorName(appliedBehavior)},{"applied_action",GetLuxLlamaActionName(appliedAction)},
            {"applied_forward_input",appliedForward},{"requested_turn_right_degrees",requestedTurn},
            {"achieved_turn_right_degrees",turned},{"horizontal_displacement",moved.Length()},
            {"player_mask_pixels",player.mlCount},{"player_mask_bounds",{player.mlMinX,player.mlMinY,player.mlMaxX,player.mlMaxY}},
            {"inputs_valid",inputs[scenario]},{"expected_command",commands[scenario]},{"expected_motion",motion[scenario]},
            {"parse_error",parseError}
        };
        Write(aScratch/(name+"-report.json"),report.dump(2));
        std::fprintf(gpCheckLog,"STEERING %s (%.2fs): session=%llu, used=%u, images=%u, output=%u, reused=%u\n%s\nENGINE: %s\nFEEDBACK: %s\n",
            name.c_str(),seconds,static_cast<unsigned long long>(saved.mRequest.mlSessionId),context.mlUsedTokens,
            context.mlImageTokens,context.mlOutputTokens,context.mlReusedTokens,saved.msReply.c_str(),saved.msEngineStatus.c_str(),feedback.c_str());
        std::fprintf(gpCheckLog,"STEERING GROUND TRUTH: player pixels=%d; bbox=%d,%d..%d,%d; achieved right turn=%.2fdeg; moved=%.3f\n",
            player.mlCount,player.mlMinX,player.mlMinY,player.mlMaxX,player.mlMaxY,turned,moved.Length());
        std::fflush(gpCheckLog);
    }
    controller->SetEnabled(false);
    inference->Unload();
    // Keep all three requests/replies and actual motor outcomes even if the
    // model fails a semantic expectation in the first comparison.
    for(int scenario=0;scenario<3;++scenario)
    {
        Require(inputs[scenario],"each steering query uses its exact grounded image and a stateless three-field request without history");
        Require(commands[scenario],scenario<2 ? "an off-center visible target produces chase with a signed stationary turn" :
            "a centered visible target produces a forward chase movement");
        Require(motion[scenario],scenario<2 ? "the real model turn rotates the body toward the target and reports measured yaw" :
            "the real centered-target command moves the body forward and reports measured progress");
    }
}

static void CheckPerceptionModel(cLuxEnemy_Llama* apOwner,const std::string& asModel,
    const std::string& asProjector,const fs::path& aScratch,bool abExplainDecision=false,bool abSteeringOnly=false)
{
    cLuxMap* map=apOwner->mpMap;
    cLuxLlamaController* controller=gpBase->mpMapHandler->GetLlamaController();
    controller->SetEnabled(false);
    controller->SetTargetSteeringOnly(abSteeringOnly);
    apOwner->SetObservationEnabled(true);
    apOwner->mpCharBody->SetPosition(cVector3f(10,2,0));
    apOwner->mpCharBody->SetYaw(0);
    apOwner->mpCharBody->SetGravityActive(false);
    gpBase->mpPlayer->mpCharBody->SetGravityActive(false);
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(12.5f,2,-6));
    PerceptionBox(map,"PerceptionFloor",cVector3f(30,0.2f,30),cVector3f(10,1,-6),cColor(0.65f,0.55f,0.35f,1));
    PerceptionBox(map,"PerceptionBackWall",cVector3f(26,6,0.2f),cVector3f(10,3,-12),cColor(0.45f,0.3f,0.15f,1));
    PerceptionBox(map,"PerceptionLeftWall",cVector3f(0.2f,6,12),cVector3f(3,3,-6),cColor(0.75f,0.75f,0.75f,1));
    cMeshEntity* wall=PerceptionBox(map,"PerceptionOccluder",cVector3f(3.5f,5,0.2f),
        cVector3f(12.5f,2.5f,-3.5f),cColor(0.35f,0.35f,0.35f,1));
    wall->SetVisible(false);
    cLuxProp_SwingDoor* door=hplNew(cLuxProp_SwingDoor,("PerceptionBreakableDoor",91,map));
    PropBody(map,door,cVector3f(8,2,-6));
    door->mbBreakable=true; door->mbDisableBreakable=false;
    door->mpDamageMeshEntity[0]=door->mpMeshEntity;
    door->mfHealthDamage[0]=door->mfHealthDamage[1]=0;
    map->GetWorld()->Compile(false);

    gpBase->mpGameCfg->SetString("Llama","ModelPath",asModel);
    gpBase->mpGameCfg->SetString("Llama","ProjectorPath",asProjector);
    cLlamaInference* inference=gpBase->mpEngine->GetLlamaInference();
    const auto loadDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(300);
    bool ready=false;
    while(std::chrono::steady_clock::now()<loadDeadline)
    {
        ready=cLuxLlamaControllerTestAdapter::EnsureLoaded(controller);
        if(ready || inference->GetState()==eLlamaState_Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if(!ready) std::fprintf(stderr,"Perception model load: %s\n",inference->GetLastError().c_str());
    Require(ready,"load real perception model using game-configured image minimum/maximum and offload");
    if(abExplainDecision)
    {
        CheckRealDecisionExplanation(apOwner,aScratch);
        return;
    }
    if(abSteeringOnly)
    {
        CheckRealSteering(apOwner,aScratch);
        return;
    }

    bool visibilityResults[2]={false,false},localization=false,doorsResults[2]={false,false};
    for(int scenario=0;scenario<2;++scenario)
    {
        const std::string name=scenario==0 ? "player-visible" : "player-occluded";
        wall->SetVisible(scenario!=0);
        apOwner->mfLastObservationAttempt=apOwner->mfElapsedTime-apOwner->mfObservationInterval-0.01f;
        apOwner->CaptureObservation();
        Require(apOwner->SnapshotModelObservation(),"capture immutable full-detail perception snapshot");
        const cLuxLlamaObservation image=apOwner->mModelObservation;
        Require(image.mvSize==cVector2l(1280,864) && image.mvRGB.size()==size_t(1280)*864*3,
            "perception test uses exact full-detail game camera payload");
        const cPerceptionMask player=PerceptionMask(image,255,0,255),cyan=PerceptionMask(image,0,255,255);
        Require((scenario==0 ? player.mlCount>100 : player.mlCount==0) && cyan.mlCount>100,
            "visible/occluded fixture ground truth comes only from depth-tested submitted masks");
        ExportPerceptionImage(aScratch/(name+".ppm"),image);

        cLlamaRequest request;
        request.mlSessionId=0;
        request.msSystemPrompt=GetLuxLlamaPerceptionSystemPrompt();
        request.msPrompt=GetLuxLlamaPerceptionPrompt();
        request.mlMaxTokens=512;
        request.mfTemperature=0;
        request.mImage.mlWidth=image.mvSize.x; request.mImage.mlHeight=image.mvSize.y;
        request.mImage.mvRGB=image.mvRGB;
        Write(aScratch/(name+"-prompt.txt"),request.msSystemPrompt+"\n\n"+request.msPrompt);
        std::string error;
        const uint64_t id=inference->Submit(request,error);
        if(!id) std::fprintf(stderr,"Perception submit: %s\n",error.c_str());
        Require(id!=0,"submit fresh stateless perception question without action grammar or engine ground truth");
        const auto started=std::chrono::steady_clock::now();
        const auto deadline=started+std::chrono::seconds(600);
        cLlamaResult result;
        bool received=false;
        while(std::chrono::steady_clock::now()<deadline)
        {
            if(inference->PollResult(result)) { received=result.mlRequestId==id; break; }
            if(inference->GetState()==eLlamaState_Failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        Write(aScratch/(name+"-reply.txt"),result.msText);
        const float seconds=std::chrono::duration<float>(std::chrono::steady_clock::now()-started).count();
        std::fprintf(gpCheckLog,"PERCEPTION %s (%.2fs): used=%u, images=%u, output=%u\n%s\n",
            name.c_str(),seconds,result.mContextStats.mlUsedTokens,result.mContextStats.mlImageTokens,
            result.mContextStats.mlOutputTokens,result.msText.c_str());
        std::fprintf(gpCheckLog,"PERCEPTION GROUND TRUTH: magenta pixels=%d; bbox=%d,%d..%d,%d; cyan pixels=%d\n",
            player.mlCount,player.mlMinX,player.mlMinY,player.mlMaxX,player.mlMaxY,cyan.mlCount);
        std::fflush(gpCheckLog);
        Require(received && result.msError.empty() && !result.mbCancelled && !result.mbTruncated,
            "real model finishes the independent perception response");
        Require(result.mContextStats.mlSessionId==0 && result.mContextStats.mlImageTokens>=1024 &&
            result.mContextStats.mlImageTokens<=2048,"full-detail image receives the configured spatial-grounding token budget");
        const std::string visible=PerceptionField(result.msText,"player_visible");
        visibilityResults[scenario]=visible.find(scenario==0 ? "yes" : "no")==0;
        doorsResults[scenario]=PerceptionField(result.msText,"breakable_doors_visible").find("yes")==0;
        if(scenario==0)
        {
            int x=-1,y=-1;
            const std::string center=PerceptionField(result.msText,"player_center");
            const size_t start=center.find_first_of("-0123456789");
            const bool parsed=start!=std::string::npos &&
                std::sscanf(center.c_str()+start,"%d , %d",&x,&y)==2;
            // The model may choose a point anywhere within the artificial cylinder;
            // allow 25/1000 around its actual rendered bounds for token quantization.
            localization=parsed && x>=0 && x<=1000 && y>=0 && y<=1000 &&
                x>=player.mlMinX-25 && x<=player.mlMaxX+25 &&
                y>=player.mlMinY-25 && y<=player.mlMaxY+25;
        }
        else visibilityResults[scenario]=visibilityResults[scenario] &&
            PerceptionField(result.msText,"player_center").find("none")==0;
    }
    inference->Unload();
    Require(visibilityResults[0],"fresh VLM recognizes a visible artificial magenta player cylinder");
    Require(localization,"fresh VLM locates the player within its rendered image bounds");
    Require(visibilityResults[1],"independent occluded-player frame produces no hidden-player hallucination");
    Require(doorsResults[0] && doorsResults[1],"both independent frames recognize the visible cyan breakable door");
    Require(!apOwner->IsModelActionActive() && apOwner->mvModelActionHistory.empty() &&
        apOwner->GetLastModelReply().empty(),"perception diagnostics neither execute actions nor enter accepted action history");
}

static void CheckDebugPreview(cLuxEnemy_Llama* apOwner)
{
    cLuxDebugHandler* pDebug = gpBase->mpDebugHandler;
    cGraphics* pGraphics = gpBase->mpEngine->GetGraphics();
    iLowLevelGraphics* pLowLevel = pGraphics->GetLowLevel();
    cGui* pGui = gpBase->mpEngine->GetGui();
    gpBase->mpDefaultFont = gpBase->mpEngine->GetResources()->GetFontManager()->CreateFontData("font_default.fnt");
    Require(gpBase->mpDefaultFont != NULL,"load real font for the actual game debug panel");
    gpBase->mpConfigHandler->mbLoadDebugMenu = true;
    pDebug->mbShowLlamaObservation = true;
    apOwner->msLastModelReply="UTF-8 \xE2\x98\x83 and truncated \xE2";
    pDebug->Update(1.0f/60);
    gpBase->mpMapHandler->OnDraw(1.0f/60);
    pDebug->OnDraw(1.0f/60);
    Require(pDebug->mpLlamaObservationGfx &&
        pDebug->mpLlamaObservationGfx->GetTexture(0) == apOwner->GetObservationTexture(),
        "actual debug OnDraw creates a preview from the current owner's observation texture");
    gpBase->mpGameDebugSet->ClearRenderObjects();

    const cVector2l vSize = apOwner->GetObservationSize();
    const std::vector<unsigned char>& rgb = apOwner->GetObservationRGB();
    int lMaskCount = 0;
    float fMaskY = 0;
    for(int y=0; y<vSize.y; ++y)
    for(int x=0; x<vSize.x; ++x)
    {
        const size_t i = ((size_t)y*vSize.x+x)*3;
        if(rgb[i] == 255 && rgb[i+1] == 0 && rgb[i+2] == 255) { ++lMaskCount; fMaskY += y; }
    }
    Require(lMaskCount>5 && fMaskY/lMaskCount>vSize.y*0.5f &&
        Near(apOwner->mpObservationCamera->GetPitch(),0) && Near(apOwner->mpObservationCamera->GetRoll(),0),
        "upright source image has the visible player below the enemy eye for a vertically asymmetric preview");

    iTexture* pTarget = pGraphics->CreateTexture("GameDebugPreviewTest",eTextureType_2D,eTextureUsage_RenderTarget);
    Require(pTarget && pTarget->CreateFromRawData(cVector3l(vSize.x,vSize.y,1),ePixelFormat_RGBA,NULL),
        "create real framebuffer color target for the game preview");
    iFrameBuffer* pBuffer = pGraphics->CreateFrameBuffer("GameDebugPreviewTest");
    pBuffer->SetTexture2D(0,pTarget);
    Require(pBuffer->CompileAndValidate(),"compile the game preview framebuffer");
    cGuiSet* pSet = pGui->CreateSet("GameDebugPreviewTest",NULL);
    pSet->SetVirtualSize(cVector2f(float(vSize.x),float(vSize.y)),-100,100);
    iFrameBuffer* pPrevious = pLowLevel->GetCurrentFrameBuffer();
    pLowLevel->PushMatrix(eMatrix_Projection);
    pLowLevel->PushMatrix(eMatrix_ModelView);
    for(int pass=0; pass<2; ++pass)
    {
        if(pass)
        {
            pLowLevel->SetCurrentFrameBuffer(pPrevious);
            pDebug->OnDraw(1.0f/60);
            gpBase->mpGameDebugSet->ClearRenderObjects();
        }
        pLowLevel->SetCurrentFrameBuffer(pBuffer);
        pLowLevel->SetColorWriteActive(true,true,true,true);
        pLowLevel->SetScissorActive(false);
        pLowLevel->SetClearColor(cColor(0,1));
        pLowLevel->ClearFrameBuffer(eClearFrameBufferFlag_Color);
        pSet->ClearRenderObjects();
        pSet->DrawGfx(pDebug->mpLlamaObservationGfx,0,cVector2f(float(vSize.x),float(vSize.y)));
        pSet->Render(NULL);
        cBitmap* pBitmap = pLowLevel->CopyFrameBufferToBitmap(cVector2l(0),vSize);
        Require(pBitmap && pBitmap->GetPixelFormat() == ePixelFormat_RGBA && pBitmap->GetData(0,0) &&
            pBitmap->GetData(0,0)->mpData,"read the actual game preview after GUI rendering");
        const unsigned char* pixels = pBitmap->GetData(0,0)->mpData;
        bool bExact = true;
        for(int y=0; y<vSize.y; ++y)
        for(int x=0; x<vSize.x; ++x)
        for(int channel=0; channel<3; ++channel)
        {
            const size_t source = ((size_t)(vSize.y-1-y)*vSize.x+x)*4+channel;
            const size_t submitted = ((size_t)y*vSize.x+x)*3+channel;
            bExact = bExact && std::abs(int(pixels[source])-int(rgb[submitted]))<=1;
        }
        hplDelete(pBitmap);
        Require(bExact,"actual debug preview matches every submitted RGB pixel on first and repeated draw");
    }
    pLowLevel->SetTexture(0,NULL);
    pLowLevel->SetBlendActive(false);
    pLowLevel->SetDepthTestActive(true);
    pLowLevel->SetDepthWriteActive(true);
    pLowLevel->PopMatrix(eMatrix_ModelView);
    pLowLevel->PopMatrix(eMatrix_Projection);
    pLowLevel->SetCurrentFrameBuffer(pPrevious);
    pGui->DestroySet(pSet);
    pGraphics->DestroyFrameBuffer(pBuffer);
    pGraphics->DestroyTexture(pTarget);
    pDebug->mbShowLlamaObservation = false;
    gpBase->mpConfigHandler->mbLoadDebugMenu = false;
}

static void CheckOwnership(cLuxMap* apMap, cLuxEnemy_Llama* apOwner, cLuxEnemy_Llama* apExtra)
{
    Require(cLuxEnemy_Llama::GetControllerOwner(apMap) == apOwner, "owner is the lowest eligible ID regardless of insertion order");
    apExtra->SetObservationEnabled(true);
    apExtra->CaptureObservation();
    apExtra->SetDebugInput(1,1);
    Hear(apExtra, 0);
    Require(apExtra->GetObservationFrameId() == 0 && apExtra->mfDebugForward == 0 && apExtra->mvSounds.empty(),
        "extra enemy has no observation, manual commands or sound evidence");
    apOwner->SetDisabled(true);
    Require(cLuxEnemy_Llama::GetControllerOwner(apMap) == apExtra, "disabled owner hands control to the next eligible ID");
    apOwner->SetDisabled(false);
    Require(cLuxEnemy_Llama::GetControllerOwner(apMap) == apOwner, "reenabled lowest ID reclaims control");
    apOwner->SetActive(false);
    Require(cLuxEnemy_Llama::GetControllerOwner(apMap) == apExtra, "inactive owner hands control to the next eligible ID");
    apOwner->SetActive(true);
    apOwner->mbDestroyMe = true;
    Require(cLuxEnemy_Llama::GetControllerOwner(apMap) == apExtra, "pending destruction is not eligible for control");
    apOwner->mbDestroyMe = false;
    gpBase->mpMapHandler->mpCurrentMap = NULL;
    apOwner->SetDebugInput(1,1);
    Require(!apOwner->IsControllerOwner() && apOwner->mfDebugForward == 0, "a retained map cannot receive active-map controls");
    gpBase->mpMapHandler->mpCurrentMap = apMap;
}

static void CheckSounds(cLuxEnemy_Llama* apOwner)
{
    Hear(apOwner, cMath::ToRad(22), 5.1f);
    Require(apOwner->mvSounds.size() == 1 && Near(apOwner->mvSounds[0].mfBearing,cMath::ToRad(15)) &&
        Near(apOwner->mvSounds[0].mfDistance,6), "sound evidence uses 15 degree bearings and coarse two-unit distance");
    Hear(apOwner, 0, 6, 0.1f);
    Hear(apOwner, 0, 20);
    Hear(apOwner, 0, 6, std::numeric_limits<float>::quiet_NaN());
    Require(apOwner->mvSounds.size() == 1, "quiet, distant and nonfinite sounds are rejected");
    apOwner->SetObservationEnabled(true);
    Capture(apOwner);
    const unsigned int frame = apOwner->GetObservationFrameId();
    Require(apOwner->GetPerceivedSounds().size() == 1, "successful image capture snapshots heard evidence");
    apOwner->SetDisabled(true);
    Require(apOwner->mvSounds.empty() && apOwner->GetPerceivedSounds().size() == 1 && apOwner->GetObservationFrameId() == frame,
        "disable expires live sounds while preserving metadata paired with the last image");
    apOwner->SetDisabled(false);
    Capture(apOwner);
    Require(apOwner->GetPerceivedSounds().empty(), "reenabling cannot revive stale disabled sound evidence");
    Hear(apOwner, 0);
    Capture(apOwner);
    apOwner->SetActive(false);
    Require(apOwner->mvSounds.empty() && apOwner->GetPerceivedSounds().size() == 1,
        "deactivation clears live sounds and leaves captured sounds paired with their image");
    apOwner->SetActive(true);
    Hear(apOwner, 0);
    apOwner->UpdateEnemySpecific(5.1f);
    Require(apOwner->mvSounds.empty(), "heard events expire after five active seconds");
    for(int i=0; i<8; ++i) Hear(apOwner, cMath::ToRad(float(i*30)));
    Require(apOwner->mvSounds.size() == 8, "recent sound queue is bounded");
    Hear(apOwner, 0, 6, 1);
    Require(apOwner->mvSounds.size() == 8 && Near(apOwner->mvSounds.back().mfBearing,0) && Near(apOwner->mvSounds.back().mfVolume,1),
        "coalesced event moves to the newest position with refreshed loudness");
    Hear(apOwner, cMath::ToRad(240));
    bool keptRefreshed = false;
    for(const cLuxLlamaSound& sound : apOwner->mvSounds) if(Near(sound.mfVolume,1)) keptRefreshed = true;
    Require(apOwner->mvSounds.size() == 8 && keptRefreshed, "queue eviction preserves the recently refreshed event");
    apOwner->mvSounds.clear();
    apOwner->mpCharBody->SetYaw(0);
    Hear(apOwner, 0);
    apOwner->mpCharBody->SetYaw(cMath::ToRad(90));
    Hear(apOwner, 0);
    Require(apOwner->mvSounds.size() == 2, "turning does not merge distinct world directions with identical relative bearings");
    Hear(apOwner, cMath::ToRad(90));
    Require(apOwner->mvSounds.size() == 2 && Near(apOwner->mvSounds.back().mfListenerYaw,cMath::ToRad(90)),
        "a repeated world direction coalesces across a listener turn and refreshes its reference heading");
    apOwner->mvCameraOffset = cVector3f(1,0,2);
    Capture(apOwner);
    const cVector3f vExpected = apOwner->mpCharBody->GetPosition() +
        cVector3f(-2,apOwner->mpCharBody->GetShape(0)->GetSize().y-apOwner->mpCharBody->GetSize().y*0.5f,-1);
    Require(cMath::Vector3Dist(apOwner->GetObservationPosition(),vExpected)<0.0001f &&
        Near(apOwner->GetObservationYaw(),cMath::ToRad(90)), "capture uses current heading for custom camera offsets before the next physics tick");
    apOwner->mvCameraOffset = cVector3f(0,-0.1f,0);
    apOwner->SetDisableTriggers(true);
    Require(apOwner->mvSounds.empty(), "disabling triggers clears heard evidence");
    apOwner->SetDisableTriggers(false);
}

static void CheckMovement(cLuxEnemy_Llama* apOwner, cLuxEnemy_Llama* apExtra)
{
    apOwner->mpCharBody->SetYaw(0);
    apOwner->SetDebugInput(1,1);
    apOwner->OnUpdate(1.0f/60);
    Require(apOwner->mfDebugForward == 1 && apOwner->mpCharBody->GetYaw() < 0,
        "owner accepts forward input and turns right through the real enemy mover");
    apOwner->OnUpdate(0.25f);
    Require(apOwner->mfDebugForward == 0 && apOwner->mfDebugTurn == 0 && !apOwner->mpMover->mbTurning,
        "missed debug updates expire both movement and outstanding turn commands");
    apExtra->mpCharBody->SetForceVelocity(cVector3f(2,-3,4));
    apExtra->UpdateEnemySpecific(1.0f/60);
    Require(apExtra->mpCharBody->GetForceVelocity() == cVector3f(0,-3,0),
        "idling extra enemies cancel lateral force without erasing falling velocity");
    cAnimationState* pIdle = apOwner->mpMeshEntity->GetAnimationState(0);
    Require(apOwner->GetWalkAnimationName() == "Idle" && apOwner->GetRunAnimationName() == "Idle", "single-clip rigs use the idle fallback for locomotion");
    apOwner->mpMover->mbOverideMoveState = false;
    apOwner->mpMover->mMoveState = eLuxEnemyMoveState_Walking;
    apOwner->mpMover->mfTurnSpeed = 0;
    apOwner->mpCurrentAnimation = pIdle;
    pIdle->SetActive(true);
    pIdle->SetLoop(true);
    pIdle->SetSpeed(0.15f);
    apOwner->mpCharBody->SetMoveSpeed(eCharDir_Forward,0);
    apOwner->mpMover->OnUpdate(1.0f/60);
    Require(apOwner->mpMover->mMoveState == eLuxEnemyMoveState_Stopped && Near(pIdle->GetSpeed(),1) && Near(pIdle->GetBaseSpeed(),0.7f),
        "aliased locomotion returns to normal idle playback while retaining clip base speed");
    const float fHeight = apExtra->mpCharBody->GetPosition().y;
    apOwner->mpMap->GetPhysicsWorld()->Update(1.0f/60);
    Require(apExtra->mpCharBody->GetPosition().y < fHeight, "an idle extra enemy continues falling through the real physics update");
}

static void CheckDeath(cLuxMap* apMap, cLuxEnemy_Llama* apOwner, cLuxEnemy_Llama* apExtra)
{
    cLuxLlamaController* controller=gpBase->mpMapHandler->GetLlamaController();
    Capture(apOwner);
    Require(apOwner->SnapshotModelObservation(),"death fixture has completed-decision evidence to invalidate");
    cLuxLlamaDecisionRecord recorded;
    recorded.mpOwner=apOwner;
    recorded.mpMap=apMap;
    recorded.mlRequestId=301;
    recorded.mRequest.mImage.mlWidth=apOwner->mModelObservation.mvSize.x;
    recorded.mRequest.mImage.mlHeight=apOwner->mModelObservation.mvSize.y;
    recorded.mRequest.mImage.mvRGB=apOwner->mModelObservation.mvRGB;
    cLuxLlamaControllerTestAdapter::LastDecision(controller,recorded);
    cAnimationState* pClip = apOwner->mpMeshEntity->GetAnimationState(0);
    apOwner->msDeathAnimation = "Idle";
    pClip->SetLoop(true);
    pClip->SetSpeed(0.1f);
    pClip->SetTimePosition(0.6f);
    apOwner->GiveDamage(200,10);
    Require(apOwner->GetHealth() <= 0 && !apOwner->mpCharBody->IsActive() && !pClip->IsLooping() &&
        Near(pClip->GetTimePosition(),0) && Near(pClip->GetSpeed(),1) && apOwner->mpMover->GetOverideMoveState(),
        "configured death reusing the active clip restarts once and overrides locomotion");
    Require(cLuxEnemy_Llama::GetControllerOwner(apMap) == apExtra, "dead lowest ID relinquishes the controller");
    Require(cLuxLlamaControllerTestAdapter::LastDecision(controller).mlRequestId==0,
        "owner death discards completed decision evidence before the replacement enemy can be queried");
    apOwner->GiveDamage(10,10);
    Require(Near(pClip->GetTimePosition(),0), "damage on a corpse does not restart its death animation");
    iLuxEntity_SaveData* pSaved = apOwner->CreateSaveData();
    apOwner->SaveToSaveData(pSaved);
    apOwner->mpCharBody->SetActive(true);
    pClip->SetLoop(true);
    apOwner->mpMover->SetOverideMoveState(false);
    apOwner->LoadFromSaveData(pSaved);
    apOwner->SetupSaveData(pSaved);
    Require(!apOwner->mpCharBody->IsActive() && !pClip->IsLooping() && apOwner->mpMover->GetOverideMoveState() &&
        apOwner->mCurrentState == eLuxEnemyState_Dead, "saved death restores inactive collision, clip mode and movement override");
    hplDelete(pSaved);
    apExtra->GiveDamage(200,10);
    Require(!apExtra->mpCharBody->IsActive() && apExtra->mpMover->GetOverideMoveState(), "a rig without clips freezes safely on death");
    Require(cLuxEnemy_Llama::GetControllerOwner(apMap) == NULL, "no eligible enemy leaves the controller uninhabited");
}

static int Run(int argc, char** argv)
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if(argc != 3 && argc != 7 && argc != 8) { std::fprintf(stderr,"Supply asset root and scratch, optionally [--perception-only, --explain-decision or --steering-only] --model path --projector path.\n"); return 2; }
    std::string modelPath,projectorPath;
    const bool focused=argc==8;
    const bool perceptionOnly=focused && std::string(argv[3])=="--perception-only";
    const bool explainDecision=focused && std::string(argv[3])=="--explain-decision";
    const bool steeringOnly=focused && std::string(argv[3])=="--steering-only";
    if(argc==7 || focused)
    {
        const int offset=focused ? 1 : 0;
        if((focused && !perceptionOnly && !explainDecision && !steeringOnly) ||
            std::string(argv[3+offset])!="--model" || std::string(argv[5+offset])!="--projector") return 2;
        modelPath=fs::absolute(argv[4+offset]).lexically_normal().generic_string();
        projectorPath=fs::absolute(argv[6+offset]).lexically_normal().generic_string();
    }
    const fs::path assetRoot = fs::absolute(argv[1]).lexically_normal();
    const fs::path scratch = fs::absolute(argv[2]).lexically_normal();
    std::string assetPath = assetRoot.generic_string(), scratchPath = scratch.generic_string();
    while(assetPath.size()>3 && assetPath.back()=='/') assetPath.pop_back();
    while(scratchPath.size()>3 && scratchPath.back()=='/') scratchPath.pop_back();
    std::transform(assetPath.begin(),assetPath.end(),assetPath.begin(),[](unsigned char c) { return char(std::tolower(c)); });
    std::transform(scratchPath.begin(),scratchPath.end(),scratchPath.begin(),[](unsigned char c) { return char(std::tolower(c)); });
    if(scratchPath == assetPath || scratchPath.find(assetPath+"/") == 0)
    { std::fprintf(stderr,"Scratch must stay outside retail data.\n"); return 2; }
    fs::create_directories(scratch);
    gpCheckLog = std::fopen((scratch/"checks.log").string().c_str(),"w");
    if(!gpCheckLog) return 2;
    fs::copy(assetRoot/"core",scratch/"core",fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    fs::copy_file(assetRoot/"materials.cfg",scratch/"materials.cfg",fs::copy_options::overwrite_existing);
    fs::create_directories(scratch/"sounds");
    fs::copy_file(assetRoot/"sounds/EnemySounds.dat",scratch/"sounds/EnemySounds.dat",fs::copy_options::overwrite_existing);
    std::string resources = "<Resources>";
    for(const char* directory : {"fonts","gui","textures","graphics","sounds","particles","lights","billboards"})
        resources += "<Directory Path=\"" + (assetRoot/directory).generic_string() + "\" AddSubDirs=\"true\" />";
    resources += "<Directory Path=\"" + assetRoot.generic_string() + "\" AddSubDirs=\"false\" /></Resources>";
    Write(scratch/"resources.cfg",resources);
    fs::current_path(scratch);
    SetLogFile(cString::To16Char((scratch/"hpl.log").generic_string()));
    cResources::SetForceCacheLoadingAndSkipSaving(true);
    gpBase = hplNew(cLuxBase, ());
    gpBase->msBaseSavePath=cString::To16Char(scratch.generic_string()+"/");
    gpBase->mpDebugHandler = NULL;
    cEngineInitVars vars;
    vars.mGraphics.mvScreenSize = cVector2l(640,480);
    vars.mGraphics.mvWindowPosition = cVector2l(-10000,-10000);
    vars.mGraphics.msWindowCaption = "Enemy_Llama Runtime Test";
    vars.mSound.mbUseThreading = false;
    vars.mSound.mbUseHRTF = false;
    gpBase->mpEngine = CreateHPLEngine(eHplAPI_OpenGL,eHplSetup_All,&vars);
    Require(gpBase->mpEngine != NULL, "initialize real OpenGL game engine");
    for(Uint32 i=1; i<16; ++i) if(SDL_Window* pWindow = SDL_GetWindowFromID(i)) SDL_HideWindow(pWindow);
    Require(gpBase->mpEngine->GetResources()->LoadResourceDirsFile("resources.cfg"), "load read-only resource paths");
    Require(gpBase->mpEngine->GetPhysics()->LoadSurfaceData("materials.cfg"), "load physics surface data");
    gpBase->mpGameCfg = Config(assetRoot/"config/game.cfg");
    gpBase->mpGameCfg->SetBool("Llama","Enabled",false);
    gpBase->mpGameCfg->SetBool("Llama","TargetSteeringOnly",false);
    gpBase->mpMenuCfg = Config(assetRoot/"config/menu.cfg");
    gpBase->mpUserConfig = Config(assetRoot/"config/default_user_settings.cfg");
    gpBase->mvHudVirtualSize = cVector2f(800,600);
    gpBase->mvHudVirtualCenterSize = gpBase->mvHudVirtualSize;
    gpBase->mvHudVirtualOffset = 0;
    gpBase->mpGameHudSet = gpBase->mpEngine->GetGui()->CreateSet("TestHud",NULL);
    gpBase->mpGameDebugSet = gpBase->mpEngine->GetGui()->CreateSet("TestDebug",NULL);
    gpBase->mpHelpFuncs = hplNew(cLuxHelpFuncs, ());
    gpBase->mpMapHelper = hplNew(cLuxMapHelper, ());
    gpBase->mpMapHandler = hplNew(cLuxMapHandler, ());
    gpBase->mpMapHandler->mbUpdateActive=true;
    gpBase->mpMapHandler->mMapChangeData.mbActive=false;
    gpBase->mpPlayer = hplNew(cLuxPlayer, ());
    gpBase->mpInputHandler = hplNew(cLuxInputHandler, ());
    gpBase->mpMusicHandler = hplNew(cLuxMusicHandler, ());
    gpBase->mpConfigHandler = hplNew(cLuxConfigHandler, ());
    gpBase->mpConfigHandler->mbLoadDebugMenu = false;
    gpBase->mpDebugHandler = hplNew(cLuxDebugHandler, ());
    gpBase->mpDebugHandler->mbShowFPS = false;
    gpBase->mpDebugHandler->mbShowSoundPlaying = false;
    gpBase->mpDebugHandler->mbShowPlayerInfo = false;
    gpBase->mpDebugHandler->mbShowEntityInfo = false;
    gpBase->mpDebugHandler->mbShowDebugMessages = false;
    gpBase->mpDebugHandler->mbShowErrorMessages = false;
    gpBase->mpDebugHandler->mbInspectionMode = false;
    gpBase->mpDebugHandler->mbFirstUpdateOnMap = false;
    gpBase->mpDebugHandler->mlTempCount = 0;
    cLuxMap* pMap = hplNew(cLuxMap, ("LlamaRuntimeTest"));
    pMap->mpWorld = gpBase->mpEngine->GetScene()->CreateWorld("LlamaRuntimeTest");
    pMap->mpPhysicsWorld = gpBase->mpEngine->GetPhysics()->CreateWorld(true);
    pMap->mpWorld->SetPhysicsWorld(pMap->mpPhysicsWorld);
    pMap->mpPhysicsWorld->SetWorldSize(cVector3f(-100),cVector3f(100));
    gpBase->mpMapHandler->mpCurrentMap = pMap;
    gpBase->mpMapHandler->mpViewport->SetWorld(pMap->mpWorld);
    gpBase->mpPlayer->mpCharBody = pMap->mpPhysicsWorld->CreateCharacterBody("TestPlayer",cVector3f(0.8f,1.8f,0.8f));
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-6));
    gpBase->mpPlayer->mfHealth = 100;
    cLuxEnemy_Llama* pExtra = Enemy(pMap,"Extra",20,false);
    cLuxEnemy_Llama* pOwner = Enemy(pMap,"Owner",10,true,NULL,
        focused ? cVector2l(1280,864) : cVector2l(64,64));
    if(!focused)
    {
        CheckFOV(pMap);
        CheckObservationResolution(pMap);
    }
    pMap->mpWorld->Compile(false);
    if(focused) CheckPerceptionModel(pOwner,modelPath,projectorPath,scratch,explainDecision,steeringOnly);
    else
    {
        pMap->mpPhysicsWorld->Update(1.0f/60);
        CheckOwnership(pMap,pOwner,pExtra);
        CheckDebugPreview(pOwner);
        CheckSounds(pOwner);
        CheckModelDecisions(pMap,pOwner,pExtra);
        CheckTargetSteeringLifecycle(pMap,pOwner);
        CheckActionFeedback(pMap,pOwner);
        CheckPerceptionLifecycle(pMap,pOwner,pExtra);
        CheckDecisionExplanation(pMap,pOwner,pExtra);
        CheckMovement(pOwner,pExtra);
        CheckModelAttacks(pMap,pOwner);
        if(!modelPath.empty()) CheckLiveModel(pOwner,modelPath,projectorPath);
        CheckDeath(pMap,pOwner,pExtra);
    }
    gpBase->mpMapHandler->mpCurrentMap = NULL;
    gpBase->mpMapHandler->mpViewport->SetWorld(NULL);
    gpBase->mpPlayer->mpCharBody = NULL; // Owned and destroyed by the test physics world.
    cWorld* pDeletedWorld = pMap->mpWorld;
    hplDelete(pMap);
    Require(!gpBase->mpEngine->GetScene()->WorldExists(pDeletedWorld), "map destruction releases the synthetic world and its enemies");
    cSoundEntity::RemoveGlobalCallback(gpBase->mpMapHandler->mpSoundCallback);
    hplDelete(gpBase->mpDebugHandler);
    gpBase->mpDebugHandler = NULL;
    hplDelete(gpBase->mpMusicHandler);
    hplDelete(gpBase->mpPlayer);
    hplDelete(gpBase->mpMapHandler);
    hplDelete(gpBase->mpHelpFuncs);
    hplDelete(gpBase->mpMapHelper);
    hplDelete(gpBase->mpInputHandler);
    hplDelete(gpBase->mpConfigHandler);
    hplDelete(gpBase->mpGameCfg);
    hplDelete(gpBase->mpMenuCfg);
    hplDelete(gpBase->mpUserConfig);
    hplDelete(gpBase); // Its destructor destroys the real engine last.
    gpBase = NULL;
    std::printf("PASS: %d checks using native game objects, synthetic rigs and actual RGB capture.\n",checks);
    std::fclose(gpCheckLog);
    return 0;
}

int main(int argc, char** argv)
{
    try { return Run(argc,argv); }
    catch(const std::exception& error)
    {
        std::fprintf(stderr,"Runtime test setup failed: %s\n",error.what());
        return 2;
    }
}

int hplMain(const tString&) { return 2; }
