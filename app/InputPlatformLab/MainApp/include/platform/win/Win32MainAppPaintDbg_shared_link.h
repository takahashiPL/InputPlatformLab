#pragma once

// MainApp.cpp — HUD 共有オーバーレイ状態のうち、legacy stacked TU が読み書きする分（HUD_LEGACY_CODE_DEPENDENCY.md §2.3）。
// 宣言はここに一本化し、Win32DebugOverlay*.cpp の個別 extern を減らす（定義は MainApp.cpp のまま）。

extern int s_paintScrollY;
extern int s_paintScrollLinePx;
extern int s_paintDbgT17DocY;
extern bool s_paintDbgT14LayoutValid;
extern int s_paintDbgT14VisibleModesDocStartY;
extern int s_paintDbgLineHeight;
extern int s_paintDbgMaxScroll;
