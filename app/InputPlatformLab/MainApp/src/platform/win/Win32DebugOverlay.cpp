#include "framework.h"
#include "Win32DebugOverlay.h"
#include "WindowsRenderer.h"
#include "Win32HudPaged.h"
#include "Win32DebugOverlayLegacyStacked_bridge.h"
#include "Win32MainAppPaintDbg_shared_link.h"

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
// Win32DebugOverlay.cpp — main TU（本ファイル）
// -----------------------------------------------------------------------------
// 役割の目安:
//   • ページ式 HUD が通常運用の正 — Win32_HudPaged_* が先に分岐する入口をここに置く。
//   • Public entry: 下記 #pragma region「Public HUD entry」— GDI WM_PAINT と D2D 左列プレフィル。
//   • Shared: 「Shared overlay」region — 表示モード・[scroll] 文字列・T44/T45/T46 スナップショット・ScrollLog 等（ページ式・レガシー双方から参照され得る）。
//   • Legacy 縦積みの実装本体は Win32DebugOverlayLegacyStacked.cpp。main からは Win32DebugOverlayLegacyStacked_bridge.h のみ（internal.h は include しない）。
//   • Legacy への入口は薄いラッパ（例: RunGdiPaintFromPaintEntry / RunComputeLayoutMetricsForD2dPrefill）→ Run*。詳細は HUD_LEGACY_CODE_DEPENDENCY.md §5 / §7。
// MainApp 共有 HUD 状態（extern）の宣言: Win32MainAppPaintDbg_shared_link.h（§2.3）。
// -----------------------------------------------------------------------------

#pragma region Public HUD entry (WM_PAINT: paged first, else legacy stacked via bridge)

// WM_PAINT 用 HUD GDI。ページ式が無効なときのみ legacy（RunGdiPaintFromPaintEntry → legacy TU）。
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
    Win32_DebugOverlay_LegacyStacked_RunGdiPaintFromPaintEntry(
        hwnd,
        hdc,
        t17ModeLabelForOverlay,
        t17CandLabel,
        t17ActLabel,
        suppressT14BodyGdi,
        skipMenuColumnGdi,
        skipScrollBandGdi);
}

#pragma endregion

// Shared overlay — ページ式・レガシー双方が参照し得る表示・スクロールバー・[scroll] テキスト・T44/T45/T46。
// レガシー専用のレイアウト計測・scratch の実体は legacy TU。bridge.h の getter / LoadMainAppPaintDbgRead はここから呼ばれる。
// docs/HUD_LEGACY_CODE_DEPENDENCY.md §7.4。
#pragma region Shared overlay (presentation, scrollbars, [scroll] text)

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

// T46/T47: legacy 経路の ComputeLayoutMetrics → T45 後に更新。Win32DebugOverlay_ScrollLog / FormatScrollDebugOverlay が参照（§7.3）。
// ページ式でも参照され得るため「legacy 専用」ではない — shared region に置く。
static int s_t46LastSiNMax = 0;
static UINT s_t46LastSiNPage = 0;
static int s_t46LastSiNPos = 0;
static int s_t46LastSiMaxScrollSi = 0;
static bool s_t46LastSiValid = false;

// T52: レイアウト指標の paint 整合フラグ（定義は legacy TU）。IsPaintLayoutMetricsFromPaintValid は bridge.h。

// F7 ジャンプ先: 「--- T17 presentation ---」行を上余白付きで見える scrollY。
int Win32DebugOverlay_ScrollTargetT17WithTopMargin(void)
{
    if (Win32_LegacyStacked_GetT14VmSplitActive())
    {
        return (std::max)(0, Win32_LegacyStacked_GetT17DocYRestScroll() - WIN32_MAIN_T17_JUMP_TOP_MARGIN);
    }
    Win32_LegacyStacked_MainAppPaintDbgRead ma{};
    Win32_LegacyStacked_LoadMainAppPaintDbgRead(&ma);
    return (std::max)(0, ma.t17DocY - WIN32_MAIN_T17_JUMP_TOP_MARGIN);
}

// F8 ジャンプ先: T17 ブロックがクライアント高さの中央付近に来る scrollY。
int Win32DebugOverlay_ScrollTargetT17Centered(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
    {
        return 0;
    }
    if (Win32_LegacyStacked_GetT14VmSplitActive())
    {
        Win32_LegacyStacked_MainAppPaintDbgRead ma{};
        Win32_LegacyStacked_LoadMainAppPaintDbgRead(&ma);
        const int maxScr = (std::max)(0, ma.maxScroll);
        const int y = Win32_LegacyStacked_GetT17DocYRestScroll() - ma.restViewportClientH / 2;
        return (std::clamp)(y, 0, maxScr);
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int ch = static_cast<int>(rc.bottom - rc.top);
    Win32_LegacyStacked_MainAppPaintDbgRead ma{};
    Win32_LegacyStacked_LoadMainAppPaintDbgRead(&ma);
    const int maxScr = (std::max)(0, ma.contentHeight - ch);
    const int y = ma.t17DocY - ch / 2;
    return (std::clamp)(y, 0, maxScr);
}

bool Win32DebugOverlay_IsT14VmSplitActive(void)
{
    return Win32_LegacyStacked_GetT14VmSplitActive();
}

void Win32_DebugOverlay_ResetProvisionalLayoutCache(void)
{
    Win32_LegacyStacked_ClearPaintLayoutMetricsFromPaintValid();
    Win32_LegacyStacked_ApplyMainAppPaintDbgResetProvisionalLayoutExtras();
    s_t46LastSiValid = false;
}

bool Win32_DebugOverlay_IsPaintLayoutMetricsValid(void)
{
    return Win32_LegacyStacked_IsPaintLayoutMetricsFromPaintValid();
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
        !Win32_LegacyStacked_IsPaintLayoutMetricsFromPaintValid() && contentHOverride < 0 &&
        t17Override < 0;
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

    Win32_LegacyStacked_MainAppPaintDbgRead ma{};
    Win32_LegacyStacked_LoadMainAppPaintDbgRead(&ma);

    int scrollVpH = ma.restViewportClientH;
    if (scrollVpH < 1)
    {
        scrollVpH = (std::max)(1, rawClientH);
    }
    const int contentH = (contentHOverride >= 0) ? contentHOverride : ma.contentHeight;
    const int t17Y = (t17Override >= 0) ? t17Override : ma.t17DocY;
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

// T45: Windowed のみ。論理レイアウトの最終 contentH / スクロール用ビューポート高から SetScrollInfo（maxScroll_si == contentH - viewportH）。T46 スナップショットは本関数内で更新。
// ComputeLayoutMetrics の unified 経路は Win32_LegacyStacked_RunUnifiedMaxScrollClampAndT45 が入口（§7.7 D）。
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

// legacy TU からの単一ホップ — T45 本体（Win32_T45_ApplyWindowedScrollInfo）は main TU に残す（§7.3）。
void Win32_DebugOverlay_LegacyStacked_InvokeT45(HWND hwnd, int scrollContentH, int scrollViewportH, int pos)
{
    Win32_T45_ApplyWindowedScrollInfo(hwnd, scrollContentH, scrollViewportH, pos);
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
#pragma endregion


#pragma region Public HUD entry (D2D prefill: paged first, else legacy via bridge)

// D2D フレーム前の左列プレフィル。ページ式なら Win32_HudPaged_PrefillD2d のみ。
// レガシー縦積み時は RunComputeLayoutMetricsForD2dPrefill → legacy TU（ComputeLayoutMetrics）。
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

    Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetricsForD2dPrefill(
        hwnd, hdc, st, t17ModeLabelForOverlay, t17CandLabel, t17ActLabel);
}

#pragma endregion
