// Modern x86/x64 replacement for the bundled 32-bit BugSlayer stack walker.
#include "BlackBoxReport.h"
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

namespace
{
    class ReportWriter
    {
    public:
        ReportWriter(char* buffer, size_t capacity) : data(buffer), size(capacity), used(0)
        {
            if (size) data[0] = 0;
        }
        void Add(const char* format, ...)
        {
            if (used + 1 >= size) return;
            va_list args;
            va_start(args, format);
            int count = vsnprintf(data + used, size - used, format, args);
            va_end(args);
            if (count < 0 || static_cast<size_t>(count) >= size - used)
            {
                const char marker[] = "\r\n[Report truncated]\r\n";
                if (size >= sizeof(marker))
                    memcpy(data + size - sizeof(marker), marker, sizeof(marker));
                used = size - 1;
            }
            else used += count;
        }
        size_t Position() const { return used; }
    private:
        char* data;
        size_t size;
        size_t used;
    };

    const char* ExceptionName(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION: return "Access violation";
        case EXCEPTION_STACK_OVERFLOW: return "Stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "Illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "Integer divide by zero";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "Floating point divide by zero";
        case EXCEPTION_IN_PAGE_ERROR: return "In-page error";
        case EXCEPTION_BREAKPOINT: return "Breakpoint";
        case 0xE06D7363: return "Unhandled C++ exception";
        default: return "Unhandled exception";
        }
    }

    void AddFrame(ReportWriter& out, HANDLE process, DWORD64 address, unsigned index, bool symbols)
    {
        out.Add("%02u  0x%016llX", index, address);
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(reinterpret_cast<void*>(static_cast<ULONG_PTR>(address)), &memory, sizeof(memory)))
        {
            wchar_t wideModule[MAX_PATH] = {};
            if (GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), wideModule, MAX_PATH))
            {
                char module[MAX_PATH * 4] = {};
                WideCharToMultiByte(CP_UTF8, 0, wideModule, -1, module, sizeof(module), NULL, NULL);
                const char* name = strrchr(module, '\\');
                out.Add("  %s+0x%llX", name ? name + 1 : module,
                        address - reinterpret_cast<ULONG_PTR>(memory.AllocationBase));
            }
        }
        if (symbols)
        {
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 offset = 0;
            if (SymFromAddr(process, address, &offset, symbol))
                out.Add("  %s+0x%llX", symbol->Name, offset);
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD displacement = 0;
            if (SymGetLineFromAddr64(process, address, &displacement, &line))
                out.Add("  (%s:%lu)", line.FileName, line.LineNumber);
        }
        out.Add("\r\n");
    }
}

void BuildBlackBoxReport(EXCEPTION_POINTERS* exception, HANDLE thread,
                         DWORD threadId, char* output, size_t capacity, BlackBoxReportSections* sections)
{
    BlackBoxReportSections ignored = {};
    if (!sections) sections = &ignored;
    *sections = {};
    ReportWriter out(output, capacity);
    SYSTEMTIME time = {};
    GetSystemTime(&time);
    out.Add("Amnesia64 BlackBox crash report\r\nUTC: %04u-%02u-%02u %02u:%02u:%02u\r\n",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    out.Add("Build: %s %s (%u-bit, %s)\r\n", __DATE__, __TIME__, unsigned(sizeof(void*) * 8),
#ifdef _DEBUG
            "Debug"
#else
            "Release"
#endif
    );
    wchar_t wideExecutable[MAX_PATH] = {};
    GetModuleFileNameW(NULL, wideExecutable, MAX_PATH);
    char executable[MAX_PATH * 4] = {};
    WideCharToMultiByte(CP_UTF8, 0, wideExecutable, -1, executable, sizeof(executable), NULL, NULL);
    out.Add("Executable: %s\r\nProcess: %lu  Crashed thread: %lu\r\n\r\n",
            executable, GetCurrentProcessId(), threadId);
    const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
    sections->exception.begin = out.Position();
    out.Add("Exception: %s (0x%08lX) at %p\r\n", ExceptionName(record.ExceptionCode),
            record.ExceptionCode, record.ExceptionAddress);
    if ((record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
        && record.NumberParameters >= 2)
    {
        const char* operation = record.ExceptionInformation[0] == 0 ? "read" :
                                record.ExceptionInformation[0] == 1 ? "write" : "execute";
        out.Add("Attempted to %s address %p\r\n", operation,
                reinterpret_cast<void*>(record.ExceptionInformation[1]));
    }

    sections->exception.end = out.Position();
    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame = {};
    DWORD machine;
    out.Add("\r\nRegisters\r\n");
    sections->registers.begin = out.Position();
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrFrame.Offset = context.Rbp;
    out.Add("RAX=%016llX RBX=%016llX\r\nRCX=%016llX RDX=%016llX\r\n"
            "RSI=%016llX RDI=%016llX\r\nRBP=%016llX RSP=%016llX\r\n"
            "R8=%016llX R9=%016llX\r\nR10=%016llX R11=%016llX\r\n"
            "R12=%016llX R13=%016llX\r\nR14=%016llX R15=%016llX\r\n"
            "RIP=%016llX EFLAGS=%08lX\r\n",
            context.Rax, context.Rbx, context.Rcx, context.Rdx,
            context.Rsi, context.Rdi, context.Rbp, context.Rsp,
            context.R8, context.R9, context.R10, context.R11,
            context.R12, context.R13, context.R14, context.R15, context.Rip, context.EFlags);
#elif defined(_M_IX86) || defined(__i386__)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrFrame.Offset = context.Ebp;
    out.Add("EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX\r\n"
            "ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX\r\nEIP=%08lX EFLAGS=%08lX\r\n",
            context.Eax, context.Ebx, context.Ecx, context.Edx,
            context.Esi, context.Edi, context.Ebp, context.Esp, context.Eip, context.EFlags);
#else
#error BlackBox currently supports Windows x86 and x64.
#endif
    out.Add("CS=%04X DS=%04X SS=%04X ES=%04X FS=%04X GS=%04X\r\n",
            context.SegCs, context.SegDs, context.SegSs, context.SegEs, context.SegFs, context.SegGs);
    sections->registers.end = out.Position();
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    out.Add("\r\nStack trace (up to 128 frames; symbols require matching PDB files)\r\n");
    sections->stack.begin = out.Position();
    // Use a distinct handle so SymCleanup cannot tear down another component's
    // symbol session keyed by the GetCurrentProcess pseudo-handle.
    HANDLE process = NULL;
    DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
                    &process, 0, FALSE, DUPLICATE_SAME_ACCESS);
    DWORD previousOptions = SymGetOptions();
    SymSetOptions(previousOptions | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME |
                  SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    // An explicit local directory avoids inherited symbol-server/network paths.
    wchar_t symbolPath[MAX_PATH] = {};
    wcscpy_s(symbolPath, wideExecutable);
    wchar_t* slash = wcsrchr(symbolPath, L'\\');
    if (slash) *slash = 0;
    bool symbols = process && SymInitializeW(process, slash ? symbolPath : L".", TRUE) != FALSE;
    if (!symbols) out.Add("Symbol initialization failed (error %lu).\r\n", GetLastError());
    AddFrame(out, process, frame.AddrPC.Offset, 0, symbols);
    if (thread && symbols)
    {
        DWORD64 lastPc = frame.AddrPC.Offset, lastSp = frame.AddrStack.Offset;
        unsigned index = 1;
        for (unsigned steps = 0; steps < 128 && index < 128; ++steps)
        {
            if (!StackWalk64(machine, process, thread, &frame, &context, NULL,
                             SymFunctionTableAccess64, SymGetModuleBase64, NULL) || !frame.AddrPC.Offset)
                break;
            // The first call can return the seeded fault frame.
            if (frame.AddrPC.Offset == lastPc && frame.AddrStack.Offset == lastSp)
            {
                if (steps == 0) continue;
                break;
            }
            AddFrame(out, process, frame.AddrPC.Offset, index++, symbols);
            lastPc = frame.AddrPC.Offset;
            lastSp = frame.AddrStack.Offset;
        }
    }
    if (symbols) SymCleanup(process);
    if (process) CloseHandle(process);
    SymSetOptions(previousOptions);
    sections->stack.end = out.Position();

    SYSTEM_INFO system = {};
    GetNativeSystemInfo(&system);
    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof(memory);
    out.Add("\r\nSystem\r\n");
    sections->cpu.begin = out.Position();
    const char* processor = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" :
                            system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86" :
                            system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "ARM64" : "Unknown";
    out.Add("Logical processors: %lu\r\nProcessor type: %s\r\n", system.dwNumberOfProcessors, processor);
    sections->cpu.end = out.Position();
    sections->os.begin = out.Position();
    // RtlGetVersion reports the actual OS version without a compatibility manifest.
    typedef LONG (WINAPI* GetVersionFunction)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    GetVersionFunction getVersion = ntdll ? reinterpret_cast<GetVersionFunction>(
        GetProcAddress(ntdll, "RtlGetVersion")) : NULL;
    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (getVersion && getVersion(&version) == 0)
        out.Add("Windows version: %lu.%lu (build %lu)\r\n",
                version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
    sections->os.end = out.Position();
    sections->memory.begin = out.Position();
    if (GlobalMemoryStatusEx(&memory))
        out.Add("Memory load: %lu%%\r\nPhysical memory: %llu bytes\r\nAvailable physical: %llu bytes\r\n"
                "Virtual memory: %llu bytes\r\nAvailable virtual: %llu bytes\r\n",
                memory.dwMemoryLoad, memory.ullTotalPhys, memory.ullAvailPhys,
                memory.ullTotalVirtual, memory.ullAvailVirtual);
    sections->memory.end = out.Position();

    out.Add("\r\nModules loaded by the game (base, size, path)\r\n");
    sections->modules.begin = out.Position();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W module = {};
        module.dwSize = sizeof(module);
        if (Module32FirstW(snapshot, &module))
        {
            do
            {
                char path[MAX_PATH * 4] = {};
                WideCharToMultiByte(CP_UTF8, 0, module.szExePath, -1, path, sizeof(path), NULL, NULL);
                out.Add("%p  %lu  %s\r\n", module.modBaseAddr, module.modBaseSize, path);
            } while (Module32NextW(snapshot, &module));
        }
        CloseHandle(snapshot);
    }
    else out.Add("Module enumeration failed (error %lu).\r\n", GetLastError());
    sections->modules.end = out.Position();
}
