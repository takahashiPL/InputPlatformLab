#pragma once

// MainApp.cpp — HUD 共有オーバーレイ状態（HUD_LEGACY_CODE_DEPENDENCY.md §2.3）。
// Win32DebugOverlay.cpp / Win32DebugOverlayLegacyStacked.cpp が参照する宣言をここに一本化（定義は MainApp.cpp）。

extern int s_paintScrollY;
extern int s_paintScrollLinePx;
extern int s_paintDbgT17DocY;
extern bool s_paintDbgT14LayoutValid;
extern int s_paintDbgT14VisibleModesDocStartY;
extern int s_paintDbgLineHeight;
extern int s_paintDbgMaxScroll;

extern int s_paintDbgContentHeight;
extern int s_paintDbgContentHeightBase;
extern int s_paintDbgExtraBottomPadding;
extern int s_paintDbgClientHeight;
extern int s_paintDbgActualOverlayHeight;
extern int s_paintDbgScrollBandReservePx;
extern int s_paintDbgLayoutRestVpBudgetHint;
extern int s_paintDbgClientW;
extern int s_paintDbgClientH;
extern int s_paintDbgT14ColumnBaseY;
extern int s_paintDbgT14BeforeVisibleDocH;
extern int s_paintDbgT14VisibleBlockDocH;
extern int s_paintDbgT14AfterVisibleDocH;
extern int s_paintDbgRestViewportClientH;
