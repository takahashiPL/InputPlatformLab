# T35 §5 A — T17 ログと committed （読み分け補助・結論なし）

**一次情報**: [t35_display_mode_policy.md](t35_display_mode_policy.md) §3・§5 先頭箇条。**方針変更ではない**（未来の別タスクとして列挙される論点の整理のみ）。

**親索引**: [T35_SECTION5_UNDECIDED_BACKLOG.md](T35_SECTION5_UNDECIDED_BACKLOG.md) の A。**観測性補助**: [T35_OBSERVABILITY_AXIS_READING_GUIDE.md](T35_OBSERVABILITY_AXIS_READING_GUIDE.md)（`axis=mode` / `axis=committed`）。

---

## T17 のログは何の軸か

ウィンドウ再生成・fill 側の話。`targetPhys` は目標クライアント寸法、`client` / `outer` は実測の矩形（§3 表）。**T17 のウィンドウ話**として `axis=mode` で読み分ける。

## committed （T14 Enter 確定）は何の軸か

**T14 Enter で確定した幅・高さ**。オフスクリーン描画やグリッド分母の主たる入力（§3 表）。**T14 committed**として `axis=committed` で読み分ける。

## 「揃え」が意味すること / 意味しないこと

- **意味する**: §5 の表現どおり、将来の**別タスク**として、UI/ログ上で**数値を並べて分かりやすくする**改善の検討領域がある。
- **意味しない**: §3 の**別軸・責務分離**をやめることではない。Borderless で `targetPhys` と committed が違っても**正常**と読む固定（§3 文末）を変えない。数値が一致しない場面は**軸が違う**可能性を先に置く（§5 観測性段落・補助文書）。

## 今まだ未決のまま残すもの

- **やるか・どこまで揃えるか**（索引 A 行と同じ）。
- 具体的なログ文言・画面・出力場所は一次情報外で、**未決**。

---

## 本書の役割 / やらないこと

- **役割**: 読み分け補助・境界の明示・次回判断の材料に限定する。
- **やらない**: `WndProc` / `WM_*` / `InvalidateRect` / T19/T20 accepted の意味変更、§5 の B/C/D の採否・優先順位、実装案・ログ/UI 変更案。
