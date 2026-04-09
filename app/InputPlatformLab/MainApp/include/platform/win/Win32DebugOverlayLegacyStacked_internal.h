#pragma once

// Cross-TU bridge: legacy stacked I/O types + Win32_LegacyStacked_* entry points (HUD_LEGACY_CODE_DEPENDENCY.md §7).
// MainApp 共有のうち legacy が触る分は Win32MainAppPaintDbg_shared_link.h + Load/Apply API（定義は MainApp.cpp）。

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

// Main TU reads legacy scratch via Load / getters — storage stays in Win32DebugOverlayLegacyStacked.cpp (§7.2).
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

bool Win32_LegacyStacked_GetT14VmSplitActive(void);
int Win32_LegacyStacked_GetT17DocYRestScroll(void);
void Win32_LegacyStacked_SetT14VmSplitActive(bool active);
void Win32_LegacyStacked_SetRestViewportTopPx(int px);

bool Win32_LegacyStacked_IsPaintLayoutMetricsFromPaintValid(void);
void Win32_LegacyStacked_ClearPaintLayoutMetricsFromPaintValid(void);

// MainApp.cpp 共有 s_paintDbg* / scroll（§2.3）— shared_link.h の extern + Load/Apply API に集約。
struct Win32_LegacyStacked_MainAppPaintDbgRead {
    int scrollY{};
    int restViewportClientH{};
    int contentHeight{};
    int t17DocY{};
    int clientHeight{};
    int maxScroll{};
    int layoutRestVpBudgetHint{};
    int scrollBandReservePx{};
    int contentHeightBase{};
    int extraBottomPadding{};
};
void Win32_LegacyStacked_LoadMainAppPaintDbgRead(Win32_LegacyStacked_MainAppPaintDbgRead* out);

void Win32_LegacyStacked_ApplyMainAppPaintDbgScrollLineMetrics(int scrollLinePx);

void Win32_LegacyStacked_ApplyMainAppPaintDbgResetProvisionalLayoutExtras(void);

// ComputeLayoutMetrics → MainApp 共有への薄い write（順序依存が強い経路は本体に残す。T45/T46 本体は未変更）
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

// vmSplit 確定時の MainApp extern への write（代入は legacy TU 内の helper に集約）
void Win32_LegacyStacked_ApplyVmSplitMainAppExternFromScratchPass(
    const Win32_LegacyStacked_VmSplitScratchPassOut& vsp);

void Win32_LegacyStacked_ApplyScrollLineMetricsFromHdc(HDC hdc);
void Win32_LegacyStacked_SetDbgMaxScrollAndClampScrollY(int maxScrollLogical, const wchar_t* where);

void Win32_LegacyStacked_RunUnifiedMaxScrollClampAndT45(
    HWND hwnd,
    const Win32_LegacyStacked_UnifiedScrollLayoutForT45& u);

void Win32_LegacyStacked_MarkPaintLayoutMetricsFromPaintValid(void);

// Main TU (Win32DebugOverlay.cpp) entry points — implementations in Win32DebugOverlayLegacyStacked.cpp.
void Win32_DebugOverlay_LegacyStacked_RunGdiPaint(const Win32_LegacyStacked_GdiPaintParams& p);
void Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics(const Win32_LegacyStacked_LayoutMetricsParams& p);
