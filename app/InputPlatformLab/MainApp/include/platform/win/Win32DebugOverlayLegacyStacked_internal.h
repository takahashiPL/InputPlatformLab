#pragma once

// =============================================================================
// Win32DebugOverlayLegacyStacked_internal.h — legacy TU 実装のための宣言面
// =============================================================================
// Win32DebugOverlayLegacyStacked.cpp のみが include（main TU の Win32DebugOverlay.cpp は include しない）。
// 先に bridge.h を include し、ここでは bridge に無い型・Apply*・scratch・vmSplit・T45 束（RunUnified…）までを宣言する。
// bridge.h に既にある宣言・型は重複しない。
// ApplyD2dHudPrefill 等で WindowsRendererState のメンバに触れるため、本ヘッダのみ WindowsRenderer.h を include（bridge は前方宣言のみ）。
// 詳細: HUD_LEGACY_CODE_DEPENDENCY.md §5 / §7.2 / §7.6。
// =============================================================================

#include "Win32DebugOverlayLegacyStacked_bridge.h"

#include "WindowsRenderer.h"

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

struct Win32_LegacyStacked_MainLayoutScratchRead {
    int finalRow1HeightPx{};
    int row2TopPx{};
    bool t14VmSplitActive{};
    int finalBodyTopPx{};
    int bodyT14DocTopPx{};
    int restViewportTopPx{};
    bool t53ScrollBandDrawEnabled{};
    int t14VmSplitPrefixH{};
    int t14VmSplitVmBandH{};
    const wchar_t* t14VmSplitPrefix{};
    const wchar_t* t14VmSplitVmBand{};
    const wchar_t* t14VmSplitRest{};
};

void Win32_LegacyStacked_LoadLayoutScratchRead(Win32_LegacyStacked_MainLayoutScratchRead* out);

void Win32_LegacyStacked_SetT14VmSplitActive(bool active);
void Win32_LegacyStacked_SetRestViewportTopPx(int px);

void Win32_LegacyStacked_ApplyMainAppPaintDbgScrollLineMetrics(int scrollLinePx);

void Win32_LegacyStacked_ApplyMainAppPaintDbgClearScrollBandReserve(void);
void Win32_LegacyStacked_ApplyMainAppPaintDbgRestViewportClientH(int scrollVpH);
void Win32_LegacyStacked_ApplyMainAppPaintDbgT14ColumnInit(int t14BaseY);
void Win32_LegacyStacked_ApplyMainAppPaintDbgT14VisibleModesStart(int visibleModesDocStartY);
void Win32_LegacyStacked_ApplyMainAppPaintDbgT17DocY(int t17DocY);

struct Win32_LegacyStacked_MainAppPaintDbgApplyContentClient {
    int contentHeight{};
    int contentHeightBase{};
    int extraBottomPadding{};
    int clientHeight{};
    int clientW{};
    int clientH{};
};
void Win32_LegacyStacked_ApplyMainAppPaintDbgContentAndClientGeometry(
    const Win32_LegacyStacked_MainAppPaintDbgApplyContentClient& in);

void Win32_LegacyStacked_ApplyMainAppPaintDbgScrollBandReservePx(int reservePx);
void Win32_LegacyStacked_ApplyMainAppPaintDbgVmSplitContentPadding(int contentHeight, int extraBottomPadding);

void Win32_LegacyStacked_ApplyMainAppPaintDbgPostOverlayMeasures(
    bool vmSplitActive, int actualOverlayHeight, int clientH);

void Win32_LegacyStacked_ApplyMainAppPaintDbgT14BudgetHeights(
    int totalBeforeVisible, int hVmLines, int contentHeightBase);

void Win32_LegacyStacked_ApplyMainAppPaintDbgLayoutRestVpBudgetHint(int hint);

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
