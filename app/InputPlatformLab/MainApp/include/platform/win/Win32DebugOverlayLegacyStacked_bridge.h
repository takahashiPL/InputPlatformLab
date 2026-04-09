#pragma once

// =============================================================================
// Win32DebugOverlayLegacyStacked_bridge.h — main TU から見える最小面のみ
// =============================================================================
// 含めるもの:
//   • Win32DebugOverlay.cpp が legacy TU とやり取りする I/O struct（CommonParams / GdiPaintParams / LayoutMetricsParams 等）
//   • MainApp 共有の読み取りスナップショット（MainAppPaintDbgRead + LoadMainAppPaintDbgRead）
//   • T52 / 仮想レイアウトまわりの getter・reset（GetT14VmSplitActive / IsPaintLayoutMetricsFromPaintValid 等）
//   • RunGdiPaint / RunComputeLayoutMetrics と、main 入口用の薄いラッパ（RunGdiPaintFromPaintEntry / RunComputeLayoutMetricsForD2dPrefill）
// 含めないもの（→ Win32DebugOverlayLegacyStacked_internal.h、legacy TU のみ）:
//   • vmSplit scratch 型、ApplyMainAppPaintDbg* の大半、LoadLayoutScratchRead、RunUnifiedMaxScrollClampAndT45 等
// MainApp extern の宣言: Win32MainAppPaintDbg_shared_link.h（§2.3）。境界の説明: HUD_LEGACY_CODE_DEPENDENCY.md §5 / §7.2。
// =============================================================================

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

// Main TU の public entry（Paint / Prefill）専用: struct 組み立てを legacy TU に寄せ、ここではシグネチャのみ（挙動は Run* と同一）。
void Win32_DebugOverlay_LegacyStacked_RunGdiPaintFromPaintEntry(
    HWND hwnd,
    HDC hdc,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel,
    bool suppressT14BodyGdi,
    bool skipMenuColumnGdi,
    bool skipScrollBandGdi);

void Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetricsForD2dPrefill(
    HWND hwnd,
    HDC hdc,
    WindowsRendererState* outHud,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel);
