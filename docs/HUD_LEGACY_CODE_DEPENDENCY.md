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
| `Win32DebugOverlay_Paint` | `Win32DebugOverlay.cpp` | **最初の分岐**: 有効時は `Win32_HudPaged_PaintGdi` のみ、無効時は `RunGdiPaintFromPaintEntry` → legacy TU（`PaintStackedLegacy`）。 |
| `Win32_DebugOverlay_PrefillHudLeftColumnForD2d` | `Win32DebugOverlay.cpp` | D2D フレーム前の左列プレフィル。有効時は `Win32_HudPaged_PrefillD2d` で早期 return。無効時は `RunComputeLayoutMetricsForD2dPrefill` → legacy TU（`ComputeLayoutMetrics`、全文バッファ＋計測）。 |
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
| レイアウト計測 | `Win32_DebugOverlay_ComputeLayoutMetrics`（実装は **legacy TU**） | **Prefill（legacy 分岐）**と **PaintStackedLegacy** 内。`FillMenuSamplePaintBuffers` + `Win32_IsT37VirtualBodyOverlayActiveForLayout()` で T37 ギャップを反映。 |
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
| `Win32DebugOverlay.cpp` | `Paint` / `PrefillHudLeftColumnForD2d` / `FormatScrollDebugOverlay` / `ScrollLog` / `ScrollTarget*` 等（**legacy の `ComputeLayoutMetrics` / `PaintStackedLegacy` の実体**は `Win32DebugOverlayLegacyStacked.cpp`）。legacy との橋渡しは **`Win32DebugOverlayLegacyStacked_bridge.h` のみ**（`internal.h` は include しない）。MainApp 共有 HUD の **`extern` は `Win32MainAppPaintDbg_shared_link.h` に集約**。 |
| `Win32DebugOverlayLegacyStacked.cpp` | レガシー縦積み用スクラッチ・`Win32_LegacyStacked_*`・**CALCRECT/T60 計測ヘルパー**・**`ComputeLayoutMetrics` / `PaintStackedLegacy`**（`RunGdiPaint` / `RunComputeLayoutMetrics` の実装）。**`Win32DebugOverlayLegacyStacked_internal.h`**（bridge + legacy 実装向け宣言）。同 **`shared_link.h`** + **`LoadMainAppPaintDbgRead` / `ApplyMainAppPaintDbg*`**。 |
| `Win32DebugOverlay.h` | 外部公開のオーバーレイ API・`Win32_FillMenuSamplePaintBuffers` 宣言。 |
| `WindowsRenderer.cpp` | D3D/D2D、T37 DWrite 本文、ページ式時の D2D HUD 重複防止。 |

---

## 6. 維持・削減の優先順位（別紙）

**維持対象・削減候補・依存が広い箇所・paged への寄せやすさ**の判断用整理は **`docs/HUD_LEGACY_MAINTENANCE_PRIORITIES.md`**（本書 §4 の補足を表形式で展開）。

---

## 7. 将来のレガシー縦積み分離（`Win32DebugOverlay.cpp`）メモ

**目的**: マクロ 0 互換経路を **別翻訳単位**へ移す検討時の、**切り出し単位**と **同一ファイルに残す理由**のメモ（実装タスクの確定ではない）。**パイプライン本体を legacy TU に移したあとも main に残る連動**（T45 / T46 / shared scroll 等）は **§8**。

### 7.1 まとまりとして切り出しやすい単位（パイプライン）

| 単位 | 内容 | 備考 |
|------|------|------|
| **Legacy stacked pipeline** | `Win32_DebugOverlay_ComputeLayoutMetrics` + `Win32_DebugOverlay_PaintStackedLegacy` | `Win32_DebugOverlay_PrefillHudLeftColumnForD2d` の legacy 分岐からも `ComputeLayoutMetrics` を呼ぶため、**D2D プレフィル**とリンク上は同じ束になりやすい。**実装**: スクラッチ・`Win32_LegacyStacked_*`・CALCRECT/T60 計測・**`ComputeLayoutMetrics` / `PaintStackedLegacy`** は **`Win32DebugOverlayLegacyStacked.cpp`**。**main TU** は **`Win32DebugOverlayLegacyStacked_bridge.h`**（`RunGdiPaint` / `RunComputeLayoutMetrics`・I/O struct・Load/getter の最小面のみ）。**legacy TU** は **`Win32DebugOverlayLegacyStacked_internal.h`**（bridge を include した上で scratch / Apply* / vmSplit 等の宣言を追加）。 |
| **GDI 入口の薄い分岐** | `Win32DebugOverlay_Paint` 内の `Win32_HudPaged_IsEnabled()` → `PaintStackedLegacy` | 既に短い。 |

### 7.2 同一ファイル先頭に置く必要があるもの（宣言順）

**legacy 縦積み用スクラッチ**（`s_paintDbgT14VmSplit*` 等）と **T52** `s_paintDbgLayoutMetricsFromPaintValid` の **定義**は **`Win32DebugOverlayLegacyStacked.cpp`**。**`Win32DebugOverlay.cpp`**（main TU）は **`Win32DebugOverlayLegacyStacked_bridge.h` のみ** include し、**`Win32DebugOverlayLegacyStacked_internal.h` は include しない**（依存境界の縮小）。**`Win32DebugOverlayLegacyStacked_internal.h`** は bridge を include したうえで、**legacy TU 実装用**の型・`Apply*`・scratch API を追加する。**グローバル `extern` は載せない**。**`Win32DebugOverlay.cpp`** は当該スクラッチ／T52 を **`extern` で直参照しない** — **`Win32_LegacyStacked_LoadLayoutScratchRead`**（`Win32_LegacyStacked_MainLayoutScratchRead`）は **legacy TU 側**の呼び出しのみ；main 側は **getter**（`GetT14VmSplitActive` / `GetT17DocYRestScroll` / `IsPaintLayoutMetricsFromPaintValid` / `ClearPaintLayoutMetricsFromPaintValid` 等）と **LoadMainAppPaintDbgRead** で読み取る。**`Win32DebugOverlay.cpp` / `Win32DebugOverlayLegacyStacked.cpp`** が参照する **MainApp 共有 HUD** の **`extern` 宣言**は **`Win32MainAppPaintDbg_shared_link.h`** に一本化（個別 `extern` 列挙を両 TU から削減）。読み取りは **`Win32_LegacyStacked_LoadMainAppPaintDbgRead`**（`scrollY` / `restViewportClientH` / `contentHeight` / `t17DocY` 等のスナップショット）、行高の書き込みは **`Win32_LegacyStacked_ApplyMainAppPaintDbgScrollLineMetrics`**、vmSplit 由来の MainApp 更新は **`Win32_LegacyStacked_ApplyVmSplitMainAppExternFromScratchPass`**、仮想レイアウトキャッシュの一部リセットは **`Win32_LegacyStacked_ApplyMainAppPaintDbgResetProvisionalLayoutExtras`** に集約。**`Win32_DebugOverlay_LegacyStacked_InvokeT45`** は **main TU** の static T45 への橋渡しで **`Win32DebugOverlay.cpp` に定義**、legacy TU は **前方宣言のみ**。**C++ では名前は宣言より前に使えない**ため、この群をさらに移す場合は共有側のスクロール API を **前方宣言・getter 化・ファイル順の再配置**のいずれかで揃える必要がある。シンボル名は `s_paintDbg*` のまま。

### 7.3 「レガシーで更新・共有で読む」ため分離が一緒に動きやすいもの

| 種別 | 例 | メモ |
|------|-----|------|
| T46 / T52 まわり | `s_t46LastSi*`、`s_paintDbgLayoutMetricsFromPaintValid` | `Win32_T45_ApplyWindowedScrollInfo`（レガシー計測経路）で更新、`Win32DebugOverlay_ScrollLog` / `Win32_DebugOverlay_FormatScrollDebugOverlay`（共有）が参照。**完全な legacy-only ファイル分離**は、`[scroll]` 文字列生成側の整理とセットになりやすい。 |

### 7.4 共有ヘルパー（legacy 専用ファイルへ「丸ごと」は移しにくい）

`Win32_MenuSampleMeasure*`、`Win32_T60_*`、`Win32_MainView_MeasureScrollOverlayTextHeight` は **`Win32DebugOverlayLegacyStacked.cpp`**（`ComputeLayoutMetrics` / `PaintStackedLegacy` と同 TU）に置く。ページ式以外の経路からも参照され得る場合は **共有ヘッダ＋共通 `.cpp`**、または **現 TU に残す**前提が現実的。

### 7.5 legacy パイプラインの入出力境界（依存面の整理）

**目的**: レガシー縦積み本体と **shared 側**のあいだで、何が **引数で渡り**、何が **副作用**になるかを固定しやすくする（`Win32_LegacyStacked_*` 束・legacy パイプライン本体は **`Win32DebugOverlayLegacyStacked.cpp`**）。

| 区分 | 内容 |
|------|------|
| **明示入力（値）** | `Win32_LegacyStacked_CommonParams`（`hwnd` / `hdc` / T17 ラベル）、GDI 用の `Win32_LegacyStacked_GdiPaintParams`（抑制フラグ）、レイアウト用の `Win32_LegacyStacked_LayoutMetricsParams`（`outHud` / `logScroll`）。`Win32DebugOverlay_Paint` と `Win32_DebugOverlay_PrefillHudLeftColumnForD2d`（legacy 分岐）が構築。 |
| **shared ヘルパー呼び出し（読み取り中心）** | `Win32_MenuSampleMeasure*`、`Win32_T60_*`、`Win32_MainView_MeasureScrollOverlayTextHeight`、`Win32_MainWindow_IsFillMonitorPresentationMode`（legacy TU 内）等（§7.4）。 |
| **MainApp 共有状態（extern `s_paint*`）** | `ComputeLayoutMetrics` / `PaintStackedLegacy` が **読み書き**（§2.3）。別 `.cpp` 化時は **extern 宣言の共有ヘッダ**または **MainApp のみ**に残す判断が必要。 |
| **ファイル先頭スクラッチ（無名名前空間）** | `s_paintDbg*`（§7.2）。`ComputeLayoutMetrics` が主に書き、共有の `ScrollTargetT17*` 等が読む。 |
| **T46 / T52（§7.3）** | レガシー計測経路で更新、`ScrollLog` / `FormatScrollDebugOverlay` が参照 — **完全な legacy-only 分離は別タスク**。 |
| **出力（副作用）** | `WindowsRendererState` の D2D プレフィル欄（`outHud` 非 null 時）、GDI `DrawText`、条件付き `Win32_T45_ApplyWindowedScrollInfo`、先頭スクラッチ・`s_paintDbg*` 更新。adapter 境界は **§7.6**、**書き込み先の区分**は **§7.7**。 |

### 7.6 副作用の出口と adapter（実装メモ）

**目的**: レガシー縦積み本体が **どこへ**副作用を出すかを、入口（§7.5）に続けて追いやすくする。adapter の実装は **`Win32DebugOverlayLegacyStacked.cpp`**（`bridge.h` の `Run*` と **`internal.h`** の残り宣言）。

| 要素 | 役割 |
|------|------|
| `Win32_DebugOverlay_LegacyStacked_RunGdiPaint` | **WM_PAINT** の legacy 分岐から **唯一**呼ばれる薄い境界（→ `Win32_DebugOverlay_PaintStackedLegacy`）。将来、GDI 入口と縦積み本文の間の接続をここに固定しやすい。 |
| `Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics` | **D2D Prefill（legacy）** と **GDI 本文の初回計測**の双方が通る境界（→ `Win32_DebugOverlay_ComputeLayoutMetrics`）。計測の副作用出口はまだ本体側に集約。 |
| **本体** `ComputeLayoutMetrics` / `PaintStackedLegacy` | 実際の **extern `s_paintDbg*`**、**スクラッチ**、`outHud` 書き込み、**T45→T46**、**GDI `DrawText`** は **`Win32DebugOverlayLegacyStacked.cpp`**（§7.3 の共有読み取り出口は別タスク）。**書き込み先の棚卸し**は **§7.7**。 |

### 7.7 legacy 本体が更新する状態（shared / scratch / scroll 系の見える化）

**目的**: `ComputeLayoutMetrics` / `PaintStackedLegacy` が **どのカテゴリの状態**を触るかを表に固定する（実装の完全な行一覧ではなく、追いかけの地図）。挙動変更はしない。

| 区分 | 主な更新対象 | 主に触る関数 | 備考 |
|------|----------------|--------------|------|
| **A — ファイル先頭スクラッチ**（無名名前空間） | `s_paintDbgFinalBodyTopPx`、`s_paintDbgBodyT14DocTopPx`、`s_paintDbgFinalRow1HeightPx`、`s_paintDbgRow2TopPx`、`s_paintDbgT14VmSplitActive`、`s_paintDbgT17DocYRestScroll`、`s_paintDbgRestViewportTopPx`、`s_paintDbgT53ScrollBandDrawEnabled`、`s_paintDbgT14VmSplitPrefix` / `VmBand` / `Rest` と各 `*H` | `ComputeLayoutMetrics` が **書き**、`PaintStackedLegacy` が **読み**（GDI クリップ・分割描画）。帯幾何・vmSplit フラグ・T53・`RestViewportTop` クリアの **一部**は **`Win32_LegacyStacked_ApplyScratchFinalHudGeometry`** / **`ResetVmSplitScratchFlags`** / **`ClearScratchRestViewportTop`** / **`ApplyScratchT53ScrollBandDrawEnabled`** に集約。vmSplit の **wmemcpy / バンド文字列 / DrawText 計測**は **`Win32_LegacyStacked_RunVmSplitScratchPass`**（`Win32_LegacyStacked_VmSplitScratchPassOut`）に **局所化**（**MainApp extern 非接触**）。 | §7.2 の宣言順の理由。将来 `.cpp` 分割時は **スクラッチごと移す**か **getter** が必要。 |
| **B — MainApp extern（`s_paintDbg*` 等）** | 例: `s_paintDbgContentHeight`、`s_paintDbgContentHeightBase`、`s_paintDbgExtraBottomPadding`、`s_paintDbgClientW`/`H`、`s_paintDbgT17DocY`、`s_paintDbgMaxScroll`、`s_paintDbgScrollBandReservePx`、`s_paintDbgLayoutRestVpBudgetHint`、`s_paintDbgT14*` 系、`s_paintScrollLinePx` 等 | ほぼ **`ComputeLayoutMetrics`** に代入が集中 | `MainApp.cpp` 定義。入力・T37・滾動 UI と共有（§2.3）。vmSplit の **`s_paintDbgT17DocY` / `s_paintDbgT14VisibleModesDocStartY` / `s_paintDbgT14LayoutValid`**（後者 2 つは `t14LayoutOutValid` が true のときのみ）は **`Win32_LegacyStacked_ApplyVmSplitMainAppExternFromScratchPass`**（`VmSplitScratchPassOut` を入力）で **`RunVmSplitScratchPass` 成功後**に適用。スクロール行高は **`ApplyScrollLineMetricsFromHdc`**。**`s_paintDbgMaxScroll`** と **`ClampScrollYToMaxScroll`** の連続は **`SetDbgMaxScrollAndClampScrollY`**（phase1 / vmSplitRefine）。**unified** では同 **+ T45** を **`RunUnifiedMaxScrollClampAndT45`**（`UnifiedScrollLayoutForT45`）に集約（§7.7 D）。**順序依存の弱い** MainApp 代入は **`ApplyMainAppPaintDbgContentAndClientGeometry`** / **`ApplyMainAppPaintDbgPostOverlayMeasures`** / **`ApplyMainAppPaintDbgT14BudgetHeights`** 等へ段階的に寄せる。 |
| **C — D2D プレフィル（`outHud`）** | `WindowsRendererState::dbgHud*` 各フィールド（左列テキスト、スクロールバンド、vmSplit バンド等） | **`Win32_LegacyStacked_ApplyD2dHudPrefill`**（`ComputeLayoutMetrics` 内、`outHud != nullptr` 時のみ）。`ComputeLayoutMetrics` 本体は `outHud->` を直接書かない。 | Prefill（legacy）経路。vmSplit 用の文字列コピーは引き続きファイル先頭スクラッチ（`s_paintDbgT14VmSplit*`）を読む。 |
| **D — T45 / T46 / T52** | `Win32_T45_ApplyWindowedScrollInfo` 経由で **`s_t46LastSi*`**；**`s_paintDbgLayoutMetricsFromPaintValid`** | **`ComputeLayoutMetrics`** 後半（スクロール範囲確定後） | `ScrollLog` / `FormatScrollDebugOverlay` が **読む**（§7.3）。unified 経路の入口は **`RunUnifiedMaxScrollClampAndT45`**（→ **`Win32_T45_ApplyWindowedScrollInfo`**、T46 は同関数内）。**T52** 確定は **`MarkPaintLayoutMetricsFromPaintValid`**。 |
| **E — GDI ラスタ（永続グローバルではない）** | `DrawTextW`、`SetTextColor`、`OffsetViewportOrgEx`（`s_paintScrollY` を参照） | **`PaintStackedLegacy`** | フレームバッファ上の可視出力。状態テーブルでは **B に含めない**。 |

---

## 8. main TU（`Win32DebugOverlay.cpp`）に残る legacy 連動要素と残留理由

レガシー縦積みの **パイプライン本体**（`ComputeLayoutMetrics` / `PaintStackedLegacy` / scratch の多く）は **`Win32DebugOverlayLegacyStacked.cpp`** へ移動済み（§5 / §7）。それでも **main TU** に残る要素は、次のとおり **「共有 API」「Win32 スクロールバー API の帰属」「T46 が [scroll] 表示と同居」** などの理由で、**いまは main に置くのが依存が最も読みやすい**、という整理である。**本節は削除方針の確定ではない**（判断材料の明文化）。

### 8.1 要素別サマリ

| 要素 | main TU にあるもの（目安） | なぜ今は main に残すか | ページ式 HUD との関係 | 触ると効きが大きいか | さらに分離できる余地 | 将来の位置づけ（目安） |
|------|---------------------------|------------------------|----------------------|---------------------|---------------------|------------------------|
| **T45** | `Win32_T45_ApplyWindowedScrollInfo`（static）、`Win32_DebugOverlay_LegacyStacked_InvokeT45` | **Windowed 時の `SetScrollInfo`（標準スクロールバー）** と `Win32_UpdateNativeScrollbarsWindowedOnly` が同一 TU にあり、legacy からは **InvokeT45 1 ホップ**で足りる。fill-monitor では T45 は実質スキップ。 | paged 本文は別経路だが、**ウィンドウ枠のスクロールバー**は MainView 全体の挙動として共有され得る。 | **高**（スクロール位置・SI 一貫性） | legacy TU に **丸ごと移す**と `InvokeT45` の向き先・リンク境界が増える。**別 `.cpp` に T45 だけ切り出す**は、shared ヘッダ＋単一実装ファイルのパターンなら検討余地。 | **長期は main 近傍 or 専用 small TU** が現実的。 |
| **T46** | `s_t46LastSiNMax` 等、`s_t46LastSiValid`（main TU の static） | **T45 が SI を書いた直後のスナップショット**を `[scroll]` 文字列（`FormatScrollDebugOverlay`）が読む。**表示テキスト生成**を main の shared region に置く設計のまま。 | ページ式でも **FormatScroll が参照し得る**（レガシー専用ではない）。 | **中〜高**（デバッグ表示と SI の整合） | T46 を **legacy TU へ移す**と、`FormatScroll`（main）との **static 参照**が TU をまたぐ。**スナップショットを struct 化して shared_link 経由**などが次の候補。 | **次の整理候補**（[scroll] とセット）。 |
| **T52** | main からは **bridge** の `Is` / `Clear` のみ（**実体 `s_paintDbgLayoutMetricsFromPaintValid` は legacy TU**） | **定義は legacy**、**公開 getter は bridge** に既に分離済み。main の `ResetProvisionalLayoutCache` が T52 クリアと **T46 無効化**をまとめて呼ぶ。 | ページ式でも「暫定レイアウト」ログ分岐に関わり得る。 | **中** | フラグ自体の移動は **済**。**Reset の責務分割**は可能だが手戻りリスクあり。 | **当面は現状維持**で可。 |
| **Shared scroll（[scroll] / ScrollLog / Target）** | `Win32_DebugOverlay_FormatScrollDebugOverlay`、`Win32DebugOverlay_ScrollLog`、`Win32DebugOverlay_ScrollTargetT17*`、`Win32_DebugOverlay_ClampScrollYToMaxScroll`、`Win32DebugOverlay_MainView_SetScrollPos` | **Win32DebugOverlay.h で宣言される共有 API**であり、**MainApp**・**legacy TU** の両方から呼ばれる。**「オーバーレイ用スクロールの言語」**を 1 か所に置く。 | **ページ式・レガシー双方**が参照し得る（§2.3 / §7.4）。 | **高**（入力・WM_VSCROLL・ログの一貫性） | 文言生成だけ別 TU にするは **可能だが**、T46 / `s_paintScrollY` / bridge Read との **結合が強い**。 | **当面 main TU 中心**が安全。**巨大化したら**「scroll 文字列専用 .cpp」の切り出しが候補。 |
| **表示モード（fill-monitor 等）** | `Win32_IsMainWindowFillMonitorPresentation`（static）、`Win32_MainWindow_IsFillMonitorPresentationMode` | **HWND のスタイル判定**とスクロールバー抑止の **入口**を overlay と共有。legacy / paged の分岐より前に必要なことが多い。 | **ページ式でも** fill-monitor 判定が入力・ペイントに絡む。 | **高** | 既に **`Win32_MainWindow_IsFillMonitorPresentationMode` は公開**。**static 版**を別 TU に移すメリットは限定的。 | **main 近傍に残す**想定。 |
| **その他** | `Win32_DebugOverlay_ResetProvisionalLayoutCache`（T52 クリア + `ApplyMainAppPaintDbgResetProvisionalLayoutExtras` + **T46 無効化**） | **暫定レイアウト**と **SI スナップショット**を一括で無効化する **オーケストレーション**が main にある。 | ページ式でも「キャッシュ破棄」の呼び出し元があり得る。 | **中** | 処理を **legacy の Apply に寄せる**ことは可能だが、**T46 が main static** のままでは完全移動しにくい。 | **T46 の置き場とセット**で検討。 |

### 8.2 読み方（誤解しやすい点）

- **「legacy 関連」＝全部 legacy TU** ではない。**[scroll] 表示・ScrollLog・ジャンプ目標**は **HUD 全体の共有言語**として main TU に残る。
- **T52 の真実の定義**は **legacy TU**；main は **bridge 経由で読むだけ**（§7.2）。
- **T45 本体**は **main**、**呼び出しトリガ**の多くは **legacy の `RunUnifiedMaxScrollClampAndT45` → InvokeT45**（§7.7 D）。
- 本書 §7 の古い記述で「`ComputeLayoutMetrics` が `Win32DebugOverlay.cpp` にある」ように読める箇所は、**§5・§1 表を優先**（実装は legacy TU）。

### 8.3 仕分け：今後も main に残す／条件付きで移せる／今は触ると危険

§8.1 の要素を **優先度・リスク**で仕分けする（**削除や大規模移動の確定ではない**）。`docs/HUD_LEGACY_MAINTENANCE_PRIORITIES.md` の「維持／削減／慎重」と併用する。

#### A — 今後も main TU に置くのが妥当（既定）

| 要素 | 理由 | 次に触るなら必要な前提 | いま大きく触らない理由 |
|------|------|------------------------|------------------------|
| **fill-monitor 判定**（`Win32_MainWindow_IsFillMonitorPresentationMode`、file-static 版） | **MainView の表示モード**の入口として、paged / legacy / T37 の前に必要なことが多い。**公開 API は既に 1 か所**。 | 別 TU に移すなら **HWND スタイルとスクロール抑止**の呼び出し順を変えないテスト方針。 | 変更は **入力・ペイント・スクロールバー**に直結（受け入れ再確認のコスト）。 |
| **`Win32DebugOverlay.h` に載る共有スクロール API の「名前と実装の所在」** | **MainApp** と **legacy TU** が同じシンボルをリンクする **安定した境界**。ページ式でも参照し得る。 | 変更なし。**巨大化したら** §8.3 B の「文言だけ別 .cpp」などとセットで検討。 | API シグネチャや TU を増やすと **全呼び出し・ビルド依存**が動く。 |
| **`Win32_DebugOverlay_LegacyStacked_InvokeT45`（bridge 実体が main）** | legacy → main の **1 ホップ**が既に固定され、T45 本体と隣接。 | T45 を別 `.cpp` に出すなら **InvokeT45 の定義位置**ごと移す（§8.3 B）。 | 向き先を変えると **legacy TU とリンク順**の見え方が変わる。 |

#### B — 条件が揃えばさらに移せる（次段 refac の候補）

| 要素 | 理由 | 次に触るなら必要な前提 | いま大きく触らない理由 |
|------|------|------------------------|------------------------|
| **T46**（`s_t46LastSi*` / `s_t46LastSiValid`） | `[scroll]` 表示と **同一 TU 内 static** だが、**スナップショットを struct 化**し `shared_link` や **読み取り専用バッファ**に載せれば、legacy 側生成にも寄せられる余地。 | **FormatScroll / ScrollLog** が読むフィールド契約を固定した **単一の型**と、**T45 直後の更新点**の一覧化。 | **§8.3 C** と同時に壊れやすい。**T19/T20 非対象**だが HUD ログ受け入れに波及し得る。 |
| **T45 本体**（`Win32_T45_ApplyWindowedScrollInfo`） | **Win32 スクロールバー API** と同居が自然だが、**scroll_win32 専用 .cpp** に切り出すパターンはあり得る。 | **InvokeT45**・`Win32_UpdateNativeScrollbarsWindowedOnly`・**リンク対象**をまとめて移す。**fill-monitor スキップ**の分岐テスト。 | 変更は **SI・本文オフセット**に直結（§8.3 C）。 |
| **`FormatScroll` の文言生成のみ** | ロジックが肥大化したとき **表示専用 .cpp** に分離し、main は **薄いラッパ**にできる **可能性**。 | **T46**・`LoadMainAppPaintDbgRead`・bridge getter との **データ契約**を変えない。 | 現状サイズで **分割の便益が小さい**（依存は残る）。 |
| **`ResetProvisionalLayoutCache` の内訳** | T52 クリアと T46 無効化の **束ね**は、T46 の置き場が決まれば **legacy 側 Apply に寄せる**余地。 | **§8.1「その他」**と **§7.7 D** の対応表を先に更新。**呼び出し元**（ページ式側のキャッシュ破棄）を grep で固定。 | **二重クリア・取りこぼし**で暫定レイアウト表示がズレる。 |

#### C — 依存が強く、今は触ると危険（防御的変更・文書優先）

| 要素 | 理由 | 次に触るなら必要な前提 | いま大きく触らない理由 |
|------|------|------------------------|------------------------|
| **Shared scroll の実装**（`FormatScrollDebugOverlay` / `ScrollLog` / `ScrollTargetT17*` / `ClampScrollYToMaxScroll` / `MainView_SetScrollPos`） | **WM_VSCROLL・マウス・`s_paintScrollY`・`s_paintDbgMaxScroll`** と **入力経路**が絡む。**ページ式・レガシー両方**のログ・ジャンプに効く。 | **レガシー `WIN32_HUD_USE_PAGED_HUD=0`** ビルドでの手動確認、または **最小スクロール回帰手順**。**`HUD_PAGED_ACCEPTANCE.md`** の該当節との照合。 | 挙動差は **デバッグ表示と実スクロールの不一致**として顕在化しやすい。 |
| **T45 → T46 の順序と SI 一貫性** | **1 フレーム内の更新順**に依存。**InvokeT45** から離すと読み取りズレが出やすい。 | **§7.7 D** の表と **unified** 経路の呼び出し木を揃えた設計メモ。 | **§8.3 B** の前提が揃うまで **本体ロジックは触らない**のが安全。 |
| **T52（bridge 契約）** | 定義は legacy、**getter/Clear** は bridge。**Reset** が T46 と束ねられている。 | **Mark/Clear** の呼び出し元一覧と **暫定レイアウト**の意味を文書化。 | フラグの意味を変えると **ログ行の真偽**が変わる（見た目は小さく、追跡は難しい）。 |

**読み方**: 「A＝方針として main 側に置き続けてよい」「B＝分離フェーズで最初に検討リストへ載せる」「C＝**いまのフェーズでは文書・小さなコメントに留める**」。同一要素が **B と C の両方**に関わる場合は、**データ契約の整理（B）**のあとに **実装移動**に進む想定とする。

§8.3 B の各項目について **移設前に固めるべきデータ契約／呼び出し契約**は **§8.4**。

### 8.4 §8.3 B の移設前提：データ契約／呼び出し契約（実装変更なしの明文化）

**対象**: §8.3 B の **T46**、**T45 本体**、**FormatScroll の文言生成**、**ResetProvisionalLayoutCache の内訳**。コード参照は現状のシンボル名に合わせる（将来リネームしても **意味**は同じ）。

#### 8.4.1 共通：`ComputeLayoutMetrics` 後半の **unified 経路**（legacy TU）

| 項目 | 契約 |
|------|------|
| **入口** | `Win32_LegacyStacked_RunUnifiedMaxScrollClampAndT45(hwnd, u)`。`u` は `Win32_LegacyStacked_UnifiedScrollLayoutForT45`（`scrollContentHFinal`, `scrollViewportHFinal`, `maxScrollUnified`）。 |
| **呼び出し順（固定）** | **①** `Win32_LegacyStacked_SetDbgMaxScrollAndClampScrollY(u.maxScrollUnified, …)` — MainApp 共有の `s_paintDbgMaxScroll` 更新と **`Win32_DebugOverlay_ClampScrollYToMaxScroll`**（`s_paintScrollY`）。**②** `Win32_LegacyStacked_LoadMainAppPaintDbgRead` — **クランプ後**の `scrollY` を含むスナップショット取得。**③** `Win32_DebugOverlay_LegacyStacked_InvokeT45(hwnd, u.scrollContentHFinal, u.scrollViewportHFinal, mainAppRead.scrollY)`。 |
| **shared state 依存** | **MainApp `extern`**（`s_paintDbgMaxScroll`, `s_paintScrollY` ほか `LoadMainAppPaintDbgRead` が読む列）と **T44**（clamp）。**T45 に渡す `pos` は ①② のあとの `scrollY`** であり、**unified の `maxScrollUnified` と論理 content/viewport から単独再計算した値に差し替えない**こと（SI と本文オフセットの整合）。 |
| **崩すと危険** | **② の前に InvokeT45** すると `pos` が古い／未クランプになり **SetScrollInfo と GDI 描画の scrollY が不一致**。**① を省略**すると maxScroll と SI の range がズレる。 |

#### 8.4.2 T45：`Win32_T45_ApplyWindowedScrollInfo`（main TU static）／`InvokeT45`

| 項目 | 契約 |
|------|------|
| **入力** | `hwnd`, `scrollContentH`, `scrollViewportH`, `pos`（論理スクロール位置。unified 経路では **Load 済み `scrollY`**）。 |
| **出力（副作用）** | **Windowed かつ非 fill-monitor** のときのみ `Win32_UpdateNativeScrollbarsWindowedOnly(SB_VERT, SCROLLINFO …)`。**標準デバッグ** `[T45] SI final …`。 |
| **T46 更新** | **同一関数内**で、上記成功パス終了時に **`s_t46LastSiValid = true`** と `nMax` / `nPage` / `nPos` / `maxScroll_si`（`nMax - nPage + 1` 系）を static に格納。**fill-monitor または hwnd 無効**では **T46 を無効化**（`s_t46LastSiValid = false`）のみ — **SetScrollInfo なし**。 |
| **数値関係（論理）** | `nMax = max(0, scrollContentH - 1)`、`nPage = max(1, scrollViewportH)`、`maxScroll_si = max(0, nMax - nPage + 1)`。呼び出し側の `maxScrollUnified` は `max(0, scrollContentHFinal - scrollViewportHFinal)` と **同一次元**であることが前提。 |
| **移す前に固めるべきこと** | **早期 return 条件**（fill-monitor）と **T46 無効化**の対応表。**`InvokeT45` のシグネチャ**（legacy → main の 1 ホップ）を維持するか、**scroll_win32 専用 TU** に **SetScrollInfo＋T46 更新**をまとめて移すかの決定。 |

#### 8.4.3 T46：`s_t46LastSi*`／`s_t46LastSiValid`

| 項目 |契約 |
|------|------|
| **入力** | **T45 の成功パスでのみ**意味のある更新（上記）。それ以外は **valid=false** または前フレームの残骸を読まないよう **Reset** で無効化。 |
| **出力（読み手）** | **`Win32_DebugOverlay_FormatScrollDebugOverlay`** — `s_t46LastSiValid` が true のとき SI 行を付与；compact 帯・通常帯で分岐。**`Win32DebugOverlay_ScrollLog`** — fill-monitor では SI 行なし；valid 時は `nMax/nPage/nPos/maxScroll_si`；**valid でないときは** **GetScrollInfo フォールバック**。 |
| **呼び出し順** | ** consumers は「T45 適用後・同一ペイント／ログセッション内」**で読むことが前提。**`ComputeLayoutMetrics` 内**では `RunUnifiedMaxScrollClampAndT45` の **後**に `MarkPaintLayoutMetricsFromPaintValid`（T52）と **`FormatScrollDebugOverlay`** が来る（§7.7 コメント木）。 |
| **shared state 依存** | **main TU の file-static**（現状）。**FormatScroll** は引数で渡る contentH／scrollY 等と **T46 を併記** — **論理 maxScroll（content−vp）と SI の maxScroll_si は別物**として表示上は両方出す。 |
| **崩すと危険** | **T46 だけ移し**て更新順をずらすと **[scroll] の SI 行と実バーが別フレーム**。**valid のまま古い hwnd での SI を表示**するとデバッグが誤誘導。 |
| **移す前に固めるべきこと** | **単一 struct**（例: `nMax, nPage, nPos, maxScroll_si, valid`）と **更新者（T45 のみ）／無効化者（Reset・T45 早期 return）** の一覧。**FormatScroll / ScrollLog が読むフィールド集合**を **1 行に固定**。 |

#### 8.4.4 `FormatScroll` の文言生成（`Win32_DebugOverlay_FormatScrollDebugOverlay`）

| 項目 | 契約 |
|------|------|
| **入力** | 引数：モードラベル、content 系、`scrollY`、`rawClientH`、`scrollVpH`、F7/F8 ターゲット、`provisionalNoSi`、`compactScrollBand` 等（シグネチャは `Win32DebugOverlay.h`）。**T46** は **引数ではなく static**（§8.4.3）。 |
| **出力** | バッファへ `[scroll]` テキスト（SI 行の有無は `s_t46LastSiValid` と fill-monitor 相当の分岐）。 |
| **呼び出し順** | legacy **`ComputeLayoutMetrics`** 内では **unified＋Mark（T52）の後**、**ScrollTarget 計算の前後**に複数箇所から呼ばれ得る — **いずれも「レイアウト確定後」**が前提。 |
| **崩すと危険** | **T46 契約を変えずに**ファイルだけ分けるのは可。**引数と static のどちらに SI を載せるか**を混在させると二重表示・未初期化。 |
| **移す前に固めるべきこと** | **§8.4.3 の struct 化**が済んでから（または **FormatScroll を薄いラッパ＋共通生成関数**に分割する契約を固定）。 |

#### 8.4.5 `Win32_DebugOverlay_ResetProvisionalLayoutCache`

| 項目 | 契約 |
|------|------|
| **入力** | なし（公開 API）。 |
| **呼び出し順（本体内部）** | **①** `Win32_LegacyStacked_ClearPaintLayoutMetricsFromPaintValid`（T52）**②** `Win32_LegacyStacked_ApplyMainAppPaintDbgResetProvisionalLayoutExtras`（MainApp 共有の暫定付随リセット）**③** `s_t46LastSiValid = false`。 |
| **呼び出し元（例）** | `MainApp.cpp` 内のキャッシュ破棄タイミング（grep で固定）。**ページ式経路からも**呼ばれ得る。 |
| **崩すと危険** | **T52 だけクリア**して T46 を残すと **「暫定レイアウト」ログと SI 行の組み合わせ**が不整合。**順序を入れ替える**と一瞬 **stale SI** が `[scroll]` に残る。 |
| **移す前に固めるべきこと** | 呼び出し元一覧と **「暫定／確定」の定義**（`HUD_PAGED_ACCEPTANCE`・本書 §2 と矛盾しないこと）。T46 の **別 TU 移設**後は **無効化を同じトランザクション**で行う API にまとめる。 |

#### 8.4.6 T45／T46 を動かす判断に使うチェックリスト（移設前）

1. **unified の ①→②→③**（§8.4.1）を崩さない。  
2. **fill-monitor** では T45 は SI を書かず T46 は無効 — **FormatScroll** は「(n/a — fill-monitor / no native scrollbar)」系の分岐と一致。  
3. **T46 のフィールド意味**を struct で固定し、**更新は T45 のみ／無効化は T45 早期 return と Reset**。  
4. **ScrollLog** の SI 行は **T46 → GetScrollInfo フォールバック**の順 — 契約を変えるなら両方更新。  
5. **T19/T20** 本文ロジックには直接触れない（本契約は **デバッグオーバーレイ・スクロール補助**）。

---

## 9. 更新履歴

| 日付 | 内容 |
|------|------|
| 2026-04-06 | 初版（実コード依存の棚卸し。ページ式正・レガシー互換・T37 共有を区分） |
| 2026-04-06 | **コード上の境界**: `Win32DebugOverlay.cpp` の file-local static を legacy レイアウト用と明示、`ComputeLayoutMetrics` / `PaintStackedLegacy` / `Win32DebugOverlay_Paint` のコメントを整理。`MainApp.cpp` の共有 `s_paint*` にブロックヘッダ（挙動不変） |
| 2026-04-06 | **§7 追加**: レガシー縦積みの将来分離メモ（パイプライン単位・宣言順・T46/共有読み取り・CALCRECT 共有） |
| 2026-04-06 | **§7.2 追記**: legacy スクラッチを `Win32DebugOverlay.cpp` 先頭で **匿名名前空間**に束ねる実装（挙動・シンボル名は維持） |
| 2026-04-06 | **§7.1 追記**: `ComputeLayoutMetrics` / `PaintStackedLegacy` 本体も同一匿名名前空間に配置（前方宣言はファイル先頭ブロックへ集約） |
| 2026-04-06 | **§7.5 追加**: legacy パイプライン入出力境界（`Win32_LegacyStacked_*` 束・shared / extern / 副作用） |
| 2026-04-06 | **§7.6 追加**: 副作用の出口と `RunGdiPaint` / `RunComputeLayoutMetrics` adapter |
| 2026-04-06 | **§7.7 追加**: legacy 本体が更新する shared / scratch / T45・T46 / GDI の区分表 |
| 2026-04-06 | **§7.7 追記**: `outHud->dbgHud*` 書き込みを `Win32_LegacyStacked_ApplyD2dHudPrefill` に集約（挙動不変） |
| 2026-04-06 | **§7.7 追記**: category A の一部を scratch 専用 apply helper に集約（T45/T46・MainApp extern は未変更） |
| 2026-04-06 | **§7.7 追記**: vmSplit スクラッチ更新を `RunVmSplitScratchPass` / `VmSplitScratchPassOut` に局所化（挙動不変、MainApp extern は呼び出し元で適用） |
| 2026-04-06 | **§7.7 追記**: vmSplit 由来の category B 一部を `ApplyVmSplitMainAppExternFromScratchPass` に集約（scratch パスは extern 非接触のまま） |
| 2026-04-06 | **§7.7 追記**: scroll 系の薄い出口として `ApplyScrollLineMetricsFromHdc` / `SetDbgMaxScrollAndClampScrollY`（T45/T46 は未変更） |
| 2026-04-06 | **§7.7 追記**: T45/T46/T52 境界として `RunUnifiedMaxScrollClampAndT45` / `UnifiedScrollLayoutForT45` / `MarkPaintLayoutMetricsFromPaintValid`（T45 本体ロジックは未変更） |
| 2026-04-06 | **§7.1 / §7.2 追記**: legacy スクラッチ・`Win32_LegacyStacked_*` を **`Win32DebugOverlayLegacyStacked.cpp`** へ物理分離（第一歩、`internal.h` で共有読み取り） |
| 2026-04-06 | **`ComputeLayoutMetrics` / `PaintStackedLegacy` / CALCRECT/T60 計測**を **`Win32DebugOverlayLegacyStacked.cpp`** へ移動（`internal.h` に `RunGdiPaint` / `RunComputeLayoutMetrics` を追加）。fill-monitor 判定は **`Win32_MainWindow_IsFillMonitorPresentationMode`** |
| 2026-04-06 | **§5 追記**: `Win32DebugOverlayLegacyStacked.cpp` / `internal.h` の役割を表に追加 |
| 2026-04-06 | **§7.2 追記**: `internal.h` の公開面を縮小（struct + `Win32_LegacyStacked_*` のみ；`extern` は各 .cpp） |
| 2026-04-06 | **§7.2 追記**: main TU の legacy スクラッチ／T52 参照を `LoadLayoutScratchRead` + getter/setter に寄せ、`Win32DebugOverlay.cpp` の `extern` ブロックを撤去（挙動不変） |
| 2026-04-06 | **§5 / §7.2 追記**: MainApp 共有 `s_paintDbg*`／scroll の legacy 側 `extern` を `Win32MainAppPaintDbg_shared_link.h` に集約し、`LoadMainAppPaintDbgRead` / `ApplyMainAppPaintDbgScrollLineMetrics` を追加（挙動不変） |
| 2026-04-06 | **§5 追記**: `shared_link.h` を overlay 用 MainApp `extern` 一式に拡張。`MainAppPaintDbgRead` を拡張し、ScrollTarget / ScrollLog / `PaintStackedLegacy` の読み取りを `Load*` に寄せ、`ApplyMainAppPaintDbgResetProvisionalLayoutExtras` を追加（挙動不変） |
| 2026-04-06 | **§7.7 追記**: `ComputeLayoutMetrics` 内の `[scroll]` / `ScrollLog`／暫定 `FormatScroll` は `LoadMainAppPaintDbgRead` スナップショットを使用（`ApplyScrollLineMetricsFromHdc` の else を `ApplyMainAppPaintDbgScrollLineMetrics` に統一；vmSplit→MainApp extern の代入は file-static helper に集約。T45/T46 本体は未変更） |
| 2026-04-06 | **§7.7 追記**: `ComputeLayoutMetrics` 内の MainApp 共有への薄い write を `ApplyMainAppPaintDbg*`（ContentAndClientGeometry / PostOverlayMeasures / T14BudgetHeights 等）に寄せる（T45/T46 本体は未変更、挙動不変） |
| 2026-04-06 | **§7.1 / §7.2 追記**: **`Win32DebugOverlayLegacyStacked_bridge.h`** を追加（main TU が include する最小橋渡し）。**`Win32DebugOverlayLegacyStacked_internal.h`** は bridge を include した legacy 実装向けの宣言に再編（`WindowsRenderer.h` は internal 側のみ。挙動不変） |
| 2026-04-06 | **bridge 追記**: **`RunGdiPaintFromPaintEntry`** / **`RunComputeLayoutMetricsForD2dPrefill`** — main TU の `Win32DebugOverlay_Paint` / `PrefillHudLeftColumnForD2d` から `RunGdiPaint` / `RunComputeLayoutMetrics` への struct 組み立てを legacy TU に寄せる（挙動不変） |
| 2026-04-06 | **§5 / コメント整備**: `Win32DebugOverlay.cpp` / `bridge.h` / `internal.h` のファイル頭・`#pragma region` を、main / bridge / internal / legacy TU の現状境界に合わせて更新（挙動不変） |
| 2026-04-06 | **§8 追加**: legacy 縦積み TU 分離後も main TU（`Win32DebugOverlay.cpp`）に残る T45/T46/T52/shared scroll 等の **残留理由・影響・分離余地** を表で整理。§1 表・§2.4 の `ComputeLayoutMetrics` 所在を legacy TU と明記。§7 冒頭から §8 へ参照（挙動不変） |
| 2026-04-06 | **§8.3 追加**: main TU 残留要素の **仕分け**（今後も main／条件付きで移せる／今触ると危険）と短い理由・前提・保留理由（挙動不変） |
| 2026-04-06 | **§8.4 追加**: §8.3 B の **データ契約／呼び出し契約**（unified ①②③、T45/T46、FormatScroll、Reset）。T45/T46 移設前チェックリスト（挙動不変） |
