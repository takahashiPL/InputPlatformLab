#pragma once

#include "framework.h"

#include <stddef.h>

struct WindowsRendererState;

// [scroll] デバッグ帯の本文（Win32_MainView_FormatScrollDebugOverlay と同一フォーマット）。final D2D HUD 用にも利用。
void Win32_DebugOverlay_FormatScrollDebugOverlay(
    wchar_t* buf,
    size_t bufCount,
    const wchar_t* modeLabel,
    int contentHBase,
    int extraBottomPadding,
    int contentHeight,
    int maxScroll,
    int t17DocY,
    int scrollY,
    int clientH,
    int jumpF7,
    int jumpF8);

// MainApp が WM_PAINT 用に組み立てたバッファを、GDI で描画するモジュール（D3D とは役割分担）。
// compactMenuForT37Layout: T37 有効時のみ左列メニューを短文化（T14 本文バッファは変えない）。
void Win32_FillMenuSamplePaintBuffers(
    HWND hwnd,
    const RECT& rcClient,
    wchar_t* menuBuf,
    size_t menuBufCount,
    wchar_t* t14Buf,
    size_t t14BufCount,
    bool compactMenuForT37Layout);

// T37 仮想本文オーバーレイ要求中（オフスクリーン経路）。GDI 左列のレイアウト分岐用。
bool Win32_IsT37VirtualBodyOverlayActiveForLayout(void);

// WM_PAINT 内・WindowsRenderer_Frame より前: 左列 HUD 全文（menu+t14）を D2D 用に state へ書き込む（GDI 計測と一致）
void Win32_DebugOverlay_PrefillHudLeftColumnForD2d(
    HWND hwnd,
    HDC hdc,
    WindowsRendererState* st,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel);

// メニュー列バッファのみ（T14 本文は組み立てない）。D2D 左列用。
void Win32_FillMenuSamplePaintBuffers_MenuColumnOnly(wchar_t* menuBuf, size_t menuBufCount);

// T26: D3D クリア後の GDI デバッグ描画（TRANSPARENT 背景・スクロールオーバーレイ含む）
// T37: suppressT14BodyGdi=true のとき T14 本文の GDI 描画を省略（DWrite 仮想解像度本文と二重表示しない）
// skipMenuColumnGdi: 左列 HUD 全文を D2D に描いたフレームでは GDI でメニュー・T14 本文を重ねない
// skipScrollBandGdi: 下端 [scroll] 帯を D2D に描いたフレームでは GDI で重ねない（T38）
void Win32DebugOverlay_Paint(
    HWND hwnd,
    HDC hdc,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel,
    bool suppressT14BodyGdi,
    bool skipMenuColumnGdi,
    bool skipScrollBandGdi);

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

// T40: T14 visible modes 帯が本文スクロールから分離されている（MainView scrollY は rest のみ）
bool Win32DebugOverlay_IsT14VmSplitActive(void);
