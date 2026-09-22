// BlackBox dialog and user-controlled report export, adapted from the original UI.
#include "BlackBoxUI.h"
#include "BlackBoxReport.h"
#include "resource.h"
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    static const char g_errorLabel[] =
        "A crash has been detected by BlackBox.\n\n"
        "To help diagnose the problem, BlackBox will try to gather "
        "information about the crash and the state of the program at the "
        "time it occurred. This report can be copied to the clipboard or "
        "saved as a plain text file.\n\n"
        "Please email the report to nathan.barnes@midwestscan.com or "
        "submit a new issue at:\n"
        "https://github.com/Saito720/Amnesia64/issues";
    static const char g_mailToAddress[] =
        "mailto:nathan.barnes@midwestscan.com";
    static const char g_submitBugURL[] =
        "https://github.com/Saito720/Amnesia64/issues";

    // Fixed storage stays off the faulting thread's stack.
    char g_report[128 * 1024];
    wchar_t g_displayReport[128 * 1024];
    BlackBoxReportSections g_sections;
    wchar_t g_sectionText[128 * 1024];
    struct ProcessEntry { DWORD id; wchar_t name[MAX_PATH]; };
    ProcessEntry g_processes[512];
    size_t g_processCount = 0;
    WNDPROC g_reportWindowProc = NULL;

    LRESULT CALLBACK ReportWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        // Claim these keys before the dialog manager can translate them into
        // a default button click (or WM_CLOSE when no Cancel button exists).
        if (message == WM_GETDLGCODE && (wParam == VK_RETURN || wParam == VK_ESCAPE))
            return CallWindowProcW(g_reportWindowProc, window, message, wParam, lParam) | DLGC_WANTMESSAGE;
        if ((message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR) &&
            (wParam == VK_RETURN || wParam == VK_ESCAPE))
            return 0;
        return CallWindowProcW(g_reportWindowProc, window, message, wParam, lParam);
    }

    const wchar_t* SectionText(const BlackBoxReportRange& range)
    {
        int count = MultiByteToWideChar(CP_UTF8, 0, g_report + range.begin,
            static_cast<int>(range.end - range.begin), g_sectionText, _countof(g_sectionText) - 1);
        g_sectionText[count] = 0;
        return g_sectionText;
    }

    void AddListLine(HWND list, const wchar_t* line)
    {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
        HDC dc = GetDC(list);
        HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0)));
        SIZE size = {};
        GetTextExtentPoint32W(dc, line, static_cast<int>(wcslen(line)), &size);
        if (size.cx + 8 > SendMessageW(list, LB_GETHORIZONTALEXTENT, 0, 0))
            SendMessageW(list, LB_SETHORIZONTALEXTENT, size.cx + 8, 0);
        SelectObject(dc, oldFont);
        ReleaseDC(list, dc);
    }

    void FillList(HWND list, const BlackBoxReportRange& range)
    {
        SectionText(range);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        wchar_t* line = g_sectionText;
        while (*line)
        {
            wchar_t* end = wcschr(line, L'\r');
            if (end) *end = 0;
            AddListLine(list, line);
            if (!end) break;
            line = end + 1;
            if (*line == L'\n') ++line;
        }
    }

    void CaptureProcesses()
    {
        g_processCount = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W process = {};
            process.dwSize = sizeof(process);
            if (Process32FirstW(snapshot, &process))
                do
                {
                    if (g_processCount == _countof(g_processes)) break;
                    g_processes[g_processCount].id = process.th32ProcessID;
                    wcscpy_s(g_processes[g_processCount++].name, process.szExeFile);
                } while (Process32NextW(snapshot, &process));
            CloseHandle(snapshot);
        }
        // Always allow inspection of the crash-time module list, even if the
        // process snapshot failed or was full before reaching the game.
        for (size_t i = 0; i < g_processCount; ++i)
            if (g_processes[i].id == GetCurrentProcessId()) return;
        if (g_processCount == _countof(g_processes)) --g_processCount;
        g_processes[g_processCount].id = GetCurrentProcessId();
        wcscpy_s(g_processes[g_processCount++].name, L"Amnesia64 (crashed process)");
    }

    void ShowProcessModules(HWND dialog)
    {
        LRESULT index = SendDlgItemMessageW(dialog, IDC_PROCESS_LIST, LB_GETCURSEL, 0, 0);
        if (index < 0 || static_cast<size_t>(index) >= g_processCount) return;
        HWND list = GetDlgItem(dialog, IDC_PROCESS_MODULE_LIST);
        if (g_processes[index].id == GetCurrentProcessId())
        {
            FillList(list, g_sections.modules);
            return;
        }
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        SendMessageW(list, LB_SETHORIZONTALEXTENT, 0, 0);
        if (!g_processes[index].id)
        {
            AddListLine(list, L"Module information unavailable for this process.");
            return;
        }
        // Other processes are queried only when selected; access may be denied
        // or the process may have exited since the crash-time process snapshot.
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_processes[index].id);
        MODULEENTRY32W module = {};
        module.dwSize = sizeof(module);
        if (snapshot != INVALID_HANDLE_VALUE && Module32FirstW(snapshot, &module))
        {
            unsigned count = 0;
            do { AddListLine(list, module.szExePath); }
            while (++count < 1024 && Module32NextW(snapshot, &module));
        }
        else AddListLine(list, L"Module information unavailable for this process.");
        if (snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
    }

    INT_PTR CALLBACK DetailDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_INITDIALOG)
        {
            if (lParam == IDD_MACHINE_INFO_DLG)
            {
                SetDlgItemTextW(dialog, IDC_CPU_LABEL, SectionText(g_sections.cpu));
                SetDlgItemTextW(dialog, IDC_OS_LABEL, SectionText(g_sections.os));
                SetDlgItemTextW(dialog, IDC_MEM_LABEL, SectionText(g_sections.memory));
            }
            else if (lParam == IDD_MACHINESTATE_DLG)
            {
                size_t selected = 0;
                for (size_t i = 0; i < g_processCount; ++i)
                {
                    wchar_t text[MAX_PATH + 40];
                    swprintf_s(text, L"%s, PID: %lu", g_processes[i].name, g_processes[i].id);
                    AddListLine(GetDlgItem(dialog, IDC_PROCESS_LIST), text);
                    if (g_processes[i].id == GetCurrentProcessId()) selected = i;
                }
                SendDlgItemMessageW(dialog, IDC_PROCESS_LIST, LB_SETCURSEL, selected, 0);
                ShowProcessModules(dialog);
            }
            return TRUE;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)))
        {
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(wParam) == IDC_PROCESS_LIST && HIWORD(wParam) == LBN_SELCHANGE)
        {
            ShowProcessModules(dialog);
            return TRUE;
        }
        return FALSE;
    }

    void ShowDetails(HWND owner, int resource)
    {
        DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(resource), owner, DetailDialogProc, resource);
    }

    void Error(HWND dialog, const wchar_t* message)
    {
        MessageBoxW(dialog, message, L"BlackBox", MB_OK | MB_ICONERROR);
    }

    void CopyReport(HWND dialog)
    {
        SIZE_T bytes = (wcslen(g_displayReport) + 1) * sizeof(wchar_t);
        HGLOBAL allocation = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!allocation)
        {
            Error(dialog, L"Could not allocate clipboard memory. Please save the report instead.");
            return;
        }
        void* data = GlobalLock(allocation);
        if (!data)
        {
            GlobalFree(allocation);
            Error(dialog, L"Could not access clipboard memory.");
            return;
        }
        memcpy(data, g_displayReport, bytes);
        GlobalUnlock(allocation);
        bool copied = false;
        if (OpenClipboard(dialog))
        {
            if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, allocation))
                copied = true; // Windows now owns the allocation.
            CloseClipboard();
        }
        if (!copied)
        {
            GlobalFree(allocation);
            Error(dialog, L"Could not copy the report. Please try again or save it to a file.");
        }
    }

    void SaveReport(HWND dialog)
    {
        wchar_t path[MAX_PATH] = {};
        SYSTEMTIME time = {};
        GetLocalTime(&time);
        swprintf_s(path, L"Amnesia64-crash-%04u-%02u-%02u-%02u%02u%02u.txt",
                   time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
        OPENFILENAMEW save = {};
        save.lStructSize = sizeof(save);
        save.hwndOwner = dialog;
        save.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
        save.lpstrFile = path;
        save.nMaxFile = MAX_PATH;
        save.lpstrDefExt = L"txt";
        save.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&save))
        {
            if (CommDlgExtendedError())
                Error(dialog, L"Could not open the Save dialog. Please copy the report instead.");
            return;
        }
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE)
        {
            Error(dialog, L"Could not create the report file. Please choose another location.");
            return;
        }
        const DWORD length = static_cast<DWORD>(strlen(g_report));
        DWORD written = 0;
        bool saved = WriteFile(file, g_report, length, &written, NULL) && written == length;
        if (!CloseHandle(file)) saved = false;
        if (!saved) Error(dialog, L"Could not write the complete report. Please try another location.");
    }

    void OpenLink(HWND dialog, const char* url)
    {
        if (reinterpret_cast<INT_PTR>(ShellExecuteA(dialog, "open", url, NULL, NULL, SW_SHOWNORMAL)) <= 32)
            Error(dialog, L"Could not open the link. Please use the address shown above.");
    }

    INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_INITDIALOG:
            g_reportWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                GetDlgItem(dialog, IDC_STACKTRACE), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ReportWindowProc)));
            SetDlgItemTextA(dialog, IDC_INTRO, g_errorLabel);
            SetDlgItemTextW(dialog, IDC_EXCEPTION, SectionText(g_sections.exception));
            SetDlgItemTextW(dialog, IDC_REGISTER, SectionText(g_sections.registers));
            FillList(GetDlgItem(dialog, IDC_STACKTRACE), g_sections.stack);
            SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(
                LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_BUG))));
            SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetForegroundWindow(dialog);
            // Gameplay keys may still be repeating when the crash steals focus.
            // Start on the report, with no default action that can dismiss it.
            SetFocus(GetDlgItem(dialog, IDC_STACKTRACE));
            return FALSE; // Focus was assigned explicitly.
        case WM_CTLCOLORSTATIC:
            if (GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == IDC_INTRO ||
                GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == IDC_INTRO2 ||
                GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == IDC_BUG_LBL)
            {
                SetTextColor(reinterpret_cast<HDC>(wParam), RGB(0, 0, 0));
                SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
                return reinterpret_cast<INT_PTR>(GetStockObject(WHITE_BRUSH));
            }
            break;
        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDOK:
            case IDCANCEL:
                // Dialog navigation translates Enter/Escape into these commands.
                // Require an explicit Close button action or the window's X.
                return TRUE;
            case IDC_CLOSE: EndDialog(dialog, IDC_CLOSE); return TRUE;
            case IDC_COPY_TO_CLIPBOARD: CopyReport(dialog); return TRUE;
            case IDC_SAVE_TO_FILE: SaveReport(dialog); return TRUE;
            case IDC_MAILTO: OpenLink(dialog, g_mailToAddress); return TRUE;
            case IDC_SUBMIT_NEW_VCF_BUG: OpenLink(dialog, g_submitBugURL); return TRUE;
            case IDC_ABOUT: ShowDetails(dialog, IDD_ABOUT_BLACKBOX); return TRUE;
            case IDC_MACHINE_INFO: ShowDetails(dialog, IDD_MACHINE_INFO_DLG); return TRUE;
            case IDC_MACHINE_STATE: ShowDetails(dialog, IDD_MACHINESTATE_DLG); return TRUE;
            }
        }
        return FALSE;
    }
}

void ShowBlackBoxUI(EXCEPTION_POINTERS* exception, HANDLE thread, DWORD threadId)
{
    HWND progress = CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_INIT_DLG),
                                      NULL, DetailDialogProc, IDD_INIT_DLG);
    if (progress) UpdateWindow(progress);
    BuildBlackBoxReport(exception, thread, threadId, g_report, sizeof(g_report), &g_sections);
    CaptureProcesses();
    MultiByteToWideChar(CP_UTF8, 0, g_report, -1, g_displayReport,
                        static_cast<int>(sizeof(g_displayReport) / sizeof(wchar_t)));
    if (progress) DestroyWindow(progress);
    // Resources must be linked into the executable, not hidden in the .lib.
    if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_BLACKBOX_ERR_DLG),
                        NULL, DialogProc, 0) == -1)
        MessageBoxW(NULL, g_displayReport, L"BlackBox crash report", MB_OK | MB_ICONERROR);
}
