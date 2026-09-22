#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

void ShowBlackBoxUI(EXCEPTION_POINTERS* exception, HANDLE thread, DWORD threadId);
