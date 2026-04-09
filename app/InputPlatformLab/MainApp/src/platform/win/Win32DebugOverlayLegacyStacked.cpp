#include "framework.h"
#include "Win32DebugOverlay.h"
#include "WindowsRenderer.h"
#include "Win32DebugOverlayLegacyStacked_internal.h"
#include "Win32MainAppPaintDbg_shared_link.h"

#include <algorithm>
#include <cstdint>

// Win32DebugOverlay.cpp — T45 wrapper (static T45 stays in that TU).
void Win32_DebugOverlay_LegacyStacked_InvokeT45(HWND hwnd, int scrollContentH, int scrollViewportH, int pos);

// -----------------------------------------------------------------------------
// Legacy stacked HUD scratch + T52 validity (HUD_LEGACY_CODE_DEPENDENCY.md §7.2 / §7.7).
// -----------------------------------------------------------------------------

int s_paintDbgFinalBodyTopPx = 0;
int s_paintDbgBodyT14DocTopPx = 0;
int s_paintDbgFinalRow1HeightPx = 0;
int s_paintDbgRow2TopPx = 0;

bool s_paintDbgT14VmSplitActive = false;
int s_paintDbgT17DocYRestScroll = 0;
int s_paintDbgRestViewportTopPx = 0;

bool s_paintDbgT53ScrollBandDrawEnabled = true;

wchar_t s_paintDbgT14VmSplitPrefix[8192]{};
wchar_t s_paintDbgT14VmSplitVmBand[8192]{};
wchar_t s_paintDbgT14VmSplitRest[16384]{};
int s_paintDbgT14VmSplitPrefixH = 0;
int s_paintDbgT14VmSplitVmBandH = 0;

bool s_paintDbgLayoutMetricsFromPaintValid = false;

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

void Win32_LegacyStacked_ClearScratchRestViewportTop()
{
    s_paintDbgRestViewportTopPx = 0;
}

void Win32_LegacyStacked_ApplyScratchFinalHudGeometry(int bodyTopPx, int t14DocTopAbsPx, int row1H, int row2TopPx)
{
    s_paintDbgFinalBodyTopPx = bodyTopPx;
    s_paintDbgBodyT14DocTopPx = t14DocTopAbsPx;
    s_paintDbgFinalRow1HeightPx = row1H;
    s_paintDbgRow2TopPx = row2TopPx;
}

void Win32_LegacyStacked_ResetVmSplitScratchFlags()
{
    s_paintDbgT14VmSplitActive = false;
    s_paintDbgT17DocYRestScroll = 0;
}

void Win32_LegacyStacked_ApplyScratchT53ScrollBandDrawEnabled(HWND hwnd, int restVpBudgetHint, int clientH)
{
    const int budgetT53 = (restVpBudgetHint >= 0) ? restVpBudgetHint : clientH;
    const bool windowedHud = !Win32_MainWindow_IsFillMonitorPresentationMode(hwnd);
    s_paintDbgT53ScrollBandDrawEnabled =
        !(windowedHud && budgetT53 >= 0 &&
          budgetT53 < WIN32_OVERLAY_T53_OMIT_SCROLL_BAND_BUDGET_PX);
}

bool Win32_LegacyStacked_RunVmSplitScratchPass(
    HDC hdc,
    int clientW,
    int clientH,
    int row2TopPx,
    int t14BaseY,
    const wchar_t* t14Buf,
    const wchar_t* pVis,
    const wchar_t* pT15,
    Win32_LegacyStacked_VmSplitScratchPassOut* out)
{
    if (!out)
    {
        return false;
    }
    const wchar_t* firstVmLine = pVis + wcslen(L"visible modes:\r\n");
    const size_t prefixChars = static_cast<size_t>(pVis - t14Buf);
    const size_t vmBandChars = static_cast<size_t>(pT15 - pVis);
    const size_t restChars = wcslen(pT15);

    if (prefixChars >= _countof(s_paintDbgT14VmSplitPrefix) ||
        vmBandChars >= _countof(s_paintDbgT14VmSplitVmBand) ||
        restChars >= _countof(s_paintDbgT14VmSplitRest))
    {
        s_paintDbgT14VmSplitActive = false;
        return false;
    }

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
    out->splitHPrefix = static_cast<int>(rcPref.bottom) - t14BaseY;

    RECT rcVm{};
    rcVm.left = 0;
    rcVm.top = t14BaseY + out->splitHPrefix;
    rcVm.right = clientW;
    rcVm.bottom = rcVm.top + 1000000;
    DrawTextW(
        hdc,
        s_paintDbgT14VmSplitVmBand,
        static_cast<int>(vmBandChars),
        &rcVm,
        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    out->splitHVmBand = static_cast<int>(rcVm.bottom) - (t14BaseY + out->splitHPrefix);

    RECT rcRest{};
    rcRest.left = 0;
    rcRest.top = t14BaseY + out->splitHPrefix + out->splitHVmBand;
    rcRest.right = clientW;
    rcRest.bottom = rcRest.top + 1000000;
    DrawTextW(
        hdc,
        s_paintDbgT14VmSplitRest,
        -1,
        &rcRest,
        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    out->splitHRest = static_cast<int>(rcRest.bottom) - (t14BaseY + out->splitHPrefix + out->splitHVmBand);

    wchar_t prefixPlusHeading[8192] = {};
    const size_t headSeg = static_cast<size_t>(firstVmLine - pVis);
    out->t14LayoutOutValid = false;
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
        out->t14VisibleModesDocStartY = static_cast<int>(rcPh.bottom);
        out->t14LayoutValid = true;
        out->t14LayoutOutValid = true;
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

    out->splitRestTopPx = row2TopPx + t14BaseY + out->splitHPrefix + out->splitHVmBand;
    out->splitRestVp = (std::max)(1, clientH - out->splitRestTopPx);
    const int maxScrollBeforePaddingRest = (std::max)(0, out->splitHRest - out->splitRestVp);
    // T56: fill-monitor でも extraBottomPadding は T17 到達に必要な分のみ（rawClientH 一律付与はしない）
    out->extraBottomPadding =
        (std::max)(0, s_paintDbgT17DocYRestScroll - maxScrollBeforePaddingRest);
    out->contentHeight = out->splitHRest + out->extraBottomPadding;
    out->maxScroll = (std::max)(0, out->contentHeight - out->splitRestVp);
    out->t17DocY = s_paintDbgT17DocYRestScroll;
    return true;
}

void Win32_LegacyStacked_ApplyVmSplitMainAppExternFromScratchPass(
    const Win32_LegacyStacked_VmSplitScratchPassOut& vsp)
{
    // MainApp 共有への書き込み塊（vmSplit 確定時）
    s_paintDbgT17DocY = vsp.t17DocY;
    if (vsp.t14LayoutOutValid)
    {
        s_paintDbgT14VisibleModesDocStartY = vsp.t14VisibleModesDocStartY;
        s_paintDbgT14LayoutValid = vsp.t14LayoutValid;
    }
}

void Win32_LegacyStacked_LoadMainAppPaintDbgRead(Win32_LegacyStacked_MainAppPaintDbgRead* out)
{
    if (!out)
    {
        return;
    }
    out->scrollY = s_paintScrollY;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgScrollLineMetrics(int scrollLinePx)
{
    s_paintScrollLinePx = scrollLinePx;
    s_paintDbgLineHeight = scrollLinePx;
}

void Win32_LegacyStacked_ApplyScrollLineMetricsFromHdc(HDC hdc)
{
    TEXTMETRICW tm{};
    if (GetTextMetricsW(hdc, &tm))
    {
        Win32_LegacyStacked_ApplyMainAppPaintDbgScrollLineMetrics(
            (std::max)(static_cast<int>(tm.tmHeight), 16));
    }
    else
    {
        s_paintDbgLineHeight = s_paintScrollLinePx;
    }
}

void Win32_LegacyStacked_SetDbgMaxScrollAndClampScrollY(int maxScrollLogical, const wchar_t* where)
{
    // MainApp 共有: maxScroll + scrollY clamp
    s_paintDbgMaxScroll = maxScrollLogical;
    Win32_DebugOverlay_ClampScrollYToMaxScroll(maxScrollLogical, where);
}

void Win32_LegacyStacked_RunUnifiedMaxScrollClampAndT45(
    HWND hwnd,
    const Win32_LegacyStacked_UnifiedScrollLayoutForT45& u)
{
    Win32_LegacyStacked_SetDbgMaxScrollAndClampScrollY(u.maxScrollUnified, L"ComputeLayoutMetrics.unified");
    Win32_LegacyStacked_MainAppPaintDbgRead mainAppRead{};
    Win32_LegacyStacked_LoadMainAppPaintDbgRead(&mainAppRead);
    Win32_DebugOverlay_LegacyStacked_InvokeT45(
        hwnd, u.scrollContentHFinal, u.scrollViewportHFinal, mainAppRead.scrollY);
}

void Win32_LegacyStacked_MarkPaintLayoutMetricsFromPaintValid(void)
{
    s_paintDbgLayoutMetricsFromPaintValid = true;
}

void Win32_LegacyStacked_LoadLayoutScratchRead(Win32_LegacyStacked_MainLayoutScratchRead* out)
{
    if (!out)
    {
        return;
    }
    out->finalRow1HeightPx = s_paintDbgFinalRow1HeightPx;
    out->row2TopPx = s_paintDbgRow2TopPx;
    out->t14VmSplitActive = s_paintDbgT14VmSplitActive;
    out->finalBodyTopPx = s_paintDbgFinalBodyTopPx;
    out->bodyT14DocTopPx = s_paintDbgBodyT14DocTopPx;
    out->restViewportTopPx = s_paintDbgRestViewportTopPx;
    out->t53ScrollBandDrawEnabled = s_paintDbgT53ScrollBandDrawEnabled;
    out->t14VmSplitPrefixH = s_paintDbgT14VmSplitPrefixH;
    out->t14VmSplitVmBandH = s_paintDbgT14VmSplitVmBandH;
    out->t14VmSplitPrefix = s_paintDbgT14VmSplitPrefix;
    out->t14VmSplitVmBand = s_paintDbgT14VmSplitVmBand;
    out->t14VmSplitRest = s_paintDbgT14VmSplitRest;
}

bool Win32_LegacyStacked_GetT14VmSplitActive(void)
{
    return s_paintDbgT14VmSplitActive;
}

int Win32_LegacyStacked_GetT17DocYRestScroll(void)
{
    return s_paintDbgT17DocYRestScroll;
}

void Win32_LegacyStacked_SetT14VmSplitActive(bool active)
{
    s_paintDbgT14VmSplitActive = active;
}

void Win32_LegacyStacked_SetRestViewportTopPx(int px)
{
    s_paintDbgRestViewportTopPx = px;
}

bool Win32_LegacyStacked_IsPaintLayoutMetricsFromPaintValid(void)
{
    return s_paintDbgLayoutMetricsFromPaintValid;
}

void Win32_LegacyStacked_ClearPaintLayoutMetricsFromPaintValid(void)
{
    s_paintDbgLayoutMetricsFromPaintValid = false;
}
