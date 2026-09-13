// Included inside the smoke test namespace so native geometry checks use the
// same check counter. Messages target only this test's hidden SDL window.
#if defined(_WIN32)
    HWND NativeWindow(SDL_Window* window)
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        Check(SDL_GetWindowWMInfo(window, &info) == SDL_TRUE, "native resize test needs its own window handle");
        return info.info.win.window;
    }

    RECT NativeRect(HWND window)
    {
        RECT rect = {};
        Check(GetWindowRect(window, &rect) != FALSE, "native resize test must read its window bounds");
        return rect;
    }

    bool SameNativeRect(const RECT& a, const RECT& b)
    {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }

    void NativeWindowCommand(HWND window, UINT message, WPARAM command, LPARAM position)
    {
        HANDLE done = CreateEvent(NULL, TRUE, FALSE, NULL);
        Check(done != NULL, "native resize watchdog event must be created");
        std::atomic<bool> timedOut(false);
        // The pre-fix native modal loop blocks SendMessage. Cancel that loop
        // from a bounded helper so a regression produces a failure, not a hang.
        std::thread watchdog([window, done, &timedOut]() {
            if(WaitForSingleObject(done, 1500) == WAIT_TIMEOUT)
            {
                timedOut = true;
                PostMessage(window, WM_CANCELMODE, 0, 0);
                PostMessage(window, WM_KEYDOWN, VK_ESCAPE, 0);
            }
        });
        const Uint32 start = SDL_GetTicks();
        SendMessage(window, message, command, position);
        const Uint32 elapsed = SDL_GetTicks() - start;
        SetEvent(done);
        watchdog.join();
        CloseHandle(done);
        Check(!timedOut && elapsed < 500, "native resize entry must return immediately to the outer event loop");
    }

    void NativeSizeCommand(HWND window, unsigned edge)
    {
        NativeWindowCommand(window, WM_SYSCOMMAND, SC_SIZE | edge, 0);
    }

    void NativeKeyMessage(HWND window, UINT message, UINT key)
    {
        // SDL derives most keys from the scan-code bits, not wParam alone.
        LPARAM data = 1 | (static_cast<LPARAM>(MapVirtualKey(key, MAPVK_VK_TO_VSC)) << 16);
        if(message == WM_KEYUP) data |= static_cast<LPARAM>(3) << 30;
        SendMessage(window, message, key, data);
    }

    POINT BeginNativeSize(HWND window, unsigned edge)
    {
        POINT pointer = {};
        Check(GetCursorPos(&pointer) != FALSE, "native resize test must read the current pointer without moving it");
        NativeSizeCommand(window, edge);
        Check(GetCapture() == window, "native resize must capture only its own window");
        return pointer;
    }

    void NativeResizeMotion(HWND window, POINT origin, int dx, int dy)
    {
        origin.x += dx;
        origin.y += dy;
        Check(ScreenToClient(window, &origin) != FALSE, "native resize motion must use current client coordinates");
        SendMessage(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(origin.x, origin.y));
    }

    void EndNativeSize(HWND window)
    {
        SendMessage(window, WM_LBUTTONUP, 0, 0);
        Check(GetCapture() != window, "committing native resize must release mouse capture");
        PumpEvents();
    }

    void ResetNativeSize(SDL_Window* window, cLowLevelGraphicsSDL& graphics)
    {
        SDL_SetWindowSize(window, 640, 480);
        PumpEvents();
        graphics.UpdateScreenSize();
        Check(WindowSize(window) == cVector2l(640, 480), "native resize case must start at its known size");
    }

    void TestNativeWindowResize()
    {
        cLowLevelGraphicsSDL graphics;
        Initialize(graphics, 640, 480, false);
        SDL_Window* window = CurrentWindow();
        const HWND native = NativeWindow(window);

        // Exercise the actual non-client entry path too: DefWindowProc and
        // SDL perform extra capture bookkeeping around the nested SC_SIZE.
        POINT borderPointer = {};
        Check(GetCursorPos(&borderPointer) != FALSE, "native border test must read its pointer origin");
        NativeWindowCommand(native, WM_NCLBUTTONDOWN, HTRIGHT, MAKELPARAM(borderPointer.x, borderPointer.y));
        Check(GetCapture() == native, "native border-click entry must leave nonmodal resize capture active");
        NativeResizeMotion(native, borderPointer, 40, 0);
        EndNativeSize(native);
        Check(WindowSize(window) == cVector2l(680, 480), "native border-click entry must resize the expected edge");

        for(unsigned edge = WMSZ_LEFT; edge <= WMSZ_BOTTOMRIGHT; ++edge)
        {
            ResetNativeSize(window, graphics);
            const RECT before = NativeRect(native);
            const bool left = edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT;
            const bool right = edge == WMSZ_RIGHT || edge == WMSZ_TOPRIGHT || edge == WMSZ_BOTTOMRIGHT;
            const bool top = edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT;
            const bool bottom = edge == WMSZ_BOTTOM || edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT;
            const POINT pointer = BeginNativeSize(native, edge);
            NativeResizeMotion(native, pointer, left ? -40 : right ? 40 : 0, top ? -30 : bottom ? 30 : 0);
            const RECT after = NativeRect(native);
            Check(after.left == before.left - (left ? 40 : 0) && after.right == before.right + (right ? 40 : 0) &&
                  after.top == before.top - (top ? 30 : 0) && after.bottom == before.bottom + (bottom ? 30 : 0),
                  "each native edge must move only the requested sides and anchor the opposite sides");
            EndNativeSize(native);
            Check(WindowSize(window) == cVector2l(640 + (left || right ? 40 : 0), 480 + (top || bottom ? 30 : 0)),
                  "native edge geometry must reach the SDL client dimensions");
            Check(graphics.UpdateScreenSize(), "native size change must reach the engine screen-size poll");
            CheckRenderedFrame(graphics, window);
        }

        ResetNativeSize(window, graphics);
        POINT pointer = BeginNativeSize(native, WMSZ_BOTTOMRIGHT);
        NativeResizeMotion(native, pointer, -2000, -2000);
        EndNativeSize(native);
        Check(WindowSize(window) == cVector2l(320, 240), "native sizing must honor SDL's minimum client size");

        ResetNativeSize(window, graphics);
        SDL_SetWindowMaximumSize(window, 700, 520);
        pointer = BeginNativeSize(native, WMSZ_BOTTOMRIGHT);
        NativeResizeMotion(native, pointer, 2000, 2000);
        EndNativeSize(native);
        Check(WindowSize(window) == cVector2l(700, 520), "native sizing must honor SDL's maximum client size");

        ResetNativeSize(window, graphics);
        const RECT initial = NativeRect(native);
        pointer = BeginNativeSize(native, WMSZ_TOPLEFT);
        NativeResizeMotion(native, pointer, -30, -20);
        NativeKeyMessage(native, WM_KEYDOWN, VK_ESCAPE);
        Check(SameNativeRect(initial, NativeRect(native)), "Escape must restore pre-resize size and position");
        Check(GetCapture() != native, "Escape must release native resize capture");
        NativeKeyMessage(native, WM_KEYUP, VK_ESCAPE);

        ResetNativeSize(window, graphics);
        BeginNativeSize(native, 0);
        NativeKeyMessage(native, WM_KEYDOWN, VK_RIGHT); // Choose the right edge.
        NativeKeyMessage(native, WM_KEYDOWN, VK_RIGHT); // Move the chosen edge.
        Check(WindowSize(window).x > 640 && WindowSize(window).y == 480,
              "system-menu sizing must support choosing and moving an edge with the keyboard");
        NativeKeyMessage(native, WM_KEYDOWN, VK_RETURN);
        Check(GetCapture() != native, "Enter must commit keyboard sizing and release capture");
        NativeKeyMessage(native, WM_KEYUP, VK_RETURN);

        const UINT finishKeys[] = { VK_ESCAPE, VK_RETURN };
        for(size_t i = 0; i < sizeof(finishKeys) / sizeof(finishKeys[0]); ++i)
        {
            ResetNativeSize(window, graphics);
            BeginNativeSize(native, WMSZ_RIGHT);
            PumpEvents();
            NativeKeyMessage(native, WM_KEYDOWN, finishKeys[i]);
            graphics.UpdateScreenSize();
            NativeKeyMessage(native, WM_KEYDOWN, finishKeys[i]);
            SDL_Event finishEvent;
            bool repeatedFinishKey = false;
            while(SDL_PollEvent(&finishEvent))
                if(finishEvent.type == SDL_KEYDOWN) repeatedFinishKey = true;
            Check(!repeatedFinishKey, "the key ending sizing must not repeat into game input before its release");
            // Its release may happen in another application. Focus loss must
            // clear suppression so the first new key press is usable on return.
            SendMessage(native, WM_KILLFOCUS, 0, 0);
            NativeKeyMessage(native, WM_KEYDOWN, finishKeys[i]);
            NativeKeyMessage(native, WM_KEYUP, finishKeys[i]);
            bool freshFinishKey = false;
            const SDL_Scancode expected = finishKeys[i] == VK_ESCAPE ? SDL_SCANCODE_ESCAPE : SDL_SCANCODE_RETURN;
            while(SDL_PollEvent(&finishEvent))
                if(finishEvent.type == SDL_KEYDOWN && finishEvent.key.keysym.scancode == expected) freshFinishKey = true;
            Check(freshFinishKey, "focus loss after ending sizing must not swallow the next Escape or Enter press");
        }

        // Focus and capture cancellation must not leave a stale operation that
        // moves the window in response to a later ordinary mouse event.
        const UINT cancellations[] = { WM_CANCELMODE, WM_KILLFOCUS, WM_CAPTURECHANGED };
        for(size_t i = 0; i < sizeof(cancellations) / sizeof(cancellations[0]); ++i)
        {
            ResetNativeSize(window, graphics);
            pointer = BeginNativeSize(native, WMSZ_RIGHT);
            NativeResizeMotion(native, pointer, 10, 0);
            if(cancellations[i] == WM_CAPTURECHANGED) ReleaseCapture();
            else SendMessage(native, cancellations[i], 0, 0);
            Check(GetCapture() != native, "native resize cancellation must release its capture");
            const RECT cancelled = NativeRect(native);
            NativeResizeMotion(native, pointer, 30, 0);
            Check(SameNativeRect(cancelled, NativeRect(native)), "mouse motion after cancellation must not continue resizing");
            PumpEvents();
        }

        // Gameplay keeps running during sizing. A menu or map transition may
        // change the requested input mode before the drag ends; the latest
        // request must win over the state captured when the drag began.
        graphics.SetWindowGrab(true);
        BeginNativeSize(native, WMSZ_RIGHT);
        Check((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_GRABBED) == 0,
              "native sizing must temporarily release requested window grab");
        Check(graphics.GetWindowGrab(), "logical input snapshots must retain requested grab during temporary native release");
        graphics.SetWindowGrab(false);
        EndNativeSize(native);
        graphics.UpdateScreenSize();
        Check((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_GRABBED) == 0,
              "a menu releasing grab during sizing must not be overwritten when sizing ends");
        BeginNativeSize(native, WMSZ_RIGHT);
        graphics.SetWindowGrab(true);
        Check(graphics.GetWindowGrab(), "logical grab queries must reflect requests made during native sizing");
        Check((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_GRABBED) == 0,
              "a new gameplay grab request must wait until sizing ends");
        EndNativeSize(native);
        graphics.UpdateScreenSize();
        Check((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_GRABBED) != 0,
              "sizing completion must apply the latest gameplay grab request");
        graphics.SetWindowGrab(false);

        BeginNativeSize(native, WMSZ_RIGHT);
        graphics.SetRelativeMouse(true);
        Check(graphics.GetRelativeMouse() && SDL_GetRelativeMouseMode() == SDL_FALSE,
              "logical relative mode must follow gameplay while physical mode remains disabled during sizing");
        graphics.SetRelativeMouse(false);
        EndNativeSize(native);
        graphics.UpdateScreenSize();
        Check(!graphics.GetRelativeMouse() && SDL_GetRelativeMouseMode() == SDL_FALSE,
              "the latest relative-mode request must remain in effect after sizing ends");

        ResetNativeSize(window, graphics);
        pointer = BeginNativeSize(native, WMSZ_RIGHT);
        PumpEvents();
        NativeResizeMotion(native, pointer, 10, 0);
        NativeKeyMessage(native, WM_KEYDOWN, 'A');
        NativeKeyMessage(native, WM_KEYUP, 'A');
        SendMessage(native, WM_LBUTTONUP, 0, 0);
        SDL_Event event;
        bool leakedResizeInput = false;
        while(SDL_PollEvent(&event))
            if(event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEMOTION || event.type == SDL_KEYDOWN)
                leakedResizeInput = true;
        Check(!leakedResizeInput, "native resize presses and motion must not become gameplay input; releases remain allowed");

        // Hold a resize open while the ordinary event loop keeps ticking. The
        // old Win32 modal behavior would never reach this loop until release.
        BeginNativeSize(native, WMSZ_BOTTOMRIGHT);
        const Uint32 start = SDL_GetTicks();
        unsigned heartbeats = 0;
        while(SDL_GetTicks() - start < 100)
        {
            PumpEvents();
            ++heartbeats;
            SDL_Delay(1);
        }
        Check(heartbeats >= 3, "outer event-loop heartbeat must continue while native resizing is held");
        EndNativeSize(native);
        std::puts("PASS: native resize edges, limits, cancellation, input isolation and event-loop progress");
    }

    // These SDL operations call ShowWindow on Windows. Keep them behind the
    // existing optional --fullscreen mode, which permits brief focus changes.
    void TestNativeWindowStates()
    {
        cLowLevelGraphicsSDL graphics;
        Initialize(graphics, 640, 480, false);
        SDL_Window* window = CurrentWindow();
        const HWND native = NativeWindow(window);

        SDL_MaximizeWindow(window);
        PumpEvents();
        Check((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0, "native maximize must reach SDL state");
        SDL_HideWindow(window);
        const cVector2l maximized = WindowSize(window);
        Check(maximized.x > 0 && maximized.y > 0, "maximized client dimensions must remain valid");
        graphics.UpdateScreenSize();
        Check(graphics.GetScreenSizeInt() == maximized, "maximizing must update the runtime rendering resolution");
        const RECT bounds = NativeRect(native);
        NativeSizeCommand(native, WMSZ_RIGHT);
        Check(GetCapture() != native && SameNativeRect(bounds, NativeRect(native)),
              "a maximized window must reject native sizing without changing its bounds");
        CheckRenderedFrame(graphics, window);

        SDL_RestoreWindow(window);
        PumpEvents();
        SDL_HideWindow(window);
        Check((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) == 0, "native restore must clear maximized state");
        Check(WindowSize(window) == cVector2l(640, 480), "restoring a maximized window must recover its original client size");
        graphics.UpdateScreenSize();
        CheckRenderedFrame(graphics, window);

        SDL_MinimizeWindow(window);
        PumpEvents();
        Check((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0, "native minimize must reach SDL state");
        const cVector2l before = graphics.GetScreenSizeInt();
        Check(!graphics.UpdateScreenSize(), "minimizing must not request render-target reallocation");
        Check(graphics.GetScreenSizeInt() == before && before.x > 0 && before.y > 0,
              "minimizing must preserve the last valid render dimensions rather than allocate zero-sized targets");
        NativeSizeCommand(native, WMSZ_BOTTOMRIGHT);
        Check(GetCapture() != native, "a minimized window must reject native sizing");

        SDL_RestoreWindow(window);
        PumpEvents();
        SDL_HideWindow(window);
        Check((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) == 0, "native restore must clear minimized state");
        Check(WindowSize(window) == cVector2l(640, 480), "restoring a minimized window must recover its client size");
        graphics.UpdateScreenSize();
        CheckRenderedFrame(graphics, window);
        std::puts("PASS: native maximize/minimize/restore, rejected sizing and valid runtime dimensions");
    }
#else
    void TestNativeWindowResize() {}
    void TestNativeWindowStates() {}
#endif
