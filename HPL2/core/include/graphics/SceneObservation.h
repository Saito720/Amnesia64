/*
 * Copyright © 2009-2020 Frictional Games
 * This file is part of Amnesia: The Dark Descent and is licensed under GPLv3.
 */

#ifndef HPL_SCENE_OBSERVATION_H
#define HPL_SCENE_OBSERVATION_H

#include "graphics/RenderFunctions.h"
#include "system/SystemTypes.h"

#include <set>

namespace hpl {

class cWorld;
class cCamera;
class cMeshEntity;
class cRenderList;
class iCharacterBody;
class iRenderable;
class iRenderableContainerNode;
class iGpuShader;
class iFrameBuffer;

/**
 * An independent, unlit, offscreen scene observation. No viewport is registered
 * with the scene, and neither the sound listener nor the deferred GBuffer is
 * changed. Capture must run on the graphics thread before Scene::Render, never
 * from a callback inside an active renderer.
 *
 * The final RGB array is packed RGB8, with the first row at the top. The color
 * texture is the matching OpenGL render texture: flip its V coordinates when
 * displaying it in the GUI. The depth texture contains hardware depth in [0,1]
 * for the same camera and final masks; retain the camera projection when using
 * it to reconstruct positions. Textures/arrays remain owned by this instance.
 */
class cSceneObservation : private iRenderFunctions
{
public:
    cSceneObservation(cGraphics* apGraphics);
    ~cSceneObservation();

    bool Initialize(const cVector2l& avSize);
    bool Capture(cWorld* apWorld, cCamera* apCamera,
                 cMeshEntity* apExcludedMesh, iCharacterBody* apPlayerBody,
                 const std::vector<cMeshEntity*>& avMaskedDoors);

    bool HasImage() const { return mbHasImage; }
    bool IsInitialized() const { return mpFrameBuffer != NULL; }
    iTexture* GetColorTexture() const { return mbHasImage ? mpColorTexture : NULL; }
    iTexture* GetDepthTexture() const { return mbHasImage ? mpDepthTexture : NULL; }
    const std::vector<unsigned char>& GetRGBPixels() const { return mvRGBPixels; }
    const cVector2l& GetSize() const { return mvSize; }
    const tString& GetLastError() const { return msLastError; }

    static cColor GetPlayerMaskColor() { return cColor(1.0f, 0.0f, 1.0f, 1.0f); }
    static cColor GetDoorMaskColor() { return cColor(0.0f, 1.0f, 1.0f, 1.0f); }

private:
    cSceneObservation(const cSceneObservation&);
    cSceneObservation& operator=(const cSceneObservation&);

    void DestroyData();
    bool CreateProgram();
    bool CreatePlayerCylinder();
    void CollectObjects(iRenderableContainerNode* apNode);
    void DrawObject(iRenderable* apObject, bool abTranslucent);
    void DrawPlayer(iCharacterBody* apBody);
    bool ReadRGB();

    cRenderList* mpRenderList;
    iFrameBuffer* mpFrameBuffer;
    iTexture* mpColorTexture;
    iTexture* mpDepthTexture;
    iGpuProgram* mpProgram;
    iGpuShader* mpVertexShader;
    iGpuShader* mpFragmentShader;
    iVertexBuffer* mpPlayerCylinder;

    int mlHasDiffuse;
    int mlHasAlpha;
    int mlMask;
    int mlMaskColor;
    int mlUVMatrix;
    int mlAlphaCutoff;

    std::set<iRenderable*> mExcludedObjects;
    std::set<iRenderable*> mDoorObjects;
    std::vector<unsigned char> mvRGBPixels;
    cVector2l mvSize;
    tString msLastError;
    bool mbHasImage;
};

}
#endif
