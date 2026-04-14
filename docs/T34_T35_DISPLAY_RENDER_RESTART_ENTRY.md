# T34 / T35 表示モード・レンダラ文書軸 — 再開入口

本書は **入力 foundation（T76/T77）とは別軸** の、**表示モード・D3D/D2D/GDI レンダラ** まわりの docs を読み直すときの **入口** である。実装手順や変更提案は書かない。

---

## 1. 何のための文書か

- **roadmap.md 第 1 表**（T34 完了・T35 方針固定）に対応する **既存ドキュメントの地図** を 1 枚にする。
- 次のセッションで「どれを一次情報として読むか」「まだ触らない論点は何か」を **すぐ決められる** ようにする。

---

## 2. 今どこまで確定しているか（読みの固定）

| 論点 | 一次情報（本リポジトリ） |
|------|---------------------------|
| **T34（Borderless のオフスクリーン committed 経路）** | **完了・維持** — [t34_t35_display_and_render.md](t34_t35_display_and_render.md) |
| **T35（Windowed / Borderless / Fullscreen の window・swapchain・offscreen・Present・GDI）** | **方針固定** — [t35_display_mode_policy.md](t35_display_mode_policy.md) |
| **T17 ログ（targetPhys / client / outer）と T34 committed の関係** | **別軸**（混同しない）— 上記両文書 §3 相当 |
| **1 フレームの入口** | **`WM_PAINT` → `Win32_MainView_PaintFrame`** — [t35_display_mode_policy.md](t35_display_mode_policy.md) §1、詳細の危険線・T19/T20 は [WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md](WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md) / [HUD_PAGED_ACCEPTANCE.md](HUD_PAGED_ACCEPTANCE.md) |

---

## 3. 何が未決か（別タスク・本入口では決めない）

- [t35_display_mode_policy.md](t35_display_mode_policy.md) **§5** に列挙のとおり: T17 と committed の **ログ上の揃え**、**Fullscreen の committed オフスクリーン** の本格化、**GDI 仮想解像度**、**T36 を正式に昇格するか** 等。
- **decisions.md**・**worklog.md** に依存する細部は、表示タスク側で都度参照する。

---

## 4. 何はまだ触らないか（この軸の docs 先行で）

- **`WM_INPUT` / `WM_TIMER` / `InvalidateRect` の意味や T19/T20 accepted の再定義**（ページ式 HUD の一次情報を上書きしない）。
- **T77 Go(1)/Go(2)** の実装議論（[T77_FOUNDATION_CLOSE.md](T77_FOUNDATION_CLOSE.md) と混線させない）。
- **レンダラ実装のリファクタ・新機能**（本入口は **判断材料** のみ）。

---

## 5. 読む順（推奨）

1. [roadmap.md](roadmap.md) 第 1 表  
2. [t34_t35_display_and_render.md](t34_t35_display_and_render.md)（T34 要約）  
3. [t35_display_mode_policy.md](t35_display_mode_policy.md)（T35 本体）  
4. 必要なら [architecture.md](architecture.md) の `platform/win`・レンダラ言及、[NEXT_THEME_RESTART_ENTRY.md](NEXT_THEME_RESTART_ENTRY.md)（入力側の再開はここで終わらない）