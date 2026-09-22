#include "BlackBox.h"
#include "BlackBoxReport.h"
#include "resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int failures = 0;
static void Check(bool condition, const char* description)
{
    printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++failures;
}

static LONG WINAPI PreviousFilter(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }
static LONG WINAPI LaterFilter(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }

__declspec(noinline) static void CrashAccessViolation()
{
    volatile int* address = reinterpret_cast<volatile int*>(static_cast<ULONG_PTR>(1));
    *address = 42;
}

#pragma warning(push)
#pragma warning(disable: 4717) // Intentional recursion to test a real stack overflow.
__declspec(noinline) static unsigned Overflow(unsigned depth)
{
    volatile char stack[4096];
    stack[depth % sizeof(stack)] = static_cast<char>(depth);
    return Overflow(depth + 1) + stack[depth % sizeof(stack)];
}
#pragma warning(pop)

static DWORD WINAPI CrashWorker(void*) { CrashAccessViolation(); return 0; }

struct ChildDialog { DWORD process; HWND window; int control; const wchar_t* title; };
static BOOL CALLBACK FindDialog(HWND window, LPARAM parameter)
{
    ChildDialog& child = *reinterpret_cast<ChildDialog*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    wchar_t title[128] = {};
    GetWindowTextW(window, title, _countof(title));
    if (process == child.process && (child.control ? GetDlgItem(window, child.control) != NULL :
        wcscmp(title, child.title) == 0))
    {
        child.window = window;
        return FALSE;
    }
    return TRUE;
}

static void ReadControl(HWND dialog, int id, wchar_t* text, size_t capacity)
{
    DWORD_PTR result = 0;
    text[0] = 0;
    SendMessageTimeoutW(GetDlgItem(dialog, id), WM_GETTEXT, capacity,
                        reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG, 2000, &result);
}

static void ReadList(HWND dialog, int id, wchar_t* text, size_t capacity)
{
    HWND list = GetDlgItem(dialog, id);
    DWORD_PTR count = 0;
    text[0] = 0;
    SendMessageTimeoutW(list, LB_GETCOUNT, 0, 0, SMTO_ABORTIFHUNG, 2000, &count);
    for (DWORD_PTR i = 0; i < count && i < 1024; ++i)
    {
        DWORD_PTR length = 0, result = 0;
        SendMessageTimeoutW(list, LB_GETTEXTLEN, i, 0, SMTO_ABORTIFHUNG, 2000, &length);
        size_t used = wcslen(text);
        if (length >= capacity - used - 2) break;
        SendMessageTimeoutW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(text + used),
                            SMTO_ABORTIFHUNG, 2000, &result);
        wcscat_s(text, capacity, L"\n");
    }
}

// Optional visual QA captures only this test's own child windows.
static void CaptureDialog(HWND dialog, const wchar_t* name)
{
    wchar_t directory[MAX_PATH] = {}, path[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"BLACKBOX_CAPTURE_DIR", directory, _countof(directory))) return;
    swprintf_s(path, L"%s\\%s.bmp", directory, name);
    RECT rect;
    GetWindowRect(dialog, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HDC screen = GetDC(dialog), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    PrintWindow(dialog, dc, 0);
    SelectObject(dc, previous);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biSizeImage = width * height * 4;
    void* pixels = HeapAlloc(GetProcessHeap(), 0, info.bmiHeader.biSizeImage);
    if (pixels && GetDIBits(dc, bitmap, 0, height, pixels, &info, DIB_RGB_COLORS))
    {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            BITMAPFILEHEADER header = {};
            header.bfType = 0x4D42;
            header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
            header.bfSize = header.bfOffBits + info.bmiHeader.biSizeImage;
            DWORD written;
            WriteFile(file, &header, sizeof(header), &written, NULL);
            WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL);
            WriteFile(file, pixels, info.bmiHeader.biSizeImage, &written, NULL);
            CloseHandle(file);
        }
    }
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(dialog, screen);
}

static void TestDetails(HWND owner, DWORD process, int button, int control, const wchar_t* title, const wchar_t* capture)
{
    PostMessageW(owner, WM_COMMAND, button, reinterpret_cast<LPARAM>(GetDlgItem(owner, button)));
    ChildDialog detail = { process, NULL, control, title };
    const ULONGLONG deadline = GetTickCount64() + 3000;
    while (!detail.window && GetTickCount64() < deadline)
    {
        EnumWindows(FindDialog, reinterpret_cast<LPARAM>(&detail));
        Sleep(20);
    }
    Check(detail.window != NULL, "original secondary dialog opens");
    if (!detail.window) return;
    static wchar_t text[32768];
    if (button == IDC_MACHINE_INFO)
    {
        ReadControl(detail.window, IDC_CPU_LABEL, text, _countof(text));
        Check(wcsstr(text, L"Logical processors") != NULL, "Information contains CPU details");
        ReadControl(detail.window, IDC_OS_LABEL, text, _countof(text));
        Check(wcsstr(text, L"Windows version") != NULL, "Information contains Windows version");
        ReadControl(detail.window, IDC_MEM_LABEL, text, _countof(text));
        Check(wcsstr(text, L"Physical memory") != NULL, "Information contains memory details");
    }
    if (button == IDC_MACHINE_STATE)
    {
        ReadList(detail.window, IDC_PROCESS_LIST, text, _countof(text));
        Check(wcsstr(text, L"BlackBoxTests.exe") != NULL, "State lists running processes");
        ReadList(detail.window, IDC_PROCESS_MODULE_LIST, text, _countof(text));
        Check(wcsstr(text, L"BlackBoxTests.exe") != NULL, "State initially shows crashed process modules");
    }
    CaptureDialog(detail.window, capture);
    DWORD_PTR result;
    SendMessageTimeoutW(detail.window, WM_COMMAND, IDOK, 0, SMTO_ABORTIFHUNG, 2000, &result);
}

static void TestCrash(const wchar_t* mode, DWORD expectedCode, bool testHeldKeys = false)
{
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(NULL, executable, MAX_PATH);
    wchar_t command[MAX_PATH + 64] = {};
    swprintf_s(command, L"\"%s\" %s", executable, mode);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
    {
        Check(false, "start isolated crash child");
        return;
    }
    ChildDialog dialog = { process.dwProcessId, NULL, IDC_STACKTRACE, NULL };
    const ULONGLONG deadline = GetTickCount64() + 20000;
    while (!dialog.window && GetTickCount64() < deadline && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT)
    {
        EnumWindows(FindDialog, reinterpret_cast<LPARAM>(&dialog));
        Sleep(50);
    }
    Check(dialog.window != NULL, "unhandled crash opens linked BlackBox dialog");
    if (dialog.window)
    {
        static wchar_t report[128 * 1024];
        DWORD_PTR result = 0;
        ReadControl(dialog.window, IDC_EXCEPTION, report, _countof(report));
        Check(wcsstr(report, expectedCode == EXCEPTION_STACK_OVERFLOW ? L"Stack overflow" : L"Access violation") != NULL,
              "report describes original exception");
        ReadControl(dialog.window, IDC_REGISTER, report, _countof(report));
        Check(wcsstr(report, sizeof(void*) == 8 ? L"RIP=" : L"EIP=") != NULL, "architecture-correct registers");
        ReadList(dialog.window, IDC_STACKTRACE, report, _countof(report));
        Check(wcsstr(report, L"BlackBoxTests.exe+0x") != NULL, "stack contains module-relative game addresses");
        Check(wcsstr(report, L"01  0x") != NULL, "stack walk produces caller frames");
        wchar_t intro[1024] = {};
        SendMessageTimeoutW(GetDlgItem(dialog.window, IDC_INTRO), WM_GETTEXT, _countof(intro),
                            reinterpret_cast<LPARAM>(intro), SMTO_ABORTIFHUNG, 2000, &result);
        Check(wcsstr(intro, L"nathan.barnes@midwestscan.com") &&
              wcsstr(intro, L"https://github.com/Saito720/Amnesia64/issues"), "updated contact details");
        if (testHeldKeys)
        {
            CaptureDialog(dialog.window, L"BlackBox");
            TestDetails(dialog.window, process.dwProcessId, IDC_MACHINE_INFO, IDC_CPU_LABEL, NULL, L"Information");
            TestDetails(dialog.window, process.dwProcessId, IDC_MACHINE_STATE, IDC_PROCESS_LIST, NULL, L"State");
            TestDetails(dialog.window, process.dwProcessId, IDC_ABOUT, 0, L"About BlackBox", L"About");
            SendMessageTimeoutW(dialog.window, WM_NEXTDLGCTL,
                                reinterpret_cast<WPARAM>(GetDlgItem(dialog.window, IDC_STACKTRACE)), TRUE,
                                SMTO_ABORTIFHUNG, 2000, &result);
            // Reproduce input spilling over from the game into the modal dialog.
            // Post to this test child's controls, never to the desktop input queue.
            HWND reportControl = GetDlgItem(dialog.window, IDC_STACKTRACE);
            for (unsigned i = 0; i < 8; ++i)
                PostMessageW(reportControl, WM_KEYDOWN, VK_RETURN, 1 | (1L << 30));
            Check(WaitForSingleObject(process.hProcess, 350) == WAIT_TIMEOUT && IsWindow(dialog.window),
                  "held Enter does not dismiss the crash report");
            for (unsigned i = 0; i < 8; ++i)
                PostMessageW(reportControl, WM_KEYDOWN, VK_ESCAPE, 1 | (1L << 30));
            Check(WaitForSingleObject(process.hProcess, 350) == WAIT_TIMEOUT && IsWindow(dialog.window),
                  "held Escape does not dismiss the crash report");
            // A new Enter press on the report should not discard it either.
            PostMessageW(reportControl, WM_KEYDOWN, VK_RETURN, 1);
            Check(WaitForSingleObject(process.hProcess, 350) == WAIT_TIMEOUT && IsWindow(dialog.window),
                  "Enter on the report is not an implicit Close action");
            // Keyboard users can still deliberately navigate to Close and activate it.
            SendMessageTimeoutW(dialog.window, WM_NEXTDLGCTL,
                                reinterpret_cast<WPARAM>(GetDlgItem(dialog.window, IDC_CLOSE)), TRUE,
                                SMTO_ABORTIFHUNG, 2000, &result);
            PostMessageW(GetDlgItem(dialog.window, IDC_CLOSE), WM_KEYDOWN, VK_RETURN, 1);
            Check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0,
                  "explicit keyboard activation of Close still works");
        }
        else if (wcscmp(mode, L"worker") == 0)
        {
            SendMessageTimeoutW(GetDlgItem(dialog.window, IDC_CLOSE), BM_CLICK, 0, 0,
                                SMTO_ABORTIFHUNG, 2000, &result);
        }
        else PostMessageW(dialog.window, WM_CLOSE, 0, 0);
    }
    if (WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT)
    {
        TerminateProcess(process.hProcess, 1);
        Check(false, "crash child exits after closing dialog");
    }
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    Check(code == expectedCode, "process terminates with original exception code");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc > 1)
    {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        if (!BlackBox::Initialize()) return 2;
        if (wcscmp(argv[1], L"overflow") == 0) return Overflow(0);
        if (wcscmp(argv[1], L"worker") == 0)
        {
            HANDLE worker = CreateThread(NULL, 0, CrashWorker, NULL, 0, NULL);
            WaitForSingleObject(worker, INFINITE);
            return 3;
        }
        CrashAccessViolation();
        return 4;
    }

    LPTOP_LEVEL_EXCEPTION_FILTER original = SetUnhandledExceptionFilter(PreviousFilter);
    for (unsigned i = 0; i < 3; ++i)
    {
        Check(BlackBox::Initialize() && BlackBox::Initialize(), "initialization is repeatable and idempotent");
        BlackBox::Shutdown();
        Check(SetUnhandledExceptionFilter(PreviousFilter) == PreviousFilter, "shutdown restores previous handler");
        BlackBox::Shutdown();
    }
    BlackBox::Initialize();
    SetUnhandledExceptionFilter(LaterFilter);
    BlackBox::Shutdown();
    Check(SetUnhandledExceptionFilter(original) == LaterFilter, "shutdown preserves a later handler");

    CONTEXT context = {};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    record.ExceptionAddress = reinterpret_cast<void*>(&TestCrash);
    EXCEPTION_POINTERS exception = { &record, &context };
    struct { char report[64]; char guard[8]; } small;
    memset(&small, 'X', sizeof(small));
    BuildBlackBoxReport(&exception, NULL, GetCurrentThreadId(), small.report, sizeof(small.report));
    Check(small.report[63] == 0 && strstr(small.report, "[Report truncated]") &&
          memcmp(small.guard, "XXXXXXXX", 8) == 0, "bounded report safely marks truncation");
    TestCrash(L"access", EXCEPTION_ACCESS_VIOLATION);
    TestCrash(L"worker", EXCEPTION_ACCESS_VIOLATION);
    TestCrash(L"overflow", EXCEPTION_STACK_OVERFLOW);
    TestCrash(L"access", EXCEPTION_ACCESS_VIOLATION, true);
    return failures ? 1 : 0;
}
