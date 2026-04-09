# レガシー縦積み HUD — 分離フェーズの一区切り（close）

**状態**: **closed**（2026-04-06 時点）。通常運用は **ページ式 HUD**；本メモは **legacy stacked の TU 分離・文書化フェーズ**を締め、同じ論点の再掘りを減らすための **短い実務サマリ**である。詳細索引は `docs/HUD_LEGACY_CODE_DEPENDENCY.md`（特に **§8**）。

**制約**: `docs/HUD_PAGED_ACCEPTANCE.md` の **T19/T20** およびページ式受け入れを壊さないこと。レガシー縦積みを「通常運用の正」には戻さない。

---

## 1. 今回のフェーズで分離・整理できたもの

- **レガシー縦積みのパイプライン本体**（`ComputeLayoutMetrics` / `PaintStackedLegacy`、スクラッチの多く）を **`Win32DebugOverlayLegacyStacked.cpp`** へ移し、**`Win32DebugOverlayLegacyStacked_bridge.h`** / **`internal.h`** で main / legacy の境界を明示。
- **MainApp 共有 HUD** の `extern` 集約（`Win32MainAppPaintDbg_shared_link.h`）、**Load/Apply** 系への寄せ（挙動は維持したまま依存を読みやすく）。
- **文書**: 依存の棚卸し（本リポ `HUD_LEGACY_CODE_DEPENDENCY.md`）、保守優先（`HUD_LEGACY_MAINTENANCE_PRIORITIES.md`）、および **§8** 以降で **残留理由 → keep/conditional/risky → 条件付き移設の契約 → Go/No-Go** まで整理。

---

## 2. main TU（`Win32DebugOverlay.cpp`）に意図的に残したもの

- **共有オーバーレイ API**（`Win32DebugOverlay.h` 上の [scroll] / ScrollLog / ScrollTarget / Clamp / MainView_SetScrollPos 等）— **ページ式・レガシー双方**が同じシンボルを参照する **言語の置き場**。
- **T45 本体**と **InvokeT45**（legacy → main の 1 ホップ）、**T46 スナップショット**（`FormatScroll` / `ScrollLog` と同居）、**fill-monitor 判定の入口**、**ResetProvisionalLayoutCache** のオーケストレーション。根拠は **`HUD_LEGACY_CODE_DEPENDENCY.md` §8.1**。

---

## 3. 今は触らないと決めたもの（defer / No-Go）

- **§8.3 B**（条件付きで移せる候補）のうち、**T45 本体の物理移設**は **当面 No-Go**。**T46** は **同一 TU 内 struct 化の次**まで **後回し**；**いきなり別 TU へ移すのは No-Go**。**FormatScroll のファイル分割**・**Reset 内訳の移動**も **当面 No-Go／後回し**。根拠は **`HUD_LEGACY_CODE_DEPENDENCY.md` §8.5**。
- **§8.3 C**（shared scroll 実装の本体、T45→T46 の順序いじり、T52 bridge 契約の変更）は **防御的・文書優先**のまま。

---

## 4. 次に本題として戻るべき方向

- **ページ式 HUD** の機能・受け入れ（`HUD_PAGED_ACCEPTANCE.md`）を **正**として進める。
- **レガシー縦積み**は **マクロ 0 の互換経路**として維持するが、**追加の分離ドキュメントや大規模 refac は当面不要**（必要になったら **§8** を起点に再開する）。

---

## 5. 関連ドキュメント（再掲・最短導線）

| 文書 | 用途 |
|------|------|
| `HUD_LEGACY_CODE_DEPENDENCY.md` | 依存索引・§8 系の詳細 |
| `HUD_LEGACY_MAINTENANCE_PRIORITIES.md` | 維持/削減/慎重の判断補助 |
| `HUD_PAGED_ACCEPTANCE.md` | ページ式受け入れ |
