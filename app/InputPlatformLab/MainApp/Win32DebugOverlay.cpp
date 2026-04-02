#include "framework.h"
#include "Win32DebugOverlay.h"

#include <algorithm>

#ifndef WIN32_MAIN_T17_JUMP_TOP_MARGIN
#define WIN32_MAIN_T17_JUMP_TOP_MARGIN 160
#endif

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
extern int s_paintDbgClientW;
extern int s_paintDbgClientH;
extern int s_paintDbgMaxScroll;

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

int Win32DebugOverlay_ScrollTargetT17WithTopMargin(void)
{
    return (std::max)(0, s_paintDbgT17DocY - WIN32_MAIN_T17_JUMP_TOP_MARGIN);
}

int Win32DebugOverlay_ScrollTargetT17Centered(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
    {
        return 0;
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int ch = static_cast<int>(rc.bottom - rc.top);
    const int maxScr = (std::max)(0, s_paintDbgContentHeight - ch);
    const int y = s_paintDbgT17DocY - ch / 2;
    return (std::clamp)(y, 0, maxScr);
}

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
    int clientH = s_paintDbgClientHeight;
    if (hwnd && IsWindow(hwnd))
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        clientH = static_cast<int>(rc.bottom - rc.top);
    }
    const int contentH = (contentHOverride >= 0) ? contentHOverride : s_paintDbgContentHeight;
    const int t17Y = (t17Override >= 0) ? t17Override : s_paintDbgT17DocY;
    const int maxScroll = (std::max)(0, contentH - clientH);

    wchar_t line[384] = {};
    swprintf_s(line, _countof(line), L"[SCROLL] where=%s\r\n", where ? where : L"?");
    OutputDebugStringW(line);
    swprintf_s(line, _countof(line), L"[SCROLL] clientH=%d\r\n", clientH);
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
    swprintf_s(line, _countof(line), L"[SCROLL] maxScroll=%d\r\n", maxScroll);
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

    if (hwnd && IsWindow(hwnd))
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
                L"[SCROLL] scrollbar nMax=%d nPage=%d pos=%d maxScroll_si=%d\r\n",
                static_cast<int>(si.nMax),
                static_cast<int>(si.nPage),
                static_cast<int>(si.nPos),
                maxSi);
            OutputDebugStringW(line);
        }
    }
    OutputDebugStringW(L"[SCROLL] ----\r\n");
}

void Win32DebugOverlay_MainView_SetScrollPos(HWND hwnd, int newY, const wchar_t* logWhere)
{
    if (!hwnd)
    {
        return;
    }
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hwnd, SB_VERT, &si))
    {
        return;
    }
    const int posBefore = static_cast<int>(si.nPos);
    const int maxScroll = (std::max)(0, static_cast<int>(si.nMax) - static_cast<int>(si.nPage) + 1);
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
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    s_paintScrollY = clamped;
    if (logWhere)
    {
        Win32DebugOverlay_ScrollLog(logWhere, hwnd, posBefore, clamped, -1, -1, -1, -1);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void Win32_MainView_FormatScrollDebugOverlay(
    wchar_t* buf,
    size_t bufCount,
    const wchar_t* modeLabel,
    int contentHBase,
    int extraBottomPadding,
    int contentHeight,
    int maxScroll,
    int t17DocY,
    int scrollY,
    int clientH,
    int jumpF7,
    int jumpF8)
{
    swprintf_s(
        buf,
        bufCount,
        L"[scroll] mode(actual)=%s\r\n"
        L"contentH(base)=%d  extraBottomPadding=%d  contentH(with padding)=%d  maxScroll=%d  T17DocY=%d\r\n"
        L"scrollY=%d  clientH=%d  jumpTargetF7=%d  jumpTargetF8=%d  (F7 margin=%d)\r\n"
        L"(PgUp/Dn=1/2 page; POPUP=pad clientH; Windowed=min pad to reach T17)",
        modeLabel,
        contentHBase,
        extraBottomPadding,
        contentHeight,
        maxScroll,
        t17DocY,
        scrollY,
        clientH,
        jumpF7,
        jumpF8,
        WIN32_MAIN_T17_JUMP_TOP_MARGIN);
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

void Win32DebugOverlay_Paint(HWND hwnd, HDC hdc, const wchar_t* t17ModeLabelForOverlay)
{
    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);
    const int clientW = static_cast<int>(rcClient.right - rcClient.left);
    const int clientH = static_cast<int>(rcClient.bottom - rcClient.top);

    wchar_t menuBuf[3072] = {};
    wchar_t t14Buf[8192] = {};
    Win32_FillMenuSamplePaintBuffers(hwnd, rcClient, menuBuf, _countof(menuBuf), t14Buf, _countof(t14Buf));

    RECT rcMenuDoc{};
    RECT rcT14Doc{};
    Win32_MenuSampleMeasurePaintLayout(hdc, clientW, menuBuf, t14Buf, rcMenuDoc, rcT14Doc);
    const int baseContentH = static_cast<int>(rcT14Doc.bottom);

    s_paintDbgT14LayoutValid = false;
    const wchar_t* visMarkerT14 = wcsstr(t14Buf, L"visible modes:\r\n");
    if (visMarkerT14 != nullptr)
    {
        const wchar_t* firstVmLine = visMarkerT14 + wcslen(L"visible modes:\r\n");
        const int prefixLenVm = static_cast<int>(firstVmLine - t14Buf);
        if (prefixLenVm > 0)
        {
            const int t14BaseY = static_cast<int>(rcMenuDoc.bottom) + 8;
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

    int t17DocY = 0;
    const wchar_t* t17Mark = wcsstr(t14Buf, L"--- T17 presentation ---");
    if (t17Mark != nullptr)
    {
        const size_t prefixChars = static_cast<size_t>(t17Mark - t14Buf);
        wchar_t prefixBuf[8192] = {};
        if (prefixChars < _countof(prefixBuf))
        {
            wmemcpy_s(prefixBuf, _countof(prefixBuf), t14Buf, prefixChars);
            prefixBuf[prefixChars] = L'\0';
            RECT rcPre{};
            rcPre.left = 0;
            rcPre.top = rcMenuDoc.bottom + 8;
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
    int extraBottomPadding = 0;
    if (Win32_IsMainWindowFillMonitorPresentation(hwnd))
    {
        extraBottomPadding = clientH;
    }
    else
    {
        extraBottomPadding = (std::max)(0, t17DocY - maxScrollBeforePadding);
    }
    const int contentHeight = baseContentH + extraBottomPadding;

    TEXTMETRICW tm{};
    if (GetTextMetricsW(hdc, &tm))
    {
        s_paintScrollLinePx = (std::max)(static_cast<int>(tm.tmHeight), 16);
    }
    s_paintDbgLineHeight = s_paintScrollLinePx;

    s_paintDbgContentHeight = contentHeight;
    s_paintDbgContentHeightBase = baseContentH;
    s_paintDbgExtraBottomPadding = extraBottomPadding;
    s_paintDbgClientHeight = clientH;
    s_paintDbgClientW = clientW;
    s_paintDbgClientH = clientH;

    const int maxScroll = (std::max)(0, contentHeight - clientH);
    s_paintDbgMaxScroll = maxScroll;
    const int scrollYBeforePaint = s_paintScrollY;
    s_paintScrollY = (std::clamp)(s_paintScrollY, 0, maxScroll);

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (std::max)(0, contentHeight - 1);
    si.nPage = static_cast<UINT>((std::max)(1, clientH));
    si.nPos = s_paintScrollY;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    Win32DebugOverlay_ScrollLog(
        L"WM_PAINT after SetScrollInfo",
        hwnd,
        scrollYBeforePaint,
        s_paintScrollY,
        contentHeight,
        s_paintDbgT17DocY,
        baseContentH,
        extraBottomPadding);

    // D3D が既にクライアントを塗っているため、ここでは白で全面上書きしない
    SetBkMode(hdc, TRANSPARENT);
    // Dark gray RTV clear (~RGB 31,36,46); light text reads over it without a full-client GDI fill.
    const COLORREF prevTextColor = SetTextColor(hdc, RGB(235, 238, 242));

    const int jumpF7 = Win32DebugOverlay_ScrollTargetT17WithTopMargin();
    const int jumpF8 = Win32DebugOverlay_ScrollTargetT17Centered(hwnd);
    wchar_t overlay[1024] = {};
    Win32_MainView_FormatScrollDebugOverlay(
        overlay,
        _countof(overlay),
        t17ModeLabelForOverlay,
        s_paintDbgContentHeightBase,
        s_paintDbgExtraBottomPadding,
        s_paintDbgContentHeight,
        maxScroll,
        s_paintDbgT17DocY,
        s_paintScrollY,
        s_paintDbgClientHeight,
        jumpF7,
        jumpF8);
    const int actualOverlayHeight =
        Win32_MainView_MeasureScrollOverlayTextHeight(hdc, clientW, overlay);
    s_paintDbgActualOverlayHeight = actualOverlayHeight;

    RECT rcClipMain = rcClient;
    {
        const int overlayReserve = (std::min)(actualOverlayHeight, clientH);
        rcClipMain.bottom =
            (std::max)(rcClipMain.top, rcClipMain.bottom - overlayReserve);
    }

    const int saved = SaveDC(hdc);
    IntersectClipRect(
        hdc,
        rcClipMain.left,
        rcClipMain.top,
        rcClipMain.right,
        rcClipMain.bottom);
    OffsetViewportOrgEx(hdc, 0, -s_paintScrollY, nullptr);

    DrawTextW(hdc, menuBuf, -1, &rcMenuDoc, DT_LEFT | DT_TOP | DT_NOPREFIX);
    DrawTextW(
        hdc,
        t14Buf,
        -1,
        &rcT14Doc,
        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

    RestoreDC(hdc, saved);

    RECT rcOv = rcClient;
    rcOv.top = (std::max)(rcClient.top, rcClient.bottom - actualOverlayHeight);
    DrawTextW(hdc, overlay, -1, &rcOv, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

    SetTextColor(hdc, prevTextColor);
}
