# レガシー縦積み HUD — 維持・削減の優先順位

**前提**: 通常運用は **ページ式 HUD**。本書は `docs/HUD_LEGACY_CODE_DEPENDENCY.md` の棚卸しを、**判断用**に整理したものである（実装タスクの確定ではない）。

**制約**: `docs/HUD_PAGED_ACCEPTANCE.md` で受け入れ済みの **T19 / T20** およびページ式の表示・入力仕様を壊さないことを前提とする。

---

## 1. 分類の読み方

| 列 | 意味 |
|----|------|
| **理由** | なぜその分類か（棚卸し・リスク・受け入れとの関係）。 |
| **影響範囲** | 触ったときに波及しやすいモジュール／経路。 |
| **先にやるとよい順** | 小さい数字ほど **着手しやすい／前提になりやすい**（必ずしも「削減が早い」とは限らない）。 |

---

## 2. 維持対象（当面は残す）

| 要素 | 理由 | 影響範囲 | 先にやるとよい順 |
|------|------|----------|------------------|
| `WIN32_HUD_USE_PAGED_HUD` と `Win32_HudPaged_IsEnabled()` | ビルド切替で回帰比較・デバッグ分岐ができる **唯一のスイッチ**。削除は「レガシー削除プロジェクト」の最終段でよい。 | 全 HUD 入口 | **1**（定義と意味を文書・grep で固定） |
| `Win32DebugOverlay_Paint` の paged 先分岐 | 既に薄いラッパ。挙動の正はページ式側。 | `Win32DebugOverlay.cpp` | **1** |
| `Win32_HudPaged_*`（ページ本文・GDI・`AdvancePage`・T19 タイマー分岐） | **通常運用の正**。維持は必須。 | `MainApp.cpp` 大半 | **1**（変更不要が基本） |
| `Win32_FillMenuSamplePaintBuffers_MenuColumn` + ページ式 GDI | メニュー帯は **paged と共有**。レガシー削除後も残る。 | `MainApp.cpp` / `Win32_HudPaged_PaintGdi` | **1** |
| 仮想入力・`VirtualInputMenuSample_*` | 入力経路は paged / legacy 共通。**ページ送り**は paged 分岐内。 | `MainApp.cpp` | **1** |
| `Win32_T37_PrepareVirtualBodyOverlay` + `FillMenuSamplePaintBuffers`（T37 条件時） | **ページ式が有効でも** Borderless/Fullscreen 合成で走る。**縦積み「表示」ではない**が全文バッファ生成に依存。 | `MainApp.cpp` / `WindowsRenderer.cpp` | **2**（T37 単体の設計整理の前提） |
| `WindowsRenderer` の D3D/D2D・T37 DWrite・paged 時の二重描画防止 | 受け入れ・グリッド表示の土台。 | `WindowsRenderer.cpp` | **1** |
| `Win32_FormatBuildDebugManifest` / T20 本文 | T20 は **受け入れ済み**。レガシー削除と独立。 | `MainApp.cpp` | **1**（触るなら受け入れとセット） |
| `Win32_DebugOverlay_PaintStackedLegacy` + `ComputeLayoutMetrics`（`WIN32_HUD_USE_PAGED_HUD=0` 時） | **明示的にマクロ 0 のときだけ**使う互換経路。利用者がいなくなるまで **削除しない**のが安全。 | `Win32DebugOverlay.cpp` | **5**（最終段の検討） |

---

## 3. 削減候補（段階的に削れる／薄くできる）

| 要素 | 理由 | 影響範囲 | 先にやるとよい順 |
|------|------|----------|------------------|
| `kT14KeyboardSelDebugLog` 等 **legacy 専用**のデバッグ分岐 | 本番経路では paged。ログ分岐の整理は **挙動がログのみ**なら影響が小さい。 | `MainApp.cpp` 局所 | **2** |
| `Win32_HudPaged_IsEnabled()` が **false のときだけ**通る **WM_VSCROLL / ホイール**のコメント・重複 | コード量は減らしにくいが、**ドキュメントで「マクロ 0 専用」**と明記すれば迷いが減る。 | `MainApp.cpp` / `Win32DebugOverlay.cpp` | **2**（文書・コメントのみ） |
| `PaintStackedLegacy` 本体の **未使用コードパス**（将来） | マクロ 0 ビルドを CI から外し、**実利用ゼロ**が確認できてから。 | `Win32DebugOverlay.cpp` 大 | **5**（依存が大きい） |
| `ComputeLayoutMetrics` の **paged 時非呼び出し**の既知事実 | 削減ではなく **「paged では呼ばれない」**のテスト／静的コメントで固定。 | `Win32DebugOverlay.cpp` | **3** |

**注意**: 行単位の「削減」より先に **`Fill` と T37 の関係**（§5）を誤解しないことが重要。

---

## 4. 依存が広く慎重扱い（触ると波及しやすい）

| 要素 | 理由 | 影響範囲 | 先にやるとよい順 |
|------|------|----------|------------------|
| `Win32_FillMenuSamplePaintBuffers` および分割関数 | **レガシー WM_PAINT** と **T37** の両方が使用。ページ式本文は別系だが **メニュー帯**は共有。 | `MainApp.cpp`、呼び出し元すべて | **4**（設計変更は T37 含む） |
| `Win32_DebugOverlay_ComputeLayoutMetrics` | スクロール・高さ・vmSplit の計測の中心。**paged では Prefill から呼ばれない**が、legacy と D2D プレフィル（マクロ 0）に残る。 | `Win32DebugOverlay.cpp` | **4** |
| `s_paintScrollY` / `s_paintDbg*` 共有 | T37 仮想スクロール・T14 追従・ログが参照。paged は `ResetScrollBar` で潰すが **別経路で参照**あり。 | `MainApp.cpp` + `Win32DebugOverlay.cpp` | **4** |
| T19 の `Win32_HudPaged_T19*` とタイマー `InvalidateRect` | **受け入れ済み**。レガシー削除と無関係だが **同一ファイル**のため変更時は混線しやすい。 | `MainApp.cpp` | **1**（原則触らない） |

---

## 5. paged 側へ寄せやすい（将来の整理余地）

| 要素 | 理由 | 影響範囲 | 先にやるとよい順 |
|------|------|----------|------------------|
| `Win32_HudPaged_Fill*` が既に **ページ本文の単一ソース** | レガシー削除後も **ここが正**。追加の「寄せ」は **重複テキスト生成**の整理（`AppendT16T18T17` との共通化検討）が候補。 | `MainApp.cpp` | **3**（リファクタ、受け入れ要） |
| T37 用の **スクロール量のソース** | 現状は `s_paintScrollY`（レガシー幾何と結びつき）。将来、**合成専用のスクロール状態**に分離すれば `Fill` 全文への依存を弱められる **可能性**（設計変更）。 | `MainApp.cpp` / `WindowsRenderer.cpp` | **4** |
| `Win32_HudPaged_PrefillD2d` の役割 | 既に paged 用に左列を空にする。**D2D 側の責務**は文書化で十分な場合あり。 | `MainApp.cpp` + `WindowsRenderer.cpp` | **2**（文書のみ） |

---

## 6. 推奨される検討順序（ロードマップ）

| 順 | 内容 |
|----|------|
| **1** | リポジトリ内で `WIN32_HUD_USE_PAGED_HUD` を **0 にしているビルド**・CI・手順の有無を確認する。利用がなければ「互換維持の理由」を README に一文。 |
| **2** | `HUD_LEGACY_CODE_DEPENDENCY.md` の §3 と本書を **レビュー用**に固定。T19/T20・ページ送りは変更時に `HUD_PAGED_ACCEPTANCE.md` を見る。 |
| **3** | **文書・コメントのみ**: legacy 専用パスに「マクロ 0」ラベルを付ける（挙動変更なし）。 |
| **4** | T37 が `Fill` / `s_paintScrollY` に依存する理由を **設計メモ**に 1 ページ化（将来分離の判断材料）。 |
| **5** | `WIN32_HUD_USE_PAGED_HUD=0` 経路の **削除**は、T37 の代替データ経路と **受け入れ再実行**が揃ってから。 |

---

## 7. 関連ドキュメント

| 文書 | 役割 |
|------|------|
| `docs/HUD_LEGACY_CODE_DEPENDENCY.md` | 実コードの区分（paged-only / legacy-only / shared / legacy+T37）。**§8.3** は **仕分け**。**§8.4** は §8.3 B の **移設前提の契約**（T45/T46・unified 順序・Reset）。 |
| `docs/HUD_PAGED_ACCEPTANCE.md` | ページ式 HUD の受け入れ。 |
| `app/.../Win32HudPaged.h` | マクロと方針コメント。 |

---

## 8. 更新履歴

| 日付 | 内容 |
|------|------|
| 2026-04-06 | 初版（維持・削減・慎重・paged 寄せの優先順位とロードマップ） |
| 2026-04-06 | `HUD_LEGACY_CODE_DEPENDENCY.md` と整合: `Win32DebugOverlay.cpp` / `MainApp.cpp` に legacy 境界のコメントブロック（挙動不変） |
| 2026-04-06 | **関連ドキュメント**表: `HUD_LEGACY_CODE_DEPENDENCY.md` §8.3（main TU 残留の仕分け）へ参照を追加（挙動不変） |
| 2026-04-06 | **関連ドキュメント**表: §8.4（§8.3 B のデータ／呼び出し契約）へ参照を追加（挙動不変） |
