# T34 / T35 — 次セッション用・一次情報の読み順（固定）

**親入口**: [T34_T35_DISPLAY_RENDER_RESTART_ENTRY.md](T34_T35_DISPLAY_RENDER_RESTART_ENTRY.md)

本書は **表示モード・レンダラ文書軸** で作業を再開するとき、**どの順で一次情報を通すかだけ**を固定する。実装・挙動変更・`WndProc` / メッセージの再定義は書かない。

---

## このセッションで通す順（上から1 本ずつ）

1. [roadmap.md](roadmap.md) **第 1 表**（T34 完了・T35 方針固定の位置づけだけ確認）
2. [t34_t35_display_and_render.md](t34_t35_display_and_render.md)（T34 要約・完了の読み）
3. [t35_display_mode_policy.md](t35_display_mode_policy.md) **§0〜§4**（Windowed / Borderless / Fullscreen・T17 と committed の別軸）
4. **§5** は **一覧として読むが決めない** — 未決項目の **在り処**の確認のみ（内容の可否は別タスク）

---

## 読了後にやらないこと（この軸の docs 先行で）

- `WM_INPUT` / `WM_TIMER` / `WM_PAINT` / `InvalidateRect` の意味や、T19/T20 accepted の再解釈（[WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md](WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md) / [HUD_PAGED_ACCEPTANCE.md](HUD_PAGED_ACCEPTANCE.md) を上書きしない）。
- T77・pack-out 軸への議論の横滑り。
- レンダラの新機能・リファクタ前提の設計書き起こし。

---

## 次に迷ったら

親入口の **§2（確定）** / **§3（未決）** / **§4（触らない）** に戻る。
