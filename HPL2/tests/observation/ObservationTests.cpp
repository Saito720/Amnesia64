/* Synthetic scene tests using a real OpenGL context and Newton character body.
 * No retail game data, models, textures, or shader assets are required.
 */
#include "hpl.h"
#include "graphics/SceneObservation.h"
#include "graphics/MaterialType.h"
#include "graphics/Renderer.h"
#include "graphics/SubMesh.h"
#include "impl/LowLevelGraphicsSDL.h"

#include <cstdio>
#include <cstdlib>
#include <crtdbg.h>
#include <vector>
#undef main

using namespace hpl;

static int glChecks = 0;
static void Require(bool abResult, const char* asLabel)
{
    ++glChecks;
    if(abResult) return;
    std::fprintf(stderr, "FAIL: %s\n", asLabel);
    std::exit(2);
}

class cTestMaterialType : public iMaterialType
{
public:
    cTestMaterialType(cGraphics* apGraphics, cResources* apResources, bool abTranslucent)
        : iMaterialType(apGraphics, apResources) { mbIsTranslucent = abTranslucent; }
    void DestroyProgram(cMaterial*, eMaterialRenderMode, iGpuProgram*, char) {}
    bool SupportsHWSkinning() { return false; }
    iTexture* GetTextureForUnit(cMaterial*, eMaterialRenderMode, int) { return NULL; }
    iGpuProgram* GetGpuProgram(cMaterial*, eMaterialRenderMode, char) { return NULL; }
    void SetupTypeSpecificData(eMaterialRenderMode, iGpuProgram*, iRenderer*) {}
    void SetupMaterialSpecificData(eMaterialRenderMode, iGpuProgram*, cMaterial*, iRenderer*) {}
    void SetupObjectSpecificData(eMaterialRenderMode, iGpuProgram*, iRenderable*, iRenderer*) {}
    void LoadData() {}
    void DestroyData() {}
    iMaterialVars* CreateSpecificVariables() { return NULL; }
    void LoadVariables(cMaterial*, cResourceVarsObject*) {}
    void GetVariableValues(cMaterial*, cResourceVarsObject*) {}
    void CompileMaterialSpecifics(cMaterial*) {}
};

static cMeshEntity* Box(cEngine* apEngine, cWorld* apWorld, const char* asName,
    const cVector3f& avSize, const cVector3f& avPosition, const cColor& aColor,
    iMaterialType* apType)
{
    cGraphics* pGraphics = apEngine->GetGraphics();
    cResources* pResources = apEngine->GetResources();
    unsigned char pixels[] = { (unsigned char)(aColor.r * 255), (unsigned char)(aColor.g * 255),
                              (unsigned char)(aColor.b * 255), (unsigned char)(aColor.a * 255) };
    iTexture* pTexture = pGraphics->CreateTexture(asName, eTextureType_2D, eTextureUsage_Normal);
    Require(pTexture->CreateFromRawData(cVector3l(1,1,1), ePixelFormat_RGBA, pixels), "create synthetic diffuse texture");
    cMaterial* pMaterial = pResources->GetMaterialManager()->CreateCustomMaterial(asName, apType);
    pMaterial->SetAutoDestroyTextures(false);
    pMaterial->SetTexture(eMaterialTexture_Diffuse, pTexture);
    pMaterial->SetBlendMode(eMaterialBlendMode_Alpha);
    cMesh* pMesh = hplNew(cMesh, (asName, cString::To16Char(asName),
        pResources->GetMaterialManager(), pResources->GetAnimationManager()));
    pMesh->IncUserCount();
    cSubMesh* pSubMesh = pMesh->CreateSubMesh("Box");
    pSubMesh->SetMaterial(pMaterial);
    pSubMesh->SetVertexBuffer(pGraphics->GetMeshCreator()->CreateBoxVertexBuffer(avSize));
    pSubMesh->Compile();
    cMeshEntity* pEntity = apWorld->CreateMeshEntity(asName, pMesh, false);
    pEntity->SetPosition(avPosition);
    return pEntity;
}

static int CountColor(const cSceneObservation& aImage, unsigned char ar, unsigned char ag,
    unsigned char ab, float* apMeanY = NULL)
{
    int lCount = 0;
    double fSumY = 0;
    const std::vector<unsigned char>& pixels = aImage.GetRGBPixels();
    for(int y = 0; y < aImage.GetSize().y; ++y)
    for(int x = 0; x < aImage.GetSize().x; ++x)
    {
        size_t i = ((size_t)y * aImage.GetSize().x + x) * 3;
        if(std::abs((int)pixels[i] - ar) <= 2 && std::abs((int)pixels[i + 1] - ag) <= 2 &&
           std::abs((int)pixels[i + 2] - ab) <= 2)
        {
            ++lCount;
            fSumY += y;
        }
    }
    if(apMeanY) *apMeanY = lCount > 0 ? (float)(fSumY / lCount) : -1;
    return lCount;
}

static void Capture(cSceneObservation& aImage, cWorld* apWorld, cCamera* apCamera,
    cMeshEntity* apSelf, iCharacterBody* apPlayer, const std::vector<cMeshEntity*>& avDoors)
{
    // Do not increment the global render frame counter: this reproduces the
    // actual game's capture point immediately before Scene::Render.
    bool bSuccess = aImage.Capture(apWorld, apCamera, apSelf, apPlayer, avDoors);
    if(!bSuccess) std::fprintf(stderr, "Capture error: %s\n", aImage.GetLastError().c_str());
    Require(bSuccess && aImage.HasImage(), "capture complete RGB/depth observation");
    Require(aImage.GetRGBPixels().size() == (size_t)aImage.GetSize().x * aImage.GetSize().y * 3,
        "packed RGB payload matches capture dimensions");
    Require(apSelf->IsVisible() && apSelf->GetSubMeshEntity(0)->IsVisible(),
        "excluding self never changes world mesh visibility");
    Require(apCamera->GetPosition() == cVector3f(0,1,0) && apCamera->GetYaw() == 0,
        "capture does not move camera");
}

static bool GuiPixelsMatch(const std::vector<unsigned char>& avRGBA,
    const cSceneObservation& aImage, bool abInverted = false)
{
    const cVector2l& size = aImage.GetSize();
    const std::vector<unsigned char>& rgb = aImage.GetRGBPixels();
    for(int y = 0; y < size.y; ++y)
    for(int x = 0; x < size.x; ++x)
    {
        // glReadPixels starts at the bottom; the observation RGB starts at the top.
        size_t source = ((size_t)(abInverted ? y : size.y - 1 - y) * size.x + x) * 4;
        size_t target = ((size_t)y * size.x + x) * 3;
        for(int channel = 0; channel < 3; ++channel)
            if(avRGBA[source + channel] != rgb[target + channel]) return false;
    }
    return true;
}

static void CheckGuiPreview(cEngine* apEngine, const cSceneObservation& aImage)
{
    cGraphics* pGraphics = apEngine->GetGraphics();
    iLowLevelGraphics* pLowLevel = pGraphics->GetLowLevel();
    cGui* pGui = apEngine->GetGui();
    const cVector2l& size = aImage.GetSize();
    iTexture* pColor = pGraphics->CreateTexture("ObservationGuiTest",
        eTextureType_2D, eTextureUsage_RenderTarget);
    Require(pColor && pColor->CreateFromRawData(cVector3l(size.x,size.y,1), ePixelFormat_RGBA, NULL),
        "allocate independent GUI preview target");
    iFrameBuffer* pTarget = pGraphics->CreateFrameBuffer("ObservationGuiTest");
    Require(pTarget != NULL, "create GUI preview framebuffer");
    pTarget->SetTexture2D(0, pColor);
    Require(pTarget->CompileAndValidate(), "validate GUI preview framebuffer");

    // Match the debug panel's actual borrowed-texture path, including the GUI's
    // automatic render-target UV flip. The observation source stays unchanged.
    cGuiGfxElement* pGfx = pGui->CreateGfxTexture(aImage.GetColorTexture(), false, eGuiMaterial_Diffuse);
    Require(pGfx && pGfx->GetFlipUvYAxis(), "GUI automatically flips render-target texture coordinates");
    cGuiSet* pSet = pGui->CreateSet("ObservationGuiTest", NULL);
    pSet->SetVirtualSize(cVector2f((float)size.x,(float)size.y), -1, 1);
    pSet->SetDrawMouse(false);
    iFrameBuffer* pPrevious = pLowLevel->GetCurrentFrameBuffer();
    pLowLevel->PushMatrix(eMatrix_Projection);
    pLowLevel->PushMatrix(eMatrix_ModelView);
    pLowLevel->SetCurrentFrameBuffer(pTarget);
    std::vector<unsigned char> pixels((size_t)size.x * size.y * 4);
    for(int pass = 0; pass < 2; ++pass)
    {
        if(pass == 1) pGfx->SetFlipUvYAxis(true); // Reproduce the former debug-panel bug.
        pLowLevel->SetColorWriteActive(true,true,true,true);
        pLowLevel->SetClearColor(cColor(0,1));
        pLowLevel->ClearFrameBuffer(eClearFrameBufferFlag_Color);
        pSet->DrawGfx(pGfx, cVector3f(0), cVector2f((float)size.x,(float)size.y));
        pSet->Render(NULL);
        glReadPixels(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if(pass == 0)
            Require(GuiPixelsMatch(pixels, aImage),
                "actual GUI preview equals top-down RGB, including red top and blue bottom");
        else
            Require(!GuiPixelsMatch(pixels, aImage) && GuiPixelsMatch(pixels, aImage, true),
                "an extra explicit UV flip reproduces the upside-down preview");
        pSet->ClearRenderObjects();
    }
    pLowLevel->SetTexture(0, NULL);
    pLowLevel->SetBlendActive(false);
    pLowLevel->SetDepthTestActive(true);
    pLowLevel->SetDepthWriteActive(true);
    pLowLevel->PopMatrix(eMatrix_ModelView);
    pLowLevel->PopMatrix(eMatrix_Projection);
    pLowLevel->SetCurrentFrameBuffer(pPrevious);
    pGui->DestroySet(pSet);
    pGui->DestroyGfx(pGfx);
    pGui->OnPostBufferSwap(); // Release the deferred borrowed GUI element before its source.
    pGraphics->DestroyFrameBuffer(pTarget);
    pGraphics->DestroyTexture(pColor);
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetLogFile(L"observation-tests.log");
    cEngineInitVars vars;
    vars.mSound.mbUseThreading = false;
    vars.mSound.mbUseHRTF = false;
    // Initialize managers/physics without the installed game renderers/assets.
    cEngine* pEngine = CreateHPLEngine(eHplAPI_OpenGL, 0, &vars);
    Require(pEngine != NULL, "initialize engine without retail assets");
    cGraphics* pGraphics = pEngine->GetGraphics();
    iLowLevelGraphics* pLowLevel = pGraphics->GetLowLevel();
    Require(pLowLevel->Init(320, 240, 0, 32, false, 0, eGpuProgramFormat_GLSL,
        "HPL2 observation verification", cVector2l(-10000)), "initialize real OpenGL context");
    for(unsigned int i = 1; i < 32; ++i)
    {
        SDL_Window* pWindow = SDL_GetWindowFromID(i);
        if(pWindow) SDL_HideWindow(pWindow);
    }
    cResources* pResources = pEngine->GetResources();
    iMaterialType* pSolidType = hplNew(cTestMaterialType, (pGraphics, pResources, false));
    iMaterialType* pTransType = hplNew(cTestMaterialType, (pGraphics, pResources, true));
    pGraphics->AddMaterialType(pSolidType, "ObservationTestSolid");
    pGraphics->AddMaterialType(pTransType, "ObservationTestTransparent");
    cWorld* pWorld = pEngine->GetScene()->CreateWorld("ObservationTest");
    iPhysicsWorld* pPhysics = pEngine->GetPhysics()->CreateWorld(true);
    pWorld->SetPhysicsWorld(pPhysics);
    pPhysics->SetWorldSize(cVector3f(-100), cVector3f(100));
    iCharacterBody* pPlayer = pPhysics->CreateCharacterBody("Player", cVector3f(0.8f,2,0.8f));
    pPlayer->SetPosition(cVector3f(0,1,-5));
    cCamera* pCamera = pEngine->GetScene()->CreateCamera(eCameraMoveMode_Walk);
    pCamera->SetPosition(cVector3f(0,1,0));
    pCamera->SetFOV(cMath::ToRad(60));
    pCamera->SetAspect(160.0f/96.0f);
    pCamera->SetNearClipPlane(0.05f);
    pCamera->SetFarClipPlane(50);

    cMeshEntity* pSelf = Box(pEngine, pWorld, "Self", cVector3f(8,8,0.2f), cVector3f(0,1,-1), cColor(1,1,0,1), pSolidType);
    cMeshEntity* pWall = Box(pEngine, pWorld, "Wall", cVector3f(8,8,0.2f), cVector3f(0,1,-2), cColor(0.25f,0.25f,0.25f,1), pSolidType);
    cMeshEntity* pDoor = Box(pEngine, pWorld, "Door", cVector3f(1.4f,2.5f,0.2f), cVector3f(0,1,-3), cColor(0.4f,0.2f,0.1f,0.25f), pTransType);
    Box(pEngine, pWorld, "TopRed", cVector3f(0.7f), cVector3f(2,2,-4), cColor(1,0,0,0.25f), pSolidType);
    Box(pEngine, pWorld, "BottomBlue", cVector3f(0.7f), cVector3f(2,0,-4), cColor(0,0,1,0.25f), pSolidType);
    pWall->SetVisible(false);
    pDoor->SetVisible(false);
    pWorld->Compile(false);
    std::vector<cMeshEntity*> doors(1, pDoor);

    GLuint lLastColor = 0, lLastDepth = 0;
    {
        cSceneObservation image(pGraphics);
        Require(!image.Initialize(cVector2l(0)), "invalid resolution rejected without allocation");
        Require(!image.Initialize(cVector2l(2049,864)) &&
            !image.Initialize(cVector2l(1280,2049)), "oversized axes rejected before texture allocation");
        Require(image.Initialize(cVector2l(2048,32)), "upper supported width initializes its framebuffer");
        Require(image.Initialize(cVector2l(1280,864)), "full-detail default initializes beyond the former 1024-pixel limit");
        pCamera->SetAspect(1280.0f/864.0f);
        Capture(image, pWorld, pCamera, pSelf, pPlayer, doors);
        Require(image.GetRGBPixels().size() == size_t(1280)*864*3 &&
            CountColor(image,255,0,255)>50, "full-detail observation preserves packed RGB and visible player mask");
        float fFullRedY = 0, fFullBlueY = 0;
        Require(CountColor(image,255,0,0,&fFullRedY)>10 &&
            CountColor(image,0,0,255,&fFullBlueY)>10 &&
            fFullRedY<432 && fFullBlueY>432, "full-detail RGB retains upright scene orientation");
        pCamera->SetAspect(160.0f/96.0f);
        Require(image.Initialize(cVector2l(160,96)), "create private observation framebuffer");
        Capture(image, pWorld, pCamera, pSelf, pPlayer, doors);
        Require(CountColor(image, 255,0,255) > 50, "directly visible player produces opaque magenta mask");
        Require(CountColor(image, 255,255,0) == 0, "self mesh excluded from observation");
        float fRedY = 0, fBlueY = 0;
        Require(CountColor(image, 255,0,0, &fRedY) > 10 && CountColor(image, 0,0,255, &fBlueY) > 10,
            "unlit diffuse colors survive low-alpha opaque materials");
        Require(fRedY < 48 && fBlueY > 48, "RGB first row is the top of the camera image");

        std::vector<unsigned char> texturePixels(160 * 96 * 4);
        glBindTexture(GL_TEXTURE_2D, image.GetColorTexture()->GetCurrentLowlevelHandle());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texturePixels.data());
        bool bExact = true, bOpaque = true;
        for(int y = 0; y < 96; ++y)
        for(int x = 0; x < 160; ++x)
        {
            size_t source = ((size_t)(95 - y) * 160 + x) * 4;
            size_t rgb = ((size_t)y * 160 + x) * 3;
            bExact = bExact && texturePixels[source] == image.GetRGBPixels()[rgb] &&
                texturePixels[source + 1] == image.GetRGBPixels()[rgb + 1] &&
                texturePixels[source + 2] == image.GetRGBPixels()[rgb + 2];
            bOpaque = bOpaque && texturePixels[source + 3] == 255;
        }
        Require(bExact, "preview texture equals exact submitted RGB after V flip");
        Require(bOpaque, "preview alpha remains opaque throughout the observation");
        std::vector<float> depth(160 * 96);
        glBindTexture(GL_TEXTURE_2D, image.GetDepthTexture()->GetCurrentLowlevelHandle());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
        Require(depth[48 * 160 + 80] > 0 && depth[48 * 160 + 80] < 1,
            "retained depth contains visible player geometry");
        glBindTexture(GL_TEXTURE_2D, 0);
        CheckGuiPreview(pEngine, image);

        pDoor->SetVisible(true);
        Capture(image, pWorld, pCamera, pSelf, pPlayer, doors);
        Require(CountColor(image, 0,255,255) > 50, "visible breakable door produces opaque cyan mask");
        Require(CountColor(image, 255,0,255) == 0, "masked door blocks player even with translucent source material");
        pWall->SetVisible(true);
        Capture(image, pWorld, pCamera, pSelf, pPlayer, doors);
        Require(CountColor(image, 255,0,255) == 0 && CountColor(image, 0,255,255) == 0,
            "opaque wall occludes both player and door masks");
        pWall->SetVisible(false);
        pDoor->SetVisible(false);
        pPlayer->SetPosition(cVector3f(30,1,-5));
        Capture(image, pWorld, pCamera, pSelf, pPlayer, doors);
        Require(CountColor(image, 255,0,255) == 0, "player outside camera FOV produces no mask");
        pPlayer->SetPosition(cVector3f(0,1,-5));
        Require(image.Capture(pWorld, pCamera, NULL, pPlayer, doors), "capture without exclusion");
        Require(CountColor(image, 255,255,0) > 1000 && CountColor(image, 255,0,255) == 0,
            "same self mesh remains renderable and occludes normally in other views");
        for(int i = 0; i < 12; ++i) Capture(image, pWorld, pCamera, pSelf, pPlayer, doors);
        Require(image.GetDepthTexture()->GetPixelFormat() == ePixelFormat_Depth24,
            "depth is independently retained rather than a main renderer buffer");
        Require(pLowLevel->GetCurrentFrameBuffer() == NULL, "capture restores previous framebuffer");
        lLastColor = image.GetColorTexture()->GetCurrentLowlevelHandle();
        lLastDepth = image.GetDepthTexture()->GetCurrentLowlevelHandle();
    }
    Require(glIsTexture(lLastColor) == GL_FALSE && glIsTexture(lLastDepth) == GL_FALSE,
        "observation destruction releases owned color and depth textures");
    for(int i = 0; i < 4; ++i)
    {
        cSceneObservation image(pGraphics);
        Require(image.Initialize(cVector2l(160,96)), "recreate observation resources");
        Capture(image, pWorld, pCamera, pSelf, pPlayer, doors);
    }
    Require(glGetError() == GL_NO_ERROR, "capture/recreation leaves no OpenGL error");
    pEngine->GetScene()->DestroyWorld(pWorld);
    pEngine->GetScene()->DestroyCamera(pCamera);
    DestroyHPLEngine(pEngine);
    std::printf("PASS: %d checks using real OpenGL scene observations\n", glChecks);
    return 0;
}

int hplMain(const tString&) { return main(); }
