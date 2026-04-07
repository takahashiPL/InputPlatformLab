#pragma once

#include <windows.h>

struct WindowsRendererState;

// ページ式デバッグ HUD（運用表示の主体）。縦積み全文 HUD はレガシー参照用。
// 0 にすると Win32_DebugOverlay_PaintStackedLegacy（縦積み・vmSplit 等）に戻す。
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
