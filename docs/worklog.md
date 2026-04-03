# 作業履歴（worklog）

本ファイルは、**主要な作業内容と、そのとき確認できたこと**を時系列で残す場所です。設計の「なぜ」は [decisions.md](decisions.md) へ、将来の段階計画は [roadmap.md](roadmap.md) へ分けます。

## 何を書くか

マージや検証の単位など、**第三者が追いかけやすい粒度**で構いません。細かいチャットログの転記は不要です。

## 記録項目テンプレート

各エントリは次の項目を埋める想定です。

| 項目 | 内容 |
|------|------|
| **日付** | 作業日（YYYY-MM-DD など、プロジェクトで統一） |
| **実施内容** | 何をしたか（要約） |
| **確認できたこと** | ビルド・動作・レビューで分かったこと |
| **未解決事項** | 残課題があれば |

---

| **日付** | 2026-03-30 |
| **実施内容** | T34（Borderless・committed 解像度オフスクリーン → swapchain へ合成）と T35（表示モード方針整理の未着手スコープ）を文書化。`docs/t34_t35_display_and_render.md` を追加。コード側はコメント・ログ接頭辞 `[T34][RT]` の整理に留める。 |
| **確認できたこと** | T34 は Borderless + committed 有効時に `[T34][RT] offscreen create / draw / composite` が通る前提。T17 の `targetPhys`/`client` ログはモード方針用でありオフスクリーン解像度とは別軸。 |
| **未解決事項** | T35 として Windowed/Borderless/Fullscreen の swapchain・offscreen・GDI の統一方針は未決定。 |

