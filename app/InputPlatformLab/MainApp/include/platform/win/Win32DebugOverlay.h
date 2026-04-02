#pragma once

#include "framework.h"

#include <stddef.h>

// MainApp.cpp — テキストバッファ組み立て（T14〜T18 等）。描画は Win32DebugOverlay.cpp。
void Win32_FillMenuSamplePaintBuffers(
    HWND hwnd,
    const RECT& rcClient,
    wchar_t* menuBuf,
    size_t menuBufCount,
    wchar_t* t14Buf,
    size_t t14BufCount);

// T26: D3D クリア後の GDI デバッグ描画（TRANSPARENT 背景・スクロールオーバーレイ含む）
void Win32DebugOverlay_Paint(HWND hwnd, HDC hdc, const wchar_t* t17ModeLabelForOverlay);

void Win32DebugOverlay_ScrollLog(
    const wchar_t* where,
    HWND hwnd,
    int scrollYBefore,
    int scrollYAfter,
    int contentHOverride,
    int t17Override,
    int contentHBase,
    int extraBottomPadding);

void Win32DebugOverlay_MainView_SetScrollPos(HWND hwnd, int newY, const wchar_t* logWhere);

int Win32DebugOverlay_ScrollTargetT17WithTopMargin(void);
int Win32DebugOverlay_ScrollTargetT17Centered(HWND hwnd);
