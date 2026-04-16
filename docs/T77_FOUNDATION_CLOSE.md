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

## Current next-step framing（2026-04-16）

**Foundation close は維持**したまま、次に扱うとしても **実装ではなく条件整理**に留める。

- **2P=Keyboard** は現行受け入れ済みの通常 live。
- **2P=XInput0** は現行受け入れ済みとして **dry-run 維持**。
- **単一 pad 環境では `2P=XInput0` を normal live に上げない。**
- **複数 pad かつ別 identity** が成立する場合だけ、将来の再検討候補とする。
- ここでは **Release defaults / 1P owner-guide / trial-debug semantics** を変えない。

## Pre-branch freeze check（readiness）

**As of 2026-04-13** — no code or default-behavior changes in this step; documentation alignment only.

| Check | Result |
|-------|--------|
| foundation docs vs each other | [architecture.md](architecture.md)（layer / Debug / Pack-out）と本ノートの **default slot0 live・Debug trial・defer** は矛盾なし。[roadmap.md](roadmap.md) は本ノートを正と明記。 |
| Debug-only (F8–F11, trial logs, `·tr=`) | architecture §Debug-only：`_DEBUG` / 非本番 consume 経路と読める。 |
| reusable / platform / app / generated | architecture §Pack-out と [.gitignore](../.gitignore) で区別可能。 |
| T19/T20 / paged HUD | 本ノート・roadmap：ページ式が正、legacy 分離は closed。 |

**Frozen baseline for branch/tag**: T76/T77 foundation as documented here + architecture layer/pack-out notes + worklog entries on 2026-04-13. **Out of scope** until a new branch: defer table above.

**Suggested names** (team convention wins):

| Kind | Example |
|------|---------|
| Tag (snapshot) | `t77-foundation-close-2026-04-13` |
| Branch from freeze | `milestone/t77-foundation-close` or `main` にマージ後の `develop` |
| Next work branch | `feature/post-t77-foundation` (first **Go** item in close note; rename to match scope) |

## References

- Layers / reuse / Debug: [architecture.md](architecture.md)（入力 foundation の整理、Pack-out）
- Code: `EffectiveInputGuideArbiter.*`, `PlayerInputSlots.h`, `PlayerInputGuideTypes.h`
- Log: [worklog.md](worklog.md)
