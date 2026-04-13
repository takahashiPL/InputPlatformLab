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

| **日付** | 2026-04-13 |
| **実施内容** | 整理フェーズ2（repo hygiene）: `architecture.md` に Pack-out / reuse boundary、`.gitignore` に `*.VC.db-shm` / `*.VC.db-wal`、`MainApp.cpp` と主要ヘッダに 補助コメント。挙動不変。 |
| **確認できたこと** | 持ち出し候補・platform・app glue・生成物の分類が一文書にまとまった。 |
| **未解決事項** | なし。 |

---

| **日付** | 2026-04-13 |
| **実施内容** | foundation 後の整理（挙動不変）: `PlayerInputGuideTypes.h` に値・意味・粒度の注釈、`architecture.md` にレイヤ・reusable/app-specific・Debug・`#ifdef` 方針、`EffectiveInputGuideArbiter` / `MainApp` に Debug 境界コメント。 |
| **確認できたこと** | 型定義と層の読み口が文書化された。 |
| **未解決事項** | なし（コード移動・機能追加なし）。 |

---

| **日付** | 2026-04-13 |
| **実施内容** | **T77 step24**: foundation の **stop-go 判断**。[T77_FOUNDATION_CLOSE.md](T77_FOUNDATION_CLOSE.md) を追記（確定・defer・Stop/Go・次候補は最大2件）。コードと既定の動作は不変。 |
| **確認できたこと** | T76/T77 の到達点（単一 live consume、slot-indexed trial、Debug 検証）で foundation として **close 可能**と整理。slot2+ 本番・owner 完成・rebind 等は **意図的 defer** と明記。 |
| **未解決事項** | 続行時は close note の **Go** にある1本目からスコープを切る（新機能の一括追加はしない）。 |

---

| **日付** | 2026-04-06 |
| **実施内容** | **レガシー縦積み HUD TU 分離フェーズを close**（`HUD_LEGACY_STACKED_PHASE_CLOSE.md`）。**次の本題テーマ**を **T35 §5（観測性・ログ整合）の第 1 歩**に絞る（legacy 分離の続き・T45/T46 移設はしない）。`MainApp.cpp` T17 コメントに `t35_display_mode_policy.md` §5 参照を 1 行追加（挙動不変）。 |
| **確認できたこと** | [roadmap.md](roadmap.md) / 本ファイルの未解決（t35 §5）と整合。ページ式 HUD は正のまま、**表示モード・レンダリングの観測**を次の実装スパインとする判断を記録。 |
| **未解決事項** | §5 の本体（T17 と T14 committed のログ揃え、GDI スケール等）は未着手。 |

---

| **日付** | 2026-03-30 |
| **実施内容** | T34（Borderless・committed 解像度オフスクリーン → swapchain へ合成）と T35（表示モード方針整理の未着手スコープ）を文書化。`docs/t34_t35_display_and_render.md` を追加。コード側はコメント・ログ接頭辞 `[T34][RT]` の整理に留める。 |
| **確認できたこと** | T34 は Borderless + committed 有効時に `[T34][RT] offscreen create / draw / composite` が通る前提。T17 の `targetPhys`/`client` ログはモード方針用でありオフスクリーン解像度とは別軸。 |
| **未解決事項** | T35 として Windowed/Borderless/Fullscreen の swapchain・offscreen・GDI の統一方針は未決定。 |

---

| **日付** | 2026-03-30 |
| **実施内容** | T35 設計メモ `docs/t35_display_mode_policy.md` を追加（3 モードの経路・表・Borderless T34 採用の利欠・Fullscreen と CDS）。`t34_t35_display_and_render.md` は T34 中心にし T35 は同ファイルへリンク。`MainApp.cpp` T17 コメントにドキュメント参照を 1 行追加。 |
| **確認できたこと** | 文書間のリンクを `architecture.md` / `roadmap.md` で更新。 |
| **未解決事項** | T17 の `targetPhys` と T14 committed のログ整合、GDI と仮想解像度の扱いは T35 継続。 |

---

| **日付** | 2026-03-30 |
| **実施内容** | T34 を完了として `t34_t35_display_and_render.md` に根拠を明記。T35 を `t35_display_mode_policy.md` で **正式方針固定**（3 モード表・T17 別軸・Fullscreen/CDS）。`decisions.md` に判断を追記。`WindowsRenderer.cpp` / `MainApp.cpp` のヘッダコメントを T35 固定に合わせて更新。 |
| **確認できたこと** | 方針は「Windowed/Fullscreen はオフスクリーンなし」「Borderless は committed 時 T34」「GDI は実クライアント」のまま文書化。 |
| **未解決事項** | t35 §5 の将来項目（ログ整合・Fullscreen オフスクリーン・GDI スケール）は未実装。 |



