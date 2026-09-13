/*
 * Copyright (c) 2026. Distributed under the GNU General Public License,
 * version 3 or later, as part of Amnesia: The Dark Descent.
 */
#ifndef HPL_WINDOWS_WINDOW_RESIZE_H
#define HPL_WINDOWS_WINDOW_RESIZE_H

// The native SC_SIZE handler runs a modal message loop inside SDL_PollEvent.
// Track sizing through ordinary messages instead so the engine remains the
// sole owner of simulation, networking and frame-boundary render-target updates.
// Moving, snapping, maximizing and restoring retain their native handlers.
#include <windows.h>
#include <commctrl.h>
#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
#include <new>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#endif

namespace hpl
{
class cWindowsWindowResize
{
public:
    static bool Install(SDL_Window* window)
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return false;
        cWindowsWindowResize* state = new(std::nothrow) cWindowsWindowResize(window, info.info.win.window);
        if(!state) return false;
        if(!SetWindowSubclass(state->mWindow, WindowProc, SubclassId, reinterpret_cast<DWORD_PTR>(state)))
        {
            delete state;
            return false;
        }
        return true;
    }

    // Gameplay keeps running during sizing and may change input mode (for
    // example on death or a menu transition). Restore its latest request.
    static bool DeferWindowGrab(SDL_Window* window, SDL_bool grab)
    {
        cWindowsWindowResize* state = Find(window);
        if(!state || (!state->mbActive && !state->mbRestorePending)) return false;
        state->mbRestoreGrab = grab;
        return true;
    }

    static bool DeferRelativeMouse(SDL_Window* window, SDL_bool relative)
    {
        cWindowsWindowResize* state = Find(window);
        if(!state || (!state->mbActive && !state->mbRestorePending)) return false;
        state->mbRestoreRelative = relative;
        return true;
    }

    static void UpdateInputState(SDL_Window* window)
    {
        cWindowsWindowResize* state = Find(window);
        if(!state) return;
        if(state->mbActive)
        {
            if(GetCapture() == state->mWindow) state->SetSizingCursor();
            return;
        }
        if(!state->mbRestorePending) return;
        // Run after SDL has processed focus changes, never from inside a focus
        // callback while its old focus flags could recapture another window.
        SDL_SetWindowGrab(window, state->mbRestoreGrab);
        if(state->mbRestoreRelative && SDL_GetKeyboardFocus() != window) return;
        state->mbRestorePending = false;
        SDL_SetRelativeMouseMode(state->mbRestoreRelative);
        POINT point;
        if(!GetCapture() && GetCursorPos(&point) && WindowFromPoint(point) == state->mWindow)
        {
            const LPARAM position = MAKELPARAM(point.x, point.y);
            const LRESULT hit = SendMessage(state->mWindow, WM_NCHITTEST, 0, position);
            SendMessage(state->mWindow, WM_SETCURSOR, reinterpret_cast<WPARAM>(state->mWindow),
                        MAKELPARAM(hit, WM_MOUSEMOVE));
        }
    }

private:
    enum { SubclassId = 0x48504c32 };
    cWindowsWindowResize(SDL_Window* window, HWND handle)
        : mpSDLWindow(window), mWindow(handle), mbActive(false), mbRestorePending(false), mlEdge(0), mFinishKey(0),
          mbRestoreGrab(SDL_FALSE), mbRestoreRelative(SDL_FALSE) {}

    static cWindowsWindowResize* Find(SDL_Window* window)
    {
        if(!window) return NULL;
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        DWORD_PTR data = 0;
        if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS ||
           !GetWindowSubclass(info.info.win.window, WindowProc, SubclassId, &data)) return NULL;
        return reinterpret_cast<cWindowsWindowResize*>(data);
    }

    static bool Left(int edge) { return edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT; }
    static bool Right(int edge) { return edge == WMSZ_RIGHT || edge == WMSZ_TOPRIGHT || edge == WMSZ_BOTTOMRIGHT; }
    static bool Top(int edge) { return edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT; }
    static bool Bottom(int edge) { return edge == WMSZ_BOTTOM || edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT; }

    void SetSizingCursor()
    {
        LPCTSTR cursor = IDC_SIZEALL;
        if(mlEdge == WMSZ_LEFT || mlEdge == WMSZ_RIGHT) cursor = IDC_SIZEWE;
        if(mlEdge == WMSZ_TOP || mlEdge == WMSZ_BOTTOM) cursor = IDC_SIZENS;
        if(mlEdge == WMSZ_TOPLEFT || mlEdge == WMSZ_BOTTOMRIGHT) cursor = IDC_SIZENWSE;
        if(mlEdge == WMSZ_TOPRIGHT || mlEdge == WMSZ_BOTTOMLEFT) cursor = IDC_SIZENESW;
        SetCursor(LoadCursor(NULL, cursor));
    }

    bool Begin(int edge)
    {
        const Uint32 flags = SDL_GetWindowFlags(mpSDLWindow);
        if((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) ||
           !(flags & SDL_WINDOW_RESIZABLE) || edge < 0 || edge > WMSZ_BOTTOMRIGHT) return false;
        if(mbActive) Finish(false);
        if(!GetWindowRect(mWindow, &mOriginalRect) || !GetCursorPos(&mOrigin)) return false;
        mPoint = mOrigin;
        mlEdge = edge;
        if(!mbRestorePending)
        {
            mbRestoreGrab = (flags & SDL_WINDOW_INPUT_GRABBED) ? SDL_TRUE : SDL_FALSE;
            mbRestoreRelative = SDL_GetRelativeMouseMode();
        }
        mbRestorePending = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_SetWindowGrab(mpSDLWindow, SDL_FALSE);
        // Disabling relative mode can move SDL's cursor back to its absolute
        // position. Anchor the drag at the resulting position.
        GetCursorPos(&mOrigin);
        mPoint = mOrigin;
        mFinishKey = 0;
        mbActive = true;
        SetCapture(mWindow);
        if(GetCapture() != mWindow)
        {
            Finish(false);
            return false;
        }
        return true;
    }

    void Finish(bool restoreRectangle, bool restoreInput = true)
    {
        if(!mbActive) return;
        // ReleaseCapture synchronously sends WM_CAPTURECHANGED through this subclass.
        mbActive = false;
        if(restoreRectangle)
            SetWindowPos(mWindow, NULL, mOriginalRect.left, mOriginalRect.top,
                         mOriginalRect.right - mOriginalRect.left, mOriginalRect.bottom - mOriginalRect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        if(GetCapture() == mWindow) ReleaseCapture();
        mbRestorePending = restoreInput;
    }

    void ResizeTo(const POINT& point)
    {
        if(!mlEdge) return; // System-menu sizing first selects an edge with an arrow.
        mPoint = point;
        RECT rect = mOriginalRect;
        const LONG dx = point.x - mOrigin.x, dy = point.y - mOrigin.y;
        if(Left(mlEdge)) rect.left += dx;
        if(Right(mlEdge)) rect.right += dx;
        if(Top(mlEdge)) rect.top += dy;
        if(Bottom(mlEdge)) rect.bottom += dy;

        // SDL's handler includes its client-size limits and the native decorations.
        // Seed the values Windows normally supplies before sending this message.
        MINMAXINFO limits = {};
        limits.ptMinTrackSize.x = GetSystemMetrics(SM_CXMINTRACK);
        limits.ptMinTrackSize.y = GetSystemMetrics(SM_CYMINTRACK);
        limits.ptMaxTrackSize.x = GetSystemMetrics(SM_CXMAXTRACK);
        limits.ptMaxTrackSize.y = GetSystemMetrics(SM_CYMAXTRACK);
        SendMessage(mWindow, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
        LONG width = rect.right - rect.left, height = rect.bottom - rect.top;
        if(width < limits.ptMinTrackSize.x) width = limits.ptMinTrackSize.x;
        if(height < limits.ptMinTrackSize.y) height = limits.ptMinTrackSize.y;
        if(limits.ptMaxTrackSize.x > 0 && width > limits.ptMaxTrackSize.x) width = limits.ptMaxTrackSize.x;
        if(limits.ptMaxTrackSize.y > 0 && height > limits.ptMaxTrackSize.y) height = limits.ptMaxTrackSize.y;
        if(Left(mlEdge)) rect.left = rect.right - width;
        else rect.right = rect.left + width;
        if(Top(mlEdge)) rect.top = rect.bottom - height;
        else rect.bottom = rect.top + height;
        SetWindowPos(mWindow, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void ArrowKey(WPARAM key)
    {
        if(!mlEdge)
        {
            if(key == VK_LEFT) mlEdge = WMSZ_LEFT;
            if(key == VK_RIGHT) mlEdge = WMSZ_RIGHT;
            if(key == VK_UP) mlEdge = WMSZ_TOP;
            if(key == VK_DOWN) mlEdge = WMSZ_BOTTOM;
            return;
        }
        // A perpendicular arrow selects the other dimension, allowing corners
        // to be sized through the system menu without moving the OS pointer.
        if((key == VK_UP || key == VK_DOWN) && !Top(mlEdge) && !Bottom(mlEdge))
            mlEdge = key == VK_UP ? (Left(mlEdge) ? WMSZ_TOPLEFT : WMSZ_TOPRIGHT) :
                                   (Left(mlEdge) ? WMSZ_BOTTOMLEFT : WMSZ_BOTTOMRIGHT);
        if((key == VK_LEFT || key == VK_RIGHT) && !Left(mlEdge) && !Right(mlEdge))
            mlEdge = key == VK_LEFT ? (Top(mlEdge) ? WMSZ_TOPLEFT : WMSZ_BOTTOMLEFT) :
                                     (Top(mlEdge) ? WMSZ_TOPRIGHT : WMSZ_BOTTOMRIGHT);
        const LONG step = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 8;
        POINT point = mPoint;
        if(key == VK_LEFT) point.x -= step;
        if(key == VK_RIGHT) point.x += step;
        if(key == VK_UP) point.y -= step;
        if(key == VK_DOWN) point.y += step;
        ResizeTo(point);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                       UINT_PTR, DWORD_PTR data)
    {
        cWindowsWindowResize* state = reinterpret_cast<cWindowsWindowResize*>(data);
        if(message == WM_NCDESTROY)
        {
            state->Finish(false, false);
            RemoveWindowSubclass(window, WindowProc, SubclassId);
            delete state;
            return DefSubclassProc(window, message, wp, lp);
        }
        if(message == WM_SYSCOMMAND && (wp & 0xfff0) == SC_SIZE)
        {
            // Never fall back to the modal handler if capture cannot be obtained.
            state->Begin(static_cast<int>(wp & 0xf));
            return 0;
        }
        // The completion key can be released over a different application.
        // Do not suppress its first fresh press after focus returns.
        if(message == WM_KILLFOCUS || (message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) ||
           (message == WM_SHOWWINDOW && !wp)) state->mFinishKey = 0;
        if((message == WM_KEYUP || message == WM_SYSKEYUP) && wp == state->mFinishKey)
            state->mFinishKey = 0;
        if(state->mFinishKey && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wp == state->mFinishKey)
            return 0;
        // TranslateMessage may already have queued text for the key that
        // ended sizing. It must not activate or type into the game's GUI.
        if((state->mbRestorePending || state->mFinishKey) &&
           (message == WM_CHAR || message == WM_SYSCHAR || message == WM_UNICHAR)) return 0;
        if(state->mbActive)
        {
            switch(message)
            {
            case WM_MOUSEMOVE:
            {
                POINT point = { static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)) };
                ClientToScreen(window, &point);
                state->ResizeTo(point);
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if(wp == VK_ESCAPE || wp == VK_RETURN)
                {
                    state->mFinishKey = wp;
                    state->Finish(wp == VK_ESCAPE);
                }
                else if(wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN) state->ArrowKey(wp);
                return 0;
            case WM_LBUTTONUP:
                state->Finish(false);
                // Fall through: SDL still needs releases for controls held
                // before sizing, but its mouse handler must not infer new
                // presses from other buttons carried in this message.
            case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:
                return DefSubclassProc(window, message,
                    wp & ~(MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2), lp);
            case WM_SETCURSOR:
                state->SetSizingCursor();
                return TRUE;
            case WM_CAPTURECHANGED:
                if(reinterpret_cast<HWND>(lp) != window) state->Finish(false);
                break;
            case WM_CANCELMODE:
            case WM_KILLFOCUS:
                state->Finish(false);
                break;
            case WM_ACTIVATE:
                if(LOWORD(wp) == WA_INACTIVE) state->Finish(false);
                break;
            case WM_SHOWWINDOW:
                if(!wp) state->Finish(false);
                break;
            case WM_SIZE:
                if(wp == SIZE_MINIMIZED || wp == SIZE_MAXIMIZED) state->Finish(false);
                break;
            case WM_DESTROY:
                state->Finish(false, false);
                break;
            case WM_SYSCOMMAND:
                state->Finish(false);
                break;
            case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            case WM_CHAR: case WM_SYSCHAR: case WM_UNICHAR:
                return 0;
            case WM_INPUT:
                // DefWindowProc must clean up foreground raw-input handles.
                return DefWindowProc(window, message, wp, lp);
            }
        }
        return DefSubclassProc(window, message, wp, lp);
    }

    SDL_Window* mpSDLWindow;
    HWND mWindow;
    bool mbActive;
    bool mbRestorePending;
    int mlEdge;
    WPARAM mFinishKey;
    RECT mOriginalRect;
    POINT mOrigin, mPoint;
    SDL_bool mbRestoreGrab, mbRestoreRelative;
};
}
#endif
