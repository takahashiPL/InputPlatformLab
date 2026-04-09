#pragma once

#include <windows.h>

struct WindowsRendererState;

// ページ式デバッグ HUD — 通常運用の正（既定: WIN32_HUD_USE_PAGED_HUD=1）。
// - 文字（status / title / page / 本文 / cand・act 相当）は GDI（Win32_HudPaged_PaintGdi）のみ。
// - D2D はグリッド・背景・非 HUD 描画のみ。ページ式有効時は HUD 文字を D2D で描かない。
// - WIN32_HUD_USE_PAGED_HUD=0 のときのみ Win32_DebugOverlay_PaintStackedLegacy（レガシー縦積み）が
//   描画経路になる互換・旧経路。既定ビルドでは無効のため、その専用ログは原則通らない。
// 実コード依存の棚卸し: docs/HUD_LEGACY_CODE_DEPENDENCY.md
// 維持・削減の優先順位: docs/HUD_LEGACY_MAINTENANCE_PRIORITIES.md
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
