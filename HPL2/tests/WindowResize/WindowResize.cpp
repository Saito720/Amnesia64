// Native SDL/OpenGL smoke test. --engine uses staged core assets; no profiles or configuration are loaded.
#define SDL_MAIN_HANDLED
#include "impl/LowLevelGraphicsSDL.h"
#include "impl/FrameBufferGL.h"
#include "graphics/Bitmap.h"
#include "engine/Engine.h"
#include "engine/EngineInitVars.h"
#include "engine/Updater.h"
#include "graphics/Graphics.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Texture.h"
#include "graphics/RendererDeferred.h"
#include "graphics/PostEffectComposite.h"
#include "graphics/PostEffect_Bloom.h"
#include "graphics/PostEffect_ImageTrail.h"
#include "scene/Scene.h"
#include "scene/Viewport.h"
#include "gui/Gui.h"
#include "gui/GuiSet.h"
#include "system/LowLevelSystem.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#include "SDL2/SDL_syswm.h"
#include <atomic>
#include <thread>
#endif

using namespace hpl;

namespace
{
    int gChecks = 0;

    void Check(bool condition, const char* message)
    {
        ++gChecks;
        if (!condition) throw std::runtime_error(message);
    }

    void CheckNoGlError(const char* stage)
    {
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            char message[256];
            std::snprintf(message, sizeof(message), "%s: OpenGL error 0x%04x", stage, error);
            throw std::runtime_error(message);
        }
        ++gChecks;
    }

    SDL_Window* CurrentWindow()
    {
        SDL_Window* window = SDL_GL_GetCurrentWindow();
        Check(window != NULL, "graphics initialization must make its SDL window current");
        // Keep this automated test from covering the user's applications.
        SDL_HideWindow(window);
        return window;
    }

    cVector2l WindowSize(SDL_Window* window)
    {
        cVector2l size;
        SDL_GetWindowSize(window, &size.x, &size.y);
        return size;
    }

    void PumpEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
    }

    void CheckWindowed(cLowLevelGraphicsSDL& graphics, SDL_Window* window)
    {
        const Uint32 flags = SDL_GetWindowFlags(window);
        Check((flags & SDL_WINDOW_RESIZABLE) != 0, "windowed mode must have native resize borders");
        Check((flags & SDL_WINDOW_FULLSCREEN) == 0, "windowed mode must stay windowed");
        Check(!graphics.GetFullscreenModeActive(), "engine must report windowed mode");
    }

    void DrawRectangle(float left, float bottom, float right, float top,
                       float red, float green, float blue)
    {
        glColor3f(red, green, blue);
        glBegin(GL_QUADS);
        glVertex2f(left, bottom);
        glVertex2f(right, bottom);
        glVertex2f(right, top);
        glVertex2f(left, top);
        glEnd();
    }

    void CheckPixel(int x, int y, int red, int green, int blue)
    {
        unsigned char pixel[4] = {};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (pixel[0] != red || pixel[1] != green || pixel[2] != blue)
        {
            char message[256];
            std::snprintf(message, sizeof(message),
                "pixel (%d,%d): expected (%d,%d,%d), got (%d,%d,%d)",
                x, y, red, green, blue, pixel[0], pixel[1], pixel[2]);
            throw std::runtime_error(message);
        }
        ++gChecks;
    }

    void DrawQuadrants(iLowLevelGraphics& graphics, const cVector2l& size)
    {
        graphics.SetScissorActive(false);
        graphics.SetColorWriteActive(true, true, true, true);
        graphics.SetCullActive(false);
        graphics.SetDepthTestActive(false);
        graphics.SetAlphaTestActive(false);
        graphics.SetBlendActive(false);
        for (int unit = 0; unit < kMaxTextureUnits; ++unit) graphics.SetTexture(unit, NULL);
        graphics.SetActiveTextureUnit(0);
        graphics.SetClearColor(cColor(0, 0, 0, 1));
        graphics.ClearFrameBuffer(eClearFrameBufferFlag_Color);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, size.x, 0, size.y, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const float halfWidth = size.x * 0.5f;
        const float halfHeight = size.y * 0.5f;
        DrawRectangle(0, 0, halfWidth, halfHeight, 1, 0, 0);
        DrawRectangle(halfWidth, 0, float(size.x), halfHeight, 0, 1, 0);
        DrawRectangle(0, halfHeight, halfWidth, float(size.y), 0, 0, 1);
        DrawRectangle(halfWidth, halfHeight, float(size.x), float(size.y), 1, 1, 0);
    }

    void CheckRenderedFrame(cLowLevelGraphicsSDL& graphics, SDL_Window* window)
    {
        const cVector2l size = WindowSize(window);
        const cVector2l screen = graphics.GetScreenSizeInt();
        Check(screen == size, "engine screen dimensions must follow the actual window");
        const cVector2f screenFloat = graphics.GetScreenSizeFloat();
        Check(screenFloat.x == float(size.x) && screenFloat.y == float(size.y),
              "integer and floating point screen dimensions must agree");

        graphics.SetCurrentFrameBuffer(NULL);
        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        Check(viewport[0] == 0 && viewport[1] == 0 && viewport[2] == size.x && viewport[3] == size.y,
              "default framebuffer viewport must use the current resolution");
        GLint framebuffer = -1;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &framebuffer);
        Check(framebuffer == 0, "window rendering must use its real framebuffer");

        DrawQuadrants(graphics, size);

        // Read the rendered back buffer before swapping: its post-swap contents
        // are not defined on all OpenGL drivers. Corners catch stale viewports.
        glReadBuffer(GL_BACK);
        CheckPixel(1, 1, 255, 0, 0);
        CheckPixel(size.x - 2, 1, 0, 255, 0);
        CheckPixel(1, size.y - 2, 0, 0, 255);
        CheckPixel(size.x - 2, size.y - 2, 255, 255, 0);
        CheckPixel(size.x / 4, size.y / 4, 255, 0, 0);
        CheckPixel(size.x * 3 / 4, size.y / 4, 0, 255, 0);
        CheckPixel(size.x / 4, size.y * 3 / 4, 0, 0, 255);
        CheckPixel(size.x * 3 / 4, size.y * 3 / 4, 255, 255, 0);

        cBitmap* screenshot = graphics.CopyFrameBufferToBitmap();
        Check(screenshot->GetWidth() == size.x && screenshot->GetHeight() == size.y,
              "framebuffer copies must use the resized resolution");
        hplDelete(screenshot);
        CheckNoGlError("render and framebuffer copy");
        graphics.SwapBuffers();
        CheckNoGlError("swap resized window");
    }

    void Initialize(cLowLevelGraphicsSDL& graphics, int width, int height, bool fullscreen)
    {
        Check(graphics.Init(width, height, 0, 32, fullscreen, 0,
                            eGpuProgramFormat_GLSL, "Amnesia window resize smoke test", cVector2l(-1)),
              "graphics initialization failed");
        graphics.SetVsyncActive(false, false);
        // Some compatibility drivers leave GL_INVALID_ENUM after glewInit.
        // The test checks errors from every operation after initialization.
        while (glGetError() != GL_NO_ERROR) {}
    }

    void TestWindowResize()
    {
        cLowLevelGraphicsSDL graphics;
        Initialize(graphics, 640, 480, false);
        SDL_Window* window = CurrentWindow();
        CheckWindowed(graphics, window);
        Check(WindowSize(window) == cVector2l(640, 480), "launch must use the requested resolution");
        Check(!graphics.UpdateScreenSize(), "unchanged window must not request a resource rebuild");
        CheckRenderedFrame(graphics, window);

        const cVector2l sizes[] = {
            cVector2l(800, 450), cVector2l(400, 600), cVector2l(320, 240),
            cVector2l(960, 720), cVector2l(640, 480)
        };
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
        {
            SDL_SetWindowSize(window, sizes[i].x, sizes[i].y);
            PumpEvents();
            Check(WindowSize(window) == sizes[i], "SDL must accept the requested test size");
            Check(graphics.UpdateScreenSize(), "a changed window must request a resource rebuild");
            Check(!graphics.UpdateScreenSize(), "one resize must be reported only once");
            CheckWindowed(graphics, window);
            CheckRenderedFrame(graphics, window);
            std::printf("PASS: resize and render %d x %d\n", sizes[i].x, sizes[i].y);
        }
        SDL_SetWindowSize(window, 1, 1);
        PumpEvents();
        Check(graphics.UpdateScreenSize(), "minimum-size resize must be reported");
        Check(WindowSize(window) == cVector2l(320, 240), "tiny resizes must clamp to a valid minimum");
        CheckRenderedFrame(graphics, window);
    }

    void TestRelaunch()
    {
        cLowLevelGraphicsSDL graphics;
        Initialize(graphics, 640, 480, false);
        SDL_Window* window = CurrentWindow();
        Check(WindowSize(window) == cVector2l(640, 480),
              "a new launch must use its requested resolution, not the previous resized window");
        CheckRenderedFrame(graphics, window);
        std::puts("PASS: relaunch uses 640 x 480");
    }

    void TestDesktopResolution()
    {
        SDL_DisplayMode desktop;
        Check(SDL_GetDesktopDisplayMode(0, &desktop) == 0, "must be able to read the desktop resolution");
        cLowLevelGraphicsSDL graphics;
        Initialize(graphics, 0, 0, false);
        SDL_Window* window = CurrentWindow();
        CheckWindowed(graphics, window);
        Check(graphics.GetScreenSizeInt() == cVector2l(desktop.w, desktop.h),
              "0,0 windowed launch must select the desktop resolution");
        CheckRenderedFrame(graphics, window);
        std::printf("PASS: 0,0 windowed launch selects desktop %d x %d\n", desktop.w, desktop.h);
    }

    #include "NativeWindowResize.h"

    void TestFullscreen()
    {
        cLowLevelGraphicsSDL graphics;
        Initialize(graphics, 0, 0, true);
        SDL_Window* window = CurrentWindow();
        const Uint32 flags = SDL_GetWindowFlags(window);
        Check((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP,
              "0,0 fullscreen must retain desktop fullscreen behavior");
        Check((flags & SDL_WINDOW_RESIZABLE) == 0, "fullscreen must not have native resize borders");
        Check(graphics.GetFullscreenModeActive(), "engine must report fullscreen mode");
        Check(!graphics.UpdateScreenSize(), "fullscreen must not request a window resize rebuild");
#if defined(_WIN32)
        const HWND native = NativeWindow(window);
        const RECT bounds = NativeRect(native);
        NativeSizeCommand(native, WMSZ_RIGHT);
        Check(GetCapture() != native && SameNativeRect(bounds, NativeRect(native)),
              "fullscreen must reject native sizing without entering a modal loop or changing its bounds");
#endif
        CheckNoGlError("fullscreen initialization");
        std::puts("PASS: fullscreen remains fullscreen");
    }

    void CheckBuffer(iFrameBuffer* buffer, const cVector2l& size)
    {
        Check(buffer->GetSize() == size, "render target dimensions must follow the resized screen");
        Check(buffer->CompileAndValidate(), "resized framebuffer attachments must remain complete");
    }

    void CheckTextureStorage(iLowLevelGraphics* graphics, iTexture* texture, const cVector2l& size)
    {
        graphics->SetTexture(0, texture);
        GLint width = 0, height = 0;
        const GLenum target = GetGLTextureTargetEnum(texture->GetType());
        glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &height);
        graphics->SetTexture(0, NULL);
        Check(width == size.x && height == size.y, "actual GPU texture storage must use the new dimensions");
    }

    void CheckPostEffectPixel(int x, int y, bool red, bool green, bool blue)
    {
        unsigned char pixel[4] = {};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        const bool expected[] = { red, green, blue };
        for (int channel = 0; channel < 3; ++channel)
        {
            if (expected[channel] ? pixel[channel] < 64 : pixel[channel] > 8)
            {
                char message[256];
                std::snprintf(message, sizeof(message),
                    "post-effect pixel (%d,%d): incorrect quadrant color (%d,%d,%d)",
                    x, y, pixel[0], pixel[1], pixel[2]);
                throw std::runtime_error(message);
            }
        }
        ++gChecks;
    }

    void TestEngineResources()
    {
        cRendererDeferred::SetSSAOLoaded(true);
        cRendererDeferred::SetEdgeSmoothLoaded(true);
        cEngineInitVars variables;
        variables.mGraphics.mvScreenSize = cVector2l(640, 480);
        variables.mGraphics.mbFullscreen = false;
        variables.mGraphics.msWindowCaption = "Amnesia renderer resize smoke test";
        variables.mSound.mbUseHRTF = false;
        variables.mSound.mbUseThreading = false;
        cEngine* engine = CreateHPLEngine(eHplAPI_OpenGL, eHplSetup_Screen, &variables);
        Check(engine != NULL, "full engine creation must succeed");
        try
        {
            SDL_Window* window = CurrentWindow();
            cGraphics* graphics = engine->GetGraphics();
            iLowLevelGraphics* lowLevel = graphics->GetLowLevel();
            lowLevel->SetVsyncActive(false, false);
            while (glGetError() != GL_NO_ERROR) {}
            cRendererDeferred* renderer = static_cast<cRendererDeferred*>(graphics->GetRenderer(eRenderer_Main));
            cScene* scene = engine->GetScene();
            cCamera* camera = scene->CreateCamera(eCameraMoveMode_Fly);
            cViewport* viewport = scene->CreateViewport(camera, scene->CreateWorld("ResizeTest"));
            viewport->GetRenderSettings()->mbSSAOActive = true;
            viewport->GetRenderSettings()->mbUseEdgeSmooth = true;
            cCamera* fixedCamera = scene->CreateCamera(eCameraMoveMode_Fly);
            fixedCamera->SetAspect(2.0f);
            cViewport* fixedViewport = scene->CreateViewport(fixedCamera);
            fixedViewport->SetSize(cVector2l(200, 100));
            fixedViewport->SetVisible(false);

            cGui* gui = engine->GetGui();
            cGuiSet* screenGui = gui->CreateSet("ScreenGui", NULL);
            cGuiSet* fixedGui = gui->CreateSet("FixedGui", NULL);
            screenGui->SetDrawMouse(false);
            fixedGui->SetDrawMouse(false);
            fixedGui->SetVirtualSize(cVector2f(800, 600), -1, 1);

            iFrameBuffer* full = graphics->GetScreenTempFrameBuffer(1, ePixelFormat_RGBA, 17);
            iFrameBuffer* quarter = graphics->GetScreenTempFrameBuffer(4, ePixelFormat_RGBA, 17);
            iFrameBuffer* fixed = graphics->GetTempFrameBuffer(cVector2l(64, 64), ePixelFormat_RGBA, 17);
            iTexture* fullTexture = full->GetColorBuffer(0)->ToTexture();
            iTexture* quarterTexture = quarter->GetColorBuffer(0)->ToTexture();
            const int fullHandle = fullTexture->GetCurrentLowlevelHandle();
            const int quarterHandle = quarterTexture->GetCurrentLowlevelHandle();
            iFrameBuffer* accumulation = renderer->GetAccumBuffer();
            iTexture* accumulationTexture = renderer->GetPostEffectTexture();
            const int accumulationHandle = accumulationTexture->GetCurrentLowlevelHandle();

            cPostEffectComposite* composite = graphics->CreatePostEffectComposite();
            cPostEffectParams_Bloom bloom;
            cPostEffectParams_ImageTrail trail;
            composite->AddPostEffect(graphics->CreatePostEffect(&bloom), 2);
            composite->AddPostEffect(graphics->CreatePostEffect(&trail), 1);
            viewport->SetPostEffectComposite(composite);
            scene->Render(1.0f / 60.0f, tSceneRenderFlag_All);
            CheckNoGlError("initial full renderer and post effects");

            const cVector2l sizes[] = {
                cVector2l(800, 450), cVector2l(400, 600), cVector2l(320, 240),
                cVector2l(960, 720), cVector2l(641, 481), cVector2l(640, 480)
            };
            for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
            {
                SDL_SetWindowSize(window, sizes[i].x, sizes[i].y);
                PumpEvents();
                Check(lowLevel->UpdateScreenSize(), "full renderer must observe the resized window");
                // Use the same broadcast as the engine's frame loop, exercising
                // registered scene, graphics and GUI owners in their real order.
                engine->GetUpdater()->BroadcastMessageToAll(eUpdateableMessage_OnScreenResize);
                CheckNoGlError("resize notification and graphics resource rebuild");
                Check(lowLevel->GetScreenSizeInt() == sizes[i], "full renderer screen dimensions must update");
                Check(graphics->GetScreenTempFrameBuffer(1, ePixelFormat_RGBA, 17) == full,
                      "full-size borrowed framebuffer pointer must remain valid");
                Check(graphics->GetScreenTempFrameBuffer(4, ePixelFormat_RGBA, 17) == quarter,
                      "quarter-size borrowed framebuffer pointer must remain valid");
                Check(full->GetColorBuffer(0)->ToTexture() == fullTexture &&
                      fullTexture->GetCurrentLowlevelHandle() == fullHandle,
                      "full-size texture object and graphics handle must remain valid");
                Check(quarter->GetColorBuffer(0)->ToTexture() == quarterTexture &&
                      quarterTexture->GetCurrentLowlevelHandle() == quarterHandle,
                      "quarter-size texture object and graphics handle must remain valid");
                CheckBuffer(full, sizes[i]);
                CheckBuffer(quarter, sizes[i] / 4);
                CheckBuffer(fixed, cVector2l(64, 64));
                Check(fullTexture->GetSizeInt2D() == sizes[i], "full-size texture storage must resize");
                Check(quarterTexture->GetSizeInt2D() == sizes[i] / 4, "quarter-size texture storage must resize");
                CheckTextureStorage(lowLevel, fullTexture, sizes[i]);
                CheckTextureStorage(lowLevel, quarterTexture, sizes[i] / 4);
                Check(renderer->GetAccumBuffer() == accumulation, "renderer accumulation framebuffer must stay valid");
                Check(renderer->GetPostEffectTexture() == accumulationTexture &&
                      accumulationTexture->GetCurrentLowlevelHandle() == accumulationHandle,
                      "renderer accumulation texture and graphics handle must remain valid");
                CheckBuffer(accumulation, sizes[i]);
                Check(renderer->GetDepthStencilBuffer()->GetSize() == sizes[i],
                      "depth/stencil attachment dimensions must resize");
                GLint oldRenderbuffer = 0, depthWidth = 0, depthHeight = 0;
                glGetIntegerv(GL_RENDERBUFFER_BINDING_EXT, &oldRenderbuffer);
                glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,
                    static_cast<cDepthStencilBufferGL*>(renderer->GetDepthStencilBuffer())->GetHandle());
                glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_WIDTH_EXT, &depthWidth);
                glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_HEIGHT_EXT, &depthHeight);
                glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, oldRenderbuffer);
                Check(depthWidth == sizes[i].x && depthHeight == sizes[i].y,
                      "actual GPU depth storage must use the new dimensions");
                Check(std::fabs(camera->GetAspect() - float(sizes[i].x) / sizes[i].y) < 0.0001f,
                      "screen camera aspect must follow the current window");
                Check(fixedCamera->GetAspect() == 2.0f, "explicitly sized viewport camera aspect must remain unchanged");
                Check(screenGui->GetVirtualSize() == cVector2f(float(sizes[i].x), float(sizes[i].y)),
                      "screen-sized GUI must follow the current resolution");
                Check(fixedGui->GetVirtualSize() == cVector2f(800, 600), "explicit GUI coordinates must remain unchanged");
                gui->SetFocus(fixedGui);
                gui->SendMousePos(cVector2l(sizes[i].x / 2, sizes[i].y / 2), 0);
                const cVector2f expectedMouse(float(sizes[i].x / 2) * 800 / sizes[i].x,
                                              float(sizes[i].y / 2) * 600 / sizes[i].y);
                const cVector2f mouse = fixedGui->GetMousePos();
                Check(std::fabs(mouse.x - expectedMouse.x) < 0.01f && std::fabs(mouse.y - expectedMouse.y) < 0.01f,
                      "GUI mouse normalization must use the current resolution");

                lowLevel->SetCurrentFrameBuffer(NULL);
                scene->Render(1.0f / 60.0f, tSceneRenderFlag_All);
                // GetGbufferTexture uses the settings from the last render.
                for (int attachment = 0; attachment < cRendererDeferred::GetNumOfGBufferTextures(); ++attachment)
                {
                    Check(renderer->GetGbufferTexture(attachment)->GetSizeInt2D() == sizes[i],
                          "deferred G-buffer attachment dimensions must resize");
                    CheckTextureStorage(lowLevel, renderer->GetGbufferTexture(attachment), sizes[i]);
                }
                CheckNoGlError("deferred renderer, SSAO, edge smoothing, bloom and image trail after resize");

                // Feed a known image through the real bloom/image-trail chain.
                // Pixel checks catch old texture coordinates or cropped output
                // even when every target reports the expected dimensions.
                lowLevel->SetCurrentFrameBuffer(full);
                DrawQuadrants(*lowLevel, sizes[i]);
                composite->Render(1.0f / 60.0f, camera->GetFrustum(), fullTexture, viewport->GetRenderTarget());
                lowLevel->SetCurrentFrameBuffer(NULL);
                glReadBuffer(GL_BACK);
                CheckPostEffectPixel(2, 2, true, false, false);
                CheckPostEffectPixel(sizes[i].x - 3, 2, false, true, false);
                CheckPostEffectPixel(2, sizes[i].y - 3, false, false, true);
                CheckPostEffectPixel(sizes[i].x - 3, sizes[i].y - 3, true, true, false);
                CheckPostEffectPixel(sizes[i].x / 4, sizes[i].y / 4, true, false, false);
                CheckPostEffectPixel(sizes[i].x * 3 / 4, sizes[i].y / 4, false, true, false);
                CheckPostEffectPixel(sizes[i].x / 4, sizes[i].y * 3 / 4, false, false, true);
                CheckPostEffectPixel(sizes[i].x * 3 / 4, sizes[i].y * 3 / 4, true, true, false);
                CheckNoGlError("resized post-effect output pixels");
                lowLevel->SwapBuffers();
                CheckNoGlError("full renderer swap");
                std::printf("PASS: renderer resources, camera, GUI and post effects %d x %d\n", sizes[i].x, sizes[i].y);
            }
        }
        catch (...)
        {
            DestroyHPLEngine(engine);
            throw;
        }
        DestroyHPLEngine(engine);
        std::puts("PASS: resized engine resource teardown");
    }
}

// HPL2's Windows entry point shares an object file with the logging functions.
int hplMain(const hpl::tString&) { return 0; }

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int cursorState = SDL_ShowCursor(SDL_QUERY);
    int result = 0;
    try
    {
        TestWindowResize();
        TestNativeWindowResize();
        TestRelaunch();
        TestDesktopResolution();
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--fullscreen") == 0) { TestFullscreen(); TestNativeWindowStates(); }
            if (std::strcmp(argv[i], "--engine") == 0) TestEngineResources();
        }
        std::printf("PASS: %d checks\n", gChecks);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        result = 1;
    }
    SDL_ShowCursor(cursorState);
    SDL_Quit();
    return result;
}
