// T18 paged HUD body formatting (post-foundation step6). Behavior matches prior MainApp.cpp.

#include "framework.h"
#include "T18PageBodyFormatGlue.h"

#include "EffectiveInputGuideArbiter.h"

// =============================================================================
// compact status / short label builders
// =============================================================================

void T18PageBodyFormat_CompactBindResolveStatus(const wchar_t* full, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }
    out[0] = L'\0';
    if (!full || full[0] == L'\0')
    {
        wcscpy_s(out, outCount, L"-");
        return;
    }
    if (wcscmp(full, L"locked,present") == 0)
    {
        wcscpy_s(out, outCount, L"+");
        return;
    }
    if (wcscmp(full, L"locked,absent") == 0)
    {
        wcscpy_s(out, outCount, L"abs");
        return;
    }
    if (wcscmp(full, L"open") == 0)
    {
        wcscpy_s(out, outCount, L"op");
        return;
    }
    if (wcscmp(full, L"none") == 0)
    {
        wcscpy_s(out, outCount, L"-");
        return;
    }
    if (wcscmp(full, L"unresolved") == 0)
    {
        wcscpy_s(out, outCount, L"?");
        return;
    }
    wcsncpy_s(out, outCount, full, _TRUNCATE);
}

void T18PageBodyFormat_BuildXInputSlotDisplay(int xinputSlot, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }
    if (xinputSlot >= 0)
    {
        swprintf_s(out, outCount, L"%d", xinputSlot);
    }
    else
    {
        wcscpy_s(out, outCount, L"-");
    }
}

void T18PageBodyFormat_BuildVidPidShortLine(bool hidFound, unsigned vid, unsigned pid, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }
    if (hidFound)
    {
        swprintf_s(out, outCount, L"%04X/%04X", vid, pid);
    }
    else
    {
        wcscpy_s(out, outCount, L"--");
    }
}

// =============================================================================
// extra-player / debug one-line helpers
// =============================================================================

namespace
{

#if defined(_DEBUG)
void FormatSlot1DebugBindShortTag(wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    wchar_t dev[96] = {};
    InputGuideArbiter_FormatSlotBoundDeviceIdentityForT18(1u, dev, _countof(dev));
    if (_wcsicmp(dev, L"keyboard") == 0)
    {
        wcscpy_s(buf, bufCount, L"·bk=kb");
        return;
    }
    if (wcsstr(dev, L"XInput user 0") != nullptr)
    {
        wcscpy_s(buf, bufCount, L"·bk=xi0");
        return;
    }
    wcscpy_s(buf, bufCount, L"·bk=?");
}
#else
void FormatSlot1DebugBindShortTag(wchar_t*, size_t) {}
#endif

} // namespace

void T18PageBodyFormat_FormatExtraPlayerOneLine(PlayerInputSlotIndex slot, wchar_t* out, size_t outCount)
{
    if (!out || outCount == 0)
    {
        return;
    }
    out[0] = L'\0';
    if (!InputGuideArbiter_IsValidSlotIndex(slot) || slot == 0u)
    {
        return;
    }
    wchar_t bsrc[80] = {};
    wchar_t bdev[80] = {};
    wchar_t bst[80] = {};
    wchar_t rc[80] = {};
    wchar_t cp[56] = {};
    wchar_t cr[56] = {};
    InputGuideArbiter_FormatSlotBoundSourceForT18(slot, bsrc, _countof(bsrc));
    InputGuideArbiter_FormatSlotBoundDeviceIdentityForT18(slot, bdev, _countof(bdev));
    InputGuideArbiter_FormatSlotBindStatusForT18(slot, bst, _countof(bst));
    InputGuideArbiter_FormatSlotRouteCandidateForT18(slot, rc, _countof(rc));
    InputGuideArbiter_FormatSlotConsumePolicyForT18(slot, cp, _countof(cp));
    InputGuideArbiter_FormatSlotConsumeResultForT18(slot, cr, _countof(cr));
    wchar_t stc[16] = {};
    T18PageBodyFormat_CompactBindResolveStatus(bst, stc, _countof(stc));
    wchar_t t1suf[20] = {};
    wchar_t bkdbg[16] = {};
    InputGuideArbiter_FormatLiveTrialObsForT18(slot, t1suf, _countof(t1suf));
#if defined(_DEBUG)
    if (slot == 1u)
    {
        FormatSlot1DebugBindShortTag(bkdbg, _countof(bkdbg));
    }
#endif
    const unsigned pn = static_cast<unsigned>(slot) + 1u;
    swprintf_s(
        out,
        outCount,
        L"%uP b=%ls·%ls st=%ls r=%ls c=%ls/%ls%ls%ls",
        pn,
        bsrc,
        bdev,
        stc,
        rc,
        cp,
        cr,
        t1suf,
        bkdbg);
}

// =============================================================================
// paged HUD body builder
// =============================================================================

void T18PageBodyFormat_FillPagedHudBody(
    wchar_t* buf,
    size_t bufCount,
    const wchar_t* slotStr,
    bool hidFound,
    const wchar_t* vidPidLine,
    const wchar_t* familyLabel,
    const wchar_t* prodTiny,
    const wchar_t* parserLabel,
    const wchar_t* supportLabel,
    const wchar_t* whyOneLine,
    const wchar_t* boundSrcLine,
    const wchar_t* boundDevLine,
    const wchar_t* p0BindStatusCompact,
    const wchar_t* p0BindMatch,
    const wchar_t* routeCand0,
    const wchar_t* activeRouteMode,
    const wchar_t* routedSource,
    const wchar_t* ownerUiLabel,
    const wchar_t* guideFamilyLabel,
    const wchar_t* consumePol0,
    const wchar_t* consumeRes0,
    const wchar_t* lvTag,
    const wchar_t* trgSuf,
    const wchar_t* stagedIn0,
    const wchar_t* stagedLog0,
    const wchar_t* line2p,
    const wchar_t* line3p,
    const wchar_t* line4p)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    swprintf_s(
        buf,
        bufCount,
        L"inv xi=%ls hid=%ls vp=%ls fam=%ls prod=%ls\r\n"
        L"ctx %ls/%ls %ls\r\n"
        L"1P bound %ls|%ls\r\n"
        L"1P bind st=%ls mt=%ls\r\n"
        L"1P route cand=%ls | act=%ls | src=%ls\r\n"
        L"1P owner=%ls guide=%ls\r\n"
        L"1P consume pol=%ls res=%ls lv=%ls%ls src=stg0\r\n"
        L"1P staged in=%ls log=%ls\r\n"
        L"%ls\r\n"
        L"%ls\r\n"
        L"%ls\r\n",
        slotStr,
        hidFound ? L"y" : L"n",
        vidPidLine,
        familyLabel,
        prodTiny,
        parserLabel,
        supportLabel,
        whyOneLine,
        boundSrcLine,
        boundDevLine,
        p0BindStatusCompact,
        p0BindMatch,
        routeCand0,
        activeRouteMode,
        routedSource,
        ownerUiLabel,
        guideFamilyLabel,
        consumePol0,
        consumeRes0,
        lvTag,
        trgSuf,
        stagedIn0,
        stagedLog0,
        line2p,
        line3p,
        line4p);
}
