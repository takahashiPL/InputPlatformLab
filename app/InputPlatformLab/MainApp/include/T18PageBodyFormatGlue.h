// T18 paged HUD: compact bind/resolve labels, 2P-4P one-liners, main body swprintf bundle.
// Post-foundation step6: app/debug display glue only (not platform/win, not input/core).
#pragma once

#include "PlayerInputGuideTypes.h"

void T18PageBodyFormat_CompactBindResolveStatus(const wchar_t* full, wchar_t* out, size_t outCount);
void T18PageBodyFormat_FormatExtraPlayerOneLine(PlayerInputSlotIndex slot, wchar_t* out, size_t outCount);

void T18PageBodyFormat_BuildXInputSlotDisplay(int xinputSlot, wchar_t* out, size_t outCount);
void T18PageBodyFormat_BuildVidPidShortLine(bool hidFound, unsigned vid, unsigned pid, wchar_t* out, size_t outCount);

// All string pointers must be non-null where used in the format (use L"" for empty).
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
    const wchar_t* line4p);
