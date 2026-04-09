# ページ式 / レガシー縦積み HUD — 実コード依存の棚卸し

**前提**: 通常運用は **ページ式 HUD**（`WIN32_HUD_USE_PAGED_HUD=1` 既定）。レガシー縦積みは **互換・旧経路**（`WIN32_HUD_USE_PAGED_HUD=0`）。  
受け入れ・表示仕様は `docs/HUD_PAGED_ACCEPTANCE.md`。コメント方針は `Win32HudPaged.h` 冒頭。

本書は **実装の依存関係**（どの関数・状態がどの経路に属するか）を追いやすくするための索引である。挙動仕様の一覧ではない。

---

## 1. 制御マクロと描画の入口

| 項目 | 場所 | 役割 |
|------|------|------|
| `WIN32_HUD_USE_PAGED_HUD` | `Win32HudPaged.h` | `0` でレガシー縦積み描画経路を有効。既定 `1`。 |
| `Win32_HudPaged_IsEnabled()` | `Win32HudPaged.h` | 上記マクロのインライン判定。 |
| `Win32DebugOverlay_Paint` | `Win32DebugOverlay.cpp` | **最初の分岐**: 有効時は `Win32_HudPaged_PaintGdi` のみ、無効時は `Win32_DebugOverlay_PaintStackedLegacy`。 |
| `Win32_DebugOverlay_PrefillHudLeftColumnForD2d` | `Win32DebugOverlay.cpp` | D2D フレーム前の左列プレフィル。有効時は `Win32_HudPaged_PrefillD2d` で早期 return。無効時は `Win32_DebugOverlay_ComputeLayoutMetrics`（全文バッファ＋計測）。 |
| `WindowsRenderer_Frame` / D2D HUD 行 | `WindowsRenderer.cpp` | ページ式有効時は **T33 行の cand/act 等を描かない**（GDI が全文）。無効時は従来の D2D 左列＋本文バンド。 |

`MainApp.cpp` の `Win32_MainView_PaintFrame` は毎フレーム `Win32_T37_PrepareVirtualBodyOverlay` → `PrefillHudLeftColumnForD2d` → `WindowsRenderer_Frame` → `Win32DebugOverlay_Paint` の順（条件付きマクロあり）。**T37** は合成パスで `Win32_FillMenuSamplePaintBuffers` を別途呼ぶ（下記 §3）。

---

## 2. 観点別インベントリ

### 2.1 paged 専用（レガシー縦積みでは通らない）

| 種別 | 代表シンボル | ファイル（目安） |
|------|----------------|-------------------|
| ページ索引・件数 | `s_hudPagedIndex`, `kHudPagedCount`, `kHudPagedPageIndexT*` | `MainApp.cpp` |
| ページ本文生成 | `Win32_HudPaged_FillT14PageBody` … `FillT20PageBody`, `Win32_HudPaged_ClampTextLines` | `MainApp.cpp` |
| T19 ページ表示 | `Win32_HudPaged_T19*`（正規化・スナップショット・本文組み立て）、`s_hudPagedT19Last*` | `MainApp.cpp` |
| GDI 描画 | `Win32_HudPaged_PaintGdi`, `Win32_HudPaged_GetOrCreateGdiHudFont`, `Win32_HudPaged_GridCellStepPx` 等 | `MainApp.cpp` |
| D2D プレフィル（paged 用） | `Win32_HudPaged_PrefillD2d`（左列テキストを空にし、スクロールバンド無効化など） | `MainApp.cpp` |
| スクロールバー抑止 | `Win32_HudPaged_ResetScrollBar`（`s_paintScrollY=0`、非 fill-monitor で `SetScrollInfo`） | `MainApp.cpp` |
| ページ送り | `Win32_HudPaged_AdvancePage`, `[HUDPAGE]` ログ | `MainApp.cpp` |
| 入力 | `Win32_UnifiedInputMenuTick_*` 内の `Win32_HudPaged_IsEnabled()` 分岐（`←` `→` で `AdvancePage`、T14/T15 の ↑↓ ホールド等） | `MainApp.cpp` |
| T19 タイマー再描画 | `Win32_WndProc_OnXInputPollTimer` 内の T19 ページ限定 `InvalidateRect` | `MainApp.cpp` |
| マウスホイール | `Win32_WndProc_OnMouseWheel` が paged 時 early return（縦積みスクロール無効） | `MainApp.cpp` |

### 2.2 レガシー専用（`WIN32_HUD_USE_PAGED_HUD=0` 時のみ実質利用）

| 種別 | 代表シンボル | 備考 |
|------|----------------|------|
| 縦積み GDI 本文 | `Win32_DebugOverlay_PaintStackedLegacy` | メニュー＋`t14Buf` 全文をスクロール付きで描画、`[scroll]` 帯。 |
| 縦スクロール UI | `WM_VSCROLL` / `WM_MOUSEWHEEL`（paged で無効化される経路） | レガシー時は `s_paintScrollY` で本文オフセット。 |
| T14 キーボード選択ログ | `kT14KeyboardSelDebugLog` 分岐で paged 無効時のみ、等 | デバッグ用途。 |

`Win32_DebugOverlay_ComputeLayoutMetrics` は **legacy D2D プレフィル**と **PaintStackedLegacy** の双方から呼ばれる（§2.4）。ページ式 **有効**時は `PrefillHudLeftColumnForD2d` が **ここに来ない**。

### 2.3 paged / legacy 共用（状態・バッファ・入力コア）

| 種別 | 代表 | 説明 |
|------|------|------|
| レイアウト・スクロール状態 | `s_paintScrollY`, `s_paintDbgContentHeight`, `s_paintDbgMaxScroll`, `s_paintDbgT14*`, `s_paintDbgClientW`/`H` 等 | `MainApp.cpp` で定義、`Win32DebugOverlay.cpp` と共有。**レガシー経路と T37 準備で主に更新**。paged GDI は `Win32_HudPaged_ResetScrollBar` でスクロールを潰すが、**T14 自動追従・T37 仮想スクロール**は別経路で同じ変数を参照し得る。 |
| 全文テキストバッファ組み立て | `Win32_FillMenuSamplePaintBuffers` と分割関数（MenuColumn / T14T15Column / AppendT16T18T17） | **レガシー WM_PAINT** と **T37 オフスクリーン準備**で使用。ページ式の **ページ本文**は `Win32_HudPaged_Fill*` が別系。 |
| メニュー帯のみ | `Win32_FillMenuSamplePaintBuffers_MenuColumn` | **ページ式 GDI**（`Win32_HudPaged_PaintGdi`）でも呼ばれ、2 行メニュー帯を共有。 |
| 仮想入力・メニュー試作 | `VirtualInputMenuSample_*`, `Win32_FillMenuSamplePaintBuffers_MenuColumn` が参照する左列 | 入力経路は paged / legacy 共通。 |
| T14〜T18 長文バッファ（縦積み形） | `Win32_T16_AppendPaintSection`, `Win32_T18_AppendPaintSection`, `Win32_T17_AppendPaintSection` 等 | `FillMenuSamplePaintBuffers` 経由で **レガシー＋T37** に供給。ページ式本文は **別関数**（`Win32_HudPaged_Fill*` / `Win32_T16` 単体呼び出し等）で組み立て。 |
| デバッグオーバーレイ補助 | `Win32DebugOverlay_ScrollLog`, `Win32DebugOverlay_MainView_SetScrollPos`, `Win32DebugOverlay_ScrollTargetT17*` | 主にレガシー滾動・ジャンプ。 |
| グリッド・レンダラ | `Win32_RefreshRendererGridDebugParams`, `WindowsRenderer_*` | D3D/D2D 本体と共存。paged 時は D2D 側 HUD 文字を抑止（`Win32HudPaged.h` 方針）。 |

### 2.4 legacy + T37 共用（ページ式が有効でも残る経路）

| 種別 | 代表 | 説明 |
|------|------|------|
| T37 仮想本文オーバーレイ | `Win32_T37_PrepareVirtualBodyOverlay` | **Borderless / Fullscreen** かつオフスクリーン合成が有効なときのみ。`Win32_FillMenuSamplePaintBuffers` で **全文バッファ**を組み、`s_paintScrollY` から `t37ScrollVirtualPx` を算出（本文文字列は空にする経路あり）。**ページ式 HUD 有無に依存せず**走り得る。 |
| レイアウト計測 | `Win32_DebugOverlay_ComputeLayoutMetrics` | **Prefill（legacy 分岐）**と **PaintStackedLegacy** 内。`FillMenuSamplePaintBuffers` + `Win32_IsT37VirtualBodyOverlayActiveForLayout()` で T37 ギャップを反映。 |
| DWrite 本文描画 | `WindowsRenderer_TryDrawT37VirtualBodyOnCompositeTarget` 等 | `t37BodyText` / `t37ScrollVirtualPx`。本文文字列は準備側で空にする経路あり（GDI 二重回避）。 |
| T37 ギャップ定数 | `WIN32_OVERLAY_T37_MENU_TOP_GAP_PX` | `Win32DebugOverlay.cpp` / `MainApp.cpp`（paged GDI の `t37TopGap`）で **レイアウト上端**に使用。 |

---

## 3. 処理の流れ（簡略）

**ページ式（既定）**

1. `Win32_MainView_PaintFrame`  
2. `Win32_T37_PrepareVirtualBodyOverlay`（条件を満たすと `Win32_FillMenuSamplePaintBuffers` — **T37 用**）  
3. `Win32_DebugOverlay_PrefillHudLeftColumnForD2d` → **`Win32_HudPaged_PrefillD2d`**（`ComputeLayoutMetrics` は**呼ばない**）  
4. `WindowsRenderer_Frame`（D2D で T33 行をスキップし得る）  
5. `Win32DebugOverlay_Paint` → **`Win32_HudPaged_PaintGdi`**（`MenuColumn` + 現在ページの `Win32_HudPaged_Fill*`）

**レガシー縦積み（`WIN32_HUD_USE_PAGED_HUD=0`）**

1. 同じく `PrefillHudLeftColumnForD2d` が **`Win32_DebugOverlay_ComputeLayoutMetrics`**（全文 `FillMenuSamplePaintBuffers`）  
2. `Win32DebugOverlay_Paint` → **`Win32_DebugOverlay_PaintStackedLegacy`**（縦スクロール本文）

---

## 4. 将来の保守・リスクのメモ

| 区分 | 内容 |
|------|------|
| **互換として残す想定** | `WIN32_HUD_USE_PAGED_HUD=0` ビルド、`PaintStackedLegacy`、`ComputeLayoutMetrics` 系。受け入れ上はページ式が正。 |
| **削減候補（検討のみ）** | 長期間 `WIN32_HUD_USE_PAGED_HUD=0` を使わないなら、レガシー専用分岐の削除・統合。**T37 は `FillMenuSamplePaintBuffers` に依存**するため、同時に T37 を別データ源へ移す設計が要る。 |
| **影響が広い箇所** | `Win32_FillMenuSamplePaintBuffers`（レガシー＋T37）、`Win32_DebugOverlay_ComputeLayoutMetrics`（スクロール・高さの単一情報源になりがち）、`s_paintScrollY` / `s_paintDbg*`（T37・T14 追従・ログ）。**T19/T20 のページ本文ロジック**は主に `Win32_HudPaged_*` に閉じるが、**ビルドマニフェスト**は `Win32_FormatBuildDebugManifest` 共有。 |
| **触らない方がよい（受け入れ済み）** | T19 タイマー間引き・論理スナップショット、T20 `Win32_FormatBuildDebugManifest`、ページ送りキー分岐。変更時は `HUD_PAGED_ACCEPTANCE.md` と照合。 |

---

## 5. 主なファイル役割

| ファイル | 役割 |
|----------|------|
| `Win32HudPaged.h` | マクロ・ページ式 API 宣言・方針コメント。 |
| `MainApp.cpp` | ページ式本文・`s_hudPagedIndex`・入力・T19 タイマー・T37 準備・`Win32_FillMenuSamplePaintBuffers` 実体・共有 `s_paint*`。 |
| `Win32DebugOverlay.cpp` | `Paint` / `PaintStackedLegacy` / `ComputeLayoutMetrics` / 滾動ログ・計測。 |
| `Win32DebugOverlay.h` | 外部公開のオーバーレイ API・`Win32_FillMenuSamplePaintBuffers` 宣言。 |
| `WindowsRenderer.cpp` | D3D/D2D、T37 DWrite 本文、ページ式時の D2D HUD 重複防止。 |

---

## 6. 維持・削減の優先順位（別紙）

**維持対象・削減候補・依存が広い箇所・paged への寄せやすさ**の判断用整理は **`docs/HUD_LEGACY_MAINTENANCE_PRIORITIES.md`**（本書 §4 の補足を表形式で展開）。

---

## 7. 更新履歴

| 日付 | 内容 |
|------|------|
| 2026-04-06 | 初版（実コード依存の棚卸し。ページ式正・レガシー互換・T37 共有を区分） |
| 2026-04-06 | **コード上の境界**: `Win32DebugOverlay.cpp` の file-local static を legacy レイアウト用と明示、`ComputeLayoutMetrics` / `PaintStackedLegacy` / `Win32DebugOverlay_Paint` のコメントを整理。`MainApp.cpp` の共有 `s_paint*` にブロックヘッダ（挙動不変） |
