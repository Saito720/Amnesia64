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

#include "LuxMsuMrScanProduct.h"

#include "LuxBase.h"
#include "LuxMsuMrSimulation.h"

#include <algorithm>
#include <cstring>
#include <cwchar>

#include "engine/Engine.h"
#include "graphics/Bitmap.h"
#include "resources/BitmapLoaderHandler.h"
#include "resources/Resources.h"
#include "SDL2/SDL.h"
#if defined(_WIN32)
#include <commdlg.h>
#include "SDL2/SDL_syswm.h"
#endif
#include "system/String.h"

namespace
{
	const int kInitialWindowWidth = 943;
	const int kInitialWindowHeight = 154;
	const int kMinimumWindowWidth = 393;
	const int kMinimumWindowHeight = 64;
	const std::uint32_t kMaximumHistoryLines = 8192;
	const std::uint32_t kCanvasResizeDebounceMilliseconds = 100;
	const char * const kWindowTitle = "MSU-MR HRPT Scan Product";
#if defined(_WIN32)
	const UINT_PTR kMenuSave = 0x4D01;
	const UINT_PTR kMenuSaveAs = 0x4D02;
	const wchar_t * const kProductWindowProperty =
		L"Lux.MsuMrScanProduct.Instance";
#endif

	int FindIndependentRendererDriver()
	{
		// HPL's bundled SDL is built with SDL_LEAN_AND_MEAN, which removes the
		// software renderer. Prefer native non-OpenGL backends so this window
		// cannot disturb the engine's current OpenGL context.
		const char *vPreferredNames[] = {
			"direct3d11", "direct3d", "metal", "software"
		};
		for(size_t preference = 0;
			preference < sizeof(vPreferredNames) / sizeof(vPreferredNames[0]);
			++preference)
		{
			for(int driver = 0; driver < SDL_GetNumRenderDrivers(); ++driver)
			{
				SDL_RendererInfo info;
				std::memset(&info, 0, sizeof(info));
				if(SDL_GetRenderDriverInfo(driver, &info) == 0 && info.name &&
					SDL_strcasecmp(info.name, vPreferredNames[preference]) == 0)
					return driver;
			}
		}
		return -1;
	}
}

static_assert(cLuxMsuMrScanProduct::kWidth ==
	cLuxMsuMrSimulation::kEarthViewSamplesPerLine,
	"The scan product width must match one complete HRPT Earth-view line");

cLuxMsuMrScanProduct::cLuxMsuMrScanProduct()
	: mpWindow(NULL), mpRenderer(NULL), mpWindowTexture(NULL),
#if defined(_WIN32)
	  mhNativeWindow(NULL), mpPreviousWindowProc(NULL), mhNativeMenu(NULL),
#endif
	  mbCloseRequested(false), mbRedrawRequested(false),
	  mbCanvasResizePending(false), mbSaveRequested(false),
	  mbSaveAsRequested(false), mlLastResizeEventTicks(0), mlWindowId(0),
	  mlCursorStateBeforeEnter(SDL_ENABLE), mbEventWatchInstalled(false),
	  mbMouseInside(false), mbInitialized(false),
	  mbHasLastLine(false), mbLineOpen(false),
	  mlOpenLineIndex(0), mlLastLineIndex(0),
	  mlLastGapLineCount(0), mlStoredLineCount(0),
	  mlWrittenSampleCount(0), mlHistoryLineCapacity(0)
{
}

cLuxMsuMrScanProduct::~cLuxMsuMrScanProduct()
{
	Reset();
}

#if defined(_WIN32)
LRESULT CALLBACK cLuxMsuMrScanProduct::NativeWindowProc(HWND ahWindow,
	UINT alMessage, WPARAM alWParam, LPARAM alLParam)
{
	cLuxMsuMrScanProduct *pProduct = reinterpret_cast<cLuxMsuMrScanProduct*>(
		GetPropW(ahWindow, kProductWindowProperty));
	if(pProduct)
	{
		if(alMessage == WM_COMMAND)
		{
			switch(LOWORD(alWParam))
			{
			case kMenuSave:
				pProduct->mbSaveRequested.store(true);
				return 0;
			case kMenuSaveAs:
				pProduct->mbSaveAsRequested.store(true);
				return 0;
			default:
				break;
			}
		}
		else if(alMessage == WM_KEYDOWN && alWParam == 'S' &&
			(GetKeyState(VK_CONTROL) & 0x8000) != 0)
		{
			if((GetKeyState(VK_SHIFT) & 0x8000) != 0)
				pProduct->mbSaveAsRequested.store(true);
			else
				pProduct->mbSaveRequested.store(true);
			return 0;
		}
		if(pProduct->mpPreviousWindowProc)
		{
			return CallWindowProcW(pProduct->mpPreviousWindowProc,
				ahWindow, alMessage, alWParam, alLParam);
		}
	}
	return DefWindowProcW(ahWindow, alMessage, alWParam, alLParam);
}

bool cLuxMsuMrScanProduct::CreateNativeMenu()
{
	if(mpWindow == NULL)
		return false;

	SDL_SysWMinfo windowInfo;
	SDL_VERSION(&windowInfo.version);
	if(SDL_GetWindowWMInfo(mpWindow, &windowInfo) != SDL_TRUE ||
		windowInfo.subsystem != SDL_SYSWM_WINDOWS ||
		windowInfo.info.win.window == NULL)
	{
		Warning("Could not obtain the native MSU-MR product window: %s.\n",
			SDL_GetError());
		return false;
	}

	HMENU hMenu = CreateMenu();
	HMENU hFileMenu = CreatePopupMenu();
	if(hMenu == NULL || hFileMenu == NULL)
	{
		if(hFileMenu) DestroyMenu(hFileMenu);
		if(hMenu) DestroyMenu(hMenu);
		return false;
	}
	if(AppendMenuW(hFileMenu, MF_STRING, kMenuSave,
		L"&Save\tCtrl+S") == FALSE ||
		AppendMenuW(hFileMenu, MF_STRING, kMenuSaveAs,
		L"Save &As...\tCtrl+Shift+S") == FALSE ||
		AppendMenuW(hMenu, MF_POPUP,
			reinterpret_cast<UINT_PTR>(hFileMenu), L"&File") == FALSE)
	{
		DestroyMenu(hFileMenu);
		DestroyMenu(hMenu);
		return false;
	}

	mhNativeWindow = windowInfo.info.win.window;
	if(SetPropW(mhNativeWindow, kProductWindowProperty,
		reinterpret_cast<HANDLE>(this)) == FALSE)
	{
		DestroyMenu(hMenu);
		mhNativeWindow = NULL;
		return false;
	}

	SetLastError(ERROR_SUCCESS);
	const LONG_PTR lPreviousWindowProc = SetWindowLongPtrW(mhNativeWindow,
		GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&NativeWindowProc));
	if(lPreviousWindowProc == 0 && GetLastError() != ERROR_SUCCESS)
	{
		RemovePropW(mhNativeWindow, kProductWindowProperty);
		DestroyMenu(hMenu);
		mhNativeWindow = NULL;
		return false;
	}
	mpPreviousWindowProc = reinterpret_cast<WNDPROC>(lPreviousWindowProc);
	if(SetMenu(mhNativeWindow, hMenu) == FALSE)
	{
		SetWindowLongPtrW(mhNativeWindow, GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(mpPreviousWindowProc));
		mpPreviousWindowProc = NULL;
		RemovePropW(mhNativeWindow, kProductWindowProperty);
		DestroyMenu(hMenu);
		mhNativeWindow = NULL;
		return false;
	}

	mhNativeMenu = hMenu;
	DrawMenuBar(mhNativeWindow);
	return true;
}

void cLuxMsuMrScanProduct::DestroyNativeMenu()
{
	if(mhNativeWindow && mpPreviousWindowProc)
	{
		SetWindowLongPtrW(mhNativeWindow, GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(mpPreviousWindowProc));
	}
	mpPreviousWindowProc = NULL;
	if(mhNativeWindow)
	{
		RemovePropW(mhNativeWindow, kProductWindowProperty);
		if(mhNativeMenu)
		{
			SetMenu(mhNativeWindow, NULL);
			DrawMenuBar(mhNativeWindow);
		}
	}
	if(mhNativeMenu)
		DestroyMenu(mhNativeMenu);
	mhNativeMenu = NULL;
	mhNativeWindow = NULL;
}

bool cLuxMsuMrScanProduct::PromptForSavePath(std::wstring &asPath)
{
	const DWORD lPathCapacity = 32768;
	std::vector<wchar_t> vPath(lPathCapacity, L'\0');
	const std::wstring sInitialPath = msSavePath.empty() ?
		L"MSU-MR_scan.png" : msSavePath;
	wcsncpy_s(&vPath[0], vPath.size(), sInitialPath.c_str(), _TRUNCATE);

	OPENFILENAMEW dialog;
	std::memset(&dialog, 0, sizeof(dialog));
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = mhNativeWindow;
	dialog.lpstrFilter =
		L"PNG image (*.png)\0*.png\0Windows bitmap (*.bmp)\0*.bmp\0\0";
	dialog.lpstrFile = &vPath[0];
	dialog.nMaxFile = lPathCapacity;
	dialog.nFilterIndex =
		hpl::cString::GetFileExtW(sInitialPath) == L"bmp" ? 2 : 1;
	dialog.lpstrTitle = L"Save MSU-MR Scan Product";
	dialog.Flags = OFN_EXPLORER | OFN_HIDEREADONLY |
		OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
	if(GetSaveFileNameW(&dialog) == FALSE)
	{
		const DWORD lError = CommDlgExtendedError();
		if(lError != 0)
		{
			Warning("Could not open the MSU-MR Save As dialog (error %lu).\n",
				static_cast<unsigned long>(lError));
			MessageBoxW(mhNativeWindow,
				L"Windows could not open the Save As dialog.",
				L"MSU-MR Scan Product", MB_OK | MB_ICONERROR);
		}
		return false;
	}

	asPath.assign(&vPath[0]);
	const std::wstring sExtension = dialog.nFilterIndex == 2 ?
		L"bmp" : L"png";
	asPath = hpl::cString::SetFileExtW(asPath, sExtension);
	if(GetFileAttributesW(asPath.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		const std::wstring sMessage = L"The file already exists:\n\n" +
			asPath + L"\n\nReplace it?";
		if(MessageBoxW(mhNativeWindow, sMessage.c_str(),
			L"Confirm Save As", MB_YESNO | MB_ICONWARNING |
			MB_DEFBUTTON2) != IDYES)
			return false;
	}
	return true;
}
#endif

bool cLuxMsuMrScanProduct::SaveCanvas(bool abSaveAs)
{
	if(mbInitialized == false || mvPixels.empty() ||
		mlHistoryLineCapacity == 0)
		return false;

	std::wstring sPath = msSavePath;
	if(abSaveAs || sPath.empty())
	{
#if defined(_WIN32)
		if(PromptForSavePath(sPath) == false)
			return false;
#else
		Warning("MSU-MR Save As is not available on this platform.\n");
		return false;
#endif
	}

	if(gpBase == NULL || gpBase->mpEngine == NULL ||
		gpBase->mpEngine->GetResources() == NULL ||
		gpBase->mpEngine->GetResources()->GetBitmapLoaderHandler() == NULL)
	{
		Warning("Could not save the MSU-MR product canvas: image resources are unavailable.\n");
		return false;
	}

	hpl::cBitmap bitmap;
	bitmap.CreateData(hpl::cVector3l(static_cast<int>(kWidth),
		static_cast<int>(mlHistoryLineCapacity), 1),
		hpl::ePixelFormat_RGBA, 0, 0);
	hpl::cBitmapData *pBitmapData = bitmap.GetData(0, 0);
	if(pBitmapData == NULL || pBitmapData->mpData == NULL ||
		pBitmapData->mlSize != static_cast<int>(mvPixels.size()))
	{
		Warning("Could not allocate the MSU-MR product export bitmap.\n");
		return false;
	}
	std::memcpy(pBitmapData->mpData, &mvPixels[0], mvPixels.size());
	// The live canvas uses alpha zero to distinguish gap rows internally,
	// while the product window presents those rows as black. Export the
	// visible opaque canvas rather than surprising PNG viewers with transparency.
	for(size_t i = 3; i < mvPixels.size(); i += 4)
		pBitmapData->mpData[i] = 255;

	const bool bSaved = gpBase->mpEngine->GetResources()->
		GetBitmapLoaderHandler()->SaveBitmap(&bitmap, sPath, 0);
	if(bSaved == false)
	{
		Warning("Could not save the MSU-MR product canvas to '%s'.\n",
			hpl::cString::To8Char(sPath).c_str());
#if defined(_WIN32)
		const std::wstring sMessage =
			L"The scan product could not be saved to:\n\n" + sPath;
		MessageBoxW(mhNativeWindow, sMessage.c_str(),
			L"MSU-MR Scan Product", MB_OK | MB_ICONERROR);
#endif
		return false;
	}

	msSavePath = sPath;
	Log("Saved MSU-MR product canvas to '%s': %ux%u RGBA, storedRows=%u.\n",
		hpl::cString::To8Char(msSavePath).c_str(), kWidth,
		mlHistoryLineCapacity, mlStoredLineCount);
	return true;
}

bool cLuxMsuMrScanProduct::Initialize()
{
	Reset();
	if((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
	{
		Warning("Could not initialize the MSU-MR product window: SDL video is not active.\n");
		return false;
	}

	mvLinePixels.assign(static_cast<size_t>(kWidth) * 4, 0);
	mvLineWritten.assign(static_cast<size_t>(kWidth), 0);
	mpWindow = SDL_CreateWindow(kWindowTitle,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		kInitialWindowWidth, kInitialWindowHeight,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if(mpWindow == NULL)
	{
		Warning("Could not create the MSU-MR product window: %s.\n", SDL_GetError());
		Reset();
		return false;
	}
	SDL_SetWindowMinimumSize(mpWindow,
		kMinimumWindowWidth, kMinimumWindowHeight);
	mlWindowId = SDL_GetWindowID(mpWindow);
#if defined(_WIN32)
	if(CreateNativeMenu() == false)
	{
		Warning("Could not add the File menu to the MSU-MR product window.\n");
	}
	else
	{
		// Adding a native menu changes the non-client area. Restore the
		// requested SDL client size so it does not reduce the scan canvas.
		SDL_SetWindowSize(mpWindow, kInitialWindowWidth, kInitialWindowHeight);
	}
#endif

	const int lRendererDriver = FindIndependentRendererDriver();
	if(lRendererDriver >= 0)
		mpRenderer = SDL_CreateRenderer(mpWindow, lRendererDriver, 0);
	if(mpRenderer == NULL)
	{
		Warning("Could not create an independent non-OpenGL MSU-MR product renderer: %s.\n",
			lRendererDriver < 0 ? "no compatible SDL render driver is registered" :
			SDL_GetError());
		Reset();
		return false;
	}
	mlHistoryLineCapacity = CalculateWindowHistoryLines();
	mvPixels.assign(static_cast<size_t>(kWidth) *
		mlHistoryLineCapacity * 4, 0);
	mpWindowTexture = CreateWindowTexture(mlHistoryLineCapacity);
	if(mpWindowTexture == NULL)
	{
		Warning("Could not create the MSU-MR product texture: %s.\n", SDL_GetError());
		Reset();
		return false;
	}
	SDL_AddEventWatch(SdlEventWatch, this);
	mbEventWatchInstalled = true;
	mbInitialized = true;
	UploadPixels();
	RenderWindow();
	SDL_ShowWindow(mpWindow);

	SDL_RendererInfo rendererInfo;
	std::memset(&rendererInfo, 0, sizeof(rendererInfo));
	const char *pRendererName = "unknown";
	if(SDL_GetRendererInfo(mpRenderer, &rendererInfo) == 0 && rendererInfo.name)
		pRendererName = rendererInfo.name;
	Log("MSU-MR product window initialized: nativeWindow=%dx%d resizable, "
		"logicalProduct=%ux%u RGBA, dynamicHistoryHeight=on, "
		"renderer=%s, scale=nearest, fileSave=%s.\n",
		kInitialWindowWidth, kInitialWindowHeight, kWidth,
		mlHistoryLineCapacity, pRendererName,
#if defined(_WIN32)
		mhNativeMenu ? "PNG/BMP" : "unavailable"
#else
		"unavailable"
#endif
		);
	return true;
}

void cLuxMsuMrScanProduct::Reset()
{
	if(mbEventWatchInstalled)
		SDL_DelEventWatch(SdlEventWatch, this);
	mbEventWatchInstalled = false;
	if(mbMouseInside)
		SDL_ShowCursor(mlCursorStateBeforeEnter);
	mbMouseInside = false;
#if defined(_WIN32)
	DestroyNativeMenu();
#endif
	if(mpWindowTexture) SDL_DestroyTexture(mpWindowTexture);
	if(mpRenderer) SDL_DestroyRenderer(mpRenderer);
	if(mpWindow) SDL_DestroyWindow(mpWindow);
	mpWindowTexture = NULL;
	mpRenderer = NULL;
	mpWindow = NULL;
	mlWindowId = 0;
	mbCloseRequested.store(false);
	mbRedrawRequested.store(false);
	mbCanvasResizePending.store(false);
	mbSaveRequested.store(false);
	mbSaveAsRequested.store(false);
	mlLastResizeEventTicks.store(0);
	msSavePath.clear();
	mvLinePixels.clear();
	mvLineWritten.clear();
	mvPixels.clear();
	mbInitialized = false;
	mbHasLastLine = false;
	mbLineOpen = false;
	mlOpenLineIndex = 0;
	mlLastLineIndex = 0;
	mlLastGapLineCount = 0;
	mlStoredLineCount = 0;
	mlWrittenSampleCount = 0;
	mlHistoryLineCapacity = 0;
}

bool cLuxMsuMrScanProduct::BeginLine(std::uint64_t alLineIndex)
{
	if(mbInitialized == false || mpWindowTexture == NULL ||
		mlHistoryLineCapacity == 0 || mbLineOpen ||
		(mbHasLastLine && alLineIndex <= mlLastLineIndex))
		return false;

	std::fill(mvLinePixels.begin(), mvLinePixels.end(), 0);
	std::fill(mvLineWritten.begin(), mvLineWritten.end(), 0);
	mlWrittenSampleCount = 0;
	mlOpenLineIndex = alLineIndex;
	mbLineOpen = true;
	return true;
}

bool cLuxMsuMrScanProduct::SetLineSample(std::uint32_t alSampleIndex,
	const unsigned char *apRgba)
{
	if(mbLineOpen == false || apRgba == NULL || alSampleIndex >= kWidth)
		return false;

	unsigned char *pPixel = &mvLinePixels[static_cast<size_t>(alSampleIndex) * 4];
	pPixel[0] = apRgba[0];
	pPixel[1] = apRgba[1];
	pPixel[2] = apRgba[2];
	pPixel[3] = apRgba[3];
	if(mvLineWritten[alSampleIndex] == 0)
	{
		mvLineWritten[alSampleIndex] = 1;
		++mlWrittenSampleCount;
	}
	return true;
}

bool cLuxMsuMrScanProduct::CommitLine()
{
	if(mbLineOpen == false || mbInitialized == false || mpWindowTexture == NULL ||
		mlWrittenSampleCount != kWidth)
		return false;

	mlLastGapLineCount = mbHasLastLine ?
		mlOpenLineIndex - mlLastLineIndex - 1 : mlOpenLineIndex;
	const std::uint32_t lRowsToAdvance =
		mlLastGapLineCount >= mlHistoryLineCapacity - 1 ?
		mlHistoryLineCapacity :
		static_cast<std::uint32_t>(mlLastGapLineCount + 1);
	const size_t lRowBytes = static_cast<size_t>(kWidth) * 4;
	if(lRowsToAdvance < mlHistoryLineCapacity)
	{
		std::memmove(&mvPixels[static_cast<size_t>(lRowsToAdvance) * lRowBytes],
			&mvPixels[0],
			static_cast<size_t>(mlHistoryLineCapacity - lRowsToAdvance) * lRowBytes);
	}
	std::memset(&mvPixels[0], 0,
		static_cast<size_t>(lRowsToAdvance) * lRowBytes);
	std::memcpy(&mvPixels[0], &mvLinePixels[0], lRowBytes);
	UploadPixels();
	RenderWindow();

	mbHasLastLine = true;
	mlLastLineIndex = mlOpenLineIndex;
	mlStoredLineCount = static_cast<std::uint32_t>(
		std::min<std::uint32_t>(mlStoredLineCount + lRowsToAdvance,
			mlHistoryLineCapacity));
	mbLineOpen = false;
	mlWrittenSampleCount = 0;
	return true;
}

void cLuxMsuMrScanProduct::CancelLine()
{
	mbLineOpen = false;
	mlWrittenSampleCount = 0;
}

void cLuxMsuMrScanProduct::UploadPixels()
{
	if(mpWindowTexture == NULL || mvPixels.empty())
		return;
	if(SDL_UpdateTexture(mpWindowTexture, NULL, &mvPixels[0],
		static_cast<int>(kWidth * 4)) != 0)
	{
		Warning("Could not update the MSU-MR product window texture: %s.\n",
			SDL_GetError());
		return;
	}
	mbRedrawRequested.store(true);
}

SDL_Texture *cLuxMsuMrScanProduct::CreateWindowTexture(
	std::uint32_t alHistoryLines)
{
	if(mpRenderer == NULL || alHistoryLines == 0)
		return NULL;
	SDL_Texture *pTexture = SDL_CreateTexture(mpRenderer,
		SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
		static_cast<int>(kWidth), static_cast<int>(alHistoryLines));
	if(pTexture)
	{
		SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_NONE);
		SDL_SetTextureScaleMode(pTexture, SDL_ScaleModeNearest);
	}
	return pTexture;
}

std::uint32_t cLuxMsuMrScanProduct::CalculateWindowHistoryLines() const
{
	if(mpRenderer == NULL)
		return 1;
	int lOutputWidth = 0;
	int lOutputHeight = 0;
	if(SDL_GetRendererOutputSize(mpRenderer,
		&lOutputWidth, &lOutputHeight) != 0 ||
		lOutputWidth <= 0 || lOutputHeight <= 0)
		return mlHistoryLineCapacity > 0 ? mlHistoryLineCapacity : 1;

	const std::uint64_t lNumerator =
		static_cast<std::uint64_t>(lOutputHeight) * kWidth;
	std::uint64_t lHistoryLines =
		(lNumerator + static_cast<std::uint64_t>(lOutputWidth) - 1) /
		static_cast<std::uint64_t>(lOutputWidth);

	std::uint32_t lMaximumLines = kMaximumHistoryLines;
	SDL_RendererInfo rendererInfo;
	std::memset(&rendererInfo, 0, sizeof(rendererInfo));
	if(SDL_GetRendererInfo(mpRenderer, &rendererInfo) == 0 &&
		rendererInfo.max_texture_height > 0)
	{
		lMaximumLines = std::min<std::uint32_t>(lMaximumLines,
			static_cast<std::uint32_t>(rendererInfo.max_texture_height));
	}
	return static_cast<std::uint32_t>(std::max<std::uint64_t>(1,
		std::min<std::uint64_t>(lHistoryLines, lMaximumLines)));
}

bool cLuxMsuMrScanProduct::ResizeCanvasToWindow()
{
	const std::uint32_t lNewHistoryLines = CalculateWindowHistoryLines();
	if(lNewHistoryLines == mlHistoryLineCapacity)
		return true;
	const std::uint32_t lOldHistoryLines = mlHistoryLineCapacity;

	const size_t lRowBytes = static_cast<size_t>(kWidth) * 4;
	std::vector<unsigned char> vNewPixels(
		static_cast<size_t>(lNewHistoryLines) * lRowBytes, 0);
	const std::uint32_t lRowsToPreserve = std::min<std::uint32_t>(
		mlHistoryLineCapacity, lNewHistoryLines);
	if(lRowsToPreserve > 0 && mvPixels.empty() == false)
	{
		std::memcpy(&vNewPixels[0], &mvPixels[0],
			static_cast<size_t>(lRowsToPreserve) * lRowBytes);
	}

	SDL_Texture *pNewTexture = CreateWindowTexture(lNewHistoryLines);
	if(pNewTexture == NULL)
	{
		Warning("Could not resize the MSU-MR product canvas to %ux%u: %s.\n",
			kWidth, lNewHistoryLines, SDL_GetError());
		return false;
	}

	SDL_DestroyTexture(mpWindowTexture);
	mpWindowTexture = pNewTexture;
	mvPixels.swap(vNewPixels);
	mlHistoryLineCapacity = lNewHistoryLines;
	mlStoredLineCount = std::min<std::uint32_t>(
		mlStoredLineCount, mlHistoryLineCapacity);
	UploadPixels();
	Log("MSU-MR product canvas resized: %ux%u -> %ux%u, preservedRows=%u.\n",
		kWidth, lOldHistoryLines, kWidth, mlHistoryLineCapacity,
		mlStoredLineCount);
	return true;
}

void cLuxMsuMrScanProduct::RenderWindow()
{
	if(mpWindow == NULL || mpRenderer == NULL || mpWindowTexture == NULL ||
		(SDL_GetWindowFlags(mpWindow) & SDL_WINDOW_MINIMIZED) != 0)
		return;
	int lOutputWidth = 0;
	int lOutputHeight = 0;
	if(SDL_GetRendererOutputSize(mpRenderer,
		&lOutputWidth, &lOutputHeight) != 0 ||
		lOutputWidth <= 0 || lOutputHeight <= 0)
	{
		Warning("Could not size the MSU-MR product window: %s.\n", SDL_GetError());
		return;
	}

	SDL_Rect destination = {0, 0, lOutputWidth,
		static_cast<int>((static_cast<std::uint64_t>(lOutputWidth) *
			mlHistoryLineCapacity + kWidth - 1) / kWidth)};
	// Keep scan row zero flush with the top. Any aspect-ratio padding belongs
	// below the product rather than being split above and below it.
	destination.y = 0;
	if(SDL_SetRenderDrawColor(mpRenderer, 0, 0, 0, 255) != 0 ||
		SDL_RenderClear(mpRenderer) != 0 ||
		SDL_RenderCopy(mpRenderer, mpWindowTexture, NULL, &destination) != 0)
	{
		Warning("Could not draw the MSU-MR product window: %s.\n", SDL_GetError());
		return;
	}
	SDL_RenderPresent(mpRenderer);
	mbRedrawRequested.store(false);
}

bool cLuxMsuMrScanProduct::UpdateWindow()
{
	if(mbInitialized == false || mbCloseRequested.load())
		return false;
	const std::uint32_t lCurrentTicks = SDL_GetTicks();
	const bool bResizeReady = mbCanvasResizePending.load() &&
		lCurrentTicks - mlLastResizeEventTicks.load() >=
		kCanvasResizeDebounceMilliseconds;
	if(bResizeReady)
	{
		ResizeCanvasToWindow();
		mbCanvasResizePending.store(false);
	}
	const bool bSaveAsRequested = mbSaveAsRequested.exchange(false);
	const bool bSaveRequested = mbSaveRequested.exchange(false);
	if(bSaveAsRequested || bSaveRequested)
		SaveCanvas(bSaveAsRequested);
	if(mbRedrawRequested.load() || bResizeReady)
		RenderWindow();
	return mbCloseRequested.load() == false;
}

int SDLCALL cLuxMsuMrScanProduct::SdlEventWatch(void *apUserData,
	SDL_Event *apEvent)
{
	cLuxMsuMrScanProduct *pProduct =
		static_cast<cLuxMsuMrScanProduct*>(apUserData);
	if(pProduct == NULL || apEvent == NULL ||
		apEvent->type != SDL_WINDOWEVENT ||
		apEvent->window.windowID != pProduct->mlWindowId)
		return 0;

	switch(apEvent->window.event)
	{
	case SDL_WINDOWEVENT_CLOSE:
		pProduct->mbCloseRequested.store(true);
		break;
	case SDL_WINDOWEVENT_ENTER:
		if(pProduct->mbMouseInside == false)
		{
			pProduct->mlCursorStateBeforeEnter = SDL_ShowCursor(SDL_QUERY);
			SDL_ShowCursor(SDL_ENABLE);
			pProduct->mbMouseInside = true;
		}
		break;
	case SDL_WINDOWEVENT_LEAVE:
		if(pProduct->mbMouseInside)
		{
			SDL_ShowCursor(pProduct->mlCursorStateBeforeEnter);
			pProduct->mbMouseInside = false;
		}
		break;
	case SDL_WINDOWEVENT_EXPOSED:
	case SDL_WINDOWEVENT_SHOWN:
	case SDL_WINDOWEVENT_RESTORED:
		pProduct->mbRedrawRequested.store(true);
		break;
	case SDL_WINDOWEVENT_RESIZED:
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		pProduct->mlLastResizeEventTicks.store(SDL_GetTicks());
		pProduct->mbCanvasResizePending.store(true);
		pProduct->mbRedrawRequested.store(true);
		break;
	default:
		break;
	}
	return 0;
}
