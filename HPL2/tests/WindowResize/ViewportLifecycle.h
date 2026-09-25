#ifndef VIEWPORT_LIFECYCLE_TEST_H
#define VIEWPORT_LIFECYCLE_TEST_H

// Uses the real scene and worlds from the engine resource smoke test.
void TestViewportLifecycle(cEngine* engine)
{
    struct Callback : iViewportCallback
    {
        int pre = 0, post = 0;
        void OnPreWorldDraw() { ++pre; }
        void OnPostWorldDraw() { ++post; }
    } first, second;

    cScene* scene = engine->GetScene();
    cWorld* world = scene->CreateWorld("listener-test");
    cWorld* otherWorld = scene->CreateWorld("secondary-camera-test");
    world->SetActive(false);
    otherWorld->SetActive(false);
    cCamera* camera = scene->CreateCamera(eCameraMoveMode_Fly);
    cViewport* listener = scene->CreateViewport(camera, world);
    Check(!world->IsSoundEmitter(), "creating a viewport must not select an audio world");
    scene->SetCurrentListener(listener);
    Check(world->IsSoundEmitter(), "selecting the listener must enable its world");
    cViewport* secondary = scene->CreateViewport(NULL, world);
    secondary->SetWorld(NULL);
    Check(world->IsSoundEmitter(), "detaching a secondary camera must not silence the listener");
    secondary->SetWorld(otherWorld);
    Check(!otherWorld->IsSoundEmitter(), "secondary cameras must not enable other audio worlds");

    listener->AddViewportCallback(&first);
    listener->AddViewportCallback(&second);
    listener->RunViewportCallbackMessage(eViewportMessage_OnPreWorldDraw);
    listener->RunViewportCallbackMessage(eViewportMessage_OnPostWorldDraw);
    Check(first.pre == 1 && first.post == 1 && second.pre == 1 && second.post == 1,
          "all registered viewport callbacks must run exactly once per message");
    listener->RemoveViewportCallback(&first);
    listener->RunViewportCallbackMessage(eViewportMessage_OnPreWorldDraw);
    Check(first.pre == 1 && second.pre == 2, "removed callbacks must not run");
    listener->RemoveViewportCallback(&second);
    listener->RunViewportCallbackMessage(eViewportMessage_OnPostWorldDraw);

    scene->SetCurrentListener(secondary);
    Check(!world->IsSoundEmitter() && otherWorld->IsSoundEmitter() && !listener->IsListener(),
          "switching listeners must transfer audio ownership");
    secondary->SetWorld(world);
    Check(world->IsSoundEmitter() && !otherWorld->IsSoundEmitter(),
          "moving the listener must transfer audio ownership between worlds");
    scene->DestroyViewport(listener);
    Check(world->IsSoundEmitter(), "destroying a non-listener must preserve listener audio");
    secondary->SetCamera(camera);
    scene->DestroyViewport(secondary);
    Check(!world->IsSoundEmitter(), "destroying the listener must clear its audio ownership");
    scene->PostUpdate(0); // Must not dereference the deleted listener or its camera.
    scene->DestroyCamera(camera);
    scene->DestroyWorld(otherWorld);
    scene->DestroyWorld(world);
    std::puts("PASS: viewport callbacks, secondary-camera audio and listener destruction");
}

#if defined(_WIN32)
void TestProcessHandles()
{
    wchar_t executable[MAX_PATH] = {};
    Check(GetModuleFileNameW(NULL, executable, MAX_PATH) != 0, "resolve process test executable");
    // Warm up any platform/runtime initialization before comparing handle counts.
    Check(!cPlatform::RunProgram(L"missing-process-test.exe", L""), "missing program must report failure");
    Check(cPlatform::RunProgram(executable, L"--process-child"), "warm up successful process launch");
    DWORD before = 0, after = 0;
    Check(GetProcessHandleCount(GetCurrentProcess(), &before) != FALSE, "read initial handle count");
    for(int i = 0; i < 16; ++i)
        Check(!cPlatform::RunProgram(L"missing-process-test.exe", L""), "failed launch must remain safe");
    for(int i = 0; i < 4; ++i)
        Check(cPlatform::RunProgram(executable, L"--process-child"), "launch process test child");
    Check(GetProcessHandleCount(GetCurrentProcess(), &after) != FALSE, "read final handle count");
    std::printf("Process handles: before=%lu, after=%lu\n", before, after);
    Check(before == after,
          "successful and failed process launches must not leak handles");
    std::puts("PASS: successful and failed process-launch handle cleanup");
}
#endif
#endif
