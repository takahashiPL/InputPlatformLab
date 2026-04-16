# T35 §5 — 未決バックログ索引（整理のみ・結論なし）

**役割**: [t35_display_mode_policy.md](t35_display_mode_policy.md) **§5** に置いた将来論点を、**方針決定せず**一覧・境界・参照に固定する。実装案・採否・優先順位は書かない。

**親**: [T34_T35_DISPLAY_RENDER_RESTART_ENTRY.md](T34_T35_DISPLAY_RENDER_RESTART_ENTRY.md) §3。

---

## 今回の進め方（T35 §5 A/D 判断同期）

- **先に A を読む**。
- **D は A の補助**として読む。
- **B/C は今回まだ未着手のまま残す**。
- 今回は **docs 上の判断同期まで**で止め、render policy 変更・実装変更には入らない。

## §5 一次情報の所在

- [t35_display_mode_policy.md](t35_display_mode_policy.md) §5（本索引の根拠文）
- [roadmap.md](roadmap.md) 第 1 表「保留事項」（§5 と同系の整理）

---

## 未決の一覧（§5 に対応）

| # | §5 での論点 | 現状どう読めるか | 未決のままのもの | 今は触らない境界 |
|---|------------|------------------|------------------|------------------|
| A | T17 ログと T14 committed 等の **数値上の揃え**（UI/ログ改善） | [t35_display_mode_policy.md](t35_display_mode_policy.md) §3 で **別軸**は固定。**A の読解結論としても、T17 logs=`axis=mode` / committed=`axis=committed` は固定**。揃えるかどうか・揃え方は §5 が **別タスク**と明示 | **やるか・どこまで揃えるか**は未決 | **ログ語の再定義で WM 連鎖や T19/T20 accepted を書き換えない**。入力・ページ HUD の一次情報へ横展開しない |
| B | **T36** を正式方針に昇格するか（Fullscreen の committed オフスクリーン実験 `[T36][RT]`） | [t35_display_mode_policy.md](t35_display_mode_policy.md) §0・§2.3・Fullscreen 節で **実験**として位置づけ済み | **昇格するか**は未決。本格化の中身も未決 | **T36 採否・設計の結論をここでは出さない**。T77・pack-out に接続しない |
| C | GDI を **仮想解像度に合わせてスケール**するか | §2 各モードの GDI 方針・§4 留意で **現在はクライアント基準**等が方針固定 | **スケールするか**・範囲は未決 | **`InvalidateRect` や `WM_PAINT` の意味を再定義して論じない** |
| D | **観測性**（`axis=mode` / `axis=committed` / `axis=offscreen` / `axis=final`） | §5 本文で **policy 変更ではない**と明記。混同時の読み分け用 | 実装・ログで **どこまで広げるか**は別タスクで都度 | **観測性の話題に T19/T20 accepted の意味改訂を混ぜない** |

---

## 参照すべき一次情報（判断・未決整理のとき）

- [t35_display_mode_policy.md](t35_display_mode_policy.md) **§0〜§4**（確定方針）
- [t34_t35_display_and_render.md](t34_t35_display_and_render.md)（T34 完了・T17 と committed の別軸の補足）
- [T34_T35_DISPLAY_RENDER_NEXT_SESSION_READING_ORDER.md](T34_T35_DISPLAY_RENDER_NEXT_SESSION_READING_ORDER.md)（読み順）
- 細部: **decisions.md**・**worklog.md**（再起動入口 §3）

**WndProc / メッセージの危険線**: [WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md](WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md)、[HUD_PAGED_ACCEPTANCE.md](HUD_PAGED_ACCEPTANCE.md) — **本索引では上書きしない**。

---

## 本書でやらないこと

- 優先順位付け・工数・採否の確定
- コード・リファクタ・新機能の設計
- `WM_INPUT` / `WM_TIMER` / `WM_PAINT` / `InvalidateRect` および T19/T20 accepted の意味変更
- T77・pack-out 軸への議論の横滑り
