# T77 foundation close（step24）

**Date**: 2026-04-13  
**Status**: T76 closed; T77 through step23 assumed done. **Foundation may stop here (Stop is OK).** If continuing, resume from one **Go** item below.

## What is settled

- **T76**: T18 device inventory vs effective guide owner; arbiter spine on the primary (1P) stream (`EffectiveInputGuideArbiter`). Full multi-seat production use is out of scope.
- **T77**: **Single live consume slot** (default 1P / slot0). **Slot-indexed live trial target** (default 2P). Debug-only F10 armed / F11 target cycle; F8/F9 affect the trial slot. Verified for **2P/3P/4P/none**: keyboard-bound promotion to live, non-keyboard stays dry-run; observables include `·tr=`, `lv=`, `nkb` (step23).
- **Default behaviour**: Normally only slot0 consumes live. Trial is **Debug-only**; default target remains 2P; **do not change Release defaults** as a rule.
- **Paged HUD**: T19/T20 accepted paths remain canonical. Legacy stacked HUD split phase is **closed — do not reopen**.

## Not done / intentionally deferred

| Item | Class |
|------|-------|
| slot2+ production routing | **Not started / defer** |
| Multi-player owner arbitration (complete) | **Not started / defer** |
| Guide family production polish (incl. “live complete”) | **Not started / defer** |
| Auto assign / rebind UI / persistence | **Not started / defer** |
| Keyboard split across multiple seats | **Not started / defer** |
| Production UI / settings affordances | **Not started / defer** |

## Stop / Go

- **Stop (recommended milestone)**: The items under **What is settled** are enough to **close the input foundation** scope. Outstanding work is explicitly **out of scope**; **stopping here does not invalidate** the codebase.
- **Go (if continuing)**: Pick **one** primary thread next (at most two candidates; implement one at a time).

  1. Move the slot-indexed trial toward **production-like routing** (incremental; keep defaults unchanged).
  2. **Multi-player owner / binding** production design (fix arbiter contracts and UI assumptions first; large refactors stay a separate phase).

## References

- Code: `EffectiveInputGuideArbiter.*`, `PlayerInputSlots.h`, `PlayerInputGuideTypes.h`
- Log: [worklog.md](worklog.md)
