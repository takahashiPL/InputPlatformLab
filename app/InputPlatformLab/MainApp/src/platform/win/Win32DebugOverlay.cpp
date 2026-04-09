#include "framework.h"
#include "Win32DebugOverlay.h"
#include "WindowsRenderer.h"
#include "Win32HudPaged.h"

#include <algorithm>

#ifndef WIN32_MAIN_T17_JUMP_TOP_MARGIN
#define WIN32_MAIN_T17_JUMP_TOP_MARGIN 160
#endif

// T37 有効時: D2D の T33 1 行帯の下に compact GDI を置く（上端の縦重なり回避）
#ifndef WIN32_OVERLAY_T37_MENU_TOP_GAP_PX
#define WIN32_OVERLAY_T37_MENU_TOP_GAP_PX 52
#endif

// T39: final HUD row1↔row2 / row2↔body の余白（final-client 基準で 3 モード共通）
#ifndef WIN32_HUD_DBG_FINAL_ROW1_BOTTOM_GAP_PX
#define WIN32_HUD_DBG_FINAL_ROW1_BOTTOM_GAP_PX 8
#endif
#ifndef WIN32_HUD_DBG_FINAL_ROW2_TO_BODY_EXTRA_GAP_PX
#define WIN32_HUD_DBG_FINAL_ROW2_TO_BODY_EXTRA_GAP_PX 28
#endif
#ifndef WIN32_HUD_DBG_MENU_TO_T14_GAP_PX
#define WIN32_HUD_DBG_MENU_TO_T14_GAP_PX 8
#endif

// T48: body（vmSplit rest）スクロールビューポートの最小高さ（scrollVpH / [scroll] と GDI/D2D クリップを一致させる）
#ifndef WIN32_OVERLAY_MIN_BODY_VIEWPORT_PX
#define WIN32_OVERLAY_MIN_BODY_VIEWPORT_PX 64
#endif

// -----------------------------------------------------------------------------
// File-local: legacy stacked HUD scratch (writers: Win32_DebugOverlay_ComputeLayoutMetrics /
// Win32_DebugOverlay_PaintStackedLegacy). Not written when only Win32_HudPaged_PaintGdi runs.
// docs/HUD_LEGACY_CODE_DEPENDENCY.md §2.2 / §2.3, §7 (future split notes)
//
// Extraction: these must stay **declared above** Win32DebugOverlay_ScrollTargetT17* / IsT14VmSplitActive
// (they read vmSplit state before the legacy #region in this .cpp). Splitting to another .cpp needs getters or TU reorder.
// Grouped in an anonymous namespace below (internal linkage; same s_paintDbg* names — not T46 / T52 flags; see §7.3).
// -----------------------------------------------------------------------------
namespace {
// Legacy stacked pipeline — explicit I/O bundles (shared callers pass these; see docs §7.5). Keeps entry-point arity stable for future .cpp split.
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

// GDI Paint と D2D final HUD で body クリップ先頭を共有（ComputeLayoutMetrics で更新）
int s_paintDbgFinalBodyTopPx = 0;
int s_paintDbgBodyT14DocTopPx = 0;
int s_paintDbgFinalRow1HeightPx = 0;
int s_paintDbgRow2TopPx = 0;

// T40: visible modes 帯を本文スクロールから分離
bool s_paintDbgT14VmSplitActive = false;
int s_paintDbgT17DocYRestScroll = 0;
int s_paintDbgRestViewportTopPx = 0;

// T53: 極小 Windowed で [scroll] 帯の GDI 重ね描きを止める（レイアウト予約は維持）
bool s_paintDbgT53ScrollBandDrawEnabled = true;

// T40: GDI パス用（ComputeLayoutMetrics で更新、Paint が参照）
wchar_t s_paintDbgT14VmSplitPrefix[8192]{};
wchar_t s_paintDbgT14VmSplitVmBand[8192]{};
wchar_t s_paintDbgT14VmSplitRest[16384]{};
int s_paintDbgT14VmSplitPrefixH = 0;
int s_paintDbgT14VmSplitVmBandH = 0;

// Legacy stacked pipeline — forward declarations (definitions in legacy #region below; same unnamed namespace).
// Public entry points call **adapters** first (side-effect boundary; docs §7.6); adapters delegate to ComputeLayoutMetrics / PaintStackedLegacy.
void Win32_DebugOverlay_LegacyStacked_RunGdiPaint(const Win32_LegacyStacked_GdiPaintParams& p);
void Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics(const Win32_LegacyStacked_LayoutMetricsParams& p);
} // namespace

// ---------------------------------------------------------------------------
// GDI: D3D で塗ったクライアント上にデバッグ文字を載せる。
// 通常運用（ページ式 HUD 既定）では Win32_HudPaged_PaintGdi が本文を描く。縦スクロール・[scroll]・T14/T17 行位置の計測は主にレガシー縦積み経路（PaintStackedLegacy）で使用。
// ---------------------------------------------------------------------------

// MainApp.cpp で定義 — 共有オーバーレイ状態（レガシー ComputeLayoutMetrics / T37 / T14 追従 / ページ式での scroll リセット）。legacy 専用ではない。
// docs/HUD_LEGACY_CODE_DEPENDENCY.md §2.3
extern int s_paintScrollY;
extern int s_paintScrollLinePx;
extern int s_paintDbgContentHeight;
extern int s_paintDbgContentHeightBase;
extern int s_paintDbgExtraBottomPadding;
extern int s_paintDbgClientHeight;
extern int s_paintDbgT17DocY;
extern bool s_paintDbgT14LayoutValid;
extern int s_paintDbgT14VisibleModesDocStartY;
extern int s_paintDbgLineHeight;
extern int s_paintDbgActualOverlayHeight;
extern int s_paintDbgScrollBandReservePx;
extern int s_paintDbgLayoutRestVpBudgetHint;
extern int s_paintDbgClientW;
extern int s_paintDbgClientH;
extern int s_paintDbgMaxScroll;
extern int s_paintDbgT14ColumnBaseY;
extern int s_paintDbgT14BeforeVisibleDocH;
extern int s_paintDbgT14VisibleBlockDocH;
extern int s_paintDbgT14AfterVisibleDocH;
extern int s_paintDbgRestViewportClientH;

#pragma region Public HUD overlay entry (GDI WM_PAINT)

// Legacy stacked pipeline: forward declarations live in the file-top anonymous namespace (with scratch).

// HUD overlay — public entry (WM_PAINT): paged branch first, then legacy stacked.
void Win32DebugOverlay_Paint(
    HWND hwnd,
    HDC hdc,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel,
    bool suppressT14BodyGdi,
    bool skipMenuColumnGdi,
    bool skipScrollBandGdi)
{
    if (Win32_HudPaged_IsEnabled())
    {
        Win32_HudPaged_PaintGdi(hwnd, hdc, t17CandLabel, t17ActLabel);
        (void)t17ModeLabelForOverlay;
        (void)suppressT14BodyGdi;
        (void)skipMenuColumnGdi;
        (void)skipScrollBandGdi;
        return;
    }
    Win32_LegacyStacked_GdiPaintParams paint{};
    paint.common.hwnd = hwnd;
    paint.common.hdc = hdc;
    paint.common.t17ModeLabelForOverlay = t17ModeLabelForOverlay;
    paint.common.t17CandLabel = t17CandLabel;
    paint.common.t17ActLabel = t17ActLabel;
    paint.suppressT14BodyGdi = suppressT14BodyGdi;
    paint.skipMenuColumnGdi = skipMenuColumnGdi;
    paint.skipScrollBandGdi = skipScrollBandGdi;
    Win32_DebugOverlay_LegacyStacked_RunGdiPaint(paint);
}

#pragma endregion

// Shared overlay helpers: presentation, scroll band strings, and CALCRECT/T60 measurement used by legacy ComputeLayoutMetrics
// (and potentially other paths). Not legacy-only — see docs/HUD_LEGACY_CODE_DEPENDENCY.md §7.4.
#pragma region Shared overlay helpers (presentation, scroll, [scroll] text; CALCRECT cluster before legacy)

// Presentation / window chrome (scrollbars, fill-monitor; shared with paged and legacy paths).
static bool Win32_IsMainWindowFillMonitorPresentation(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
    {
        return false;
    }
    const LONG_PTR st = GetWindowLongPtr(hwnd, GWL_STYLE);
    return (st & static_cast<LONG_PTR>(WS_POPUP)) != 0;
}

bool Win32_MainWindow_IsFillMonitorPresentationMode(HWND hwnd)
{
    return Win32_IsMainWindowFillMonitorPresentation(hwnd);
}

void Win32_UpdateNativeScrollbarsWindowedOnly(HWND hwnd, int nBar, SCROLLINFO* si, BOOL redraw)
{
    if (!hwnd || !si)
    {
        return;
    }
    if (Win32_IsMainWindowFillMonitorPresentation(hwnd))
    {
        return;
    }
    SetScrollInfo(hwnd, nBar, si, redraw);
}

// T46/T47: updated on legacy ComputeLayoutMetrics → T45 path; read by shared Win32DebugOverlay_ScrollLog /
// Win32_DebugOverlay_FormatScrollDebugOverlay ([scroll] / [SCROLL]). Not pure "legacy-only" state — keep near scroll helpers until those are refactored. docs/HUD_LEGACY_CODE_DEPENDENCY.md §7.3
static int s_t46LastSiNMax = 0;
static UINT s_t46LastSiNPage = 0;
static int s_t46LastSiNPos = 0;
static int s_t46LastSiMaxScrollSi = 0;
static bool s_t46LastSiValid = false;

// T52: legacy ComputeLayoutMetrics が T45 まで完了し、contentH / scrollVpH / SI スナップショットが整合している
static bool s_paintDbgLayoutMetricsFromPaintValid = false;

// F7 ジャンプ先: 「--- T17 presentation ---」行を上余白付きで見える scrollY。
int Win32DebugOverlay_ScrollTargetT17WithTopMargin(void)
{
    if (s_paintDbgT14VmSplitActive)
    {
        return (std::max)(0, s_paintDbgT17DocYRestScroll - WIN32_MAIN_T17_JUMP_TOP_MARGIN);
    }
    return (std::max)(0, s_paintDbgT17DocY - WIN32_MAIN_T17_JUMP_TOP_MARGIN);
}

// F8 ジャンプ先: T17 ブロックがクライアント高さの中央付近に来る scrollY。
int Win32DebugOverlay_ScrollTargetT17Centered(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
    {
        return 0;
    }
    if (s_paintDbgT14VmSplitActive)
    {
        const int maxScr = (std::max)(0, s_paintDbgMaxScroll);
        const int y = s_paintDbgT17DocYRestScroll - s_paintDbgRestViewportClientH / 2;
        return (std::clamp)(y, 0, maxScr);
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int ch = static_cast<int>(rc.bottom - rc.top);
    const int maxScr = (std::max)(0, s_paintDbgContentHeight - ch);
    const int y = s_paintDbgT17DocY - ch / 2;
    return (std::clamp)(y, 0, maxScr);
}

bool Win32DebugOverlay_IsT14VmSplitActive(void)
{
    return s_paintDbgT14VmSplitActive;
}

void Win32_DebugOverlay_ResetProvisionalLayoutCache(void)
{
    s_paintDbgLayoutMetricsFromPaintValid = false;
    s_paintDbgLayoutRestVpBudgetHint = -1;
    s_paintDbgScrollBandReservePx = 0;
    s_paintDbgT14LayoutValid = false;
    s_paintDbgActualOverlayHeight = 0;
    s_t46LastSiValid = false;
}

bool Win32_DebugOverlay_IsPaintLayoutMetricsValid(void)
{
    return s_paintDbgLayoutMetricsFromPaintValid;
}

// スクロール調整時のデバッグ出力（T47: [scroll] 帯と同じ rawClientH / scrollVpH / maxScroll(contentH-scrollVpH) / SI）
void Win32DebugOverlay_ScrollLog(
    const wchar_t* where,
    HWND hwnd,
    int scrollYBefore,
    int scrollYAfter,
    int contentHOverride,
    int t17Override,
    int contentHBase,
    int extraBottomPadding)
{
    int rawClientH = s_paintDbgClientHeight;
    if (hwnd && IsWindow(hwnd))
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        rawClientH = static_cast<int>(rc.bottom - rc.top);
    }

    const bool provisionalLayoutLog =
        !s_paintDbgLayoutMetricsFromPaintValid && contentHOverride < 0 && t17Override < 0;
    if (provisionalLayoutLog)
    {
        wchar_t line[512] = {};
        swprintf_s(line, _countof(line), L"[SCROLL] where=%s\r\n", where ? where : L"?");
        OutputDebugStringW(line);
        OutputDebugStringW(
            L"[SCROLL] note: layout metrics unsettled until Win32_DebugOverlay_ComputeLayoutMetrics "
            L"(WM_PAINT); contentH/scrollVpH/maxScroll/T17/SI below omitted as stale\r\n");
        swprintf_s(line, _countof(line), L"[SCROLL] rawClientH=%d\r\n", rawClientH);
        OutputDebugStringW(line);
        swprintf_s(
            line,
            _countof(line),
            L"[SCROLL] scrollY(before)=%d scrollY(after)=%d\r\n",
            scrollYBefore,
            scrollYAfter);
        OutputDebugStringW(line);
        OutputDebugStringW(L"[SCROLL] ----\r\n");
        return;
    }

    int scrollVpH = s_paintDbgRestViewportClientH;
    if (scrollVpH < 1)
    {
        scrollVpH = (std::max)(1, rawClientH);
    }
    const int contentH = (contentHOverride >= 0) ? contentHOverride : s_paintDbgContentHeight;
    const int t17Y = (t17Override >= 0) ? t17Override : s_paintDbgT17DocY;
    const int maxScroll = (std::max)(0, contentH - scrollVpH);

    wchar_t line[512] = {};
    swprintf_s(line, _countof(line), L"[SCROLL] where=%s\r\n", where ? where : L"?");
    OutputDebugStringW(line);
    swprintf_s(line, _countof(line), L"[SCROLL] rawClientH=%d\r\n", rawClientH);
    OutputDebugStringW(line);
    swprintf_s(line, _countof(line), L"[SCROLL] scrollVpH=%d\r\n", scrollVpH);
    OutputDebugStringW(line);
    if (contentHBase >= 0)
    {
        swprintf_s(line, _countof(line), L"[SCROLL] contentH(base)=%d\r\n", contentHBase);
        OutputDebugStringW(line);
    }
    if (extraBottomPadding >= 0)
    {
        swprintf_s(line, _countof(line), L"[SCROLL] extraBottomPadding=%d\r\n", extraBottomPadding);
        OutputDebugStringW(line);
    }
    if (contentHBase >= 0 && extraBottomPadding >= 0)
    {
        swprintf_s(line, _countof(line), L"[SCROLL] contentH(with padding)=%d\r\n", contentH);
        OutputDebugStringW(line);
    }
    else
    {
        swprintf_s(line, _countof(line), L"[SCROLL] contentH=%d\r\n", contentH);
        OutputDebugStringW(line);
    }
    swprintf_s(
        line,
        _countof(line),
        L"[SCROLL] maxScroll(contentH-scrollVpH)=%d\r\n",
        maxScroll);
    OutputDebugStringW(line);
    swprintf_s(
        line,
        _countof(line),
        L"[SCROLL] scrollY(before)=%d scrollY(after)=%d\r\n",
        scrollYBefore,
        scrollYAfter);
    OutputDebugStringW(line);
    swprintf_s(line, _countof(line), L"[SCROLL] T17DocY=%d\r\n", t17Y);
    OutputDebugStringW(line);

    if (t17Y > maxScroll && maxScroll >= 0)
    {
        OutputDebugStringW(L"[SCROLL] note: T17DocY > maxScroll (cannot scroll T17 line to top)\r\n");
    }
    if (where &&
        (wcsstr(where, L"F7") != nullptr || wcsstr(where, L"F8") != nullptr) &&
        scrollYAfter >= maxScroll &&
        maxScroll > 0)
    {
        swprintf_s(
            line,
            _countof(line),
            L"[SCROLL] note: F7/F8 scrollY(after)==maxScroll=%d (target may be clamped)\r\n",
            maxScroll);
        OutputDebugStringW(line);
    }

    if (hwnd && IsWindow(hwnd) && Win32_IsMainWindowFillMonitorPresentation(hwnd))
    {
        OutputDebugStringW(L"[SCROLL] SI: (n/a fill-monitor)\r\n");
    }
    else if (s_t46LastSiValid)
    {
        swprintf_s(
            line,
            _countof(line),
            L"[SCROLL] SI: nMax=%d nPage=%u pos=%d maxScroll_si=%d\r\n",
            s_t46LastSiNMax,
            s_t46LastSiNPage,
            s_t46LastSiNPos,
            s_t46LastSiMaxScrollSi);
        OutputDebugStringW(line);
    }
    else if (hwnd && IsWindow(hwnd))
    {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        if (GetScrollInfo(hwnd, SB_VERT, &si))
        {
            const int maxSi = (std::max)(0, static_cast<int>(si.nMax) - static_cast<int>(si.nPage) + 1);
            swprintf_s(
                line,
                _countof(line),
                L"[SCROLL] SI: (fallback GetScrollInfo) nMax=%d nPage=%u pos=%d maxScroll_si=%d\r\n",
                static_cast<int>(si.nMax),
                static_cast<int>(si.nPage),
                static_cast<int>(si.nPos),
                maxSi);
            OutputDebugStringW(line);
        }
        else
        {
            OutputDebugStringW(L"[SCROLL] SI: (n/a)\r\n");
        }
    }
    OutputDebugStringW(L"[SCROLL] ----\r\n");
}

void Win32_DebugOverlay_ClampScrollYToMaxScroll(int maxScroll, const wchar_t* where)
{
    const int oldY = s_paintScrollY;
    const int maxSafe = (std::max)(0, maxScroll);
    const int newY = (std::clamp)(oldY, 0, maxSafe);
    if (newY != oldY)
    {
        wchar_t line[320] = {};
        swprintf_s(
            line,
            _countof(line),
            L"[T44] clamp scrollY where=%s old=%d max=%d new=%d\r\n",
            where ? where : L"?",
            oldY,
            maxSafe,
            newY);
        OutputDebugStringW(line);
    }
    s_paintScrollY = newY;
}

// T45: Windowed のみ。論理レイアウトの最終 contentH / スクロール用ビューポート高から SetScrollInfo（maxScroll_si == contentH - viewportH）
static void Win32_T45_ApplyWindowedScrollInfo(HWND hwnd, int scrollContentH, int scrollViewportH, int pos)
{
    if (!hwnd || Win32_IsMainWindowFillMonitorPresentation(hwnd))
    {
        s_t46LastSiValid = false;
        return;
    }
    const int nMax = (std::max)(0, scrollContentH - 1);
    const UINT nPage = static_cast<UINT>((std::max)(1, scrollViewportH));
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = nMax;
    si.nPage = nPage;
    si.nPos = pos;
    Win32_UpdateNativeScrollbarsWindowedOnly(hwnd, SB_VERT, &si, TRUE);
    const int maxScrollSi = (std::max)(0, static_cast<int>(nMax) - static_cast<int>(nPage) + 1);
    s_t46LastSiValid = true;
    s_t46LastSiNMax = nMax;
    s_t46LastSiNPage = nPage;
    s_t46LastSiNPos = pos;
    s_t46LastSiMaxScrollSi = maxScrollSi;
    wchar_t line[320] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T45] SI final nMax=%d nPage=%u pos=%d maxScroll=%d\r\n",
        nMax,
        nPage,
        pos,
        maxScrollSi);
    OutputDebugStringW(line);
}

// 垂直スクロール位置を更新し、必要なら再描画。F7/F8 や WM_VSCROLL から呼ばれる。
void Win32DebugOverlay_MainView_SetScrollPos(HWND hwnd, int newY, const wchar_t* logWhere)
{
    if (!hwnd)
    {
        return;
    }
    if (Win32_IsMainWindowFillMonitorPresentation(hwnd))
    {
        const int maxScroll = (std::max)(0, s_paintDbgMaxScroll);
        Win32_DebugOverlay_ClampScrollYToMaxScroll(maxScroll, L"MainView_SetScrollPos.fillMonitor.preflight");
        const int posBefore = s_paintScrollY;
        const int clamped = (std::clamp)(newY, 0, maxScroll);
        if (clamped == posBefore)
        {
            if (logWhere)
            {
                Win32DebugOverlay_ScrollLog(logWhere, hwnd, posBefore, clamped, -1, -1, -1, -1);
            }
            return;
        }
        s_paintScrollY = clamped;
        if (logWhere)
        {
            Win32DebugOverlay_ScrollLog(logWhere, hwnd, posBefore, clamped, -1, -1, -1, -1);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hwnd, SB_VERT, &si))
    {
        return;
    }
    const int maxScroll = (std::max)(0, static_cast<int>(si.nMax) - static_cast<int>(si.nPage) + 1);
    Win32_DebugOverlay_ClampScrollYToMaxScroll(maxScroll, L"MainView_SetScrollPos.windowed.preflight");
    if (!GetScrollInfo(hwnd, SB_VERT, &si))
    {
        return;
    }
    const int posBefore = static_cast<int>(si.nPos);
    const int clamped = (std::clamp)(newY, 0, maxScroll);
    if (clamped == posBefore)
    {
        if (logWhere)
        {
            Win32DebugOverlay_ScrollLog(logWhere, hwnd, posBefore, clamped, -1, -1, -1, -1);
        }
        return;
    }
    si.fMask = SIF_POS;
    si.nPos = clamped;
    Win32_UpdateNativeScrollbarsWindowedOnly(hwnd, SB_VERT, &si, TRUE);
    s_paintScrollY = clamped;
    if (logWhere)
    {
        Win32DebugOverlay_ScrollLog(logWhere, hwnd, posBefore, clamped, -1, -1, -1, -1);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

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
    bool compactScrollBand)
{
    const int maxScrollLogical = (std::max)(0, contentHeight - scrollVpH);
    if (compactScrollBand)
    {
        // T50/T51: rawClientH が極小、または実効 scrollVpH が小さいとき 1 行固定
        const bool scrollUltraOneLine =
            (rawClientH > 0 && rawClientH < WIN32_OVERLAY_T50_TINY_CLIENT_H) ||
            (scrollVpH > 0 && scrollVpH < WIN32_OVERLAY_T51_SCROLL_ULTRA_RESTVP_PX);
        if (scrollUltraOneLine)
        {
            if (provisionalNoSi)
            {
                swprintf_s(
                    buf,
                    bufCount,
                    L"[scroll] y=%d vp=%d max=%d\r\n",
                    scrollY,
                    scrollVpH,
                    maxScrollLogical);
            }
            else
            {
                swprintf_s(
                    buf,
                    bufCount,
                    L"[scroll] %s y=%d vp=%d max=%d\r\n",
                    modeLabel,
                    scrollY,
                    scrollVpH,
                    maxScrollLogical);
            }
            return;
        }
        if (provisionalNoSi)
        {
            swprintf_s(
                buf,
                bufCount,
                L"[scroll] %s  y=%d vp=%d max=%d  (prov)\r\n",
                modeLabel,
                scrollY,
                scrollVpH,
                maxScrollLogical);
            return;
        }
        if (s_t46LastSiValid)
        {
            swprintf_s(
                buf,
                bufCount,
                L"[scroll] %s  y=%d vp=%d max=%d  SI nMax=%d nPage=%u pos=%d max_si=%d\r\n",
                modeLabel,
                scrollY,
                scrollVpH,
                maxScrollLogical,
                s_t46LastSiNMax,
                s_t46LastSiNPage,
                s_t46LastSiNPos,
                s_t46LastSiMaxScrollSi);
        }
        else
        {
            swprintf_s(
                buf,
                bufCount,
                L"[scroll] %s  y=%d vp=%d max=%d  (no native SI)\r\n",
                modeLabel,
                scrollY,
                scrollVpH,
                maxScrollLogical);
        }
        return;
    }
    if (provisionalNoSi)
    {
        swprintf_s(
            buf,
            bufCount,
            L"[scroll] mode(actual)=%s\r\n"
            L"contentH(base)=%d  extraBottomPadding=%d  contentH(with padding)=%d  "
            L"maxScroll(contentH-scrollVpH)=%d  T17DocY=%d\r\n"
            L"scrollY=%d  rawClientH=%d  scrollVpH=%d  jumpTargetF7=%d  jumpTargetF8=%d  (F7 margin=%d)\r\n"
            L"(provisional; SI line after T45)\r\n"
            L"(PgUp/Dn=1/2 page; POPUP=pad clientH; Windowed=min pad to reach T17)",
            modeLabel,
            contentHBase,
            extraBottomPadding,
            contentHeight,
            maxScrollLogical,
            t17DocY,
            scrollY,
            rawClientH,
            scrollVpH,
            jumpF7,
            jumpF8,
            WIN32_MAIN_T17_JUMP_TOP_MARGIN);
        return;
    }
    if (s_t46LastSiValid)
    {
        swprintf_s(
            buf,
            bufCount,
            L"[scroll] mode(actual)=%s\r\n"
            L"contentH(base)=%d  extraBottomPadding=%d  contentH(with padding)=%d  "
            L"maxScroll(contentH-scrollVpH)=%d  T17DocY=%d\r\n"
            L"scrollY=%d  rawClientH=%d  scrollVpH=%d  jumpTargetF7=%d  jumpTargetF8=%d  (F7 margin=%d)\r\n"
            L"SI: nMax=%d nPage=%u pos=%d maxScroll_si=%d\r\n"
            L"(PgUp/Dn=1/2 page; POPUP=pad clientH; Windowed=min pad to reach T17)",
            modeLabel,
            contentHBase,
            extraBottomPadding,
            contentHeight,
            maxScrollLogical,
            t17DocY,
            scrollY,
            rawClientH,
            scrollVpH,
            jumpF7,
            jumpF8,
            WIN32_MAIN_T17_JUMP_TOP_MARGIN,
            s_t46LastSiNMax,
            s_t46LastSiNPage,
            s_t46LastSiNPos,
            s_t46LastSiMaxScrollSi);
    }
    else
    {
        swprintf_s(
            buf,
            bufCount,
            L"[scroll] mode(actual)=%s\r\n"
            L"contentH(base)=%d  extraBottomPadding=%d  contentH(with padding)=%d  "
            L"maxScroll(contentH-scrollVpH)=%d  T17DocY=%d\r\n"
            L"scrollY=%d  rawClientH=%d  scrollVpH=%d  jumpTargetF7=%d  jumpTargetF8=%d  (F7 margin=%d)\r\n"
            L"SI: (n/a — fill-monitor / no native scrollbar)\r\n"
            L"(PgUp/Dn=1/2 page; POPUP=pad clientH; Windowed=min pad to reach T17)",
            modeLabel,
            contentHBase,
            extraBottomPadding,
            contentHeight,
            maxScrollLogical,
            t17DocY,
            scrollY,
            rawClientH,
            scrollVpH,
            jumpF7,
            jumpF8,
            WIN32_MAIN_T17_JUMP_TOP_MARGIN);
    }
}

// CALCRECT / text measurement (static helpers; used by legacy ComputeLayoutMetrics and PaintStackedLegacy).
// メニュー列のみ CALCRECT（左列 D2D 用。幅は committed / client のレイアウト幅に合わせる）
static void Win32_MenuSampleMeasureMenuColumnOnly(
    HDC hdc,
    int layoutW,
    const wchar_t* menuBuf,
    RECT& outMenuDoc)
{
    outMenuDoc.left = 0;
    outMenuDoc.top = 0;
    outMenuDoc.right = layoutW;
    outMenuDoc.bottom = 0;
    DrawTextW(hdc, menuBuf, -1, &outMenuDoc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_CALCRECT);
}

// メニューと T14 本文のドキュメント高さを CALCRECT で求める（スクロール範囲・T17 行 Y の計測用）。
static void Win32_MenuSampleMeasurePaintLayout(
    HDC hdc,
    int clientW,
    const wchar_t* menuBuf,
    const wchar_t* t14Buf,
    RECT& outMenuDoc,
    RECT& outT14Doc,
    int menuToT14GapPx)
{
    outMenuDoc.left = 0;
    outMenuDoc.top = 0;
    outMenuDoc.right = clientW;
    outMenuDoc.bottom = 0;
    DrawTextW(hdc, menuBuf, -1, &outMenuDoc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_CALCRECT);

    outT14Doc.left = 0;
    outT14Doc.top = outMenuDoc.bottom + menuToT14GapPx;
    outT14Doc.right = clientW;
    outT14Doc.bottom = outT14Doc.top + 1000000;
    DrawTextW(
        hdc,
        t14Buf,
        -1,
        &outT14Doc,
        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
}

static int Win32_MainView_MeasureScrollOverlayTextHeight(HDC hdc, int clientW, const wchar_t* text)
{
    RECT rc = {};
    rc.left = 0;
    rc.top = 0;
    rc.right = clientW;
    rc.bottom = 1000000;
    DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    return static_cast<int>(rc.bottom - rc.top);
}

// T60: T14 ドキュメント内のブロック別に DrawText 高さを取る（vmSplit 有無に関わらず同一バッファで実測）
static int Win32_T60_MeasureDocSliceHeight(HDC hdc, int w, const wchar_t* s, int lenChars)
{
    if (!s || lenChars <= 0)
    {
        return 0;
    }
    RECT rc{};
    rc.left = 0;
    rc.top = 0;
    rc.right = w;
    rc.bottom = 1000000;
    DrawTextW(hdc, s, lenChars, &rc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    return static_cast<int>(rc.bottom - rc.top);
}

// T14 本文内で T15→T16→T18→T17 の先頭を順に探索（前方一致の誤検出を避ける）
static void Win32_T60_FindT14AppendixMarkers(
    const wchar_t* pAfterVis,
    const wchar_t*& outT15,
    const wchar_t*& outT16,
    const wchar_t*& outT18,
    const wchar_t*& outT17)
{
    outT15 = nullptr;
    outT16 = nullptr;
    outT18 = nullptr;
    outT17 = nullptr;
    if (!pAfterVis)
    {
        return;
    }
    outT15 = wcsstr(pAfterVis, L"\r\n--- T15 nearest resolution ---");
    if (!outT15)
    {
        outT15 = wcsstr(pAfterVis, L"\r\n--- T15");
    }
    if (!outT15)
    {
        outT15 = wcsstr(pAfterVis, L"\r\nT15:");
    }
    const wchar_t* scan16 = outT15 ? outT15 : pAfterVis;
    {
        const wchar_t* p16a = wcsstr(scan16, L"\r\n--- T16");
        const wchar_t* p16b = wcsstr(scan16, L"\r\nT16:");
        if (p16a && p16b)
        {
            outT16 = (p16a < p16b) ? p16a : p16b;
        }
        else
        {
            outT16 = p16a ? p16a : p16b;
        }
    }
    const wchar_t* scan18 = outT16 ? outT16 : scan16;
    {
        const wchar_t* p18a = wcsstr(scan18, L"\r\n--- T18");
        const wchar_t* p18b = wcsstr(scan18, L"\r\nT18:");
        if (p18a && p18b)
        {
            outT18 = (p18a < p18b) ? p18a : p18b;
        }
        else
        {
            outT18 = p18a ? p18a : p18b;
        }
    }
    const wchar_t* scan17 = outT18 ? outT18 : scan18;
    outT17 = wcsstr(scan17, L"\r\n--- T17");
    // T61: 非 CRLF の行末のみの環境向け（通常は swprintf が \r\n を出す）
    if (!outT16)
    {
        const wchar_t* scan16b = outT15 ? outT15 : pAfterVis;
        outT16 = wcsstr(scan16b, L"\nT16:");
        if (!outT16)
        {
            outT16 = wcsstr(scan16b, L"\n--- T16");
        }
    }
    if (!outT18)
    {
        const wchar_t* scan18b = outT16 ? outT16 : (outT15 ? outT15 : pAfterVis);
        outT18 = wcsstr(scan18b, L"\nT18:");
        if (!outT18)
        {
            outT18 = wcsstr(scan18b, L"\n--- T18");
        }
    }
}

#pragma endregion

// -----------------------------------------------------------------------------
// Legacy stacked HUD — cohesive pipeline for macro WIN32_HUD_USE_PAGED_HUD=0 (see docs §2.2, §7.1, §7.6).
// Implementations: Win32_DebugOverlay_ComputeLayoutMetrics, Win32_DebugOverlay_PaintStackedLegacy; public-side adapters: RunComputeLayoutMetrics, RunGdiPaint.
// Depends on: shared CALCRECT/T60 helpers above; MainApp.cpp extern s_paint*; file-top static scratch; Win32_FillMenuSamplePaintBuffers via ComputeLayoutMetrics.
// PrefillHudLeftColumnForD2d (D2D region below) calls ComputeLayoutMetrics when legacy — same extraction bundle in practice.
// Implementations below are in the same anonymous namespace as scratch + forward decls (file top).
// -----------------------------------------------------------------------------
#pragma region Legacy stacked HUD (ComputeLayoutMetrics + PaintStackedLegacy)

namespace {
// D2D prefill: consolidates all `outHud->dbgHud*` writes from layout metrics (§7.7 category C); ComputeLayoutMetrics no longer touches `outHud` directly.
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
    int dbgHudRestViewportTopPx)
{
    if (!outHud)
    {
        return;
    }
    outHud->dbgHudDrawScrollBand = dbgHudDrawScrollBand;
    outHud->dbgHudLeftColumnText[0] = L'\0';
    wcscpy_s(outHud->dbgHudLeftColumnText, _countof(outHud->dbgHudLeftColumnText), menuBuf);
    wcscat_s(outHud->dbgHudLeftColumnText, _countof(outHud->dbgHudLeftColumnText), L"\r\n");
    wcscat_s(outHud->dbgHudLeftColumnText, _countof(outHud->dbgHudLeftColumnText), t14Buf);
    outHud->dbgHudLeftColumnTopPx = t37TopGap;
    outHud->dbgHudLeftColumnScrollYPx = dbgHudLeftColumnScrollYPx;
    outHud->dbgHudLeftColumnClipBottomPadPx = scrollBandReservePx;
    outHud->dbgHudLeftColumnPrefillClientW = static_cast<std::uint32_t>(clientW);
    outHud->dbgHudLeftColumnPrefillClientH = static_cast<std::uint32_t>(clientH);
    wcscpy_s(outHud->dbgHudScrollBandText, _countof(outHud->dbgHudScrollBandText), overlay);
    outHud->dbgHudScrollBandHeightPx = scrollBandReservePx;
    wcscpy_s(outHud->dbgHudMenuBandText, _countof(outHud->dbgHudMenuBandText), menuBuf);
    outHud->dbgHudFinalRow1HeightPx = row1H;
    outHud->dbgHudFinalBodyTopPx = bodyTopPx;
    outHud->dbgHudBodyT14DocTopPx = t14DocTopAbsPx;
    outHud->dbgHudRow2TopPx = row2TopPx;
    outHud->dbgHudMenuColumnHeightPx = static_cast<int>(rcMenuDoc.bottom);

    outHud->dbgHudT14VmSplit = vmSplitActive;
    if (vmSplitActive && pVis != nullptr && pT15 != nullptr)
    {
        wcscpy_s(outHud->dbgHudT14PrefixText, _countof(outHud->dbgHudT14PrefixText), s_paintDbgT14VmSplitPrefix);
        const wchar_t* fvl = pVis + wcslen(L"visible modes:\r\n");
        const size_t headingLen = static_cast<size_t>(fvl - pVis);
        wcsncpy_s(
            outHud->dbgHudVmHeadingText,
            _countof(outHud->dbgHudVmHeadingText),
            pVis,
            headingLen);
        outHud->dbgHudVmHeadingText[_countof(outHud->dbgHudVmHeadingText) - 1] = L'\0';
        const size_t listChars = static_cast<size_t>(pT15 - fvl);
        wcsncpy_s(
            outHud->dbgHudVmListBandText,
            _countof(outHud->dbgHudVmListBandText),
            fvl,
            listChars);
        outHud->dbgHudVmListBandText[_countof(outHud->dbgHudVmListBandText) - 1] = L'\0';
        wcscpy_s(outHud->dbgHudT14RestText, _countof(outHud->dbgHudT14RestText), s_paintDbgT14VmSplitRest);
        outHud->dbgHudT14PrefixHeightPx = splitHPrefix;
        outHud->dbgHudVmBandHeightPx = splitHVmBand;
        outHud->dbgHudRestViewportTopPx = dbgHudRestViewportTopPx;
        wcscpy_s(
            outHud->dbgHudBodyBandText,
            _countof(outHud->dbgHudBodyBandText),
            s_paintDbgT14VmSplitRest);
    }
    else
    {
        outHud->dbgHudT14PrefixText[0] = L'\0';
        outHud->dbgHudVmHeadingText[0] = L'\0';
        outHud->dbgHudVmListBandText[0] = L'\0';
        outHud->dbgHudT14RestText[0] = L'\0';
        outHud->dbgHudT14PrefixHeightPx = 0;
        outHud->dbgHudVmBandHeightPx = 0;
        outHud->dbgHudRestViewportTopPx = 0;
        wcscpy_s(outHud->dbgHudBodyBandText, _countof(outHud->dbgHudBodyBandText), t14Buf);
    }
}

// Legacy: Win32_DebugOverlay_PrefillHudLeftColumnForD2d（!Win32_HudPaged_IsEnabled() 時）および
// Win32_DebugOverlay_PaintStackedLegacy からのみ呼ばれる。ページ式 HUD 既定時は呼ばれない（Win32_HudPaged_PrefillD2d）。
// スクロール・[scroll] 帯の高さ・T17 行位置などを計測。outHud 非 null のときは D2D final HUD 用に左列全文（menu+t14）とスクロール値を書き込む。
//
// Side-effect map (categories; not every assignment): ① file-top scratch (this TU), ② MainApp extern s_paintDbg* / line height,
// ③ outHud dbgHud* when non-null (via Win32_LegacyStacked_ApplyD2dHudPrefill), ④ Win32_T45_ApplyWindowedScrollInfo → T46 snapshot + T52 validity.  HUD_LEGACY_CODE_DEPENDENCY.md §7.7
void Win32_DebugOverlay_ComputeLayoutMetrics(const Win32_LegacyStacked_LayoutMetricsParams& p)
{
    HWND hwnd = p.common.hwnd;
    HDC hdc = p.common.hdc;
    const wchar_t* t17ModeLabelForOverlay = p.common.t17ModeLabelForOverlay;
    const wchar_t* t17CandLabel = p.common.t17CandLabel;
    const wchar_t* t17ActLabel = p.common.t17ActLabel;
    WindowsRendererState* outHud = p.outHud;
    bool logScroll = p.logScroll;
    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);
    const int clientW = static_cast<int>(rcClient.right - rcClient.left);
    const int clientH = static_cast<int>(rcClient.bottom - rcClient.top);

    int restVpBudgetHint = -1;
    bool layoutRefilledForBudget = false;
    // T54: T51 refill で短縮したバッファを、final で収まったあと 1 回だけ clientH 相当へ戻す
    bool t54ReExpanded = false;

    wchar_t menuBuf[3072] = {};
    wchar_t t14Buf[8192] = {};

refill_budget:
    s_paintDbgScrollBandReservePx = 0;
    s_paintDbgRestViewportTopPx = 0;

    Win32_FillMenuSamplePaintBuffers(
        hwnd,
        rcClient,
        menuBuf,
        _countof(menuBuf),
        t14Buf,
        _countof(t14Buf),
        Win32_IsT37VirtualBodyOverlayActiveForLayout(),
        restVpBudgetHint);

    RECT rcMenuDoc{};
    RECT rcT14Doc{};
    const bool t64FullscreenContent =
        Win32_IsMainWindowFillMonitorPresentation(hwnd) &&
        Win32_MainWindow_IsFullscreenPresentationMode(hwnd);
    const int menuT14GapPx =
        t64FullscreenContent ? WIN32_OVERLAY_T64_FULLSCREEN_MENU_TO_T14_GAP_PX
                             : WIN32_HUD_DBG_MENU_TO_T14_GAP_PX;
    Win32_MenuSampleMeasurePaintLayout(hdc, clientW, menuBuf, t14Buf, rcMenuDoc, rcT14Doc, menuT14GapPx);
    const int t37TopGap =
        Win32_IsT37VirtualBodyOverlayActiveForLayout() ? WIN32_OVERLAY_T37_MENU_TOP_GAP_PX : 0;

    wchar_t row1Buf[128];
    swprintf_s(
        row1Buf,
        L"cand=%s act=%s",
        t17CandLabel != nullptr ? t17CandLabel : L"?",
        t17ActLabel != nullptr ? t17ActLabel : L"?");
    RECT rcRow1{};
    rcRow1.left = 0;
    rcRow1.top = 0;
    rcRow1.right = clientW;
    rcRow1.bottom = 0;
    DrawTextW(hdc, row1Buf, -1, &rcRow1, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    const int row1H = static_cast<int>(rcRow1.bottom - rcRow1.top);
    // T48: 小画面では row1↔row2 / row2↔本文の固定余白を圧縮し、本文ビューポートを確保する
    int row1GapPx =
        (clientH < 360) ? (WIN32_HUD_DBG_FINAL_ROW1_BOTTOM_GAP_PX / 2) : WIN32_HUD_DBG_FINAL_ROW1_BOTTOM_GAP_PX;
    int row2ToBodyExtraGapPx =
        (clientH < 360) ? (WIN32_HUD_DBG_FINAL_ROW2_TO_BODY_EXTRA_GAP_PX / 2)
                        : WIN32_HUD_DBG_FINAL_ROW2_TO_BODY_EXTRA_GAP_PX;
    // T58/T59: 760px 以下の Windowed で本文上端までの縦をさらに削る（fill-monitor は触らない）
    if (!Win32_IsMainWindowFillMonitorPresentation(hwnd) && clientH <= WIN32_OVERLAY_T59_WINDOWED_COMPACT_CLIENT_H)
    {
        row1GapPx = (std::min)(row1GapPx, 4);
        row2ToBodyExtraGapPx = (std::min)(row2ToBodyExtraGapPx, 12);
    }
    // T64: fill-monitor Fullscreen のみ（Borderless は除外）
    if (t64FullscreenContent)
    {
        row1GapPx = (std::min)(row1GapPx, WIN32_OVERLAY_T64_FULLSCREEN_ROW1_GAP_PX);
        row2ToBodyExtraGapPx =
            (std::min)(row2ToBodyExtraGapPx, WIN32_OVERLAY_T64_FULLSCREEN_ROW2_TO_BODY_GAP_PX);
    }
    const int row2TopPx = row1H + row1GapPx + t37TopGap;
    const int t14DocTopAbsPx = static_cast<int>(rcT14Doc.top) + t37TopGap;
    const int bodyTopPx = row2TopPx + static_cast<int>(rcT14Doc.top) + row2ToBodyExtraGapPx;
    s_paintDbgFinalBodyTopPx = bodyTopPx;
    s_paintDbgBodyT14DocTopPx = t14DocTopAbsPx;
    s_paintDbgFinalRow1HeightPx = row1H;
    s_paintDbgRow2TopPx = row2TopPx;

    const int baseContentH = static_cast<int>(rcT14Doc.bottom) + t37TopGap;

    s_paintDbgT14VmSplitActive = false;
    s_paintDbgT17DocYRestScroll = 0;
    s_paintDbgRestViewportClientH = clientH;

    const wchar_t* pVis = wcsstr(t14Buf, L"visible modes:\r\n");
    const wchar_t* pT15 = wcsstr(t14Buf, L"\r\n--- T15 nearest resolution ---");
    const bool vmSplit = (pVis != nullptr && pT15 != nullptr && pT15 > pVis);

    s_paintDbgT14LayoutValid = false;
    const int t14BaseY = static_cast<int>(rcT14Doc.top) + t37TopGap;
    s_paintDbgT14ColumnBaseY = t14BaseY;

    bool vmSplitActive = false;
    int splitHPrefix = 0;
    int splitHVmBand = 0;
    int splitHRest = 0;
    int splitRestTopPx = 0;
    int splitRestVp = 0;

    int t17DocY = 0;
    int extraBottomPadding = 0;
    int contentHeight = 0;
    int maxScroll = 0;

    if (vmSplit)
    {
        vmSplitActive = true;
        s_paintDbgT14VmSplitActive = true;

        const wchar_t* firstVmLine = pVis + wcslen(L"visible modes:\r\n");
        const size_t prefixChars = static_cast<size_t>(pVis - t14Buf);
        const size_t vmBandChars = static_cast<size_t>(pT15 - pVis);
        const size_t restChars = wcslen(pT15);

        if (prefixChars < _countof(s_paintDbgT14VmSplitPrefix) &&
            vmBandChars < _countof(s_paintDbgT14VmSplitVmBand) &&
            restChars < _countof(s_paintDbgT14VmSplitRest))
        {
            wmemcpy_s(s_paintDbgT14VmSplitPrefix, _countof(s_paintDbgT14VmSplitPrefix), t14Buf, prefixChars);
            s_paintDbgT14VmSplitPrefix[prefixChars] = L'\0';
            wmemcpy_s(s_paintDbgT14VmSplitVmBand, _countof(s_paintDbgT14VmSplitVmBand), pVis, vmBandChars);
            s_paintDbgT14VmSplitVmBand[vmBandChars] = L'\0';
            wcscpy_s(s_paintDbgT14VmSplitRest, _countof(s_paintDbgT14VmSplitRest), pT15);

            RECT rcPref{};
            rcPref.left = 0;
            rcPref.top = t14BaseY;
            rcPref.right = clientW;
            rcPref.bottom = t14BaseY + 1000000;
            DrawTextW(
                hdc,
                s_paintDbgT14VmSplitPrefix,
                static_cast<int>(prefixChars),
                &rcPref,
                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
            splitHPrefix = static_cast<int>(rcPref.bottom) - t14BaseY;

            RECT rcVm{};
            rcVm.left = 0;
            rcVm.top = t14BaseY + splitHPrefix;
            rcVm.right = clientW;
            rcVm.bottom = rcVm.top + 1000000;
            DrawTextW(
                hdc,
                s_paintDbgT14VmSplitVmBand,
                static_cast<int>(vmBandChars),
                &rcVm,
                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
            splitHVmBand = static_cast<int>(rcVm.bottom) - (t14BaseY + splitHPrefix);

            RECT rcRest{};
            rcRest.left = 0;
            rcRest.top = t14BaseY + splitHPrefix + splitHVmBand;
            rcRest.right = clientW;
            rcRest.bottom = rcRest.top + 1000000;
            DrawTextW(
                hdc,
                s_paintDbgT14VmSplitRest,
                -1,
                &rcRest,
                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
            splitHRest = static_cast<int>(rcRest.bottom) - (t14BaseY + splitHPrefix + splitHVmBand);

            wchar_t prefixPlusHeading[8192] = {};
            const size_t headSeg = static_cast<size_t>(firstVmLine - pVis);
            if (prefixChars + headSeg < _countof(prefixPlusHeading))
            {
                wmemcpy_s(prefixPlusHeading, _countof(prefixPlusHeading), t14Buf, prefixChars + headSeg);
                prefixPlusHeading[prefixChars + headSeg] = L'\0';
                RECT rcPh{};
                rcPh.left = 0;
                rcPh.top = t14BaseY;
                rcPh.right = clientW;
                rcPh.bottom = t14BaseY + 1000000;
                DrawTextW(
                    hdc,
                    prefixPlusHeading,
                    -1,
                    &rcPh,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
                s_paintDbgT14VisibleModesDocStartY = static_cast<int>(rcPh.bottom);
                s_paintDbgT14LayoutValid = true;
            }

            const wchar_t* t17InRest = wcsstr(s_paintDbgT14VmSplitRest, L"--- T17 presentation ---");
            if (t17InRest != nullptr)
            {
                const size_t prefixToT17 = static_cast<size_t>(t17InRest - s_paintDbgT14VmSplitRest);
                wchar_t preT17[8192] = {};
                if (prefixToT17 < _countof(preT17))
                {
                    wmemcpy_s(preT17, _countof(preT17), s_paintDbgT14VmSplitRest, prefixToT17);
                    preT17[prefixToT17] = L'\0';
                    RECT rcT17{};
                    rcT17.left = 0;
                    rcT17.top = 0;
                    rcT17.right = clientW;
                    rcT17.bottom = 1000000;
                    DrawTextW(
                        hdc,
                        preT17,
                        -1,
                        &rcT17,
                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
                    s_paintDbgT17DocYRestScroll = static_cast<int>(rcT17.bottom);
                }
            }

            splitRestTopPx = row2TopPx + t14BaseY + splitHPrefix + splitHVmBand;
            splitRestVp = (std::max)(1, clientH - splitRestTopPx);
            const int maxScrollBeforePaddingRest = (std::max)(0, splitHRest - splitRestVp);
            // T56: fill-monitor でも extraBottomPadding は T17 到達に必要な分のみ（rawClientH 一律付与はしない）
            extraBottomPadding =
                (std::max)(0, s_paintDbgT17DocYRestScroll - maxScrollBeforePaddingRest);
            contentHeight = splitHRest + extraBottomPadding;
            maxScroll = (std::max)(0, contentHeight - splitRestVp);
            t17DocY = s_paintDbgT17DocYRestScroll;
            s_paintDbgT17DocY = t17DocY;
        }
        else
        {
            vmSplitActive = false;
            s_paintDbgT14VmSplitActive = false;
        }
    }

    if (!vmSplitActive)
    {
        const wchar_t* visMarkerT14 = pVis;
        if (visMarkerT14 != nullptr)
        {
            const wchar_t* firstVmLineNs = visMarkerT14 + wcslen(L"visible modes:\r\n");
            const int prefixLenVm = static_cast<int>(firstVmLineNs - t14Buf);
            if (prefixLenVm > 0)
            {
                RECT rcVm{};
                rcVm.left = 0;
                rcVm.top = t14BaseY;
                rcVm.right = clientW;
                rcVm.bottom = t14BaseY + 1000000;
                DrawTextW(
                    hdc,
                    t14Buf,
                    prefixLenVm,
                    &rcVm,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
                s_paintDbgT14VisibleModesDocStartY = static_cast<int>(rcVm.bottom);
                s_paintDbgT14LayoutValid = true;
            }
        }

        const wchar_t* t17Mark = wcsstr(t14Buf, L"--- T17 presentation ---");
        if (t17Mark != nullptr)
        {
            const size_t prefixCharsFull = static_cast<size_t>(t17Mark - t14Buf);
            wchar_t prefixBuf[8192] = {};
            if (prefixCharsFull < _countof(prefixBuf))
            {
                wmemcpy_s(prefixBuf, _countof(prefixBuf), t14Buf, prefixCharsFull);
                prefixBuf[prefixCharsFull] = L'\0';
                RECT rcPre{};
                rcPre.left = 0;
                rcPre.top = t14BaseY;
                rcPre.right = clientW;
                rcPre.bottom = rcPre.top + 1000000;
                DrawTextW(
                    hdc,
                    prefixBuf,
                    -1,
                    &rcPre,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
                t17DocY = static_cast<int>(rcPre.bottom);
            }
        }
        s_paintDbgT17DocY = t17DocY;

        const int maxScrollBeforePadding = (std::max)(0, baseContentH - clientH);
        // T56: fill-monitor でも Windowed と同様に T17 到達分のみ（仮想解像度は contentBudget 側で扱う）
        extraBottomPadding = (std::max)(0, t17DocY - maxScrollBeforePadding);
        contentHeight = baseContentH + extraBottomPadding;
        maxScroll = (std::max)(0, contentHeight - clientH);
    }

    TEXTMETRICW tm{};
    if (GetTextMetricsW(hdc, &tm))
    {
        s_paintScrollLinePx = (std::max)(static_cast<int>(tm.tmHeight), 16);
    }
    s_paintDbgLineHeight = s_paintScrollLinePx;

    s_paintDbgContentHeight = contentHeight;
    s_paintDbgContentHeightBase = vmSplitActive ? splitHRest : baseContentH;
    s_paintDbgExtraBottomPadding = extraBottomPadding;
    s_paintDbgClientHeight = clientH;
    s_paintDbgClientW = clientW;
    s_paintDbgClientH = clientH;

    s_paintDbgMaxScroll = maxScroll;
    const int scrollYBeforePaint = s_paintScrollY;
    Win32_DebugOverlay_ClampScrollYToMaxScroll(maxScroll, L"ComputeLayoutMetrics.phase1");

    int jumpF7 = Win32DebugOverlay_ScrollTargetT17WithTopMargin();
    int jumpF8 = Win32DebugOverlay_ScrollTargetT17Centered(hwnd);
    wchar_t overlay[2048] = {};
    int actualOverlayHeight = 0;
    const bool compactScrollBand =
        (clientH < WIN32_OVERLAY_T49_SCROLL_COMPACT_CLIENT_H) ||
        (vmSplitActive && splitRestVp > 0 && splitRestVp < WIN32_OVERLAY_T51_COMPACT_SCROLL_RESTVP_PX) ||
        Win32_IsMainWindowFillMonitorPresentation(hwnd);

    if (vmSplitActive)
    {
        Win32_DebugOverlay_FormatScrollDebugOverlay(
            overlay,
            _countof(overlay),
            t17ModeLabelForOverlay,
            s_paintDbgContentHeightBase,
            s_paintDbgExtraBottomPadding,
            s_paintDbgContentHeight,
            s_paintDbgT17DocY,
            s_paintScrollY,
            clientH,
            splitRestVp,
            jumpF7,
            jumpF8,
            true,
            compactScrollBand);
        actualOverlayHeight =
            Win32_MainView_MeasureScrollOverlayTextHeight(hdc, clientW, overlay);
    }

    if (vmSplitActive)
    {
        int kMinBody = WIN32_OVERLAY_MIN_BODY_VIEWPORT_PX;
        if (Win32_IsMainWindowFillMonitorPresentation(hwnd) &&
            Win32_MainWindow_IsFullscreenPresentationMode(hwnd))
        {
            kMinBody = WIN32_OVERLAY_T60_FULLSCREEN_MIN_BODY_VIEWPORT_PX;
        }
        int splitRestTopPxEff = splitRestTopPx;
        int overlayReserve = (std::min)(actualOverlayHeight, clientH);
        {
            const int maxOverlayForBody = (std::max)(0, clientH - splitRestTopPxEff - kMinBody);
            overlayReserve = (std::min)(overlayReserve, maxOverlayForBody);
            const int maxTopForRest = (std::max)(0, clientH - kMinBody - overlayReserve);
            if (splitRestTopPxEff > maxTopForRest)
            {
                splitRestTopPxEff = maxTopForRest;
            }
        }
        int restVp2 = clientH - overlayReserve - splitRestTopPxEff;
        restVp2 = (std::max)(1, restVp2);

        s_paintDbgRestViewportTopPx = splitRestTopPxEff;
        s_paintDbgScrollBandReservePx = overlayReserve;

        {
            const int maxScrollBeforePaddingRest2 = (std::max)(0, splitHRest - restVp2);
            int extra2 = 0;
            extra2 = (std::max)(
                0, s_paintDbgT17DocYRestScroll - maxScrollBeforePaddingRest2);
            const int contentH2 = splitHRest + extra2;
            const int maxScroll2 = (std::max)(0, contentH2 - restVp2);
            s_paintDbgMaxScroll = maxScroll2;
            Win32_DebugOverlay_ClampScrollYToMaxScroll(maxScroll2, L"ComputeLayoutMetrics.vmSplitRefine");
            s_paintDbgContentHeight = contentH2;
            s_paintDbgExtraBottomPadding = extra2;
            maxScroll = maxScroll2;
            contentHeight = contentH2;
            extraBottomPadding = extra2;
        }
        s_paintDbgRestViewportClientH = restVp2;

        if (!layoutRefilledForBudget && restVp2 < WIN32_OVERLAY_T51_REFILL_RESTVP_PX)
        {
            // T55: fill-monitor では原則 refill しない。T57: Fullscreen のみ restVp が極小のとき短縮バッファで prefix/vm 帯を抑え scrollVpH を回復
            const bool fill = Win32_IsMainWindowFillMonitorPresentation(hwnd);
            const bool allowFillRefill =
                !fill ||
                (Win32_MainWindow_IsFullscreenPresentationMode(hwnd) &&
                 restVp2 < WIN32_OVERLAY_T57_MIN_RESTVP_FULLSCREEN_PX);
            if (allowFillRefill)
            {
                restVpBudgetHint = restVp2;
                layoutRefilledForBudget = true;
                goto refill_budget;
            }
        }
    }

    const int scrollContentHFinal = s_paintDbgContentHeight;
    const int scrollViewportHFinal = vmSplitActive ? s_paintDbgRestViewportClientH : clientH;
    const int maxScrollUnified = (std::max)(0, scrollContentHFinal - scrollViewportHFinal);
    {
        s_paintDbgMaxScroll = maxScrollUnified;
        Win32_DebugOverlay_ClampScrollYToMaxScroll(maxScrollUnified, L"ComputeLayoutMetrics.unified");
        Win32_T45_ApplyWindowedScrollInfo(hwnd, scrollContentHFinal, scrollViewportHFinal, s_paintScrollY);
    }

    // T54: provisional restVpBudgetHint で短縮した本文が、確定レイアウトでは不要になったら通常 budget で再生成（最大 1 回）
    if (!t54ReExpanded && !Win32_IsMainWindowFillMonitorPresentation(hwnd) && restVpBudgetHint >= 0)
    {
        const bool finalLayoutFits =
            !vmSplitActive || maxScrollUnified == 0 || scrollContentHFinal <= scrollViewportHFinal;
        if (finalLayoutFits)
        {
            restVpBudgetHint = -1;
            layoutRefilledForBudget = false;
            t54ReExpanded = true;
            goto refill_budget;
        }
    }

    s_paintDbgLayoutMetricsFromPaintValid = true;

    if (logScroll)
    {
        Win32DebugOverlay_ScrollLog(
            L"WM_PAINT after SetScrollInfo",
            hwnd,
            scrollYBeforePaint,
            s_paintScrollY,
            s_paintDbgContentHeight,
            s_paintDbgT17DocY,
            vmSplitActive ? splitHRest : baseContentH,
            s_paintDbgExtraBottomPadding);
    }

    jumpF7 = Win32DebugOverlay_ScrollTargetT17WithTopMargin();
    jumpF8 = Win32DebugOverlay_ScrollTargetT17Centered(hwnd);
    Win32_DebugOverlay_FormatScrollDebugOverlay(
        overlay,
        _countof(overlay),
        t17ModeLabelForOverlay,
        s_paintDbgContentHeightBase,
        s_paintDbgExtraBottomPadding,
        s_paintDbgContentHeight,
        s_paintDbgT17DocY,
        s_paintScrollY,
        s_paintDbgClientHeight,
        s_paintDbgRestViewportClientH,
        jumpF7,
        jumpF8,
        false,
        compactScrollBand);
    actualOverlayHeight =
        Win32_MainView_MeasureScrollOverlayTextHeight(hdc, clientW, overlay);

    s_paintDbgActualOverlayHeight = actualOverlayHeight;

    if (!vmSplitActive)
    {
        s_paintDbgScrollBandReservePx = (std::min)(actualOverlayHeight, clientH);
        s_paintDbgRestViewportTopPx = 0;
    }

    {
        const int budgetT53 = (restVpBudgetHint >= 0) ? restVpBudgetHint : clientH;
        const bool windowedHud = !Win32_IsMainWindowFillMonitorPresentation(hwnd);
        s_paintDbgT53ScrollBandDrawEnabled =
            !(windowedHud && budgetT53 >= 0 &&
              budgetT53 < WIN32_OVERLAY_T53_OMIT_SCROLL_BAND_BUDGET_PX);
    }

    if (outHud)
    {
        Win32_LegacyStacked_ApplyD2dHudPrefill(
            outHud,
            menuBuf,
            t14Buf,
            overlay,
            clientW,
            clientH,
            t37TopGap,
            row1H,
            bodyTopPx,
            t14DocTopAbsPx,
            row2TopPx,
            rcMenuDoc,
            vmSplitActive,
            pVis,
            pT15,
            splitHPrefix,
            splitHVmBand,
            s_paintDbgT53ScrollBandDrawEnabled,
            s_paintScrollY,
            s_paintDbgScrollBandReservePx,
            s_paintDbgRestViewportTopPx);
    }

    {
        const int row1H_log = s_paintDbgFinalRow1HeightPx;
        const int row2H_log = s_paintDbgRow2TopPx - row1H_log;
        const int prefixTop_log = s_paintDbgRow2TopPx + t14BaseY;
        const int vmTop_log =
            s_paintDbgT14VmSplitActive ? (prefixTop_log + splitHPrefix) : 0;
        int hT14Header = 0;
        int hVmHeading = 0;
        int hVmLines = 0;
        int hT15 = 0;
        int hT16 = 0;
        int hT18 = 0;
        int hT17 = 0;
        int totalBeforeVisible = 0;
        const int visStartDoc = s_paintDbgT14VisibleModesDocStartY - t14BaseY;
        const int gapsPx = row1GapPx + row2ToBodyExtraGapPx;

        const wchar_t* pVis = wcsstr(t14Buf, L"visible modes:\r\n");
        const wchar_t* p0 = t14Buf;
        const wchar_t* const pEndT14 = t14Buf + wcslen(t14Buf);
        if (pVis != nullptr)
        {
            const int nVisHead = static_cast<int>(wcslen(L"visible modes:\r\n"));
            const wchar_t* pAfterVis = pVis + nVisHead;
            hT14Header = Win32_T60_MeasureDocSliceHeight(
                hdc, clientW, p0, static_cast<int>(pVis - p0));
            hVmHeading = Win32_T60_MeasureDocSliceHeight(hdc, clientW, pVis, nVisHead);
            totalBeforeVisible = Win32_T60_MeasureDocSliceHeight(
                hdc, clientW, p0, static_cast<int>(pAfterVis - p0));

            const wchar_t* mT15 = nullptr;
            const wchar_t* mT16 = nullptr;
            const wchar_t* mT18 = nullptr;
            const wchar_t* mT17 = nullptr;
            Win32_T60_FindT14AppendixMarkers(pAfterVis, mT15, mT16, mT18, mT17);

            {
                const int offEnd = static_cast<int>(pEndT14 - t14Buf);
                const int t15BegOff = mT15 ? static_cast<int>(mT15 - t14Buf) : -1;
                int t15EndOff = -1;
                if (mT15 != nullptr)
                {
                    t15EndOff = offEnd;
                    if (mT16 != nullptr && mT16 > mT15)
                    {
                        t15EndOff = static_cast<int>(mT16 - t14Buf);
                    }
                    else if (mT18 != nullptr && mT18 > mT15)
                    {
                        t15EndOff = static_cast<int>(mT18 - t14Buf);
                    }
                    else if (mT17 != nullptr && mT17 > mT15)
                    {
                        t15EndOff = static_cast<int>(mT17 - t14Buf);
                    }
                }
                const int t16BegOff = mT16 ? static_cast<int>(mT16 - t14Buf) : -1;
                int t16EndOff = -1;
                if (mT16 != nullptr)
                {
                    t16EndOff = offEnd;
                    if (mT18 != nullptr && mT18 > mT16)
                    {
                        t16EndOff = static_cast<int>(mT18 - t14Buf);
                    }
                    else if (mT17 != nullptr && mT17 > mT16)
                    {
                        t16EndOff = static_cast<int>(mT17 - t14Buf);
                    }
                }
                const int t18BegOff = mT18 ? static_cast<int>(mT18 - t14Buf) : -1;
                int t18EndOff = -1;
                if (mT18 != nullptr)
                {
                    t18EndOff = offEnd;
                    if (mT17 != nullptr && mT17 > mT18)
                    {
                        t18EndOff = static_cast<int>(mT17 - t14Buf);
                    }
                }
                const int t17BegOff = mT17 ? static_cast<int>(mT17 - t14Buf) : -1;
                const int t17EndOff = mT17 ? offEnd : -1;

                wchar_t t60mk[768];
                swprintf_s(
                    t60mk,
                    _countof(t60mk),
                    L"[T60MARK] vmSplit=%d bufLen=%d visOff=%d "
                    L"t15[%d,%d) t16[%d,%d) t18[%d,%d) t17[%d,%d) restVpBudgetHint=%d\r\n",
                    vmSplitActive ? 1 : 0,
                    static_cast<int>(wcslen(t14Buf)),
                    pVis ? static_cast<int>(pVis - t14Buf) : -1,
                    t15BegOff,
                    t15EndOff,
                    t16BegOff,
                    t16EndOff,
                    t18BegOff,
                    t18EndOff,
                    t17BegOff,
                    t17EndOff,
                    restVpBudgetHint);
                OutputDebugStringW(t60mk);
            }

            const wchar_t* pVmEnd = mT15 ? mT15 : (mT16 ? mT16 : (mT18 ? mT18 : mT17));
            if (pVmEnd != nullptr && pVmEnd > pAfterVis)
            {
                hVmLines = Win32_T60_MeasureDocSliceHeight(
                    hdc, clientW, pAfterVis, static_cast<int>(pVmEnd - pAfterVis));
            }
            else if (pVmEnd == nullptr)
            {
                hVmLines = Win32_T60_MeasureDocSliceHeight(
                    hdc,
                    clientW,
                    pAfterVis,
                    static_cast<int>(wcslen(pAfterVis)));
            }

            if (mT15 != nullptr)
            {
                const wchar_t* endT15 = nullptr;
                if (mT16 != nullptr && mT16 > mT15)
                {
                    endT15 = mT16;
                }
                else if (mT18 != nullptr && mT18 > mT15)
                {
                    endT15 = mT18;
                }
                else if (mT17 != nullptr && mT17 > mT15)
                {
                    endT15 = mT17;
                }
                else
                {
                    // T61: 次マーカーが無い／バッファ末尾までが T15 ブロック（ultra 1 行 T15 のみ等）
                    endT15 = pEndT14;
                }
                if (endT15 != nullptr && endT15 > mT15)
                {
                    hT15 = Win32_T60_MeasureDocSliceHeight(
                        hdc, clientW, mT15, static_cast<int>(endT15 - mT15));
                }
            }

            if (mT16 != nullptr)
            {
                const wchar_t* endT16 = nullptr;
                if (mT18 != nullptr && mT18 > mT16)
                {
                    endT16 = mT18;
                }
                else if (mT17 != nullptr && mT17 > mT16)
                {
                    endT16 = mT17;
                }
                else
                {
                    endT16 = pEndT14;
                }
                if (endT16 != nullptr && endT16 > mT16)
                {
                    hT16 = Win32_T60_MeasureDocSliceHeight(
                        hdc, clientW, mT16, static_cast<int>(endT16 - mT16));
                }
            }

            if (mT18 != nullptr)
            {
                if (mT17 != nullptr && mT17 > mT18)
                {
                    hT18 = Win32_T60_MeasureDocSliceHeight(
                        hdc, clientW, mT18, static_cast<int>(mT17 - mT18));
                }
                else
                {
                    hT18 = Win32_T60_MeasureDocSliceHeight(
                        hdc, clientW, mT18, static_cast<int>(wcslen(mT18)));
                }
            }

            if (mT17 != nullptr)
            {
                hT17 = Win32_T60_MeasureDocSliceHeight(
                    hdc, clientW, mT17, static_cast<int>(wcslen(mT17)));
            }
        }
        else
        {
            hT14Header = Win32_T60_MeasureDocSliceHeight(
                hdc, clientW, p0, static_cast<int>(wcslen(p0)));
        }

        s_paintDbgT14BeforeVisibleDocH = totalBeforeVisible;
        s_paintDbgT14VisibleBlockDocH = hVmLines;
        s_paintDbgT14AfterVisibleDocH = (std::max)(
            0, s_paintDbgContentHeightBase - s_paintDbgT14BeforeVisibleDocH - s_paintDbgT14VisibleBlockDocH);

        wchar_t t60[1024] = {};
        swprintf_s(
            t60,
            _countof(t60),
            L"[T60BUDGET] vmSplit=%d row1H=%d row2Band=%d row1Gap=%d row2ToBodyGap=%d gaps=%d "
            L"t14Header=%d vmHeading=%d vmLines=%d t15=%d t16=%d t18=%d t17=%d "
            L"totalBeforeVisible=%d beforeVisibleDocH=%d visibleBlockDocH=%d afterVisibleDocH=%d "
            L"visibleModesStartDoc=%d docContentH=%d lineH=%d\r\n",
            vmSplitActive ? 1 : 0,
            row1H_log,
            row2H_log,
            row1GapPx,
            row2ToBodyExtraGapPx,
            gapsPx,
            hT14Header,
            hVmHeading,
            hVmLines,
            hT15,
            hT16,
            hT18,
            hT17,
            totalBeforeVisible,
            s_paintDbgT14BeforeVisibleDocH,
            s_paintDbgT14VisibleBlockDocH,
            s_paintDbgT14AfterVisibleDocH,
            visStartDoc,
            s_paintDbgContentHeight,
            s_paintDbgLineHeight);
        OutputDebugStringW(t60);

        wchar_t t57[512] = {};
        swprintf_s(
            t57,
            _countof(t57),
            L"[T57LAYOUT] rawClientH=%d row1H=%d row2H=%d prefixTop=%d vmTop=%d restTop=%d "
            L"scrollBandReserve=%d scrollVpH=%d contentH=%d maxScroll=%d fillMonitor=%d mode=%s\r\n",
            clientH,
            row1H_log,
            row2H_log,
            prefixTop_log,
            vmTop_log,
            s_paintDbgRestViewportTopPx,
            s_paintDbgScrollBandReservePx,
            s_paintDbgRestViewportClientH,
            s_paintDbgContentHeight,
            s_paintDbgMaxScroll,
            Win32_IsMainWindowFillMonitorPresentation(hwnd) ? 1 : 0,
            Win32_MainWindow_GetPresentationModeLabelForDebug());
        OutputDebugStringW(t57);
    }

    s_paintDbgLayoutRestVpBudgetHint = restVpBudgetHint;
}

// Side-effect exit adapter: every call site that needs layout metrics (Prefill + first pass inside GDI paint) funnels here (docs §7.6).
void Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics(const Win32_LegacyStacked_LayoutMetricsParams& p)
{
    Win32_DebugOverlay_ComputeLayoutMetrics(p);
}

// Legacy stacked HUD — WIN32_HUD_USE_PAGED_HUD=0 の GDI 本文（メニュー + T14〜T18 縦積み + 下端 [scroll]）。
// Win32DebugOverlay_Paint はページ式を先に分岐し、本関数は互換経路のみ。
// Side-effect map: persistent writes go through RunComputeLayoutMetrics → ComputeLayoutMetrics; this function adds GDI raster ops only (§7.7 category E).
void Win32_DebugOverlay_PaintStackedLegacy(const Win32_LegacyStacked_GdiPaintParams& p)
{
    HWND hwnd = p.common.hwnd;
    HDC hdc = p.common.hdc;
    const wchar_t* t17ModeLabelForOverlay = p.common.t17ModeLabelForOverlay;
    const wchar_t* t17CandLabel = p.common.t17CandLabel;
    const wchar_t* t17ActLabel = p.common.t17ActLabel;
    bool suppressT14BodyGdi = p.suppressT14BodyGdi;
    bool skipMenuColumnGdi = p.skipMenuColumnGdi;
    bool skipScrollBandGdi = p.skipScrollBandGdi;
    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);
    const int clientW = static_cast<int>(rcClient.right - rcClient.left);
    const int clientH = static_cast<int>(rcClient.bottom - rcClient.top);

    wchar_t menuBuf[3072] = {};
    wchar_t t14Buf[8192] = {};
    Win32_LegacyStacked_LayoutMetricsParams lmForPaint{};
    lmForPaint.common = p.common;
    lmForPaint.outHud = nullptr;
    lmForPaint.logScroll = true;
    Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics(lmForPaint);

    Win32_FillMenuSamplePaintBuffers(
        hwnd,
        rcClient,
        menuBuf,
        _countof(menuBuf),
        t14Buf,
        _countof(t14Buf),
        Win32_IsT37VirtualBodyOverlayActiveForLayout(),
        s_paintDbgLayoutRestVpBudgetHint);
    RECT rcMenuDoc{};
    RECT rcT14Doc{};
    const bool t64FullscreenContentPaint =
        Win32_IsMainWindowFillMonitorPresentation(hwnd) &&
        Win32_MainWindow_IsFullscreenPresentationMode(hwnd);
    const int menuT14GapPxPaint =
        t64FullscreenContentPaint ? WIN32_OVERLAY_T64_FULLSCREEN_MENU_TO_T14_GAP_PX
                                  : WIN32_HUD_DBG_MENU_TO_T14_GAP_PX;
    Win32_MenuSampleMeasurePaintLayout(
        hdc, clientW, menuBuf, t14Buf, rcMenuDoc, rcT14Doc, menuT14GapPxPaint);
    const int t37TopGap =
        Win32_IsT37VirtualBodyOverlayActiveForLayout() ? WIN32_OVERLAY_T37_MENU_TOP_GAP_PX : 0;

    wchar_t overlay[2048] = {};
    const int jumpF7 = Win32DebugOverlay_ScrollTargetT17WithTopMargin();
    const int jumpF8 = Win32DebugOverlay_ScrollTargetT17Centered(hwnd);
    const bool compactScrollBandPaint =
        (clientH < WIN32_OVERLAY_T49_SCROLL_COMPACT_CLIENT_H) ||
        (s_paintDbgT14VmSplitActive && s_paintDbgRestViewportClientH > 0 &&
         s_paintDbgRestViewportClientH < WIN32_OVERLAY_T51_COMPACT_SCROLL_RESTVP_PX) ||
        Win32_IsMainWindowFillMonitorPresentation(hwnd);
    Win32_DebugOverlay_FormatScrollDebugOverlay(
        overlay,
        _countof(overlay),
        t17ModeLabelForOverlay,
        s_paintDbgContentHeightBase,
        s_paintDbgExtraBottomPadding,
        s_paintDbgContentHeight,
        s_paintDbgT17DocY,
        s_paintScrollY,
        s_paintDbgClientHeight,
        s_paintDbgRestViewportClientH,
        jumpF7,
        jumpF8,
        false,
        compactScrollBandPaint);

    // D3D が既にクライアントを塗っているため、ここでは白で全面上書きしない
    SetBkMode(hdc, TRANSPARENT);
    // Dark gray RTV clear (~RGB 31,36,46); light text reads over it without a full-client GDI fill.
    const COLORREF prevTextColor = SetTextColor(hdc, RGB(235, 238, 242));

    RECT rcClipMain = rcClient;
    {
        const int scrollBandReserve = s_paintDbgScrollBandReservePx;
        rcClipMain.bottom =
            (std::max)(rcClipMain.top, rcClipMain.bottom - scrollBandReserve);
    }

    const int bodyTopPx = s_paintDbgFinalBodyTopPx;
    wchar_t row1Buf[128];
    swprintf_s(
        row1Buf,
        L"cand=%s act=%s",
        t17CandLabel != nullptr ? t17CandLabel : L"?",
        t17ActLabel != nullptr ? t17ActLabel : L"?");

    if (!skipMenuColumnGdi)
    {
        const int saved = SaveDC(hdc);
        IntersectClipRect(
            hdc,
            rcClipMain.left,
            rcClipMain.top,
            rcClipMain.right,
            rcClipMain.bottom);
        IntersectClipRect(
            hdc,
            rcClipMain.left,
            bodyTopPx,
            rcClipMain.right,
            rcClipMain.bottom);

        RECT rcT14Draw = rcT14Doc;
        if (t37TopGap != 0)
        {
            ::OffsetRect(&rcT14Draw, 0, t37TopGap);
        }

        if (!suppressT14BodyGdi)
        {
            if (s_paintDbgT14VmSplitActive)
            {
                const int t14TextStartPx = s_paintDbgRow2TopPx + s_paintDbgBodyT14DocTopPx;
                const int restTopNatural =
                    t14TextStartPx + s_paintDbgT14VmSplitPrefixH + s_paintDbgT14VmSplitVmBandH;
                const int restTopPx =
                    (s_paintDbgRestViewportTopPx > 0) ? s_paintDbgRestViewportTopPx : restTopNatural;
                RECT rcPrefDraw = rcT14Draw;
                rcPrefDraw.top = t14TextStartPx;
                rcPrefDraw.bottom = (std::min)(
                    static_cast<int>(rcPrefDraw.top) + s_paintDbgT14VmSplitPrefixH + 4, restTopPx);
                rcPrefDraw.right = rcClipMain.right;
                {
                    const int savedPref = SaveDC(hdc);
                    IntersectClipRect(
                        hdc,
                        rcPrefDraw.left,
                        rcPrefDraw.top,
                        rcPrefDraw.right,
                        rcPrefDraw.bottom);
                    DrawTextW(
                        hdc,
                        s_paintDbgT14VmSplitPrefix,
                        -1,
                        &rcPrefDraw,
                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
                    RestoreDC(hdc, savedPref);
                }

                RECT rcVmDraw = rcT14Draw;
                rcVmDraw.top = t14TextStartPx + s_paintDbgT14VmSplitPrefixH;
                rcVmDraw.bottom = (std::min)(
                    static_cast<int>(rcVmDraw.top) + s_paintDbgT14VmSplitVmBandH + 4, restTopPx);
                rcVmDraw.right = rcClipMain.right;
                {
                    const int savedVm = SaveDC(hdc);
                    IntersectClipRect(
                        hdc,
                        rcVmDraw.left,
                        rcVmDraw.top,
                        rcVmDraw.right,
                        rcVmDraw.bottom);
                    DrawTextW(
                        hdc,
                        s_paintDbgT14VmSplitVmBand,
                        -1,
                        &rcVmDraw,
                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
                    RestoreDC(hdc, savedVm);
                }

                const int savedRest = SaveDC(hdc);
                IntersectClipRect(
                    hdc,
                    rcClipMain.left,
                    restTopPx,
                    rcClipMain.right,
                    rcClipMain.bottom);
                OffsetViewportOrgEx(hdc, 0, -s_paintScrollY, nullptr);
                RECT rcRestDraw = rcT14Draw;
                rcRestDraw.top = restTopPx;
                rcRestDraw.bottom = rcRestDraw.top + 1000000;
                DrawTextW(
                    hdc,
                    s_paintDbgT14VmSplitRest,
                    -1,
                    &rcRestDraw,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
                RestoreDC(hdc, savedRest);
            }
            else
            {
                OffsetViewportOrgEx(hdc, 0, -s_paintScrollY, nullptr);
                DrawTextW(
                    hdc,
                    t14Buf,
                    -1,
                    &rcT14Draw,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
            }
        }
        RestoreDC(hdc, saved);

        const int row1H = s_paintDbgFinalRow1HeightPx;
        const int row2TopPx = s_paintDbgRow2TopPx;
        RECT rcRow1{};
        rcRow1.left = 0;
        rcRow1.top = 0;
        rcRow1.right = clientW;
        rcRow1.bottom = row1H + 4;
        DrawTextW(hdc, row1Buf, -1, &rcRow1, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

        RECT rcMenuDraw = rcMenuDoc;
        ::OffsetRect(&rcMenuDraw, 0, row2TopPx);
        DrawTextW(hdc, menuBuf, -1, &rcMenuDraw, DT_LEFT | DT_TOP | DT_NOPREFIX);
    }

    if (!skipScrollBandGdi && s_paintDbgT53ScrollBandDrawEnabled)
    {
        RECT rcOv = rcClient;
        const int scrollBandReserve = s_paintDbgScrollBandReservePx;
        rcOv.top = (std::max)(rcClient.top, rcClient.bottom - scrollBandReserve);
        const int savedOv = SaveDC(hdc);
        IntersectClipRect(hdc, rcOv.left, rcOv.top, rcOv.right, rcOv.bottom);
        DrawTextW(hdc, overlay, -1, &rcOv, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
        RestoreDC(hdc, savedOv);
    }

    SetTextColor(hdc, prevTextColor);
}

// GDI public-entry adapter: WM_PAINT legacy branch only (single hop to PaintStackedLegacy; docs §7.6).
void Win32_DebugOverlay_LegacyStacked_RunGdiPaint(const Win32_LegacyStacked_GdiPaintParams& p)
{
    Win32_DebugOverlay_PaintStackedLegacy(p);
}

} // namespace (legacy stacked pipeline; merges with file-top unnamed namespace)

#pragma endregion

#pragma region Public HUD overlay entry (D2D prefill)

// Win32DebugOverlay_Paint (HUD GDI 入口) はファイル先頭の「Public HUD overlay entry (GDI WM_PAINT)」に定義。

// D2D フレーム用の左列プレフィル。ページ式 HUD 既定時は Win32_HudPaged_PrefillD2d のみ。レガシー縦積み時のみ ComputeLayoutMetrics 経路。
void Win32_DebugOverlay_PrefillHudLeftColumnForD2d(
    HWND hwnd,
    HDC hdc,
    WindowsRendererState* st,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel)
{
    if (!st || !hwnd || !hdc)
    {
        return;
    }
    st->dbgHudLeftColumnText[0] = L'\0';
    st->dbgHudLeftColumnTopPx = 0;
    st->dbgHudLeftColumnScrollYPx = 0;
    st->dbgHudLeftColumnClipBottomPadPx = 0;
    st->dbgHudLeftColumnPrefillClientW = 0;
    st->dbgHudLeftColumnPrefillClientH = 0;
    st->dbgHudScrollBandText[0] = L'\0';
    st->dbgHudScrollBandHeightPx = 0;
    st->dbgHudDrawScrollBand = true;
    st->dbgHudMenuBandText[0] = L'\0';
    st->dbgHudBodyBandText[0] = L'\0';
    st->dbgHudFinalRow1HeightPx = 0;
    st->dbgHudFinalBodyTopPx = 0;
    st->dbgHudBodyT14DocTopPx = 0;
    st->dbgHudRow2TopPx = 0;
    st->dbgHudMenuColumnHeightPx = 0;
    st->dbgHudT14VmSplit = false;
    st->dbgHudT14PrefixText[0] = L'\0';
    st->dbgHudVmHeadingText[0] = L'\0';
    st->dbgHudVmListBandText[0] = L'\0';
    st->dbgHudT14RestText[0] = L'\0';
    st->dbgHudT14PrefixHeightPx = 0;
    st->dbgHudVmBandHeightPx = 0;
    st->dbgHudRestViewportTopPx = 0;

    RECT rcActual{};
    GetClientRect(hwnd, &rcActual);
    const int actualW = static_cast<int>(rcActual.right - rcActual.left);
    const int actualH = static_cast<int>(rcActual.bottom - rcActual.top);
    if (actualW < 1 || actualH < 1)
    {
        return;
    }

    if (Win32_HudPaged_IsEnabled())
    {
        Win32_HudPaged_PrefillD2d(st, static_cast<UINT>(actualW), static_cast<UINT>(actualH));
        return;
    }

    Win32_LegacyStacked_LayoutMetricsParams lmPrefill{};
    lmPrefill.common.hwnd = hwnd;
    lmPrefill.common.hdc = hdc;
    lmPrefill.common.t17ModeLabelForOverlay = t17ModeLabelForOverlay != nullptr ? t17ModeLabelForOverlay : L"?";
    lmPrefill.common.t17CandLabel = t17CandLabel != nullptr ? t17CandLabel : L"?";
    lmPrefill.common.t17ActLabel = t17ActLabel != nullptr ? t17ActLabel : L"?";
    lmPrefill.outHud = st;
    lmPrefill.logScroll = false;
    Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics(lmPrefill);
}

#pragma endregion
