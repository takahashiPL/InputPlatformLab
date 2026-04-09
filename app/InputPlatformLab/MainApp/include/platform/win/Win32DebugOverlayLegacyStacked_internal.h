#pragma once

// Cross-TU bridge: legacy stacked I/O types + Win32_LegacyStacked_* entry points (HUD_LEGACY_CODE_DEPENDENCY.md §7).
// Scratch / MainApp extern は各 .cpp で宣言（ヘッダの公開面を抑える）。

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
