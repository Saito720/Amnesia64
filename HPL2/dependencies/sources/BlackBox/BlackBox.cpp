// Static-library replacement for BlackBox's original DLL entry point.
#include "BlackBox.h"
#include "BlackBoxUI.h"

namespace
{
    HANDLE g_request = NULL;
    HANDLE g_worker = NULL;
    HANDLE g_crashedThread = NULL;
    DWORD g_crashedThreadId = 0;
    volatile LONG g_reporting = 0;
    volatile LONG g_stopping = 0;
    LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = NULL;
    EXCEPTION_RECORD g_exception = {};
    CONTEXT g_context = {};

    DWORD WINAPI ReportThread(void*)
    {
        if (WaitForSingleObject(g_request, INFINITE) == WAIT_OBJECT_0 &&
            !InterlockedCompareExchange(&g_stopping, 0, 0))
        {
            // Use a fresh stack, including when the game exhausted its stack.
            EXCEPTION_POINTERS exception = { &g_exception, &g_context };
            ShowBlackBoxUI(&exception, g_crashedThread, g_crashedThreadId);
        }
        return 0;
    }

    LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exception)
    {
        if (IsDebuggerPresent() || !exception ||
            InterlockedCompareExchange(&g_reporting, 1, 0) != 0)
            return EXCEPTION_CONTINUE_SEARCH;

        g_exception = *exception->ExceptionRecord;
        g_exception.ExceptionRecord = NULL;
        g_context = *exception->ContextRecord;
        g_crashedThreadId = GetCurrentThreadId();
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                        &g_crashedThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        if (!SetEvent(g_request))
            return EXCEPTION_CONTINUE_SEARCH;
        // Keep the faulting thread and its stack alive until the dialog closes.
        WaitForSingleObject(g_worker, INFINITE);
        return EXCEPTION_EXECUTE_HANDLER;
    }
}

bool BlackBox::Initialize()
{
    if (g_worker)
        return true;
    g_stopping = 0;
    g_reporting = 0;
    g_request = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_request)
        return false;
    g_worker = CreateThread(NULL, 0, ReportThread, NULL, 0, NULL);
    if (!g_worker)
    {
        CloseHandle(g_request);
        g_request = NULL;
        return false;
    }
    ULONG reserve = 64 * 1024;
    SetThreadStackGuarantee(&reserve);
    g_previousFilter = SetUnhandledExceptionFilter(CrashFilter);
    return true;
}

void BlackBox::Shutdown()
{
    if (!g_worker)
        return;
    // Preserve a handler installed by another component after ours.
    LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter(g_previousFilter);
    if (current != CrashFilter)
        SetUnhandledExceptionFilter(current);
    InterlockedExchange(&g_stopping, 1);
    SetEvent(g_request);
    WaitForSingleObject(g_worker, INFINITE);
    CloseHandle(g_worker);
    CloseHandle(g_request);
    if (g_crashedThread)
        CloseHandle(g_crashedThread);
    g_worker = g_request = g_crashedThread = NULL;
}
