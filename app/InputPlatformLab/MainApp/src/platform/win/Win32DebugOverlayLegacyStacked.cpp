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

// vmSplit 確定時の MainApp extern への最小 write（ComputeLayoutMetrics はここ経由のみ）
static void Win32_LegacyStacked_ApplyMainAppPaintDbgVmSplitScratchOutToExtern(
    const Win32_LegacyStacked_VmSplitScratchPassOut& vsp)
{
    s_paintDbgT17DocY = vsp.t17DocY;
    if (vsp.t14LayoutOutValid)
    {
        s_paintDbgT14VisibleModesDocStartY = vsp.t14VisibleModesDocStartY;
        s_paintDbgT14LayoutValid = vsp.t14LayoutValid;
    }
}

void Win32_LegacyStacked_ApplyVmSplitMainAppExternFromScratchPass(
    const Win32_LegacyStacked_VmSplitScratchPassOut& vsp)
{
    Win32_LegacyStacked_ApplyMainAppPaintDbgVmSplitScratchOutToExtern(vsp);
}

void Win32_LegacyStacked_LoadMainAppPaintDbgRead(Win32_LegacyStacked_MainAppPaintDbgRead* out)
{
    if (!out)
    {
        return;
    }
    out->scrollY = s_paintScrollY;
    out->restViewportClientH = s_paintDbgRestViewportClientH;
    out->contentHeight = s_paintDbgContentHeight;
    out->t17DocY = s_paintDbgT17DocY;
    out->clientHeight = s_paintDbgClientHeight;
    out->maxScroll = s_paintDbgMaxScroll;
    out->layoutRestVpBudgetHint = s_paintDbgLayoutRestVpBudgetHint;
    out->scrollBandReservePx = s_paintDbgScrollBandReservePx;
    out->contentHeightBase = s_paintDbgContentHeightBase;
    out->extraBottomPadding = s_paintDbgExtraBottomPadding;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgResetProvisionalLayoutExtras(void)
{
    s_paintDbgLayoutRestVpBudgetHint = -1;
    s_paintDbgScrollBandReservePx = 0;
    s_paintDbgT14LayoutValid = false;
    s_paintDbgActualOverlayHeight = 0;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgClearScrollBandReserve(void)
{
    s_paintDbgScrollBandReservePx = 0;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgRestViewportClientH(int scrollVpH)
{
    s_paintDbgRestViewportClientH = scrollVpH;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgT14ColumnInit(int t14BaseY)
{
    s_paintDbgT14LayoutValid = false;
    s_paintDbgT14ColumnBaseY = t14BaseY;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgT14VisibleModesStart(int visibleModesDocStartY)
{
    s_paintDbgT14VisibleModesDocStartY = visibleModesDocStartY;
    s_paintDbgT14LayoutValid = true;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgT17DocY(int t17DocY)
{
    s_paintDbgT17DocY = t17DocY;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgContentAndClientGeometry(
    const Win32_LegacyStacked_MainAppPaintDbgApplyContentClient& in)
{
    s_paintDbgContentHeight = in.contentHeight;
    s_paintDbgContentHeightBase = in.contentHeightBase;
    s_paintDbgExtraBottomPadding = in.extraBottomPadding;
    s_paintDbgClientHeight = in.clientHeight;
    s_paintDbgClientW = in.clientW;
    s_paintDbgClientH = in.clientH;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgScrollBandReservePx(int reservePx)
{
    s_paintDbgScrollBandReservePx = reservePx;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgVmSplitContentPadding(int contentHeight, int extraBottomPadding)
{
    s_paintDbgContentHeight = contentHeight;
    s_paintDbgExtraBottomPadding = extraBottomPadding;
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgPostOverlayMeasures(
    bool vmSplitActive, int actualOverlayHeight, int clientH)
{
    s_paintDbgActualOverlayHeight = actualOverlayHeight;
    if (!vmSplitActive)
    {
        s_paintDbgScrollBandReservePx = (std::min)(actualOverlayHeight, clientH);
        Win32_LegacyStacked_ClearScratchRestViewportTop();
    }
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgT14BudgetHeights(
    int totalBeforeVisible, int hVmLines, int contentHeightBase)
{
    s_paintDbgT14BeforeVisibleDocH = totalBeforeVisible;
    s_paintDbgT14VisibleBlockDocH = hVmLines;
    s_paintDbgT14AfterVisibleDocH =
        (std::max)(0, contentHeightBase - totalBeforeVisible - hVmLines);
}

void Win32_LegacyStacked_ApplyMainAppPaintDbgLayoutRestVpBudgetHint(int hint)
{
    s_paintDbgLayoutRestVpBudgetHint = hint;
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
        Win32_LegacyStacked_ApplyMainAppPaintDbgScrollLineMetrics(s_paintScrollLinePx);
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

// -----------------------------------------------------------------------------
// Legacy stacked HUD: CALCRECT/T60 helpers + ComputeLayoutMetrics + PaintStackedLegacy (moved from Win32DebugOverlay.cpp).
// -----------------------------------------------------------------------------
#ifndef WIN32_MAIN_T17_JUMP_TOP_MARGIN
#define WIN32_MAIN_T17_JUMP_TOP_MARGIN 160
#endif
#ifndef WIN32_OVERLAY_T37_MENU_TOP_GAP_PX
#define WIN32_OVERLAY_T37_MENU_TOP_GAP_PX 52
#endif
#ifndef WIN32_HUD_DBG_FINAL_ROW1_BOTTOM_GAP_PX
#define WIN32_HUD_DBG_FINAL_ROW1_BOTTOM_GAP_PX 8
#endif
#ifndef WIN32_HUD_DBG_FINAL_ROW2_TO_BODY_EXTRA_GAP_PX
#define WIN32_HUD_DBG_FINAL_ROW2_TO_BODY_EXTRA_GAP_PX 28
#endif
#ifndef WIN32_HUD_DBG_MENU_TO_T14_GAP_PX
#define WIN32_HUD_DBG_MENU_TO_T14_GAP_PX 8
#endif
#ifndef WIN32_OVERLAY_MIN_BODY_VIEWPORT_PX
#define WIN32_OVERLAY_MIN_BODY_VIEWPORT_PX 64
#endif
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


// -----------------------------------------------------------------------------
// Legacy stacked HUD — cohesive pipeline for macro WIN32_HUD_USE_PAGED_HUD=0 (see docs §2.2, §7.1, §7.6).
// Implementations: Win32_DebugOverlay_ComputeLayoutMetrics, Win32_DebugOverlay_PaintStackedLegacy; public-side adapters: RunComputeLayoutMetrics, RunGdiPaint.
// Depends on: shared CALCRECT/T60 helpers above; MainApp.cpp extern s_paint*; legacy scratch/helpers in Win32DebugOverlayLegacyStacked.cpp; Win32_FillMenuSamplePaintBuffers via ComputeLayoutMetrics.
// PrefillHudLeftColumnForD2d (D2D region below) calls ComputeLayoutMetrics when legacy — same extraction bundle in practice.
// Implementations live in this TU (after CALCRECT/T60 helpers below).
// -----------------------------------------------------------------------------
#pragma region Legacy stacked HUD (ComputeLayoutMetrics + PaintStackedLegacy)

// Legacy: Win32_DebugOverlay_PrefillHudLeftColumnForD2d（!Win32_HudPaged_IsEnabled() 時）および
// Win32_DebugOverlay_PaintStackedLegacy からのみ呼ばれる。ページ式 HUD 既定時は呼ばれない（Win32_HudPaged_PrefillD2d）。
// スクロール・[scroll] 帯の高さ・T17 行位置などを計測。outHud 非 null のときは D2D final HUD 用に左列全文（menu+t14）とスクロール値を書き込む。
//
// Side-effect map (categories; not every assignment): ① file-top scratch (ApplyScratch* / ResetVmSplit* / ClearScratchRestViewportTop helpers), ② MainApp extern s_paintDbg* / line height（vmSplit: ApplyMainAppPaintDbgVmSplitScratchOutToExtern / ApplyVmSplitMainAppExternFromScratchPass; scroll line: ApplyScrollLineMetricsFromHdc; maxScroll+clamp: SetDbgMaxScrollAndClampScrollY; 薄い write 束: ApplyMainAppPaintDbgContentAndClientGeometry / ApplyMainAppPaintDbgPostOverlayMeasures 等）,
// ③ [scroll] / ScrollLog 用の MainApp スナップショットは LoadMainAppPaintDbgRead（本体では s_paintDbg* を直接読まない）,
// ④ outHud dbgHud* when non-null (via Win32_LegacyStacked_ApplyD2dHudPrefill), ⑤ unified clamp+T45 (RunUnifiedMaxScrollClampAndT45) → T46 inside T45; MarkPaintLayoutMetricsFromPaintValid (T52).  HUD_LEGACY_CODE_DEPENDENCY.md §7.7
static void Win32_DebugOverlay_ComputeLayoutMetrics(const Win32_LegacyStacked_LayoutMetricsParams& p)
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
    Win32_LegacyStacked_ApplyMainAppPaintDbgClearScrollBandReserve();
    Win32_LegacyStacked_ClearScratchRestViewportTop();

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
        Win32_MainWindow_IsFillMonitorPresentationMode(hwnd) &&
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
    if (!Win32_MainWindow_IsFillMonitorPresentationMode(hwnd) && clientH <= WIN32_OVERLAY_T59_WINDOWED_COMPACT_CLIENT_H)
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
    Win32_LegacyStacked_ApplyScratchFinalHudGeometry(bodyTopPx, t14DocTopAbsPx, row1H, row2TopPx);

    const int baseContentH = static_cast<int>(rcT14Doc.bottom) + t37TopGap;

    Win32_LegacyStacked_ResetVmSplitScratchFlags();
    Win32_LegacyStacked_ApplyMainAppPaintDbgRestViewportClientH(clientH);

    const wchar_t* pVis = wcsstr(t14Buf, L"visible modes:\r\n");
    const wchar_t* pT15 = wcsstr(t14Buf, L"\r\n--- T15 nearest resolution ---");
    const bool vmSplit = (pVis != nullptr && pT15 != nullptr && pT15 > pVis);

    const int t14BaseY = static_cast<int>(rcT14Doc.top) + t37TopGap;
    Win32_LegacyStacked_ApplyMainAppPaintDbgT14ColumnInit(t14BaseY);

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
        Win32_LegacyStacked_SetT14VmSplitActive(true);

        Win32_LegacyStacked_VmSplitScratchPassOut vsp{};
        if (Win32_LegacyStacked_RunVmSplitScratchPass(
                hdc,
                clientW,
                clientH,
                row2TopPx,
                t14BaseY,
                t14Buf,
                pVis,
                pT15,
                &vsp))
        {
            splitHPrefix = vsp.splitHPrefix;
            splitHVmBand = vsp.splitHVmBand;
            splitHRest = vsp.splitHRest;
            splitRestTopPx = vsp.splitRestTopPx;
            splitRestVp = vsp.splitRestVp;
            extraBottomPadding = vsp.extraBottomPadding;
            contentHeight = vsp.contentHeight;
            maxScroll = vsp.maxScroll;
            t17DocY = vsp.t17DocY;
            Win32_LegacyStacked_ApplyVmSplitMainAppExternFromScratchPass(vsp);
        }
        else
        {
            vmSplitActive = false;
            Win32_LegacyStacked_SetT14VmSplitActive(false);
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
                Win32_LegacyStacked_ApplyMainAppPaintDbgT14VisibleModesStart(static_cast<int>(rcVm.bottom));
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
        Win32_LegacyStacked_ApplyMainAppPaintDbgT17DocY(t17DocY);

        const int maxScrollBeforePadding = (std::max)(0, baseContentH - clientH);
        // T56: fill-monitor でも Windowed と同様に T17 到達分のみ（仮想解像度は contentBudget 側で扱う）
        extraBottomPadding = (std::max)(0, t17DocY - maxScrollBeforePadding);
        contentHeight = baseContentH + extraBottomPadding;
        maxScroll = (std::max)(0, contentHeight - clientH);
    }

    Win32_LegacyStacked_ApplyScrollLineMetricsFromHdc(hdc);

    {
        Win32_LegacyStacked_MainAppPaintDbgApplyContentClient cc{};
        cc.contentHeight = contentHeight;
        cc.contentHeightBase = vmSplitActive ? splitHRest : baseContentH;
        cc.extraBottomPadding = extraBottomPadding;
        cc.clientHeight = clientH;
        cc.clientW = clientW;
        cc.clientH = clientH;
        Win32_LegacyStacked_ApplyMainAppPaintDbgContentAndClientGeometry(cc);
    }

    const int scrollYBeforePaint = s_paintScrollY;
    Win32_LegacyStacked_SetDbgMaxScrollAndClampScrollY(maxScroll, L"ComputeLayoutMetrics.phase1");

    int jumpF7 = Win32DebugOverlay_ScrollTargetT17WithTopMargin();
    int jumpF8 = Win32DebugOverlay_ScrollTargetT17Centered(hwnd);
    wchar_t overlay[2048] = {};
    int actualOverlayHeight = 0;
    const bool compactScrollBand =
        (clientH < WIN32_OVERLAY_T49_SCROLL_COMPACT_CLIENT_H) ||
        (vmSplitActive && splitRestVp > 0 && splitRestVp < WIN32_OVERLAY_T51_COMPACT_SCROLL_RESTVP_PX) ||
        Win32_MainWindow_IsFillMonitorPresentationMode(hwnd);

    if (vmSplitActive)
    {
        Win32_LegacyStacked_MainAppPaintDbgRead provMa{};
        Win32_LegacyStacked_LoadMainAppPaintDbgRead(&provMa);
        Win32_DebugOverlay_FormatScrollDebugOverlay(
            overlay,
            _countof(overlay),
            t17ModeLabelForOverlay,
            provMa.contentHeightBase,
            provMa.extraBottomPadding,
            provMa.contentHeight,
            provMa.t17DocY,
            provMa.scrollY,
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
        if (Win32_MainWindow_IsFillMonitorPresentationMode(hwnd) &&
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

        Win32_LegacyStacked_SetRestViewportTopPx(splitRestTopPxEff);
        Win32_LegacyStacked_ApplyMainAppPaintDbgScrollBandReservePx(overlayReserve);

        {
            const int maxScrollBeforePaddingRest2 = (std::max)(0, splitHRest - restVp2);
            int extra2 = 0;
            extra2 = (std::max)(
                0, Win32_LegacyStacked_GetT17DocYRestScroll() - maxScrollBeforePaddingRest2);
            const int contentH2 = splitHRest + extra2;
            const int maxScroll2 = (std::max)(0, contentH2 - restVp2);
            Win32_LegacyStacked_SetDbgMaxScrollAndClampScrollY(maxScroll2, L"ComputeLayoutMetrics.vmSplitRefine");
            Win32_LegacyStacked_ApplyMainAppPaintDbgVmSplitContentPadding(contentH2, extra2);
            maxScroll = maxScroll2;
            contentHeight = contentH2;
            extraBottomPadding = extra2;
        }
        Win32_LegacyStacked_ApplyMainAppPaintDbgRestViewportClientH(restVp2);

        if (!layoutRefilledForBudget && restVp2 < WIN32_OVERLAY_T51_REFILL_RESTVP_PX)
        {
            // T55: fill-monitor では原則 refill しない。T57: Fullscreen のみ restVp が極小のとき短縮バッファで prefix/vm 帯を抑え scrollVpH を回復
            const bool fill = Win32_MainWindow_IsFillMonitorPresentationMode(hwnd);
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
    Win32_LegacyStacked_RunUnifiedMaxScrollClampAndT45(
        hwnd,
        {scrollContentHFinal, scrollViewportHFinal, maxScrollUnified});

    // T54: provisional restVpBudgetHint で短縮した本文が、確定レイアウトでは不要になったら通常 budget で再生成（最大 1 回）
    if (!t54ReExpanded && !Win32_MainWindow_IsFillMonitorPresentationMode(hwnd) && restVpBudgetHint >= 0)
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

    Win32_LegacyStacked_MarkPaintLayoutMetricsFromPaintValid();

    Win32_LegacyStacked_MainAppPaintDbgRead postUnifiedMa{};
    Win32_LegacyStacked_LoadMainAppPaintDbgRead(&postUnifiedMa);

    if (logScroll)
    {
        Win32DebugOverlay_ScrollLog(
            L"WM_PAINT after SetScrollInfo",
            hwnd,
            scrollYBeforePaint,
            postUnifiedMa.scrollY,
            postUnifiedMa.contentHeight,
            postUnifiedMa.t17DocY,
            vmSplitActive ? splitHRest : baseContentH,
            postUnifiedMa.extraBottomPadding);
    }

    jumpF7 = Win32DebugOverlay_ScrollTargetT17WithTopMargin();
    jumpF8 = Win32DebugOverlay_ScrollTargetT17Centered(hwnd);
    Win32_DebugOverlay_FormatScrollDebugOverlay(
        overlay,
        _countof(overlay),
        t17ModeLabelForOverlay,
        postUnifiedMa.contentHeightBase,
        postUnifiedMa.extraBottomPadding,
        postUnifiedMa.contentHeight,
        postUnifiedMa.t17DocY,
        postUnifiedMa.scrollY,
        postUnifiedMa.clientHeight,
        postUnifiedMa.restViewportClientH,
        jumpF7,
        jumpF8,
        false,
        compactScrollBand);
    actualOverlayHeight =
        Win32_MainView_MeasureScrollOverlayTextHeight(hdc, clientW, overlay);

    Win32_LegacyStacked_ApplyMainAppPaintDbgPostOverlayMeasures(
        vmSplitActive, actualOverlayHeight, clientH);

    Win32_LegacyStacked_ApplyScratchT53ScrollBandDrawEnabled(hwnd, restVpBudgetHint, clientH);

    Win32_LegacyStacked_MainLayoutScratchRead legLayoutScratch{};
    Win32_LegacyStacked_LoadLayoutScratchRead(&legLayoutScratch);

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
            legLayoutScratch.t53ScrollBandDrawEnabled,
            s_paintScrollY,
            s_paintDbgScrollBandReservePx,
            legLayoutScratch.restViewportTopPx);
    }

    {
        const int row1H_log = legLayoutScratch.finalRow1HeightPx;
        const int row2H_log = legLayoutScratch.row2TopPx - row1H_log;
        const int prefixTop_log = legLayoutScratch.row2TopPx + t14BaseY;
        const int vmTop_log =
            legLayoutScratch.t14VmSplitActive ? (prefixTop_log + splitHPrefix) : 0;
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

        Win32_LegacyStacked_ApplyMainAppPaintDbgT14BudgetHeights(
            totalBeforeVisible, hVmLines, s_paintDbgContentHeightBase);

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
            legLayoutScratch.restViewportTopPx,
            s_paintDbgScrollBandReservePx,
            s_paintDbgRestViewportClientH,
            s_paintDbgContentHeight,
            s_paintDbgMaxScroll,
            Win32_MainWindow_IsFillMonitorPresentationMode(hwnd) ? 1 : 0,
            Win32_MainWindow_GetPresentationModeLabelForDebug());
        OutputDebugStringW(t57);
    }

    Win32_LegacyStacked_ApplyMainAppPaintDbgLayoutRestVpBudgetHint(restVpBudgetHint);
}

// Side-effect exit adapter: every call site that needs layout metrics (Prefill + first pass inside GDI paint) funnels here (docs §7.6).
void Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics(const Win32_LegacyStacked_LayoutMetricsParams& p)
{
    Win32_DebugOverlay_ComputeLayoutMetrics(p);
}

// Legacy stacked HUD — WIN32_HUD_USE_PAGED_HUD=0 の GDI 本文（メニュー + T14〜T18 縦積み + 下端 [scroll]）。
// Win32DebugOverlay_Paint はページ式を先に分岐し、本関数は互換経路のみ。
// Side-effect map: persistent writes go through RunComputeLayoutMetrics → ComputeLayoutMetrics; this function adds GDI raster ops only (§7.7 category E).
static void Win32_DebugOverlay_PaintStackedLegacy(const Win32_LegacyStacked_GdiPaintParams& p)
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

    Win32_LegacyStacked_MainLayoutScratchRead legLayoutScratch{};
    Win32_LegacyStacked_LoadLayoutScratchRead(&legLayoutScratch);

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
        Win32_MainWindow_IsFillMonitorPresentationMode(hwnd) &&
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
    Win32_LegacyStacked_MainAppPaintDbgRead mainAppRead{};
    Win32_LegacyStacked_LoadMainAppPaintDbgRead(&mainAppRead);
    const bool compactScrollBandPaint =
        (clientH < WIN32_OVERLAY_T49_SCROLL_COMPACT_CLIENT_H) ||
        (legLayoutScratch.t14VmSplitActive && mainAppRead.restViewportClientH > 0 &&
         mainAppRead.restViewportClientH < WIN32_OVERLAY_T51_COMPACT_SCROLL_RESTVP_PX) ||
        Win32_MainWindow_IsFillMonitorPresentationMode(hwnd);
    Win32_DebugOverlay_FormatScrollDebugOverlay(
        overlay,
        _countof(overlay),
        t17ModeLabelForOverlay,
        mainAppRead.contentHeightBase,
        mainAppRead.extraBottomPadding,
        mainAppRead.contentHeight,
        mainAppRead.t17DocY,
        mainAppRead.scrollY,
        mainAppRead.clientHeight,
        mainAppRead.restViewportClientH,
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
        const int scrollBandReserve = mainAppRead.scrollBandReservePx;
        rcClipMain.bottom =
            (std::max)(rcClipMain.top, rcClipMain.bottom - scrollBandReserve);
    }

    const int bodyTopPx = legLayoutScratch.finalBodyTopPx;
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
            if (legLayoutScratch.t14VmSplitActive)
            {
                const int t14TextStartPx = legLayoutScratch.row2TopPx + legLayoutScratch.bodyT14DocTopPx;
                const int restTopNatural =
                    t14TextStartPx + legLayoutScratch.t14VmSplitPrefixH + legLayoutScratch.t14VmSplitVmBandH;
                const int restTopPx =
                    (legLayoutScratch.restViewportTopPx > 0) ? legLayoutScratch.restViewportTopPx
                                                             : restTopNatural;
                RECT rcPrefDraw = rcT14Draw;
                rcPrefDraw.top = t14TextStartPx;
                rcPrefDraw.bottom = (std::min)(
                    static_cast<int>(rcPrefDraw.top) + legLayoutScratch.t14VmSplitPrefixH + 4, restTopPx);
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
                        legLayoutScratch.t14VmSplitPrefix,
                        -1,
                        &rcPrefDraw,
                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
                    RestoreDC(hdc, savedPref);
                }

                RECT rcVmDraw = rcT14Draw;
                rcVmDraw.top = t14TextStartPx + legLayoutScratch.t14VmSplitPrefixH;
                rcVmDraw.bottom = (std::min)(
                    static_cast<int>(rcVmDraw.top) + legLayoutScratch.t14VmSplitVmBandH + 4, restTopPx);
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
                        legLayoutScratch.t14VmSplitVmBand,
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
                OffsetViewportOrgEx(hdc, 0, -mainAppRead.scrollY, nullptr);
                RECT rcRestDraw = rcT14Draw;
                rcRestDraw.top = restTopPx;
                rcRestDraw.bottom = rcRestDraw.top + 1000000;
                DrawTextW(
                    hdc,
                    legLayoutScratch.t14VmSplitRest,
                    -1,
                    &rcRestDraw,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
                RestoreDC(hdc, savedRest);
            }
            else
            {
                OffsetViewportOrgEx(hdc, 0, -mainAppRead.scrollY, nullptr);
                DrawTextW(
                    hdc,
                    t14Buf,
                    -1,
                    &rcT14Draw,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
            }
        }
        RestoreDC(hdc, saved);

        const int row1H = legLayoutScratch.finalRow1HeightPx;
        const int row2TopPx = legLayoutScratch.row2TopPx;
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

    if (!skipScrollBandGdi && legLayoutScratch.t53ScrollBandDrawEnabled)
    {
        RECT rcOv = rcClient;
        const int scrollBandReserve = mainAppRead.scrollBandReservePx;
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

void Win32_DebugOverlay_LegacyStacked_RunGdiPaintFromPaintEntry(
    HWND hwnd,
    HDC hdc,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel,
    bool suppressT14BodyGdi,
    bool skipMenuColumnGdi,
    bool skipScrollBandGdi)
{
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

void Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetricsForD2dPrefill(
    HWND hwnd,
    HDC hdc,
    WindowsRendererState* outHud,
    const wchar_t* t17ModeLabelForOverlay,
    const wchar_t* t17CandLabel,
    const wchar_t* t17ActLabel)
{
    Win32_LegacyStacked_LayoutMetricsParams lmPrefill{};
    lmPrefill.common.hwnd = hwnd;
    lmPrefill.common.hdc = hdc;
    lmPrefill.common.t17ModeLabelForOverlay = t17ModeLabelForOverlay != nullptr ? t17ModeLabelForOverlay : L"?";
    lmPrefill.common.t17CandLabel = t17CandLabel != nullptr ? t17CandLabel : L"?";
    lmPrefill.common.t17ActLabel = t17ActLabel != nullptr ? t17ActLabel : L"?";
    lmPrefill.outHud = outHud;
    lmPrefill.logScroll = false;
    Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics(lmPrefill);
}

#pragma endregion
