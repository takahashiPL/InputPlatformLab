# T35 §5 B — T36 （読み分け補助・結論なし）

**一次情報**: [t35_display_mode_policy.md](t35_display_mode_policy.md) §0・§2.3・§2.4・§5 （T36 関連箇条）。**方針変更や昇格の決定はしない**。

**親索引**: [T35_SECTION5_UNDECIDED_BACKLOG.md](T35_SECTION5_UNDECIDED_BACKLOG.md) の B。**`axis=offscreen`**: [T35_OBSERVABILITY_AXIS_READING_GUIDE.md](T35_OBSERVABILITY_AXIS_READING_GUIDE.md)。**補足**: [t34_t35_display_and_render.md](t34_t35_display_and_render.md) 冒頭（T34 は Borderless のみ）。

---

## T35 既定（Fullscreen）と T36 実験の違い

- **T35 既定**: バックバッファへの**直接描画**（§2.3 Offscreen 表）。
- **T36（実験）**: **committed が有効なときのみ**、**T14 committed サイズ**のオフスクリーン RT に描画しバックバッファへ**合成**（ログ `[T36][RT]`）。**失敗時は直接描画**。
- **§0**: Windowed では T34/T36 オフスクリーンは使わない。Fullscreen で T36 が committed オフスクリーンを試す（T34 とは別）。

## `axis=offscreen` は何を読むための束か

**`[T34][RT]` / `[T36][RT]`** のオフスクリーン経路の文脈としてログを読み分ける（§5 観測性段落・補助文書）。

## T34 と T36 が「別リソース」とはどういう読みか

- §2.3 は、T36 経路が **T34（Borderless）とは別リソース**と明記。
- §0 および t34 冒頭: **T34 は Borderless の正式オフスクリーン経路**、**T36 は Fullscreen の実験**。同じ committed 解像度の話でも**モードと経路が異なり RT も別**と読む。
- §2.4: **T14 committed と CDS は別概念**。ウィンドウ寸法は T15 最近傍 + CDS が主。**T36 は committed 解像度のオフスクリーンを試すレンダラ実験**で、**CDS / outer / client のロジックは変えない**。

## 今まだ未決のまま残すもの

- **T36 を正式方針に昇格するか**（§5・索引 B）。現状は Fullscreen のみの**実験**、`[T36][RT]`。
- **昇格するとしたときの中身・範囲**も未決（索引 B 行と同じ）。

---

## 本書の役割 / やらないこと

- **役割**: 読み分け補助・境界の明示・次回判断の材料に限定する。
- **やらない**: `WndProc` / `WM_*` / `InvalidateRect` / T19/T20 accepted の意味変更、§5 の C の採否・優先順位、T36 昇格案・実装案・ログ変更案、T77・pack-out への横滑り。
