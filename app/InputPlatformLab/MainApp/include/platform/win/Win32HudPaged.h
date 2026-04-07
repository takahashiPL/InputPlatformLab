#pragma once

#include <windows.h>

struct WindowsRendererState;

// ページ式デバッグ HUD（運用の正式仕様）。
// - 文字（status / title / page / 本文 / cand・act 相当）は GDI（Win32_HudPaged_PaintGdi）のみ。
// - D2D はグリッド・背景・非 HUD 描画のみ。ページ式有効時は HUD 文字を D2D で描かない。
// - WIN32_HUD_USE_PAGED_HUD=0 のときのみ Win32_DebugOverlay_PaintStackedLegacy（縦積み）が
//   通常描画経路になるレガシー参照。ページ式有効時はその経路・専用ログは原則通らない。
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
