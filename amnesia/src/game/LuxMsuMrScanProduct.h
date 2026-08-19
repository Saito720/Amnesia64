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

#ifndef LUX_MSU_MR_SCAN_PRODUCT_H
#define LUX_MSU_MR_SCAN_PRODUCT_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "SDL2/SDL_events.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

// Owns the accumulating rendered scan image independently of instrument
// scheduling and sensor acquisition. The product is presented in its own
// SDL window; gap rows remain black while acquired black remains valid data.
class cLuxMsuMrScanProduct
{
public:
	static const std::uint32_t kWidth = 1572;

	cLuxMsuMrScanProduct();
	~cLuxMsuMrScanProduct();

	bool Initialize();
	void Reset();
	bool BeginLine(std::uint64_t alLineIndex);
	bool SetLineSample(std::uint32_t alSampleIndex,
					   const unsigned char *apRgba);
	bool CommitLine();
	void CancelLine();
	// Returns false after the user requests that the product window close.
	bool UpdateWindow();

	bool IsInitialized() const { return mbInitialized; }
	std::uint32_t GetStoredLineCount() const { return mlStoredLineCount; }
	std::uint32_t GetHistoryLineCapacity() const { return mlHistoryLineCapacity; }
	std::uint64_t GetLastLineIndex() const { return mlLastLineIndex; }
	std::uint64_t GetLastGapLineCount() const { return mlLastGapLineCount; }

private:
	static int SDLCALL SdlEventWatch(void *apUserData, SDL_Event *apEvent);
#if defined(_WIN32)
	static LRESULT CALLBACK NativeWindowProc(HWND ahWindow, UINT alMessage,
		WPARAM alWParam, LPARAM alLParam);
	bool CreateNativeMenu();
	void DestroyNativeMenu();
	bool PromptForSavePath(std::wstring &asPath);
#endif
	bool SaveCanvas(bool abSaveAs);
	SDL_Texture *CreateWindowTexture(std::uint32_t alHistoryLines);
	std::uint32_t CalculateWindowHistoryLines() const;
	bool ResizeCanvasToWindow();
	void UploadPixels();
	void RenderWindow();

	std::vector<unsigned char> mvLinePixels;
	std::vector<unsigned char> mvLineWritten;
	std::vector<unsigned char> mvPixels;
	SDL_Window *mpWindow;
	SDL_Renderer *mpRenderer;
	SDL_Texture *mpWindowTexture;
#if defined(_WIN32)
	HWND mhNativeWindow;
	WNDPROC mpPreviousWindowProc;
	HMENU mhNativeMenu;
#endif
	std::atomic<bool> mbCloseRequested;
	std::atomic<bool> mbRedrawRequested;
	std::atomic<bool> mbCanvasResizePending;
	std::atomic<bool> mbSaveRequested;
	std::atomic<bool> mbSaveAsRequested;
	std::atomic<std::uint32_t> mlLastResizeEventTicks;
	std::wstring msSavePath;
	std::uint32_t mlWindowId;
	int mlCursorStateBeforeEnter;
	bool mbEventWatchInstalled;
	bool mbMouseInside;
	bool mbInitialized;
	bool mbHasLastLine;
	bool mbLineOpen;
	std::uint64_t mlOpenLineIndex;
	std::uint64_t mlLastLineIndex;
	std::uint64_t mlLastGapLineCount;
	std::uint32_t mlStoredLineCount;
	std::uint32_t mlWrittenSampleCount;
	std::uint32_t mlHistoryLineCapacity;
};

#endif // LUX_MSU_MR_SCAN_PRODUCT_H
