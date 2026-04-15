# 段階計画・見通し（roadmap）

本ファイルは、**今後どの順序で何を目指すか**の見通しを共有するための場所です。確定スケジュールや期限の管理ではなく、**段階的な整理・拡張の意図**を書き留めます。詳細な実装状況の一覧はここでは扱いません。

## 何を書くか

次のようなテーマを、抽象度を揃えて並べる用途を想定しています。

- **再利用しやすさの改善**（境界の明確化、依存の整理）
- **新プラットフォーム追加の準備**（ディレクトリ・ビルド・インターフェースの方針）
- **描画 / 入力 / バックエンド**まわりの整理方針

日付やマイルストーン番号に縛らず、「先に」「あとで」の**想定順序**が分かれば足ります。

## 記録項目テンプレート

各項目は次の欄を埋める想定です。

| 項目 | 内容 |
|------|------|
| **目的** | 達成したい状態や整理したい論点 |
| **想定順序** | 着手の前後関係（番号リストでも可） |
| **前提条件** | 判断・環境・依存があれば |
| **保留事項** | 先送りにした理由や未決点 |

---

| **目的** | **T34 完了**。**T35** で表示モードごとの window/client・swapchain・offscreen・Present・GDI を文書・コメント上 **固定**（[t35_display_mode_policy.md](t35_display_mode_policy.md)）。 |
| **想定順序** | T34 完了の明文化 → T35 方針固定 → （任意）ログ/UI の数値整合や GDI スケールは別タスク。 |
| **前提条件** | T17 ログは枠・クライアント、T34 committed はレンダラ解像度として **別軸**（[t34_t35_display_and_render.md](t34_t35_display_and_render.md)、[decisions.md](decisions.md)）。 |
| **保留事項** | T17 と committed のログ上の揃え、Fullscreen での committed オフスクリーン、GDI の仮想解像度対応は **将来**（t35 §5）。 |

---

| **目的** | **入力 foundation（T76/T77）を一区切り**。[T77_FOUNDATION_CLOSE.md](T77_FOUNDATION_CLOSE.md) を正とする（step24 foundation close を **ここでは再定義しない**）。ページ式 HUD・T19/T20 受け入れ・既定の 1P live を崩さない。 |
| **想定順序** | foundation close 後も **停止可**。続く場合は close note の **Go** から **1本だけ**。**現況同期**: Go(1) を `EffectiveInputGuideArbiter.cpp` のみ **2 コミット**まで（`fdd33b0` trial live 資格判定の集約、`57cf06f` T18 consume-result と述語の整合）。T19/T20・trial 実機は **OK**。Release 既定は **不変**。**一区切り** — 次は別テーマ優先か T77 再開か **判断のみ**（Go(2)・slot2+ 本番は対象外）。 |
| **前提条件** | レガシー縦積み HUD 分離フェーズは closed。slot2+ 本番・rebind・保存は **別フェーズ / defer**。 |
| **保留事項** | auto assign、keyboard 複数席分割、guide family 本番完成は **意図的に未着手**。Go(1) の **3 手目以降**は当面見送り。 |

---

| **目的** | Win32 周辺の**小工事フェーズを一区切り**。責務ラベル地図の固定と `architecture.md` / `roadmap.md` の短文同期は完了済み。**次の第一候補**は、[NEXT_THEME_RESTART_ENTRY.md](NEXT_THEME_RESTART_ENTRY.md) に沿った **可変更新 / 固定寄り更新 / 描画フレームの現況整理**の docs 固定。 |
| **想定順序** | `WM_INPUT` / `WM_TIMER` / `WM_PAINT` を **variable-like / fixed-like / render** の読みで揃える → `ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md` と `WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md` の参照を再開入口に固定 → 必要なら `decisions.md` に 1 行だけ残す。 |
| **前提条件** | ページ式 HUD が通常運用の正。レガシー縦積み HUD 分離フェーズは closed。T19/T20 受け入れ・T76 close・T77 foundation close・Go(1) 2 手で一区切りの一次情報は変えない。**`WM_INPUT` / `WM_TIMER` / `WM_PAINT` / `InvalidateRect` / T19-T20 accepted の意味を上書きしない**。 |
| **保留事項** | 実装としての `MainApp.cpp` 分割、レンダラ実験、`WindowsAppShell` / backend への物理移設は別テーマ。 |
