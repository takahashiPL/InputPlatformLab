#pragma once

#include <windows.h>

struct WindowsRendererState;

// ページ式デバッグ HUD（運用の正式仕様）。WIN32_HUD_USE_PAGED_HUD=0 のときのみ
// Win32_DebugOverlay_PaintStackedLegacy（縦積み・vmSplit 等）が通常描画経路になるレガシー参照。
#ifndef WIN32_HUD_USE_PAGED_HUD
#define WIN32_HUD_USE_PAGED_HUD 1
#endif
inline bool Win32_HudPaged_IsEnabled()
{
    return WIN32_HUD_USE_PAGED_HUD != 0;
}

void Win32_HudPaged_PaintGdi(
    HWND hwnd,
    HDC hdc,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel);
void Win32_HudPaged_PrefillD2d(WindowsRendererState* st, UINT clientW, UINT clientH);
void Win32_HudPaged_ResetScrollBar(HWND hwnd);
void Win32_HudPaged_AdvancePage(int delta);
