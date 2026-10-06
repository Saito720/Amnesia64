/*
 * Copyright © 2009-2020 Frictional Games
 * This file is part of Amnesia: The Dark Descent and is licensed under GPLv3.
 */

#include "graphics/SceneObservation.h"

#include "graphics/Bitmap.h"
#include "graphics/FrameBuffer.h"
#include "graphics/GPUProgram.h"
#include "graphics/GPUShader.h"
#include "graphics/Graphics.h"
#include "graphics/LowLevelGraphics.h"
#include "graphics/Material.h"
#include "graphics/MaterialType.h"
#include "graphics/Renderable.h"
#include "graphics/Renderer.h"
#include "graphics/RenderList.h"
#include "graphics/Texture.h"
#include "graphics/VertexBuffer.h"
#include "math/Math.h"
#include "math/Frustum.h"
#include "physics/CharacterBody.h"
#include "scene/Camera.h"
#include "scene/MeshEntity.h"
#include "scene/RenderableContainer.h"
#include "scene/World.h"
#include "system/LowLevelSystem.h"

#include <cstring>

namespace hpl {
namespace {

// Embedded shaders keep this path independent of installed game shader assets.
// HPL skins meshes into their dynamic vertex buffers before they are drawn.
const char* gsObservationVertexShader =
    "#version 120\n"
    "uniform mat4 aUVMatrix;\n"
    "varying vec2 vUV;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "  vUV = (aUVMatrix * vec4(gl_MultiTexCoord0.xy, 0.0, 1.0)).xy;\n"
    "  vColor = gl_Color;\n"
    "}\n";

const char* gsObservationFragmentShader =
    "#version 120\n"
    "uniform sampler2D aDiffuse;\n"
    "uniform sampler2D aAlpha;\n"
    "uniform int aHasDiffuse;\n"
    "uniform int aHasAlpha;\n"
    "uniform int aMask;\n"
    "uniform vec4 aMaskColor;\n"
    "uniform float aAlphaCutoff;\n"
    "varying vec2 vUV;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "  vec4 color = vColor;\n"
    "  if (aHasDiffuse != 0) color *= texture2D(aDiffuse, vUV);\n"
    "  if (aHasAlpha != 0 && texture2D(aAlpha, vUV).r < 0.5) discard;\n"
    "  if (color.a < aAlphaCutoff) discard;\n"
    "  gl_FragColor = aMask != 0 ? aMaskColor : color;\n"
    "}\n";

void AddMeshObjects(cMeshEntity* apMesh, std::set<iRenderable*>& aObjects)
{
    if(apMesh == NULL) return;
    for(int i = 0; i < apMesh->GetSubMeshEntityNum(); ++i)
        aObjects.insert(apMesh->GetSubMeshEntity(i));
}

}

cSceneObservation::cSceneObservation(cGraphics* apGraphics)
    : mpRenderList(NULL), mpFrameBuffer(NULL), mpColorTexture(NULL),
      mpDepthTexture(NULL), mpProgram(NULL), mpVertexShader(NULL),
      mpFragmentShader(NULL), mpPlayerCylinder(NULL), mlHasDiffuse(-1),
      mlHasAlpha(-1), mlMask(-1), mlMaskColor(-1), mlUVMatrix(-1),
      mlAlphaCutoff(-1), mvSize(0), mbHasImage(false)
{
    mpGraphics = apGraphics;
    SetupRenderFunctions(apGraphics->GetLowLevel());
    mpRenderList = hplNew(cRenderList, ());
}

cSceneObservation::~cSceneObservation()
{
    DestroyData();
    hplDelete(mpRenderList);
}

void cSceneObservation::DestroyData()
{
    mbHasImage = false;
    mvRGBPixels.clear();
    mExcludedObjects.clear();
    mDoorObjects.clear();
    if(mpFrameBuffer) mpGraphics->DestroyFrameBuffer(mpFrameBuffer);
    if(mpColorTexture) mpGraphics->DestroyTexture(mpColorTexture);
    if(mpDepthTexture) mpGraphics->DestroyTexture(mpDepthTexture);
    if(mpProgram) mpGraphics->DestroyGpuProgram(mpProgram);
    if(mpVertexShader) hplDelete(mpVertexShader);
    if(mpFragmentShader) hplDelete(mpFragmentShader);
    if(mpPlayerCylinder) hplDelete(mpPlayerCylinder);
    mpFrameBuffer = NULL;
    mpColorTexture = NULL;
    mpDepthTexture = NULL;
    mpProgram = NULL;
    mpVertexShader = NULL;
    mpFragmentShader = NULL;
    mpPlayerCylinder = NULL;
    mvSize = cVector2l(0);
}

bool cSceneObservation::CreateProgram()
{
    mpVertexShader = mpLowLevelGraphics->CreateGpuShader("SceneObservationVertex", eGpuShaderType_Vertex);
    mpFragmentShader = mpLowLevelGraphics->CreateGpuShader("SceneObservationFragment", eGpuShaderType_Fragment);
    if(mpVertexShader == NULL || mpFragmentShader == NULL ||
       !mpVertexShader->CreateFromString(gsObservationVertexShader) ||
       !mpFragmentShader->CreateFromString(gsObservationFragmentShader))
        return false;

    mpFragmentShader->AddSamplerUnit("aDiffuse", 0);
    mpFragmentShader->AddSamplerUnit("aAlpha", 1);
    mpProgram = mpGraphics->CreateGpuProgram("SceneObservation");
    if(mpProgram == NULL) return false;
    // These shaders are created directly, rather than owned by GpuShaderManager.
    mpProgram->SetAutoDestroyShaders(false);
    mpProgram->SetShader(eGpuShaderType_Vertex, mpVertexShader);
    mpProgram->SetShader(eGpuShaderType_Fragment, mpFragmentShader);
    if(!mpProgram->Link()) return false;

    mlHasDiffuse = mpProgram->GetVariableId("aHasDiffuse");
    mlHasAlpha = mpProgram->GetVariableId("aHasAlpha");
    mlMask = mpProgram->GetVariableId("aMask");
    mlMaskColor = mpProgram->GetVariableId("aMaskColor");
    mlUVMatrix = mpProgram->GetVariableId("aUVMatrix");
    mlAlphaCutoff = mpProgram->GetVariableId("aAlphaCutoff");
    return mlHasDiffuse >= 0 && mlHasAlpha >= 0 && mlMask >= 0 &&
           mlMaskColor >= 0 && mlUVMatrix >= 0 && mlAlphaCutoff >= 0;
}

bool cSceneObservation::CreatePlayerCylinder()
{
    const int lSections = 24;
    mpPlayerCylinder = mpLowLevelGraphics->CreateVertexBuffer(
        eVertexBufferType_Hardware, eVertexBufferDrawType_Tri,
        eVertexBufferUsageType_Static);
    if(mpPlayerCylinder == NULL) return false;
    mpPlayerCylinder->CreateElementArray(eVertexBufferElement_Position, eVertexBufferElementFormat_Float, 4);
    mpPlayerCylinder->CreateElementArray(eVertexBufferElement_Color0, eVertexBufferElementFormat_Float, 4);
    mpPlayerCylinder->CreateElementArray(eVertexBufferElement_Texture0, eVertexBufferElementFormat_Float, 3);

    // Unit radius and unit height; scaled to the player's current collision body.
    for(int ring = 0; ring < 2; ++ring)
    {
        for(int i = 0; i < lSections; ++i)
        {
            float fAngle = k2Pif * (float)i / (float)lSections;
            mpPlayerCylinder->AddVertexVec3f(eVertexBufferElement_Position,
                cVector3f(cos(fAngle), ring == 0 ? -0.5f : 0.5f, sin(fAngle)));
            mpPlayerCylinder->AddVertexColor(eVertexBufferElement_Color0, cColor(1,1));
            mpPlayerCylinder->AddVertexVec3f(eVertexBufferElement_Texture0, cVector3f(0));
        }
    }
    for(int cap = 0; cap < 2; ++cap)
    {
        mpPlayerCylinder->AddVertexVec3f(eVertexBufferElement_Position,
            cVector3f(0, cap == 0 ? -0.5f : 0.5f, 0));
        mpPlayerCylinder->AddVertexColor(eVertexBufferElement_Color0, cColor(1,1));
        mpPlayerCylinder->AddVertexVec3f(eVertexBufferElement_Texture0, cVector3f(0));
    }
    for(int i = 0; i < lSections; ++i)
    {
        int next = (i + 1) % lSections;
        unsigned int indices[] = {
            (unsigned int)i, (unsigned int)next, (unsigned int)(lSections + i),
            (unsigned int)next, (unsigned int)(lSections + next), (unsigned int)(lSections + i),
            (unsigned int)(2 * lSections), (unsigned int)next, (unsigned int)i,
            (unsigned int)(2 * lSections + 1), (unsigned int)(lSections + i), (unsigned int)(lSections + next)
        };
        for(int j = 0; j < 12; ++j) mpPlayerCylinder->AddIndex(indices[j]);
    }
    return mpPlayerCylinder->Compile(0);
}

bool cSceneObservation::Initialize(const cVector2l& avSize)
{
    if(mpFrameBuffer && avSize == mvSize) return true;
    DestroyData();
    msLastError.clear();
    // Limit the allocation/readback cost, including malformed entity settings.
    if(avSize.x < 32 || avSize.y < 32 || avSize.x > 1024 || avSize.y > 1024)
    {
        msLastError = "Observation dimensions must be between 32 and 1024 pixels.";
        return false;
    }
    if(!CreateProgram() || !CreatePlayerCylinder())
    {
        msLastError = "Could not create observation shader or player mask geometry.";
        DestroyData();
        return false;
    }

    mvSize = avSize;
    mpColorTexture = mpGraphics->CreateTexture("SceneObservationRGB", eTextureType_2D, eTextureUsage_RenderTarget);
    mpDepthTexture = mpGraphics->CreateTexture("SceneObservationDepth", eTextureType_2D, eTextureUsage_RenderTarget);
    if(mpColorTexture == NULL || mpDepthTexture == NULL ||
       !mpColorTexture->CreateFromRawData(cVector3l(avSize.x, avSize.y, 1), ePixelFormat_RGBA, NULL) ||
       !mpDepthTexture->CreateFromRawData(cVector3l(avSize.x, avSize.y, 1), ePixelFormat_Depth24, NULL))
    {
        msLastError = "Could not allocate observation color/depth textures.";
        DestroyData();
        return false;
    }
    mpColorTexture->SetFilter(eTextureFilter_Nearest);
    mpColorTexture->SetWrapSTR(eTextureWrap_ClampToEdge);
    mpDepthTexture->SetFilter(eTextureFilter_Nearest);
    mpDepthTexture->SetWrapSTR(eTextureWrap_ClampToEdge);
    mpDepthTexture->SetCompareMode(eTextureCompareMode_None);
    mpFrameBuffer = mpGraphics->CreateFrameBuffer("SceneObservation");
    if(mpFrameBuffer)
    {
        mpFrameBuffer->SetTexture2D(0, mpColorTexture);
        mpFrameBuffer->SetDepthTexture2D(mpDepthTexture);
    }
    if(mpFrameBuffer == NULL || !mpFrameBuffer->CompileAndValidate())
    {
        msLastError = "Observation framebuffer is incomplete.";
        DestroyData();
        return false;
    }
    return true;
}

void cSceneObservation::CollectObjects(iRenderableContainerNode* apNode)
{
    if(apNode == NULL) return;
    apNode->UpdateBeforeUse();
    if(apNode->UsesFlagsAndVisibility() && !apNode->HasVisibleObjects()) return;
    if(mpCurrentFrustum->CollideNode(apNode) == eCollision_Outside) return;
    for(tRenderableListIt it = apNode->GetObjectList()->begin(); it != apNode->GetObjectList()->end(); ++it)
    {
        iRenderable* pObject = *it;
        if(mExcludedObjects.find(pObject) != mExcludedObjects.end() || !pObject->IsVisible() ||
           pObject->GetMaterial() == NULL || pObject->GetVertexBuffer() == NULL ||
           !pObject->CollidesWithFrustum(mpCurrentFrustum)) continue;
        // OnDraw precedes Scene's increment of the render frame counter. Objects
        // seen in the previous main view would otherwise keep stale skinned
        // vertices after the latest simulation tick. Refresh at zero elapsed
        // time; observations must never advance animations/material effects.
        if(pObject->GetRenderFrameCount() == iRenderer::GetRenderFrameCount())
            pObject->UpdateGraphicsForFrame(0);
        mpRenderList->AddObject(pObject);
    }
    for(tRenderableContainerNodeListIt it = apNode->GetChildNodeList()->begin(); it != apNode->GetChildNodeList()->end(); ++it)
        CollectObjects(*it);
}

void cSceneObservation::DrawObject(iRenderable* apObject, bool abTranslucent)
{
    cMaterial* pMaterial = apObject->GetMaterial();
    bool bDoor = mDoorObjects.find(apObject) != mDoorObjects.end();
    iTexture* pDiffuse = pMaterial->GetTexture(eMaterialTexture_Diffuse);
    iTexture* pAlpha = pMaterial->GetTexture(eMaterialTexture_Alpha);
    SetTexture(0, pDiffuse);
    SetTexture(1, pAlpha);
    mpProgram->SetInt(mlHasDiffuse, pDiffuse ? 1 : 0);
    mpProgram->SetInt(mlHasAlpha, pAlpha ? 1 : 0);
    mpProgram->SetInt(mlMask, bDoor ? 1 : 0);
    mpProgram->SetColor4f(mlMaskColor, GetDoorMaskColor());
    mpProgram->SetMatrixf(mlUVMatrix, pMaterial->HasUvAnimation() ? pMaterial->GetUvMatrix() : cMatrixf::Identity);
    mpProgram->SetFloat(mlAlphaCutoff, abTranslucent ? 0.005f :
        (pMaterial->GetAlphaMode() == eMaterialAlphaMode_Trans ? 0.5f : 0.0f));
    SetBlendMode(bDoor ? eMaterialBlendMode_None :
        (abTranslucent ? pMaterial->GetBlendMode() : eMaterialBlendMode_None));
    SetDepthWrite(!abTranslucent || bDoor);
    SetMatrix(apObject->GetModelMatrixPtr());
    SetVertexBuffer(apObject->GetVertexBuffer());
    DrawCurrent();
}

void cSceneObservation::DrawPlayer(iCharacterBody* apBody)
{
    if(apBody == NULL || !apBody->IsActive()) return;
    const cVector3f& vSize = apBody->GetSize();
    float fRadius = cMath::Max(vSize.x, vSize.z) * 0.5f;
    if(fRadius <= 0 || vSize.y <= 0) return;
    cMatrixf mtx = cMath::MatrixScale(cVector3f(fRadius, vSize.y, fRadius));
    mtx.SetTranslation(apBody->GetPosition());
    SetTextureRange(NULL, 0);
    SetBlendMode(eMaterialBlendMode_None);
    SetCullActive(false);
    SetDepthWrite(true);
    mpProgram->SetInt(mlHasDiffuse, 0);
    mpProgram->SetInt(mlHasAlpha, 0);
    mpProgram->SetInt(mlMask, 1);
    mpProgram->SetColor4f(mlMaskColor, GetPlayerMaskColor());
    mpProgram->SetMatrixf(mlUVMatrix, cMatrixf::Identity);
    mpProgram->SetFloat(mlAlphaCutoff, 0);
    SetMatrix(&mtx);
    SetVertexBuffer(mpPlayerCylinder);
    DrawCurrent();
}

bool cSceneObservation::ReadRGB()
{
    cBitmap* pBitmap = mpLowLevelGraphics->CopyFrameBufferToBitmap(cVector2l(0), mvSize);
    if(pBitmap == NULL) return false;
    bool bValid = pBitmap->GetWidth() == mvSize.x && pBitmap->GetHeight() == mvSize.y &&
                  pBitmap->GetPixelFormat() == ePixelFormat_RGBA &&
                  pBitmap->GetBytesPerPixel() == 4 && pBitmap->GetData(0,0) &&
                  pBitmap->GetData(0,0)->mpData;
    if(bValid)
    {
        const unsigned char* pPixels = pBitmap->GetData(0,0)->mpData;
        mvRGBPixels.resize((size_t)mvSize.x * (size_t)mvSize.y * 3);
        for(int y = 0; y < mvSize.y; ++y)
        {
            const unsigned char* pSrc = pPixels + (size_t)(mvSize.y - 1 - y) * (size_t)mvSize.x * 4;
            unsigned char* pDest = &mvRGBPixels[(size_t)y * (size_t)mvSize.x * 3];
            for(int x = 0; x < mvSize.x; ++x)
            {
                std::memcpy(pDest, pSrc, 3);
                pSrc += 4;
                pDest += 3;
            }
        }
    }
    hplDelete(pBitmap);
    return bValid;
}

bool cSceneObservation::Capture(cWorld* apWorld, cCamera* apCamera,
    cMeshEntity* apExcludedMesh, iCharacterBody* apPlayerBody,
    const std::vector<cMeshEntity*>& avMaskedDoors)
{
    mbHasImage = false;
    mvRGBPixels.clear();
    if(mpFrameBuffer == NULL || apWorld == NULL || apCamera == NULL)
    {
        msLastError = "Observation renderer, world, or camera is unavailable.";
        return false;
    }
    msLastError.clear();
    mExcludedObjects.clear();
    mDoorObjects.clear();
    AddMeshObjects(apExcludedMesh, mExcludedObjects);
    for(size_t i = 0; i < avMaskedDoors.size(); ++i) AddMeshObjects(avMaskedDoors[i], mDoorObjects);

    iFrameBuffer* pPreviousBuffer = mpLowLevelGraphics->GetCurrentFrameBuffer();
    mpLowLevelGraphics->PushMatrix(eMatrix_Projection);
    mpLowLevelGraphics->PushMatrix(eMatrix_ModelView);
    cRenderTarget target;
    target.mpFrameBuffer = mpFrameBuffer;
    target.mvSize = mvSize;
    InitAndResetRenderFunctions(apCamera->GetFrustum(), &target, false);

    // Establish all relevant states at this standalone pre-world render boundary.
    mpLowLevelGraphics->SetCurrentFrameBuffer(mpFrameBuffer);
    mpLowLevelGraphics->SetColorWriteActive(true, true, true, true);
    mpLowLevelGraphics->SetDepthTestActive(true);
    mpLowLevelGraphics->SetDepthWriteActive(true);
    mpLowLevelGraphics->SetDepthTestFunc(eDepthTestFunc_LessOrEqual);
    mpLowLevelGraphics->SetCullActive(true);
    mpLowLevelGraphics->SetCullMode(eCullMode_CounterClockwise);
    mpLowLevelGraphics->SetStencilActive(false);
    mpLowLevelGraphics->SetScissorActive(false);
    mpLowLevelGraphics->SetPolygonOffsetActive(false);
    for(int i = 0; i < kMaxClipPlanes; ++i) mpLowLevelGraphics->SetClipPlaneActive(i, false);
    for(int i = 0; i < kMaxTextureUnits; ++i) mpLowLevelGraphics->SetTexture(i, NULL);
    mpLowLevelGraphics->SetColor(cColor(1,1));
    SetAlphaMode(eMaterialAlphaMode_Solid);
    SetBlendMode(eMaterialBlendMode_None);
    mpLowLevelGraphics->SetClearColor(cColor(0.08f, 0.08f, 0.08f, 1));
    mpLowLevelGraphics->SetClearDepth(1);
    mpLowLevelGraphics->ClearFrameBuffer(eClearFrameBufferFlag_Color | eClearFrameBufferFlag_Depth);
    // Preserve the cleared opaque alpha so the GUI preview exactly shows the
    // RGB submitted to inference, including translucent surface compositing.
    SetChannelMode(eMaterialChannelMode_RGB);
    SetNormalFrustumProjection();
    SetProgram(mpProgram);

    mpRenderList->Clear();
    mpRenderList->Setup(0, mpCurrentFrustum);
    for(int i = 0; i < eWorldContainerType_LastEnum; ++i)
    {
        iRenderableContainer* pContainer = apWorld->GetRenderableContainer((eWorldContainerType)i);
        pContainer->UpdateBeforeRendering();
        CollectObjects(pContainer->GetRoot());
    }
    mpRenderList->Compile(eRenderListCompileFlag_Diffuse | eRenderListCompileFlag_Decal | eRenderListCompileFlag_Translucent);
    cRenderableVecIterator solidIt = mpRenderList->GetArrayIterator(eRenderListType_Diffuse);
    while(solidIt.HasNext()) DrawObject(solidIt.Next(), false);

    SetDepthWrite(false);
    cRenderableVecIterator decalIt = mpRenderList->GetArrayIterator(eRenderListType_Decal);
    while(decalIt.HasNext()) DrawObject(decalIt.Next(), true);
    cRenderableVecIterator transIt = mpRenderList->GetArrayIterator(eRenderListType_Translucent);
    while(transIt.HasNext())
    {
        iRenderable* pObject = transIt.Next();
        if(!pObject->UpdateGraphicsForViewport(mpCurrentFrustum, 0)) continue;
        pObject->SetModelMatrixPtr(pObject->GetModelMatrix(mpCurrentFrustum));
        DrawObject(pObject, true);
    }

    // A physical proxy with normal scene depth testing, never a through-wall overlay.
    DrawPlayer(apPlayerBody);
    mbHasImage = ReadRGB();
    if(!mbHasImage) msLastError = "Could not read the final observation RGB pixels.";

    SetVertexBuffer(NULL);
    SetProgram(NULL);
    SetTextureRange(NULL, 0);
    SetBlendMode(eMaterialBlendMode_None);
    SetChannelMode(eMaterialChannelMode_RGBA);
    SetDepthWrite(true);
    SetCullActive(true);
    mpLowLevelGraphics->PopMatrix(eMatrix_ModelView);
    mpLowLevelGraphics->PopMatrix(eMatrix_Projection);
    mpLowLevelGraphics->SetCurrentFrameBuffer(pPreviousBuffer);
    ExitAndCleanUpRenderFunctions();
    mExcludedObjects.clear();
    mDoorObjects.clear();
    mpRenderList->Clear();
    return mbHasImage;
}

}
