/*
 * Copyright © 2009-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "LuxMsuMrScanEpochDialog.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cwchar>

#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"

namespace
{
	const wchar_t * const kWindowClassName = L"Lux.MsuMrScanEpochDialog";
	const wchar_t * const kWindowTitle = L"MSU-MR Scan Start UTC";
	const int kClientWidth = 460;
	const int kClientHeight = 170;
	const int kDateControlId = 0x4D10;
	const int kTimeControlId = 0x4D11;
	const int kNowButtonId = 0x4D12;

	struct cDialogState
	{
		cDialogState()
			: mhWindow(NULL), mhDate(NULL), mhTime(NULL), mbFinished(false),
			  mbConfirmed(false)
		{
			ZeroMemory(&mSelectedUtc, sizeof(mSelectedUtc));
		}

		HWND mhWindow;
		HWND mhDate;
		HWND mhTime;
		bool mbFinished;
		bool mbConfirmed;
		SYSTEMTIME mSelectedUtc;
	};

	void SetControlFont(HWND ahControl, HFONT ahFont)
	{
		if(ahControl && ahFont)
			SendMessageW(ahControl, WM_SETFONT,
				reinterpret_cast<WPARAM>(ahFont), TRUE);
	}

	void SetPickerUtc(HWND ahDate, HWND ahTime, const SYSTEMTIME& aUtc)
	{
		SendMessageW(ahDate, DTM_SETSYSTEMTIME, GDT_VALID,
			reinterpret_cast<LPARAM>(&aUtc));
		SendMessageW(ahTime, DTM_SETSYSTEMTIME, GDT_VALID,
			reinterpret_cast<LPARAM>(&aUtc));
	}

	bool CapturePickerUtc(cDialogState& aState)
	{
		SYSTEMTIME dateUtc;
		SYSTEMTIME timeUtc;
		ZeroMemory(&dateUtc, sizeof(dateUtc));
		ZeroMemory(&timeUtc, sizeof(timeUtc));
		if(SendMessageW(aState.mhDate, DTM_GETSYSTEMTIME, 0,
			reinterpret_cast<LPARAM>(&dateUtc)) != GDT_VALID ||
			SendMessageW(aState.mhTime, DTM_GETSYSTEMTIME, 0,
			reinterpret_cast<LPARAM>(&timeUtc)) != GDT_VALID)
			return false;

		aState.mSelectedUtc = dateUtc;
		aState.mSelectedUtc.wHour = timeUtc.wHour;
		aState.mSelectedUtc.wMinute = timeUtc.wMinute;
		aState.mSelectedUtc.wSecond = timeUtc.wSecond;
		aState.mSelectedUtc.wMilliseconds = 0;

		FILETIME fileTime;
		return SystemTimeToFileTime(&aState.mSelectedUtc, &fileTime) != FALSE;
	}

	LRESULT CALLBACK DialogWindowProc(HWND ahWindow, UINT alMessage,
		WPARAM alWParam, LPARAM alLParam)
	{
		cDialogState *pState = reinterpret_cast<cDialogState*>(
			GetWindowLongPtrW(ahWindow, GWLP_USERDATA));
		if(alMessage == WM_NCCREATE)
		{
			CREATESTRUCTW *pCreate = reinterpret_cast<CREATESTRUCTW*>(alLParam);
			pState = reinterpret_cast<cDialogState*>(pCreate->lpCreateParams);
			SetWindowLongPtrW(ahWindow, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(pState));
			if(pState)
				pState->mhWindow = ahWindow;
		}

		if(pState)
		{
			switch(alMessage)
			{
			case WM_COMMAND:
				switch(LOWORD(alWParam))
				{
				case kNowButtonId:
					{
						SYSTEMTIME nowUtc;
						GetSystemTime(&nowUtc);
						SetPickerUtc(pState->mhDate, pState->mhTime, nowUtc);
						return 0;
					}
				case IDOK:
					if(CapturePickerUtc(*pState))
					{
						pState->mbConfirmed = true;
						pState->mbFinished = true;
						DestroyWindow(ahWindow);
					}
					else
					{
						MessageBoxW(ahWindow,
							L"Please select a valid UTC date and time.",
							kWindowTitle, MB_OK | MB_ICONWARNING);
					}
					return 0;
				case IDCANCEL:
					pState->mbFinished = true;
					DestroyWindow(ahWindow);
					return 0;
				default:
					break;
				}
				break;
			case WM_CLOSE:
				pState->mbFinished = true;
				DestroyWindow(ahWindow);
				return 0;
			case WM_DESTROY:
				pState->mbFinished = true;
				pState->mhWindow = NULL;
				return 0;
			case WM_NCDESTROY:
				SetWindowLongPtrW(ahWindow, GWLP_USERDATA, 0);
				break;
			default:
				break;
			}
		}

		return DefWindowProcW(ahWindow, alMessage, alWParam, alLParam);
	}

	bool RegisterDialogClass(HINSTANCE ahInstance)
	{
		WNDCLASSEXW windowClass;
		ZeroMemory(&windowClass, sizeof(windowClass));
		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpfnWndProc = DialogWindowProc;
		windowClass.hInstance = ahInstance;
		windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
		windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
		windowClass.lpszClassName = kWindowClassName;
		if(RegisterClassExW(&windowClass) != 0)
			return true;
		return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}

	HWND GetMainNativeWindow()
	{
		SDL_Window *pWindow = SDL_GetKeyboardFocus();
		if(pWindow == NULL)
			pWindow = SDL_GL_GetCurrentWindow();
		if(pWindow == NULL)
			return NULL;

		SDL_SysWMinfo windowInfo;
		SDL_VERSION(&windowInfo.version);
		if(SDL_GetWindowWMInfo(pWindow, &windowInfo) != SDL_TRUE ||
			windowInfo.subsystem != SDL_SYSWM_WINDOWS)
			return NULL;
		return windowInfo.info.win.window;
	}

	void CentreWindowOnOwner(HWND ahWindow, HWND ahOwner)
	{
		RECT windowRect;
		RECT ownerRect;
		if(GetWindowRect(ahWindow, &windowRect) == FALSE)
			return;
		if((ahOwner == NULL || GetWindowRect(ahOwner, &ownerRect) == FALSE) &&
			SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRect, 0) == FALSE)
			return;

		const int width = windowRect.right - windowRect.left;
		const int height = windowRect.bottom - windowRect.top;
		int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
		int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

		HMONITOR hMonitor = MonitorFromWindow(ahOwner ? ahOwner : ahWindow,
			MONITOR_DEFAULTTONEAREST);
		MONITORINFO monitorInfo;
		ZeroMemory(&monitorInfo, sizeof(monitorInfo));
		monitorInfo.cbSize = sizeof(monitorInfo);
		if(GetMonitorInfoW(hMonitor, &monitorInfo))
		{
			const int workLeft = static_cast<int>(monitorInfo.rcWork.left);
			const int workTop = static_cast<int>(monitorInfo.rcWork.top);
			const int workRight = static_cast<int>(monitorInfo.rcWork.right);
			const int workBottom = static_cast<int>(monitorInfo.rcWork.bottom);
			x = std::max(workLeft, std::min(x, workRight - width));
			y = std::max(workTop, std::min(y, workBottom - height));
		}
		SetWindowPos(ahWindow, NULL, x, y, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	bool CreateDialogControls(cDialogState& aState,
		const std::wstring& asSatelliteName)
	{
		const std::wstring prompt = L"Assign scan phase 0 / HRPT line 0 for " +
			asSatelliteName + L" to this UTC instant:";
		HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

		HWND hPrompt = CreateWindowExW(0, L"STATIC", prompt.c_str(),
			WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 18, 420, 36,
			aState.mhWindow, NULL, NULL, NULL);
		aState.mhDate = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
			20, 66, 195, 26, aState.mhWindow,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDateControlId)), NULL, NULL);
		aState.mhTime = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT | DTS_UPDOWN,
			230, 66, 125, 26, aState.mhWindow,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTimeControlId)), NULL, NULL);
		HWND hUtc = CreateWindowExW(0, L"STATIC", L"UTC",
			WS_CHILD | WS_VISIBLE | SS_LEFT, 367, 71, 55, 20,
			aState.mhWindow, NULL, NULL, NULL);
		HWND hNow = CreateWindowExW(0, L"BUTTON", L"Now (UTC)",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			20, 116, 105, 28, aState.mhWindow,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNowButtonId)), NULL, NULL);
		HWND hStart = CreateWindowExW(0, L"BUTTON", L"Start Scan",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			242, 116, 95, 28, aState.mhWindow,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), NULL, NULL);
		HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			347, 116, 93, 28, aState.mhWindow,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), NULL, NULL);

		if(hPrompt == NULL || aState.mhDate == NULL || aState.mhTime == NULL ||
			hUtc == NULL || hNow == NULL || hStart == NULL || hCancel == NULL)
			return false;

		HWND controls[] = { hPrompt, aState.mhDate, aState.mhTime,
			hUtc, hNow, hStart, hCancel };
		for(size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
			SetControlFont(controls[i], hFont);

		SendMessageW(aState.mhDate, DTM_SETFORMATW, 0,
			reinterpret_cast<LPARAM>(L"yyyy'-'MM'-'dd"));
		SendMessageW(aState.mhTime, DTM_SETFORMATW, 0,
			reinterpret_cast<LPARAM>(L"HH':'mm':'ss"));
		SYSTEMTIME nowUtc;
		GetSystemTime(&nowUtc);
		SetPickerUtc(aState.mhDate, aState.mhTime, nowUtc);
		return true;
	}

	bool SystemTimeUtcToJulianDate(const SYSTEMTIME& aUtc,
		double& afJulianDateUtc)
	{
		FILETIME fileTime;
		if(SystemTimeToFileTime(&aUtc, &fileTime) == FALSE)
			return false;

		ULARGE_INTEGER ticks;
		ticks.LowPart = fileTime.dwLowDateTime;
		ticks.HighPart = fileTime.dwHighDateTime;
		const LONGLONG kFileTimeAtUnixEpoch = 116444736000000000LL;
		const double kFileTimeTicksPerDay = 864000000000.0;
		const double kUnixEpochJulianDate = 2440587.5;
		const LONGLONG ticksSinceUnixEpoch =
			static_cast<LONGLONG>(ticks.QuadPart) - kFileTimeAtUnixEpoch;
		afJulianDateUtc = kUnixEpochJulianDate +
			static_cast<double>(ticksSinceUnixEpoch) / kFileTimeTicksPerDay;
		return true;
	}

	std::wstring FormatUtc(const SYSTEMTIME& aUtc)
	{
		wchar_t text[32];
		std::swprintf(text, sizeof(text) / sizeof(text[0]),
			L"%04u-%02u-%02uT%02u:%02u:%02uZ",
			static_cast<unsigned int>(aUtc.wYear),
			static_cast<unsigned int>(aUtc.wMonth),
			static_cast<unsigned int>(aUtc.wDay),
			static_cast<unsigned int>(aUtc.wHour),
			static_cast<unsigned int>(aUtc.wMinute),
			static_cast<unsigned int>(aUtc.wSecond));
		return text;
	}
}

cLuxMsuMrScanEpochDialog::eResult cLuxMsuMrScanEpochDialog::Show(
	const std::wstring& asSatelliteName, double& afJulianDateUtc,
	std::wstring& asUtcText)
{
	afJulianDateUtc = 0.0;
	asUtcText.clear();

	INITCOMMONCONTROLSEX commonControls;
	ZeroMemory(&commonControls, sizeof(commonControls));
	commonControls.dwSize = sizeof(commonControls);
	commonControls.dwICC = ICC_DATE_CLASSES;
	if(InitCommonControlsEx(&commonControls) == FALSE)
		return eResult_Unavailable;

	HINSTANCE hInstance = GetModuleHandleW(NULL);
	if(hInstance == NULL || RegisterDialogClass(hInstance) == false)
		return eResult_Unavailable;

	HWND hOwner = GetMainNativeWindow();
	if(hOwner == NULL)
		return eResult_Unavailable;

	const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
	const DWORD extendedStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
	RECT windowRect = { 0, 0, kClientWidth, kClientHeight };
	if(AdjustWindowRectEx(&windowRect, style, FALSE, extendedStyle) == FALSE)
		return eResult_Unavailable;

	cDialogState state;
	HWND hWindow = CreateWindowExW(extendedStyle, kWindowClassName,
		kWindowTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
		windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
		hOwner, NULL, hInstance, &state);
	if(hWindow == NULL)
		return eResult_Unavailable;
	if(CreateDialogControls(state, asSatelliteName) == false)
	{
		DestroyWindow(hWindow);
		return eResult_Unavailable;
	}

	CentreWindowOnOwner(hWindow, hOwner);
	const bool ownerWasEnabled = IsWindowEnabled(hOwner) != FALSE;
	if(ownerWasEnabled)
		EnableWindow(hOwner, FALSE);
	ShowWindow(hWindow, SW_SHOW);
	UpdateWindow(hWindow);
	SetFocus(state.mhDate);

	bool messageLoopFailed = false;
	MSG message;
	while(state.mbFinished == false)
	{
		const BOOL result = GetMessageW(&message, NULL, 0, 0);
		if(result == -1)
		{
			messageLoopFailed = true;
			break;
		}
		if(result == 0)
		{
			PostQuitMessage(static_cast<int>(message.wParam));
			break;
		}
		if(message.message == WM_KEYDOWN &&
			(message.hwnd == hWindow || IsChild(hWindow, message.hwnd)))
		{
			if(message.wParam == VK_RETURN)
			{
				HWND hFocus = GetFocus();
				const int focusedControlId = hFocus ? GetDlgCtrlID(hFocus) : 0;
				if(focusedControlId == kNowButtonId ||
					focusedControlId == IDOK || focusedControlId == IDCANCEL)
				{
					SendMessageW(hFocus, BM_CLICK, 0, 0);
				}
				else
				{
					SendMessageW(hWindow, WM_COMMAND,
						MAKEWPARAM(IDOK, BN_CLICKED), 0);
				}
				continue;
			}
			if(message.wParam == VK_ESCAPE)
			{
				SendMessageW(hWindow, WM_COMMAND,
					MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
				continue;
			}
		}
		if(IsDialogMessageW(hWindow, &message) == FALSE)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}

	if(state.mhWindow)
		DestroyWindow(state.mhWindow);
	if(ownerWasEnabled && IsWindow(hOwner))
	{
		EnableWindow(hOwner, TRUE);
		SetActiveWindow(hOwner);
		SetForegroundWindow(hOwner);
	}

	if(messageLoopFailed || state.mbConfirmed == false)
		return messageLoopFailed ? eResult_Unavailable : eResult_Cancelled;
	if(SystemTimeUtcToJulianDate(state.mSelectedUtc, afJulianDateUtc) == false)
		return eResult_Unavailable;
	asUtcText = FormatUtc(state.mSelectedUtc);
	return eResult_Confirmed;
}

#else

cLuxMsuMrScanEpochDialog::eResult cLuxMsuMrScanEpochDialog::Show(
	const std::wstring&, double&, std::wstring&)
{
	return eResult_Unavailable;
}

#endif
