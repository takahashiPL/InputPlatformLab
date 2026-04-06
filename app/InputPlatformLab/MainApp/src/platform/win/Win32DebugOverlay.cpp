#include "framework.h"
#include "Win32DebugOverlay.h"
#include "WindowsRenderer.h"

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

// T48: body（vmSplit rest）スクロールビューポートの最小高さ（scrollVpH / [scroll] と GDI/D2D クリップを一致させる）
#ifndef WIN32_OVERLAY_MIN_BODY_VIEWPORT_PX
#define WIN32_OVERLAY_MIN_BODY_VIEWPORT_PX 64
#endif

// GDI Paint と D2D final HUD で body クリップ先頭を共有（ComputeLayoutMetrics で更新）
static int s_paintDbgFinalBodyTopPx = 0;
static int s_paintDbgBodyT14DocTopPx = 0;
static int s_paintDbgFinalRow1HeightPx = 0;
static int s_paintDbgRow2TopPx = 0;

// T40: visible modes 帯を本文スクロールから分離
static bool s_paintDbgT14VmSplitActive = false;
static int s_paintDbgT17DocYRestScroll = 0;
static int s_paintDbgRestViewportClientH = 0;
static int s_paintDbgRestViewportTopPx = 0;

// T40: GDI パス用（ComputeLayoutMetrics で更新、Paint が参照）
static wchar_t s_paintDbgT14VmSplitPrefix[8192]{};
static wchar_t s_paintDbgT14VmSplitVmBand[8192]{};
static wchar_t s_paintDbgT14VmSplitRest[16384]{};
static int s_paintDbgT14VmSplitPrefixH = 0;
static int s_paintDbgT14VmSplitVmBandH = 0;

// ---------------------------------------------------------------------------
// GDI: D3D で塗ったクライアント上にデバッグ文字を載せる。縦スクロール・[scroll] オーバーレイ・T14/T17 行位置の計測。
// ---------------------------------------------------------------------------

// MainApp.cpp で定義（スクロール・レイアウトキャッシュ。入力/T14 オートフォローと共有）
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
    RECT& outT14Doc)
{
    outMenuDoc.left = 0;
    outMenuDoc.top = 0;
    outMenuDoc.right = clientW;
    outMenuDoc.bottom = 0;
    DrawTextW(hdc, menuBuf, -1, &outMenuDoc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_CALCRECT);

    outT14Doc.left = 0;
    outT14Doc.top = outMenuDoc.bottom + 8;
    outT14Doc.right = clientW;
    outT14Doc.bottom = outT14Doc.top + 1000000;
    DrawTextW(
        hdc,
        t14Buf,
        -1,
        &outT14Doc,
        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
}

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

// T46/T47: 直近の Windowed SetScrollInfo（fill-monitor では無効）— [scroll] 帯と [SCROLL] ログで共有
static int s_t46LastSiNMax = 0;
static UINT s_t46LastSiNPage = 0;
static int s_t46LastSiNPos = 0;
static int s_t46LastSiMaxScrollSi = 0;
static bool s_t46LastSiValid = false;

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

// Prefill / WM_PAINT 共通: スクロール・[scroll] 帯の高さ・T17 行位置などを計測。
// outHud 非 null のときは D2D final HUD 用に左列全文（menu+t14）とスクロール値を書き込む。
static void Win32_DebugOverlay_ComputeLayoutMetrics(
    HWND hwnd,
    HDC hdc,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel,
    WindowsRendererState* outHud,
    bool logScroll)
{
    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);
    const int clientW = static_cast<int>(rcClient.right - rcClient.left);
    const int clientH = static_cast<int>(rcClient.bottom - rcClient.top);

    int restVpBudgetHint = -1;
    bool layoutRefilledForBudget = false;

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
    Win32_MenuSampleMeasurePaintLayout(hdc, clientW, menuBuf, t14Buf, rcMenuDoc, rcT14Doc);
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
    const int row1GapPx =
        (clientH < 360) ? (WIN32_HUD_DBG_FINAL_ROW1_BOTTOM_GAP_PX / 2) : WIN32_HUD_DBG_FINAL_ROW1_BOTTOM_GAP_PX;
    const int row2ToBodyExtraGapPx =
        (clientH < 360) ? (WIN32_HUD_DBG_FINAL_ROW2_TO_BODY_EXTRA_GAP_PX / 2)
                        : WIN32_HUD_DBG_FINAL_ROW2_TO_BODY_EXTRA_GAP_PX;
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
            if (Win32_IsMainWindowFillMonitorPresentation(hwnd))
            {
                extraBottomPadding = clientH;
            }
            else
            {
                extraBottomPadding =
                    (std::max)(0, s_paintDbgT17DocYRestScroll - maxScrollBeforePaddingRest);
            }
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
        if (Win32_IsMainWindowFillMonitorPresentation(hwnd))
        {
            extraBottomPadding = clientH;
        }
        else
        {
            extraBottomPadding = (std::max)(0, t17DocY - maxScrollBeforePadding);
        }
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
        (vmSplitActive && splitRestVp > 0 && splitRestVp < WIN32_OVERLAY_T51_COMPACT_SCROLL_RESTVP_PX);

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
        const int kMinBody = WIN32_OVERLAY_MIN_BODY_VIEWPORT_PX;
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
            if (Win32_IsMainWindowFillMonitorPresentation(hwnd))
            {
                extra2 = clientH;
            }
            else
            {
                extra2 = (std::max)(
                    0, s_paintDbgT17DocYRestScroll - maxScrollBeforePaddingRest2);
            }
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
            restVpBudgetHint = restVp2;
            layoutRefilledForBudget = true;
            goto refill_budget;
        }
    }

    {
        const int scrollContentHFinal = s_paintDbgContentHeight;
        const int scrollViewportHFinal = vmSplitActive ? s_paintDbgRestViewportClientH : clientH;
        const int maxScrollUnified = (std::max)(0, scrollContentHFinal - scrollViewportHFinal);
        s_paintDbgMaxScroll = maxScrollUnified;
        Win32_DebugOverlay_ClampScrollYToMaxScroll(maxScrollUnified, L"ComputeLayoutMetrics.unified");
        Win32_T45_ApplyWindowedScrollInfo(hwnd, scrollContentHFinal, scrollViewportHFinal, s_paintScrollY);
    }

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

    if (outHud)
    {
        outHud->dbgHudLeftColumnText[0] = L'\0';
        wcscpy_s(outHud->dbgHudLeftColumnText, _countof(outHud->dbgHudLeftColumnText), menuBuf);
        wcscat_s(outHud->dbgHudLeftColumnText, _countof(outHud->dbgHudLeftColumnText), L"\r\n");
        wcscat_s(outHud->dbgHudLeftColumnText, _countof(outHud->dbgHudLeftColumnText), t14Buf);
        outHud->dbgHudLeftColumnTopPx = t37TopGap;
        outHud->dbgHudLeftColumnScrollYPx = s_paintScrollY;
        outHud->dbgHudLeftColumnClipBottomPadPx = s_paintDbgScrollBandReservePx;
        outHud->dbgHudLeftColumnPrefillClientW = static_cast<std::uint32_t>(clientW);
        outHud->dbgHudLeftColumnPrefillClientH = static_cast<std::uint32_t>(clientH);
        wcscpy_s(outHud->dbgHudScrollBandText, _countof(outHud->dbgHudScrollBandText), overlay);
        outHud->dbgHudScrollBandHeightPx = s_paintDbgScrollBandReservePx;
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
            outHud->dbgHudRestViewportTopPx = s_paintDbgRestViewportTopPx;
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

    s_paintDbgLayoutRestVpBudgetHint = restVpBudgetHint;
}

// 本文（メニュー + T14〜T18 テキスト）をスクロール付きで描画し、下端に [scroll] サマリを載せる。
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
    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);
    const int clientW = static_cast<int>(rcClient.right - rcClient.left);
    const int clientH = static_cast<int>(rcClient.bottom - rcClient.top);

    wchar_t menuBuf[3072] = {};
    wchar_t t14Buf[8192] = {};
    Win32_DebugOverlay_ComputeLayoutMetrics(
        hwnd, hdc, t17ModeLabelForOverlay, t17CandLabel, t17ActLabel, nullptr, true);

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
    Win32_MenuSampleMeasurePaintLayout(hdc, clientW, menuBuf, t14Buf, rcMenuDoc, rcT14Doc);
    const int t37TopGap =
        Win32_IsT37VirtualBodyOverlayActiveForLayout() ? WIN32_OVERLAY_T37_MENU_TOP_GAP_PX : 0;

    wchar_t overlay[2048] = {};
    const int jumpF7 = Win32DebugOverlay_ScrollTargetT17WithTopMargin();
    const int jumpF8 = Win32DebugOverlay_ScrollTargetT17Centered(hwnd);
    const bool compactScrollBandPaint =
        (clientH < WIN32_OVERLAY_T49_SCROLL_COMPACT_CLIENT_H) ||
        (s_paintDbgT14VmSplitActive && s_paintDbgRestViewportClientH > 0 &&
         s_paintDbgRestViewportClientH < WIN32_OVERLAY_T51_COMPACT_SCROLL_RESTVP_PX);
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

    if (!skipScrollBandGdi)
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

    Win32_DebugOverlay_ComputeLayoutMetrics(
        hwnd,
        hdc,
        t17ModeLabelForOverlay != nullptr ? t17ModeLabelForOverlay : L"?",
        t17CandLabel != nullptr ? t17CandLabel : L"?",
        t17ActLabel != nullptr ? t17ActLabel : L"?",
        st,
        false);
}
