#pragma once

// Main TU ↔ legacy TU の最小橋渡し（Win32DebugOverlay.cpp のみ include）。
// レガシー実装の残りの型・Apply*・scratch API は Win32DebugOverlayLegacyStacked_internal.h（legacy TU のみ）。
// MainApp 共有 extern は Win32MainAppPaintDbg_shared_link.h（HUD_LEGACY_CODE_DEPENDENCY.md §2.3 / §7.2）。

#include <windows.h>

struct WindowsRendererState;

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

bool Win32_LegacyStacked_GetT14VmSplitActive(void);
int Win32_LegacyStacked_GetT17DocYRestScroll(void);

void Win32_LegacyStacked_ClearPaintLayoutMetricsFromPaintValid(void);
void Win32_LegacyStacked_ApplyMainAppPaintDbgResetProvisionalLayoutExtras(void);
bool Win32_LegacyStacked_IsPaintLayoutMetricsFromPaintValid(void);

void Win32_DebugOverlay_LegacyStacked_RunGdiPaint(const Win32_LegacyStacked_GdiPaintParams& p);
void Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics(const Win32_LegacyStacked_LayoutMetricsParams& p);
