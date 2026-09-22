#pragma once
#include "BlackBoxUI.h"
#include <stddef.h>

struct BlackBoxReportRange { size_t begin, end; };
struct BlackBoxReportSections
{
    BlackBoxReportRange exception, registers, stack, cpu, os, memory, modules;
};

// Sections reference the same immutable report used for Copy/Save.
// Caller-provided storage avoids growing buffers while handling a crash.
void BuildBlackBoxReport(EXCEPTION_POINTERS* exception, HANDLE thread,
                         DWORD threadId, char* output, size_t capacity,
                         BlackBoxReportSections* sections = NULL);
