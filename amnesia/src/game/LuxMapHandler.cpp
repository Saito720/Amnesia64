/*
 * Copyright © 2009-2020 Frictional Games
 * 
 * This file is part of Amnesia: The Dark Descent.
 * 
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "LuxMapHandler.h"

#include "LuxMap.h"
#include "LuxPlayer.h"
#include "LuxEffectRenderer.h"
#include "LuxEffectHandler.h"
#include "LuxDebugHandler.h"
#include "LuxHelpFuncs.h"
#include "LuxSavedGame.h"
#include "LuxSaveHandler.h"
#include "LuxConfigHandler.h"
#include "LuxLoadScreenHandler.h"
#include "LuxMainMenu.h"

#include "LuxEnemy.h"
#include "LuxAchievementHandler.h"

//////////////////////////////////////////////////////////////////////////
// SOUND ENTITY CALLBACK
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cMapHandlerSoundCallback::cMapHandlerSoundCallback()
{
	///////////////////////
	//Load document
	tString sFile = "sounds/EnemySounds.dat";
	iXmlDocument* pXmlDoc = gpBase->mpEngine->GetResources()->LoadXmlDocument(sFile);
	if(pXmlDoc ==NULL)
	{
		Error("Couldn't load XML file '%s'!\n",sFile.c_str());
		return;
	}

	//////////////////////
	// Load data
	cXmlNodeListIterator it = pXmlDoc->GetChildIterator();
	while(it.HasNext())
	{
		cXmlElement *pChildElem = it.Next()->ToElement();

		tString sName = pChildElem->GetAttributeString("name");
		mvEnemyHearableSounds.push_back(sName);
	}

	gpBase->mpEngine->GetResources()->DestroyXmlDocument( pXmlDoc );
}

//-----------------------------------------------------------------------

void cMapHandlerSoundCallback::OnStart(cSoundEntity *apSoundEntity)
{
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL) return;
	
	///////////////////////////
	//Check if the sound is something to worry bout
	tString sTypeName = apSoundEntity->GetData()->GetName();

	bool bUsed=false;
	for(size_t i=0; i< mvEnemyHearableSounds.size(); ++i)
	{
		tString &sName = mvEnemyHearableSounds[i];
		if(sTypeName.size() >= sName.size() && sName == sTypeName.substr(0,sName.size()))
		{
			bUsed = true;
			break;
		}
	}
	if(bUsed == false) return;
	
	///////////////////////////
	//Iterate enemies and send sound message to those close enough
	float fMaxDist = apSoundEntity->GetMaxDistance();
	float fMinDist = apSoundEntity->GetMaxDistance();
	float fVolume = apSoundEntity->GetVolume();
	cVector3f vPos = apSoundEntity->GetWorldPosition();
	
	pMap->BroadcastEnemySoundMessage(vPos, fVolume, fMinDist, fMaxDist);
}	

//-----------------------------------------------------------------------


//////////////////////////////////////////////////////////////////////////
// RENDER CALLBACK
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxDebugRenderCallback::cLuxDebugRenderCallback()
{
}

//-----------------------------------------------------------------------

void cLuxDebugRenderCallback::OnPostSolidDraw(cRendererCallbackFunctions* apFunctions)
{
	if(mpPhysicsWorld)
	{
		apFunctions->SetMatrix(NULL);
		apFunctions->SetBlendMode(eMaterialBlendMode_Alpha);
		apFunctions->SetTextureRange(NULL,0);
		apFunctions->SetProgram(NULL);
		
		apFunctions->SetDepthTest(true);
		apFunctions->SetDepthWrite(false);

		//mpPhysicsWorld->RenderDebugGeometry(apFunctions->GetLowLevelGfx(), cColor(1,1,1,1));
	}

	gpBase->mpDebugHandler->RenderSolid(apFunctions);
	gpBase->mpMapHandler->RenderSolid(apFunctions);
	gpBase->mpPlayer->RenderSolid(apFunctions);
	gpBase->mpEffectRenderer->RenderSolid(apFunctions);
}

//-----------------------------------------------------------------------

void cLuxDebugRenderCallback::OnPostTranslucentDraw(cRendererCallbackFunctions* apFunctions)
{
	gpBase->mpPlayer->RenderTrans(apFunctions);
	gpBase->mpEffectRenderer->RenderTrans(apFunctions);
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// CAMERA VIEW DESC
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxCameraViewDesc::cLuxCameraViewDesc()
{
	mvResolution = cVector2l(512, 512);
	mfFOV = 0;
	mfNearClipPlane = 0;
	mfFarClipPlane = 0;
	mvPosition = 0;
	m_mtxRotation = cMatrixf::Identity;
	mpWorld = NULL;
	mRenderer = eRenderer_Main;
	mMoveMode = eCameraMoveMode_Fly;
	mbActive = true;
	mbVisible = true;
	mbPushFront = true;
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// CAMERA VIEW
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxCameraView::cLuxCameraView(cLuxMapHandler *apMapHandler, const cLuxCameraViewDesc& aDesc)
{
	mpMapHandler = apMapHandler;
	mpCamera = NULL;
	mpViewport = NULL;
	mpFrameBuffer = NULL;
	mpRenderTexture = NULL;
	mpDepthStencilBuffer = NULL;
	mvResolution = aDesc.mvResolution;
	mbActive = aDesc.mbActive;
	mbVisible = aDesc.mbVisible;
	mbContainerActive = true;
	mbUseCurrentMapWorld = aDesc.mpWorld == NULL;
	mbValid = Init(aDesc);
}

//-----------------------------------------------------------------------

cLuxCameraView::~cLuxCameraView()
{
	DestroyResources();
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetActive(bool abX)
{
	mbActive = abX;
	ApplyActiveState();
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetVisible(bool abX)
{
	mbVisible = abX;
	ApplyActiveState();
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetContainerActive(bool abX)
{
	mbContainerActive = abX;
	ApplyActiveState();
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetWorld(cWorld *apWorld)
{
	mbUseCurrentMapWorld = false;
	SetViewportWorld(apWorld);
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetUseCurrentMapWorld(bool abX)
{
	mbUseCurrentMapWorld = abX;
	if(mbUseCurrentMapWorld == false) return;

	cWorld *pWorld = NULL;
	if(mpMapHandler && mpMapHandler->GetCurrentMap())
		pWorld = mpMapHandler->GetCurrentMap()->GetWorld();

	SetViewportWorld(pWorld);
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetPosition(const cVector3f& avPosition)
{
	if(mpCamera) mpCamera->SetPosition(avPosition);
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetRotationMatrix(const cMatrixf& a_mtxRotation)
{
	if(mpCamera == NULL) return;

	mpCamera->SetRotateMode(eCameraRotateMode_Matrix);
	mpCamera->SetRotationMatrix(a_mtxRotation);
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetTransform(const cVector3f& avPosition, const cMatrixf& a_mtxRotation)
{
	SetPosition(avPosition);
	SetRotationMatrix(a_mtxRotation);
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetFOV(float afFOV)
{
	if(mpCamera) mpCamera->SetFOV(afFOV);
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetClipPlanes(float afNearClipPlane, float afFarClipPlane)
{
	if(mpCamera == NULL) return;

	mpCamera->SetNearClipPlane(afNearClipPlane);
	mpCamera->SetFarClipPlane(afFarClipPlane);
}

//-----------------------------------------------------------------------

bool cLuxCameraView::Init(const cLuxCameraViewDesc& aDesc)
{
	if(gpBase == NULL || gpBase->mpEngine == NULL) return false;

	if(aDesc.mvResolution.x <= 0 || aDesc.mvResolution.y <= 0)
	{
		Error("Could not create camera view with invalid resolution %dx%d\n", aDesc.mvResolution.x, aDesc.mvResolution.y);
		return false;
	}

	cScene *pScene = gpBase->mpEngine->GetScene();
	cGraphics *pGraphics = gpBase->mpEngine->GetGraphics();

	mpRenderTexture = pGraphics->CreateTexture("LuxCameraViewTarget", eTextureType_Rect, eTextureUsage_RenderTarget);
	if(mpRenderTexture == NULL) return false;

	mpRenderTexture->SetWrapSTR(eTextureWrap_ClampToEdge);
	if(mpRenderTexture->CreateFromRawData(cVector3l(aDesc.mvResolution.x, aDesc.mvResolution.y, 0), ePixelFormat_RGBA, NULL) == false)
		return false;

	mpFrameBuffer = pGraphics->CreateFrameBuffer("LuxCameraView");
	if(mpFrameBuffer == NULL) return false;

	mpFrameBuffer->SetTexture2D(0, mpRenderTexture);

	mpDepthStencilBuffer = pGraphics->CreateDepthStencilBuffer(aDesc.mvResolution, 24, 8, false);
	if(mpDepthStencilBuffer == NULL) return false;

	mpFrameBuffer->SetDepthStencilBuffer(mpDepthStencilBuffer);
	if(mpFrameBuffer->CompileAndValidate() == false)
		return false;

	mpCamera = pScene->CreateCamera(aDesc.mMoveMode);
	if(mpCamera == NULL) return false;

	cCamera *pBaseCamera = (mpMapHandler && mpMapHandler->GetViewport()) ? mpMapHandler->GetViewport()->GetCamera() : NULL;
	float fFOV = aDesc.mfFOV > 0 ? aDesc.mfFOV : (pBaseCamera ? pBaseCamera->GetFOV() : cMath::ToRad(70.0f));
	float fNearClip = aDesc.mfNearClipPlane > 0 ? aDesc.mfNearClipPlane : (pBaseCamera ? pBaseCamera->GetNearClipPlane() : 0.05f);
	float fFarClip = aDesc.mfFarClipPlane > 0 ? aDesc.mfFarClipPlane : (pBaseCamera ? pBaseCamera->GetFarClipPlane() : 1000.0f);

	mpCamera->SetRotateMode(eCameraRotateMode_Matrix);
	mpCamera->SetFOV(fFOV);
	mpCamera->SetAspect((float)aDesc.mvResolution.x / (float)aDesc.mvResolution.y);
	mpCamera->SetNearClipPlane(fNearClip);
	mpCamera->SetFarClipPlane(fFarClip);
	mpCamera->SetPosition(aDesc.mvPosition);
	mpCamera->SetRotationMatrix(aDesc.m_mtxRotation);

	cWorld *pWorld = aDesc.mpWorld;
	if(pWorld == NULL && mpMapHandler && mpMapHandler->GetCurrentMap())
		pWorld = mpMapHandler->GetCurrentMap()->GetWorld();

	mpViewport = pScene->CreateViewport(mpCamera, pWorld, aDesc.mbPushFront);
	if(mpViewport == NULL) return false;

	mpViewport->SetRenderer(pGraphics->GetRenderer(aDesc.mRenderer));
	mpViewport->SetFrameBuffer(mpFrameBuffer);
	mpViewport->SetPosition(cVector2l(0, 0));
	mpViewport->SetSize(aDesc.mvResolution);

	ApplyActiveState();

	return true;
}

//-----------------------------------------------------------------------

void cLuxCameraView::ApplyActiveState()
{
	if(mpViewport == NULL) return;

	mpViewport->SetActive(mbActive && mbContainerActive);
	mpViewport->SetVisible(mbVisible && mbContainerActive);
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetCurrentMapWorld(cWorld *apWorld)
{
	SetViewportWorld(apWorld);
}

//-----------------------------------------------------------------------

void cLuxCameraView::SetViewportWorld(cWorld *apWorld)
{
	if(mpViewport) mpViewport->SetWorld(apWorld);
}

//-----------------------------------------------------------------------

void cLuxCameraView::DestroyResources()
{
	if(gpBase == NULL || gpBase->mpEngine == NULL) return;

	cScene *pScene = gpBase->mpEngine->GetScene();
	cGraphics *pGraphics = gpBase->mpEngine->GetGraphics();

	if(mpViewport)
	{
		mpViewport->SetFrameBuffer(NULL);
		pScene->DestroyViewport(mpViewport);
		mpViewport = NULL;
	}

	if(mpCamera)
	{
		pScene->DestroyCamera(mpCamera);
		mpCamera = NULL;
	}

	if(mpFrameBuffer)
	{
		pGraphics->DestroyFrameBuffer(mpFrameBuffer);
		mpFrameBuffer = NULL;
	}

	if(mpDepthStencilBuffer)
	{
		pGraphics->DestoroyDepthStencilBuffer(mpDepthStencilBuffer);
		mpDepthStencilBuffer = NULL;
	}

	if(mpRenderTexture)
	{
		pGraphics->DestroyTexture(mpRenderTexture);
		mpRenderTexture = NULL;
	}
}

//-----------------------------------------------------------------------


//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxMapHandler::cLuxMapHandler() : iLuxUpdateable("LuxMapHandler")
{
	//////////////////////////
	//Create and setup view port
	mpViewport = gpBase->mpEngine->GetScene()->CreateViewport();
	gpBase->mpEngine->GetScene()->SetCurrentListener(mpViewport);

	//////////////////////////
	//Set up post effects
	cGraphics *pGraphics = gpBase->mpEngine->GetGraphics();
	cPostEffectComposite *pPostEffectComp = pGraphics->CreatePostEffectComposite();
	mpViewport->SetPostEffectComposite(pPostEffectComp);
	
	//Bloom
	cPostEffectParams_Bloom bloomParams;
	bloomParams.mfBlurSize = 1.0f;
	bloomParams.mvRgbToIntensity = bloomParams.mvRgbToIntensity * 1.0f;
	mpPostEffect_Bloom = pGraphics->CreatePostEffect(&bloomParams);
	pPostEffectComp->AddPostEffect(mpPostEffect_Bloom, 100);
	
	//Image trail
	cPostEffectParams_ImageTrail imageTrailParams;
	mpPostEffect_ImageTrail = pGraphics->CreatePostEffect(&imageTrailParams);
	pPostEffectComp->AddPostEffect(mpPostEffect_ImageTrail, 10);
	mpPostEffect_ImageTrail->SetActive(false);

	//Radial
	cPostEffectParams_RadialBlur radialBlurParams;
	radialBlurParams.mfSize = 0.0f;
	mpPostEffect_RadialBlur = pGraphics->CreatePostEffect(&radialBlurParams);
	pPostEffectComp->AddPostEffect(mpPostEffect_RadialBlur, 9);
	mpPostEffect_RadialBlur->SetActive(false);

	//Sepia
	cPostEffectParams_ColorConvTex sepiaParams;
	sepiaParams.msTextureFile = "colorconv_sepia.tga";
	sepiaParams.mfFadeAlpha = 0.0f;
	mpPostEffect_Sepia = pGraphics->CreatePostEffect(&sepiaParams);
	pPostEffectComp->AddPostEffect(mpPostEffect_Sepia, 4);
	mpPostEffect_Sepia->SetActive(false);
	
	//////////////////////////
	//Saving
	mpSavedGame = hplNew( cLuxSavedGameMapCollection, () );

	
	//////////////////////////
	//Callbacks
	mpSoundCallback = hplNew( cMapHandlerSoundCallback, () );
	cSoundEntity::AddGlobalCallback(mpSoundCallback);

	//////////////////////////
	//Threading
	mpSavedGameMutex = cPlatform::CreateMutEx();

	//////////////////////////
	//Variables
	mbPausedSoundsAndMusic = false;
	mpDataCache =NULL;

	Reset();
}

//-----------------------------------------------------------------------

cLuxMapHandler::~cLuxMapHandler()
{
	STLDeleteAll(mlstCameraViews);
	hplDelete(mpSavedGame);
	hplDelete(mpSavedGameMutex);
	hplDelete(mpSoundCallback);
	STLDeleteAll(mlstMaps);
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxMapHandler::OnStart()
{
	//////////////////////
	//Set up viewport 	
	mpViewport->SetCamera(gpBase->mpPlayer->GetCamera());

	mpViewport->AddGuiSet(gpBase->mpGameDebugSet);
	mpViewport->AddGuiSet(gpBase->mpGameHudSet);

    
	mpViewport->AddRendererCallback(&mRenderCallback);
	UpdateViewportRenderProperties();
}

//-----------------------------------------------------------------------

void cLuxMapHandler::UpdateViewportRenderProperties()
{
	ApplyViewportRenderProperties(mpViewport);

	tLuxCameraViewListIt it = mlstCameraViews.begin();
	for(; it != mlstCameraViews.end(); ++it)
	{
		ApplyViewportRenderProperties((*it)->GetViewport());
	}
}

//-----------------------------------------------------------------------

void cLuxMapHandler::Update(float afTimeStep)
{
	//TODO: Bad placement! Moooove!
	gpBase->mpEffectRenderer->ClearRenderLists();

	CheckMapChange(afTimeStep);

	if(mpCurrentMap && mMapChangeData.mbActive==false)
		mpCurrentMap->Update(afTimeStep);
}

//-----------------------------------------------------------------------

void cLuxMapHandler::Reset()
{
	// Stop all sounds (deleting maps will stop world entries, but will let GUI ones live)
	cSound *pSound = gpBase->mpEngine->GetSound();
	pSound->GetSoundHandler()->StopAll(eSoundEntryType_All);

	if(mpViewport) mpViewport->SetWorld(NULL);
	SetCurrentMapForCameraViews(NULL);

	STLDeleteAll(mlstMaps);
	mpCurrentMap = NULL;

	ResumeSoundsAndMusic(); //Make sure that all sounds and music are resumed!

	msMapFolder = "";

	mbUpdateActive = true;

	mMapChangeData.mbActive = false;

	mpSavedGame->Reset();

	gpBase->mpHelpFuncs->CleanupData();

	DestroyDataCache();
}

//-----------------------------------------------------------------------

void cLuxMapHandler::OnQuit()
{
    gpBase->mpEngine->GetUpdater()->SetContainer("MainMenu");

    gpBase->mpLoadScreenHandler->DrawMenuScreen();
    
    //Destroy map
    cLuxMapHandler *mpMapHandler = gpBase->mpMapHandler;
    if(mpMapHandler->GetCurrentMap())
    {
        //Save
        gpBase->mpSaveHandler->AutoSave();
        
        mpMapHandler->DestroyMap(mpMapHandler->GetCurrentMap(),false);

        //Reset game
        gpBase->mpEngine->GetUpdater()->BroadcastMessageToAll(eUpdateableMessage_Reset);
        gpBase->SetCustomStory(NULL);
    }

    //Start up menu again
    gpBase->mpMainMenu->OnLeaveContainer("");
    gpBase->mpMainMenu->OnEnterContainer("");
}


//-----------------------------------------------------------------------

void cLuxMapHandler::LoadUserConfig()
{
	mbShowCommentary = gpBase->mpUserConfig->GetBool("Game","ShowCommentary", false);
}

void cLuxMapHandler::SaveUserConfig()
{
	gpBase->mpUserConfig->SetBool("Game","ShowCommentary", mbShowCommentary);
}

//-----------------------------------------------------------------------

void cLuxMapHandler::CreateDataCache()
{
	if(mpDataCache) DestroyDataCache();
	
	mpDataCache = hplNew(cLuxModelCache, () );
	mpDataCache->Create();
}

void cLuxMapHandler::DestroyDataCache()
{
	if(mpDataCache==NULL) return;
	hplDelete(mpDataCache);
	mpDataCache = NULL;
}

//-----------------------------------------------------------------------

void cLuxMapHandler::SetUpdateActive(bool abX)
{
	mbUpdateActive = abX;
	
	if(mpCurrentMap) mpCurrentMap->GetWorld()->SetActive(mbUpdateActive);
}

//-----------------------------------------------------------------------

void cLuxMapHandler::RenderSolid(cRendererCallbackFunctions* apFunctions)
{
	//mpViewport->GetRenderSettings()->mbLog = false;
	if(mpCurrentMap) mpCurrentMap->OnRenderSolid(apFunctions);
}

//-----------------------------------------------------------------------

void cLuxMapHandler::OnEnterContainer(const tString& asOldContainer)
{
	mpViewport->SetActive(true);
	mpViewport->SetVisible(true);
	SetCameraViewsContainerActive(true);

	if(mpCurrentMap) mpCurrentMap->GetWorld()->SetActive(true);

	ResumeSoundsAndMusic();
}

void cLuxMapHandler::OnLeaveContainer(const tString& asNewContainer)
{
	mpViewport->SetActive(false);
	mpViewport->SetVisible(false);
	SetCameraViewsContainerActive(false);

	if(mpCurrentMap) mpCurrentMap->GetWorld()->SetActive(false);
}


//-----------------------------------------------------------------------

void cLuxMapHandler::ChangeMap(const tString& asMapName, const tString& asStartPos, const tString& asStartSound, const tString& asEndSound)
{
	mMapChangeData.mbActive = true;
	mMapChangeData.msMapFile = cString::SetFileExt(asMapName, "map");
	mMapChangeData.msStartPos = asStartPos;
    mMapChangeData.msSound = asEndSound;

    gpBase->mpHelpFuncs->PlayGuiSoundData(asStartSound, eSoundEntryType_Gui);

	gpBase->mpEffectHandler->GetFade()->FadeOut(1.5f);

	gpBase->mpPlayer->SetActive(false);
}

//-----------------------------------------------------------------------

cLuxMap* cLuxMapHandler::LoadMap(const tString& asFileName, bool abLoadEntities)
{
	cLuxMap *pMap = hplNew( cLuxMap, ( FileToMapName(asFileName)) );
	
	pMap->LoadFromFile(msMapFolder+asFileName, abLoadEntities);

	mlstMaps.push_back(pMap);

	return pMap;
}
//-----------------------------------------------------------------------

void cLuxMapHandler::DestroyMap(cLuxMap* apMap, bool abLoadingSaveGame)
{
	////////////////////////////////
	//If the map do me destroyed is current, make sure it is not current
	if(mpCurrentMap == apMap) SetCurrentMap(NULL, abLoadingSaveGame, false,"");

    STLFindAndDelete(mlstMaps,apMap);
}

//-----------------------------------------------------------------------

void cLuxMapHandler::SetCurrentMap(cLuxMap* apMap, bool abRunScript, bool abFirstTime, const tString& asPlayerPos)
{
	if(mpCurrentMap == apMap) return;

	//////////////////////////////////
	//Unload stuff from previous map
    if(mpCurrentMap)
	{
		mpCurrentMap->OnLeave(abRunScript);
		
		//Leave callback for modules
		gpBase->RunModuleMessage(eLuxUpdateableMessage_DestroyWorldEntities, mpCurrentMap);
		gpBase->RunModuleMessage(eLuxUpdateableMessage_OnMapLeave, mpCurrentMap);
	}

	mpCurrentMap = apMap;

	//////////////////////////////////
	//Setup stuff for previous map
	if(mpCurrentMap)
	{
		//Enter callback for modules
		gpBase->RunModuleMessage(eLuxUpdateableMessage_CreateWorldEntities, mpCurrentMap);
		gpBase->RunModuleMessage(eLuxUpdateableMessage_OnMapEnter, mpCurrentMap);

		//Set the player position
		mpCurrentMap->PlacePlayerAtStartPos(asPlayerPos);

		//Create an automatic checkpoint
		mpCurrentMap->SetCheckPoint("_auto", asPlayerPos, "");
		
		//Map enter callback
		mpCurrentMap->OnEnter(abRunScript, abFirstTime);

		//Set this as world in viewport
		mpViewport->SetWorld(mpCurrentMap->GetWorld());
		SetCurrentMapForCameraViews(mpCurrentMap->GetWorld());

		mRenderCallback.mpPhysicsWorld = mpCurrentMap->GetPhysicsWorld();
		mRenderCallback.mpLowLevelGfx = gpBase->mpEngine->GetGraphics()->GetLowLevel();
	}
	else
	{
		//If no map, set NULL as world
		mpViewport->SetWorld(NULL);
		SetCurrentMapForCameraViews(NULL);
	}
}

//-----------------------------------------------------------------------

cLuxCameraView* cLuxMapHandler::CreateCameraView(const cVector2l& avResolution)
{
	cLuxCameraViewDesc desc;
	desc.mvResolution = avResolution;

	return CreateCameraView(desc);
}

//-----------------------------------------------------------------------

cLuxCameraView* cLuxMapHandler::CreateCameraView(const cLuxCameraViewDesc& aDesc)
{
	cLuxCameraView *pView = hplNew(cLuxCameraView, (this, aDesc));
	if(pView->IsValid() == false)
	{
		hplDelete(pView);
		return NULL;
	}

	mlstCameraViews.push_back(pView);
	ApplyViewportRenderProperties(pView->GetViewport());

	return pView;
}

//-----------------------------------------------------------------------

void cLuxMapHandler::DestroyCameraView(cLuxCameraView* apView)
{
	if(apView == NULL) return;

	tLuxCameraViewListIt it = mlstCameraViews.begin();
	for(; it != mlstCameraViews.end(); ++it)
	{
		if(*it == apView)
		{
			mlstCameraViews.erase(it);
			hplDelete(apView);
			return;
		}
	}
}

//-----------------------------------------------------------------------

void cLuxMapHandler::PauseSoundsAndMusic()
{
	if(mbPausedSoundsAndMusic) return;

	cSound *pSound = gpBase->mpEngine->GetSound();
	pSound->GetSoundHandler()->PauseAll(eSoundEntryType_All);
	pSound->GetMusicHandler()->Pause();
	mbPausedSoundsAndMusic = true;
}

void cLuxMapHandler::ResumeSoundsAndMusic()
{
	if(mbPausedSoundsAndMusic)
	{
		cSound *pSound = gpBase->mpEngine->GetSound();
		pSound->GetSoundHandler()->ResumeAll(eSoundEntryType_All);
		pSound->GetMusicHandler()->Resume();
		mbPausedSoundsAndMusic = false;
	}
}

//-----------------------------------------------------------------------

void cLuxMapHandler::ClearSaveMapCollection()
{
	mpSavedGameMutex->Lock();
		mpSavedGame->Reset();
	mpSavedGameMutex->Unlock();
}

//-----------------------------------------------------------------------

void cLuxMapHandler::SetSavedMapCollection(cLuxSavedGameMapCollection *apMaps)
{
	mpSavedGameMutex->Lock();
	{
		hplDelete(mpSavedGame);

		mpSavedGame = apMaps;
	}
	mpSavedGameMutex->Unlock();
}

//-----------------------------------------------------------------------

void cLuxMapHandler::AppLostInputFocus()
{
	PauseSoundsAndMusic();
}

//-----------------------------------------------------------------------

void cLuxMapHandler::AppGotInputFocus()
{
	ResumeSoundsAndMusic();
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxMapHandler::LoadMainConfig()
{
	mpPostEffect_Bloom->SetDisabled(gpBase->mpMainConfig->GetBool("Graphics", "PostEffectBloom", true)==false);
	mpPostEffect_ImageTrail->SetDisabled(gpBase->mpMainConfig->GetBool("Graphics", "PostEffectImageTrail", true)==false);
	mpPostEffect_Sepia->SetDisabled(gpBase->mpMainConfig->GetBool("Graphics", "PostEffectSepia", true)==false);
	mpPostEffect_RadialBlur->SetDisabled(gpBase->mpMainConfig->GetBool("Graphics", "PostEffectRadialBlur", true)==false);

	UpdateViewportRenderProperties();
}

void cLuxMapHandler::SaveMainConfig()
{
	gpBase->mpMainConfig->SetBool("Graphics", "PostEffectBloom", mpPostEffect_Bloom->IsDisabled()==false);
	gpBase->mpMainConfig->SetBool("Graphics", "PostEffectImageTrail", mpPostEffect_ImageTrail->IsDisabled()==false);
	gpBase->mpMainConfig->SetBool("Graphics", "PostEffectSepia", mpPostEffect_Sepia->IsDisabled()==false);
	gpBase->mpMainConfig->SetBool("Graphics", "PostEffectRadialBlur", mpPostEffect_RadialBlur->IsDisabled()==false);
}

//-----------------------------------------------------------------------

tString cLuxMapHandler::FileToMapName(const tString& asFile)
{
	return cString::ToLowerCase(cString::GetFileName(cString::SetFileExt(asFile, "")));
}

//-----------------------------------------------------------------------

void cLuxMapHandler::SetShowCommentary(bool abX)
{
	mbShowCommentary = abX;

	if(mbShowCommentary==false)
	{
		gpBase->mpEffectHandler->GetPlayCommentary()->Stop();
	}
}

//-----------------------------------------------------------------------

void cLuxMapHandler::CheckMapChange(float afTimeStep)
{
	if(mMapChangeData.mbActive==false) return;
	if(gpBase->mpEffectHandler->GetFade()->IsFading()) return;

	///////////////////////////////////////
	// Setup variables
    float fTimeTaken =0;

	///////////////////////////////////////
	// Fade out and disable player
	mMapChangeData.mbActive = false;
	gpBase->mpEffectHandler->GetFade()->FadeIn(2.0f);
	gpBase->mpPlayer->SetActive(true);

	///////////////////////////////////////
	// Write pending savegame queries
	cLuxSaveHandlerThreadClass* pThreadClass = gpBase->mpSaveHandler->GetThreadClass();
	if(pThreadClass->IsRunning())
		pThreadClass->ProcessPendingSaves();

	//////////////////////////////////
	//Clean up
	// Must do this before OnEnter!
	gpBase->mpHelpFuncs->CleanupData();

	///////////////////////
	// Load map
	tString sNewMapName = FileToMapName(mMapChangeData.msMapFile);
	if(mpCurrentMap->GetName() != sNewMapName)
	{
		mpSavedGameMutex->Lock();

		//////////////////////
		// Run onleave before saving!
		mpCurrentMap->RunScript("OnLeave()");//since script is not run in SetCurrenMap

		///////////////////////////////////////
		// Draw loading screen
		unsigned long lLoadStartTime = cPlatform::GetApplicationTime();
		gpBase->mpLoadScreenHandler->DrawGameScreen();

		//////////////////////
		// Save old map
		mpSavedGame->SaveMap(mpCurrentMap);

		//////////////////////
		// Fadeout sounds and disable stop (meaning they will fade even when sound entities are destroyed)
		gpBase->mpEngine->GetSound()->GetSoundHandler()->FadeOutAll(eSoundEntryType_World, 0.5f, true);
		
		//////////////////////
		// Load new map
		cLuxMap *pLastMap = mpCurrentMap;
		cLuxMap *pMap = LoadMap(mMapChangeData.msMapFile,true);
		if(pMap == NULL)
		{
			Error("Could not load map '%s'!\n", mMapChangeData.msMapFile.c_str());
			return;
		}
		
		if(pLastMap)
		{
			if (pLastMap->GetName() == "08_cellar_maze" && pMap->GetName() == "09_back_hall")
			{
				gpBase->mpAchievementHandler->UnlockAchievement(eLuxAchievement_EscapeArtist);
			}

			if (pLastMap->GetName() == "14_elevator" && pMap->GetName() == "15_prison_south")
			{
				gpBase->mpAchievementHandler->UnlockAchievement(eLuxAchievement_Descendant);
			}

			//////////////
			// HARDMODE
			if (gpBase->mbHardMode &&
				pLastMap->GetName() == "27_torture_chancel_redux" && pMap->GetName() == "28_inner_sanctum")
			{
				gpBase->mpPlayer->AddSanity(100.f, true);
			}
		}

		//////////////////////
		// Set new and destroy old
		bool bFirstTime = mpSavedGame->MapExists(sNewMapName)==false;	
		
		SetCurrentMap(pMap, false, bFirstTime, mMapChangeData.msStartPos);
		DestroyMap(pLastMap, false);

		//////////////////////
		// Load new map data
		mpSavedGame->LoadMap(mpCurrentMap);

		//////////////////////
		// Run enter script! (otherwise a save in oneter will not be correct!)
		if(bFirstTime) mpCurrentMap->RunScript("OnStart()");
		mpCurrentMap->RunScript("OnEnter()");


		mpSavedGameMutex->Unlock();

		//////////////////////////////////
		//Check if any more load time needed
		fTimeTaken = (float)(cPlatform::GetApplicationTime() - lLoadStartTime)/1000.0f;
		
		ProgLog(eLuxProgressLogLevel_High, "Entering map "+ mpCurrentMap->GetName());
	}
	///////////////////////
	// Map already loaded.
	else
	{
		mpCurrentMap->PlacePlayerAtStartPos(mMapChangeData.msStartPos);
	}

	//////////////////////////////////
	// Check if text should be left on a bit longer
	if(fTimeTaken>0)
	{
		gpBase->mpLoadScreenHandler->GameScreenLoadDone(mMapChangeData.msSound, fTimeTaken);
	}
	else
	{
		//Play finished sound
		gpBase->mpHelpFuncs->PlayGuiSoundData(mMapChangeData.msSound, eSoundEntryType_Gui);

	}
}

//-----------------------------------------------------------------------

void cLuxMapHandler::ApplyViewportRenderProperties(cViewport *apViewport)
{
	if(apViewport == NULL) return;

	cRenderSettings *pRenderSettings = apViewport->GetRenderSettings();
	if(apViewport == mpViewport || mpViewport == NULL)
	{
		pRenderSettings->mbRenderWorldReflection = gpBase->mpConfigHandler->mbWorldReflection;
		pRenderSettings->mbRenderShadows = gpBase->mpConfigHandler->mbShadowsActive;
		pRenderSettings->mbUseEdgeSmooth = gpBase->mpConfigHandler->mbEdgeSmooth;
		pRenderSettings->mbSSAOActive = gpBase->mpConfigHandler->mbSSAOActive;
		pRenderSettings->mMaxShadowMapResolution = (eShadowMapResolution)gpBase->mpConfigHandler->mlShadowRes;
		return;
	}

	cRenderSettings *pBaseRenderSettings = mpViewport->GetRenderSettings();

	pRenderSettings->mbLog = pBaseRenderSettings->mbLog;
	pRenderSettings->mClearColor = pBaseRenderSettings->mClearColor;

	pRenderSettings->mlMinimumObjectsBeforeOcclusionTesting = pBaseRenderSettings->mlMinimumObjectsBeforeOcclusionTesting;
	pRenderSettings->mlSampleVisiblilityLimit = pBaseRenderSettings->mlSampleVisiblilityLimit;
	pRenderSettings->mbClipReflectionScreenRect = pBaseRenderSettings->mbClipReflectionScreenRect;
	pRenderSettings->mbUseOcclusionCulling = pBaseRenderSettings->mbUseOcclusionCulling;
	pRenderSettings->mbUseEdgeSmooth = pBaseRenderSettings->mbUseEdgeSmooth;
	pRenderSettings->mbUseCallbacks = pBaseRenderSettings->mbUseCallbacks;
	pRenderSettings->mMaxShadowMapResolution = pBaseRenderSettings->mMaxShadowMapResolution;

	pRenderSettings->mbRenderWorldReflection = pBaseRenderSettings->mbRenderWorldReflection;
	pRenderSettings->mbRenderShadows = pBaseRenderSettings->mbRenderShadows;
	pRenderSettings->mfShadowMapBias = pBaseRenderSettings->mfShadowMapBias;
	pRenderSettings->mfShadowMapSlopeScaleBias = pBaseRenderSettings->mfShadowMapSlopeScaleBias;
	pRenderSettings->mbSSAOActive = pBaseRenderSettings->mbSSAOActive;
}

//-----------------------------------------------------------------------

void cLuxMapHandler::SetCameraViewsContainerActive(bool abX)
{
	tLuxCameraViewListIt it = mlstCameraViews.begin();
	for(; it != mlstCameraViews.end(); ++it)
	{
		(*it)->SetContainerActive(abX);
	}
}

//-----------------------------------------------------------------------

void cLuxMapHandler::SetCurrentMapForCameraViews(cWorld *apWorld)
{
	tLuxCameraViewListIt it = mlstCameraViews.begin();
	for(; it != mlstCameraViews.end(); ++it)
	{
		cLuxCameraView *pView = *it;
		if(pView->UsesCurrentMapWorld())
			pView->SetCurrentMapWorld(apWorld);
	}
}

//-----------------------------------------------------------------------
