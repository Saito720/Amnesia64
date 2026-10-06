// Native integration tests against the real game objects and synthetic bodies,
// meshes and clips. Retail files are read only; all output stays in scratch.
#include "hpl.h"
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <crtdbg.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
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
    if(argc != 3) { std::fprintf(stderr,"Supply read-only asset root and scratch directory.\n"); return 2; }
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
    gpBase->mpMenuCfg = Config(assetRoot/"config/menu.cfg");
    gpBase->mpUserConfig = Config(assetRoot/"config/default_user_settings.cfg");
    gpBase->mvHudVirtualSize = cVector2f(800,600);
    gpBase->mvHudVirtualOffset = 0;
    gpBase->mpGameHudSet = gpBase->mpEngine->GetGui()->CreateSet("TestHud",NULL);
    gpBase->mpGameDebugSet = gpBase->mpEngine->GetGui()->CreateSet("TestDebug",NULL);
    gpBase->mpHelpFuncs = hplNew(cLuxHelpFuncs, ());
    gpBase->mpMapHandler = hplNew(cLuxMapHandler, ());
    gpBase->mpPlayer = hplNew(cLuxPlayer, ());
    gpBase->mpMusicHandler = hplNew(cLuxMusicHandler, ());
    gpBase->mpConfigHandler = hplNew(cLuxConfigHandler, ());
    gpBase->mpConfigHandler->mbLoadDebugMenu = false;
    gpBase->mpDebugHandler = hplNew(cLuxDebugHandler, ());
    gpBase->mpDebugHandler->mbShowDebugMessages = false;
    cLuxMap* pMap = hplNew(cLuxMap, ("LlamaRuntimeTest"));
    pMap->mpWorld = gpBase->mpEngine->GetScene()->CreateWorld("LlamaRuntimeTest");
    pMap->mpPhysicsWorld = gpBase->mpEngine->GetPhysics()->CreateWorld(true);
    pMap->mpWorld->SetPhysicsWorld(pMap->mpPhysicsWorld);
    pMap->mpPhysicsWorld->SetWorldSize(cVector3f(-100),cVector3f(100));
    gpBase->mpMapHandler->mpCurrentMap = pMap;
    gpBase->mpMapHandler->mpViewport->SetWorld(pMap->mpWorld);
    gpBase->mpPlayer->mpCharBody = pMap->mpPhysicsWorld->CreateCharacterBody("TestPlayer",cVector3f(0.8f,1.8f,0.8f));
    gpBase->mpPlayer->mpCharBody->SetPosition(cVector3f(10,2,-6));
    cLuxEnemy_Llama* pExtra = Enemy(pMap,"Extra",20,false);
    cLuxEnemy_Llama* pOwner = Enemy(pMap,"Owner",10,true);
    CheckFOV(pMap);
    pMap->mpWorld->Compile(false);
    pMap->mpPhysicsWorld->Update(1.0f/60);
    CheckOwnership(pMap,pOwner,pExtra);
    CheckSounds(pOwner);
    CheckMovement(pOwner,pExtra);
    CheckDeath(pMap,pOwner,pExtra);
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
