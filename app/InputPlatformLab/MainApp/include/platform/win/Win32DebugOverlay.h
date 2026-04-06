#pragma once

#include "framework.h"

#include <stddef.h>

struct WindowsRendererState;

// [scroll] デバッグ帯の本文（Win32_MainView_FormatScrollDebugOverlay と同一フォーマット）。final D2D HUD 用にも利用。
// T46: maxScroll = contentH(with padding) - scrollVpH（本文スクロール用）。rawClientH=GetClientRect、scrollVpH=nPage 相当。
// provisionalNoSi: vmSplit のオーバーレイ高さ見積り用に T45 前で呼ぶとき true（SI 行なし）。
// T49: compactScrollBand — clientH が小さいとき [scroll] を短縮行にし帯高さ・重なりを抑える
void Win32_DebugOverlay_FormatScrollDebugOverlay(
    wchar_t* buf,
    size_t bufCount,
    const wchar_t* modeLabel,
    int contentHBase,
    int extraBottomPadding,
    int contentHeight,
    int t17DocY,
    int scrollY,
    int rawClientH,
    int scrollVpH,
    int jumpF7,
    int jumpF8,
    bool provisionalNoSi,
    bool compactScrollBand);

#ifndef WIN32_OVERLAY_T49_SCROLL_COMPACT_CLIENT_H
#define WIN32_OVERLAY_T49_SCROLL_COMPACT_CLIENT_H 360
#endif
#ifndef WIN32_OVERLAY_T49_T14_MINIMAL_HEADER_CLIENT_H
#define WIN32_OVERLAY_T49_T14_MINIMAL_HEADER_CLIENT_H 300
#endif
// T50: 極小 clientH 以下で T14/T15/T16/[scroll] の情報量を予算どおり削る（MainApp / FormatScroll で共有）
#ifndef WIN32_OVERLAY_T50_TINY_CLIENT_H
#define WIN32_OVERLAY_T50_TINY_CLIENT_H 280
#endif
// T51: vmSplit 後の実効 restVp が小さいときも compact/tiny 表示へ（rawClientH だけに依存しない）
#ifndef WIN32_OVERLAY_T51_REFILL_RESTVP_PX
#define WIN32_OVERLAY_T51_REFILL_RESTVP_PX 120
#endif
#ifndef WIN32_OVERLAY_T51_COMPACT_SCROLL_RESTVP_PX
#define WIN32_OVERLAY_T51_COMPACT_SCROLL_RESTVP_PX 120
#endif
#ifndef WIN32_OVERLAY_T51_SCROLL_ULTRA_RESTVP_PX
#define WIN32_OVERLAY_T51_SCROLL_ULTRA_RESTVP_PX 120
#endif
#ifndef WIN32_OVERLAY_T51_OMIT_T16_RESTVP_PX
#define WIN32_OVERLAY_T51_OMIT_T16_RESTVP_PX 120
#endif

// T53: 極小 Windowed のみ budgetPx（clientH または restVpBudgetHint）で段階的縮退（fill-monitor では原則未使用）
#ifndef WIN32_OVERLAY_T53_OMIT_T16_BUDGET_PX
#define WIN32_OVERLAY_T53_OMIT_T16_BUDGET_PX 140
#endif
#ifndef WIN32_OVERLAY_T53_T15_SUMMARY_BUDGET_PX
#define WIN32_OVERLAY_T53_T15_SUMMARY_BUDGET_PX 110
#endif
#ifndef WIN32_OVERLAY_T53_OMIT_T15_BUDGET_PX
#define WIN32_OVERLAY_T53_OMIT_T15_BUDGET_PX 90
#endif
#ifndef WIN32_OVERLAY_T53_OMIT_SCROLL_BAND_BUDGET_PX
#define WIN32_OVERLAY_T53_OMIT_SCROLL_BAND_BUDGET_PX 80
#endif
#ifndef WIN32_OVERLAY_T53_ONE_VISIBLE_ROW_BUDGET_PX
#define WIN32_OVERLAY_T53_ONE_VISIBLE_ROW_BUDGET_PX 70
#endif
#ifndef WIN32_OVERLAY_T53_MINIMAL_T14_BUDGET_PX
#define WIN32_OVERLAY_T53_MINIMAL_T14_BUDGET_PX 60
#endif

// T55/T56: fill-monitor では T51 refill を抑止。HUD の contentBudgetPx（行数・tiny 判定）は MainApp で committed 仮想高と合成し、scroll 幾何とは分離（Win32DebugOverlay）

// MainApp が WM_PAINT 用に組み立てたバッファを、GDI で描画するモジュール（D3D とは役割分担）。
// compactMenuForT37Layout: T37 有効時のみ左列メニューを短文化（T14 本文バッファは変えない）。
void Win32_FillMenuSamplePaintBuffers(
    HWND hwnd,
    const RECT& rcClient,
    wchar_t* menuBuf,
    size_t menuBufCount,
    wchar_t* t14Buf,
    size_t t14BufCount,
    bool compactMenuForT37Layout,
    int restVpBudgetHint = -1);

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

// T52: recreate / WM_SIZE 直後は暫定キャッシュを捨て、ログは WM_PAINT 計算完了まで stale な contentH/restVp を出さない。
void Win32_DebugOverlay_ResetProvisionalLayoutCache(void);
bool Win32_DebugOverlay_IsPaintLayoutMetricsValid(void);

// T47: Output の [SCROLL] は [scroll] 帯と同じ rawClientH / scrollVpH / maxScroll(contentH-scrollVpH) / SI（T45 スナップショット）
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

// T43: Borderless / Fullscreen（fill-monitor）では標準スクロールバー API を呼ばない。Windowed のみ SetScrollInfo 等を実行。
bool Win32_MainWindow_IsFillMonitorPresentationMode(HWND hwnd);
void Win32_UpdateNativeScrollbarsWindowedOnly(HWND hwnd, int nBar, SCROLLINFO* si, BOOL redraw);

// T44: maxScroll 確定後に s_paintScrollY を [0,maxScroll] に揃える（再生成・client 縮小後の外れ値除去）
void Win32_DebugOverlay_ClampScrollYToMaxScroll(int maxScroll, const wchar_t* where);

int Win32DebugOverlay_ScrollTargetT17WithTopMargin(void);
int Win32DebugOverlay_ScrollTargetT17Centered(HWND hwnd);

// T40: T14 visible modes 帯が本文スクロールから分離されている（MainView scrollY は rest のみ）
bool Win32DebugOverlay_IsT14VmSplitActive(void);
