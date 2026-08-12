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

#ifndef LUX_MAP_HANDLER_H
#define LUX_MAP_HANDLER_H

//----------------------------------------------

#include "LuxBase.h"

//----------------------------------------------

class cLuxMap;
class cLuxSavedGameMapCollection;
class cLuxModelCache;
class cLuxMapHandler;
class cLuxCameraView;

typedef std::list<cLuxMap*> tLuxMapList;
typedef tLuxMapList::iterator tLuxMapListIt;
typedef std::list<cLuxCameraView*> tLuxCameraViewList;
typedef tLuxCameraViewList::iterator tLuxCameraViewListIt;

//----------------------------------------------

class cLuxCameraViewDesc
{
public:
	cLuxCameraViewDesc();

	cVector2l mvResolution;
	float mfFOV;
	float mfNearClipPlane;
	float mfFarClipPlane;
	cVector3f mvPosition;
	cMatrixf m_mtxRotation;
	cWorld *mpWorld;
	eRenderer mRenderer;
	eCameraMoveMode mMoveMode;
	bool mbActive;
	bool mbVisible;
	bool mbPushFront;
};

//----------------------------------------------

class cLuxCameraView
{
friend class cLuxMapHandler;
public:
	cLuxCameraView(cLuxMapHandler *apMapHandler, const cLuxCameraViewDesc& aDesc);
	~cLuxCameraView();

	bool IsValid(){ return mbValid; }

	cCamera* GetCamera(){ return mpCamera; }
	cViewport* GetViewport(){ return mpViewport; }
	iFrameBuffer* GetFrameBuffer(){ return mpFrameBuffer; }
	iTexture* GetRenderTexture(){ return mpRenderTexture; }
	const cVector2l& GetResolution(){ return mvResolution; }

	void SetActive(bool abX);
	bool IsActive(){ return mbActive; }
	void SetVisible(bool abX);
	bool IsVisible(){ return mbVisible; }

	void SetWorld(cWorld *apWorld);
	void SetUseCurrentMapWorld(bool abX);
	bool UsesCurrentMapWorld(){ return mbUseCurrentMapWorld; }

	void SetPosition(const cVector3f& avPosition);
	void SetRotationMatrix(const cMatrixf& a_mtxRotation);
	void SetTransform(const cVector3f& avPosition, const cMatrixf& a_mtxRotation);
	void SetFOV(float afFOV);
	void SetClipPlanes(float afNearClipPlane, float afFarClipPlane);

private:
	bool Init(const cLuxCameraViewDesc& aDesc);
	void ApplyActiveState();
	void SetContainerActive(bool abX);
	void SetCurrentMapWorld(cWorld *apWorld);
	void SetViewportWorld(cWorld *apWorld);
	void DestroyResources();

	cLuxMapHandler *mpMapHandler;
	cCamera *mpCamera;
	cViewport *mpViewport;
	iFrameBuffer *mpFrameBuffer;
	iTexture *mpRenderTexture;
	iDepthStencilBuffer *mpDepthStencilBuffer;
	cVector2l mvResolution;
	bool mbActive;
	bool mbVisible;
	bool mbContainerActive;
	bool mbUseCurrentMapWorld;
	bool mbValid;
};

//----------------------------------------------

class cMapHandlerSoundCallback : public iSoundEntityGlobalCallback
{
public:
	cMapHandlerSoundCallback();

	void OnStart(cSoundEntity *apSoundEntity);

private:
	tStringVec mvEnemyHearableSounds;
};

//----------------------------------------------

class cLuxDebugRenderCallback : public iRendererCallback
{
public:
	cLuxDebugRenderCallback();

	void OnPostSolidDraw(cRendererCallbackFunctions* apFunctions);

	void OnPostTranslucentDraw(cRendererCallbackFunctions* apFunctions);

	iPhysicsWorld* mpPhysicsWorld;
	iLowLevelGraphics* mpLowLevelGfx;
};


//----------------------------------------------

class cLuxMapHandler_ChangeMap
{
public:
	cLuxMapHandler_ChangeMap() : mbActive(false){}

	bool mbActive;
	tString msMapFile;
	tString msStartPos;
	tString msSound;
};

//----------------------------------------------

class cLuxMapHandler : public iLuxUpdateable
{
friend class cMapHandlerSoundCallback;
public:	
	cLuxMapHandler();
	~cLuxMapHandler();
	
	void OnStart();
	void Update(float afTimeStep);
	void Reset();
    void OnQuit();

	void LoadUserConfig();
	void SaveUserConfig();

	void CreateDataCache();
	void DestroyDataCache();

	void UpdateViewportRenderProperties();

	void SetUpdateActive(bool abX);
	
	void RenderSolid(cRendererCallbackFunctions* apFunctions);

	void OnEnterContainer(const tString& asOldContainer);
	void OnLeaveContainer(const tString& asNewContainer);

	void ChangeMap(const tString& asMapName, const tString& asStartPos, const tString& asStartSound, const tString& asEndSound);

	bool MapIsLoaded(){ return mpCurrentMap != NULL;}

	cLuxMap* LoadMap(const tString& asName, bool abLoadEntities);
	void DestroyMap(cLuxMap* apMap, bool abRunScript);

	void SetCurrentMap(cLuxMap* apMap, bool abRunScript, bool abFirstTime, const tString& asPlayerPos);
	cLuxMap* GetCurrentMap(){ return mpCurrentMap;}

	cViewport* GetViewport(){ return mpViewport;}

	cLuxCameraView* CreateCameraView(const cVector2l& avResolution);
	cLuxCameraView* CreateCameraView(const cLuxCameraViewDesc& aDesc);
	void DestroyCameraView(cLuxCameraView* apView);

	const tString& GetMapFolder(){ return msMapFolder;}
	void SetMapFolder(const tString& asFolder){ msMapFolder = asFolder;}

	void PauseSoundsAndMusic();
	void ResumeSoundsAndMusic();

	iPostEffect *GetPostEffect_Bloom(){ return mpPostEffect_Bloom;}
	iPostEffect *GetPostEffect_ImageTrail(){ return mpPostEffect_ImageTrail;}
	iPostEffect *GetPostEffect_Sepia(){ return mpPostEffect_Sepia;}
	iPostEffect *GetPostEffect_RadialBlur(){ return mpPostEffect_RadialBlur;}

	void ClearSaveMapCollection();
	cLuxSavedGameMapCollection *GetSavedMapCollection(){ return mpSavedGame;}
	void SetSavedMapCollection(cLuxSavedGameMapCollection *apMaps);

	tString FileToMapName(const tString& asFile);

	void SetShowCommentary(bool abX);
	bool GetShowCommentary(){ return mbShowCommentary;}

	void AppLostInputFocus();
	void AppGotInputFocus();

	//////////////////////////////////
	// Used to lock the SavedMapCollection
    iMutex *mpSavedGameMutex; 
private:
	void LoadMainConfig();
	void SaveMainConfig();

	void CheckMapChange(float afTimeStep);
	void ApplyViewportRenderProperties(cViewport *apViewport);
	void SetCameraViewsContainerActive(bool abX);
	void SetCurrentMapForCameraViews(cWorld *apWorld);

	cLuxDebugRenderCallback mRenderCallback;

	tString msMapFolder;

	cLuxModelCache *mpDataCache;

	cLuxMap* mpCurrentMap;

	tLuxMapList mlstMaps;
	tLuxCameraViewList mlstCameraViews;

	cViewport *mpViewport;
	cMapHandlerSoundCallback* mpSoundCallback;

	bool mbPausedSoundsAndMusic;

	bool mbUpdateActive;

	bool mbShowCommentary;

	iPostEffect *mpPostEffect_Bloom;
	iPostEffect *mpPostEffect_ImageTrail;
	iPostEffect *mpPostEffect_Sepia;
	iPostEffect *mpPostEffect_RadialBlur;

	cLuxMapHandler_ChangeMap mMapChangeData;

	cLuxSavedGameMapCollection *mpSavedGame;
};

//----------------------------------------------


#endif // LUX_MAP_HANDLER_H
