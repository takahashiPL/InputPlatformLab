#pragma once

// Internal: legacy stacked HUD (WIN32_HUD_USE_PAGED_HUD=0) helpers + scratch.
// First physical split from Win32DebugOverlay.cpp — not a public API (HUD_LEGACY_CODE_DEPENDENCY.md §7).

#include "WindowsRenderer.h"

#include <windows.h>

struct Win32_LegacyStacked_CommonParams {
    HWND hwnd{};
    HDC hdc{};
    const wchar_t* t17ModeLabelForOverlay{};
    const wchar_t* t17CandLabel{};
    const wchar_t* t17ActLabel{};
};

struct Win32_LegacyStacked_LayoutMetricsParams {
    Win32_LegacyStacked_CommonParams common{};
    WindowsRendererState* outHud{};
    bool logScroll{};
};

struct Win32_LegacyStacked_GdiPaintParams {
    Win32_LegacyStacked_CommonParams common{};
    bool suppressT14BodyGdi{};
    bool skipMenuColumnGdi{};
    bool skipScrollBandGdi{};
};

struct Win32_LegacyStacked_VmSplitScratchPassOut {
    int splitHPrefix{};
    int splitHVmBand{};
    int splitHRest{};
    int splitRestTopPx{};
    int splitRestVp{};
    int extraBottomPadding{};
    int contentHeight{};
    int maxScroll{};
    int t17DocY{};
    int t14VisibleModesDocStartY{};
    bool t14LayoutValid{};
    bool t14LayoutOutValid{};
};

struct Win32_LegacyStacked_UnifiedScrollLayoutForT45 {
    int scrollContentHFinal{};
    int scrollViewportHFinal{};
    int maxScrollUnified{};
};

// Scratch + T52 (definitions: Win32DebugOverlayLegacyStacked.cpp). Same names as §7.2.
extern int s_paintDbgFinalBodyTopPx;
extern int s_paintDbgBodyT14DocTopPx;
extern int s_paintDbgFinalRow1HeightPx;
extern int s_paintDbgRow2TopPx;
extern bool s_paintDbgT14VmSplitActive;
extern int s_paintDbgT17DocYRestScroll;
extern int s_paintDbgRestViewportTopPx;
extern bool s_paintDbgT53ScrollBandDrawEnabled;
extern wchar_t s_paintDbgT14VmSplitPrefix[8192];
extern wchar_t s_paintDbgT14VmSplitVmBand[8192];
extern wchar_t s_paintDbgT14VmSplitRest[16384];
extern int s_paintDbgT14VmSplitPrefixH;
extern int s_paintDbgT14VmSplitVmBandH;

extern bool s_paintDbgLayoutMetricsFromPaintValid;

extern int s_paintScrollY;
extern int s_paintScrollLinePx;
extern int s_paintDbgT17DocY;
extern bool s_paintDbgT14LayoutValid;
extern int s_paintDbgT14VisibleModesDocStartY;
extern int s_paintDbgLineHeight;
extern int s_paintDbgMaxScroll;

void Win32_DebugOverlay_ClampScrollYToMaxScroll(int maxScroll, const wchar_t* where);

void Win32_DebugOverlay_LegacyStacked_InvokeT45(HWND hwnd, int scrollContentH, int scrollViewportH, int pos);

void Win32_LegacyStacked_ApplyD2dHudPrefill(
    WindowsRendererState* outHud,
    const wchar_t* menuBuf,
    const wchar_t* t14Buf,
    const wchar_t* overlay,
    int clientW,
    int clientH,
    int t37TopGap,
    int row1H,
    int bodyTopPx,
    int t14DocTopAbsPx,
    int row2TopPx,
    const RECT& rcMenuDoc,
    bool vmSplitActive,
    const wchar_t* pVis,
    const wchar_t* pT15,
    int splitHPrefix,
    int splitHVmBand,
    bool dbgHudDrawScrollBand,
    int dbgHudLeftColumnScrollYPx,
    int scrollBandReservePx,
    int dbgHudRestViewportTopPx);

void Win32_LegacyStacked_ClearScratchRestViewportTop();
void Win32_LegacyStacked_ApplyScratchFinalHudGeometry(int bodyTopPx, int t14DocTopAbsPx, int row1H, int row2TopPx);
void Win32_LegacyStacked_ResetVmSplitScratchFlags();
void Win32_LegacyStacked_ApplyScratchT53ScrollBandDrawEnabled(HWND hwnd, int restVpBudgetHint, int clientH);

bool Win32_LegacyStacked_RunVmSplitScratchPass(
    HDC hdc,
    int clientW,
    int clientH,
    int row2TopPx,
    int t14BaseY,
    const wchar_t* t14Buf,
    const wchar_t* pVis,
    const wchar_t* pT15,
    Win32_LegacyStacked_VmSplitScratchPassOut* out);

void Win32_LegacyStacked_ApplyVmSplitMainAppExternFromScratchPass(
    const Win32_LegacyStacked_VmSplitScratchPassOut& vsp);

void Win32_LegacyStacked_ApplyScrollLineMetricsFromHdc(HDC hdc);
void Win32_LegacyStacked_SetDbgMaxScrollAndClampScrollY(int maxScrollLogical, const wchar_t* where);

void Win32_LegacyStacked_RunUnifiedMaxScrollClampAndT45(
    HWND hwnd,
    const Win32_LegacyStacked_UnifiedScrollLayoutForT45& u);

void Win32_LegacyStacked_MarkPaintLayoutMetricsFromPaintValid(void);
