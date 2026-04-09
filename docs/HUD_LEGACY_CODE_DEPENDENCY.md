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
| `Win32DebugOverlay.cpp` | `Paint` / `PaintStackedLegacy` / `ComputeLayoutMetrics` / 滾動ログ・計測（legacy スクラッチの **定義**は `Win32DebugOverlayLegacyStacked.cpp`）。 |
| `Win32DebugOverlayLegacyStacked.cpp` | レガシー縦積み用スクラッチ・`Win32_LegacyStacked_*`。`internal.h` は型とヘルパ宣言のみ（`extern` は各 .cpp）。 |
| `Win32DebugOverlay.h` | 外部公開のオーバーレイ API・`Win32_FillMenuSamplePaintBuffers` 宣言。 |
| `WindowsRenderer.cpp` | D3D/D2D、T37 DWrite 本文、ページ式時の D2D HUD 重複防止。 |

---

## 6. 維持・削減の優先順位（別紙）

**維持対象・削減候補・依存が広い箇所・paged への寄せやすさ**の判断用整理は **`docs/HUD_LEGACY_MAINTENANCE_PRIORITIES.md`**（本書 §4 の補足を表形式で展開）。

---

## 7. 将来のレガシー縦積み分離（`Win32DebugOverlay.cpp`）メモ

**目的**: マクロ 0 互換経路を **別翻訳単位**へ移す検討時の、**切り出し単位**と **同一ファイルに残す理由**のメモ（実装タスクの確定ではない）。

### 7.1 まとまりとして切り出しやすい単位（パイプライン）

| 単位 | 内容 | 備考 |
|------|------|------|
| **Legacy stacked pipeline** | `Win32_DebugOverlay_ComputeLayoutMetrics` + `Win32_DebugOverlay_PaintStackedLegacy` | `Win32_DebugOverlay_PrefillHudLeftColumnForD2d` の legacy 分岐からも `ComputeLayoutMetrics` を呼ぶため、**D2D プレフィル**とリンク上は同じ束になりやすい。**実装**: スクラッチ・`Win32_LegacyStacked_*` ヘルパーは **`Win32DebugOverlayLegacyStacked.cpp`**（`Win32DebugOverlayLegacyStacked_internal.h`）へ **第一歩分離**（シンボル名は従来どおり）。本体パイプラインは **`Win32DebugOverlay.cpp`** の匿名名前空間に残し、共有側は `internal.h` 経由で scratch を **読む**。 |
| **GDI 入口の薄い分岐** | `Win32DebugOverlay_Paint` 内の `Win32_HudPaged_IsEnabled()` → `PaintStackedLegacy` | 既に短い。 |

### 7.2 同一ファイル先頭に置く必要があるもの（宣言順）

**legacy 縦積み用スクラッチ**（`s_paintDbgT14VmSplit*` 等）と **T52** `s_paintDbgLayoutMetricsFromPaintValid` の **定義**は **`Win32DebugOverlayLegacyStacked.cpp`**。`Win32DebugOverlayLegacyStacked_internal.h` は **I/O struct** と **`Win32_LegacyStacked_*` の宣言**のみ（グローバル `extern` は載せない）。**`Win32DebugOverlay.cpp`** は当該スクラッチ／T52 を **`extern` ブロック**で参照（ScrollTarget・Reset・GDI paint）。**`Win32DebugOverlayLegacyStacked.cpp`** は **MainApp.cpp** 由来の **`extern`** を **同 .cpp 先頭**に限定（ApplyVmSplit 等が触る `s_paintDbgT17DocY` 等）。**`Win32_DebugOverlay_LegacyStacked_InvokeT45`** は **main TU** の static T45 への橋渡しで **`Win32DebugOverlay.cpp` に定義**、legacy TU は **前方宣言のみ**。**C++ では名前は宣言より前に使えない**ため、この群をさらに移す場合は共有側のスクロール API を **前方宣言・getter 化・ファイル順の再配置**のいずれかで揃える必要がある。シンボル名は `s_paintDbg*` のまま。

### 7.3 「レガシーで更新・共有で読む」ため分離が一緒に動きやすいもの

| 種別 | 例 | メモ |
|------|-----|------|
| T46 / T52 まわり | `s_t46LastSi*`、`s_paintDbgLayoutMetricsFromPaintValid` | `Win32_T45_ApplyWindowedScrollInfo`（レガシー計測経路）で更新、`Win32DebugOverlay_ScrollLog` / `Win32_DebugOverlay_FormatScrollDebugOverlay`（共有）が参照。**完全な legacy-only ファイル分離**は、`[scroll]` 文字列生成側の整理とセットになりやすい。 |

### 7.4 共有ヘルパー（legacy 専用ファイルへ「丸ごと」は移しにくい）

`Win32_MenuSampleMeasure*`、`Win32_T60_*`、`Win32_MainView_MeasureScrollOverlayTextHeight` 等は **CALCRECT / 計測**として `ComputeLayoutMetrics` と **ページ式以外の経路**からも参照され得る。将来分離する場合は **共有ヘッダ＋共通 `.cpp`**、または **現ファイルに残す**前提が現実的。

### 7.5 legacy パイプラインの入出力境界（依存面の整理）

**目的**: レガシー縦積み本体と **shared 側**のあいだで、何が **引数で渡り**、何が **副作用**になるかを固定しやすくする（実装は `Win32DebugOverlay.cpp` 先頭の無名名前空間内 `Win32_LegacyStacked_*` 束）。

| 区分 | 内容 |
|------|------|
| **明示入力（値）** | `Win32_LegacyStacked_CommonParams`（`hwnd` / `hdc` / T17 ラベル）、GDI 用の `Win32_LegacyStacked_GdiPaintParams`（抑制フラグ）、レイアウト用の `Win32_LegacyStacked_LayoutMetricsParams`（`outHud` / `logScroll`）。`Win32DebugOverlay_Paint` と `Win32_DebugOverlay_PrefillHudLeftColumnForD2d`（legacy 分岐）が構築。 |
| **shared ヘルパー呼び出し（読み取り中心）** | `Win32_MenuSampleMeasure*`、`Win32_T60_*`、`Win32_MainView_MeasureScrollOverlayTextHeight`、`Win32_IsMainWindowFillMonitorPresentation` 等（§7.4）。レガシー専用ファイルへ切り出すときは **宣言を共有ヘッダ**へ寄せる想定。 |
| **MainApp 共有状態（extern `s_paint*`）** | `ComputeLayoutMetrics` / `PaintStackedLegacy` が **読み書き**（§2.3）。別 `.cpp` 化時は **extern 宣言の共有ヘッダ**または **MainApp のみ**に残す判断が必要。 |
| **ファイル先頭スクラッチ（無名名前空間）** | `s_paintDbg*`（§7.2）。`ComputeLayoutMetrics` が主に書き、共有の `ScrollTargetT17*` 等が読む。 |
| **T46 / T52（§7.3）** | レガシー計測経路で更新、`ScrollLog` / `FormatScrollDebugOverlay` が参照 — **完全な legacy-only 分離は別タスク**。 |
| **出力（副作用）** | `WindowsRendererState` の D2D プレフィル欄（`outHud` 非 null 時）、GDI `DrawText`、条件付き `Win32_T45_ApplyWindowedScrollInfo`、先頭スクラッチ・`s_paintDbg*` 更新。adapter 境界は **§7.6**、**書き込み先の区分**は **§7.7**。 |

### 7.6 副作用の出口と adapter（実装メモ）

**目的**: レガシー縦積み本体が **どこへ**副作用を出すかを、入口（§7.5）に続けて追いやすくする。実装は `Win32DebugOverlay.cpp` の無名名前空間。

| 要素 | 役割 |
|------|------|
| `Win32_DebugOverlay_LegacyStacked_RunGdiPaint` | **WM_PAINT** の legacy 分岐から **唯一**呼ばれる薄い境界（→ `Win32_DebugOverlay_PaintStackedLegacy`）。将来、GDI 入口と縦積み本文の間の接続をここに固定しやすい。 |
| `Win32_DebugOverlay_LegacyStacked_RunComputeLayoutMetrics` | **D2D Prefill（legacy）** と **GDI 本文の初回計測**の双方が通る境界（→ `Win32_DebugOverlay_ComputeLayoutMetrics`）。計測の副作用出口はまだ本体側に集約。 |
| **本体** `ComputeLayoutMetrics` / `PaintStackedLegacy` | 実際の **extern `s_paintDbg*`**、**先頭スクラッチ**、`outHud` 書き込み、**T45→T46**、**GDI `DrawText`** はここに残る（§7.3 の共有読み取り出口は別タスク）。**書き込み先の棚卸し**は **§7.7**。 |

### 7.7 legacy 本体が更新する状態（shared / scratch / scroll 系の見える化）

**目的**: `ComputeLayoutMetrics` / `PaintStackedLegacy` が **どのカテゴリの状態**を触るかを表に固定する（実装の完全な行一覧ではなく、追いかけの地図）。挙動変更はしない。

| 区分 | 主な更新対象 | 主に触る関数 | 備考 |
|------|----------------|--------------|------|
| **A — ファイル先頭スクラッチ**（無名名前空間） | `s_paintDbgFinalBodyTopPx`、`s_paintDbgBodyT14DocTopPx`、`s_paintDbgFinalRow1HeightPx`、`s_paintDbgRow2TopPx`、`s_paintDbgT14VmSplitActive`、`s_paintDbgT17DocYRestScroll`、`s_paintDbgRestViewportTopPx`、`s_paintDbgT53ScrollBandDrawEnabled`、`s_paintDbgT14VmSplitPrefix` / `VmBand` / `Rest` と各 `*H` | `ComputeLayoutMetrics` が **書き**、`PaintStackedLegacy` が **読み**（GDI クリップ・分割描画）。帯幾何・vmSplit フラグ・T53・`RestViewportTop` クリアの **一部**は **`Win32_LegacyStacked_ApplyScratchFinalHudGeometry`** / **`ResetVmSplitScratchFlags`** / **`ClearScratchRestViewportTop`** / **`ApplyScratchT53ScrollBandDrawEnabled`** に集約。vmSplit の **wmemcpy / バンド文字列 / DrawText 計測**は **`Win32_LegacyStacked_RunVmSplitScratchPass`**（`Win32_LegacyStacked_VmSplitScratchPassOut`）に **局所化**（**MainApp extern 非接触**）。 | §7.2 の宣言順の理由。将来 `.cpp` 分割時は **スクラッチごと移す**か **getter** が必要。 |
| **B — MainApp extern（`s_paintDbg*` 等）** | 例: `s_paintDbgContentHeight`、`s_paintDbgContentHeightBase`、`s_paintDbgExtraBottomPadding`、`s_paintDbgClientW`/`H`、`s_paintDbgT17DocY`、`s_paintDbgMaxScroll`、`s_paintDbgScrollBandReservePx`、`s_paintDbgLayoutRestVpBudgetHint`、`s_paintDbgT14*` 系、`s_paintScrollLinePx` 等 | ほぼ **`ComputeLayoutMetrics`** に代入が集中 | `MainApp.cpp` 定義。入力・T37・滾動 UI と共有（§2.3）。vmSplit の **`s_paintDbgT17DocY` / `s_paintDbgT14VisibleModesDocStartY` / `s_paintDbgT14LayoutValid`**（後者 2 つは `t14LayoutOutValid` が true のときのみ）は **`Win32_LegacyStacked_ApplyVmSplitMainAppExternFromScratchPass`**（`VmSplitScratchPassOut` を入力）で **`RunVmSplitScratchPass` 成功後**に適用。スクロール行高は **`ApplyScrollLineMetricsFromHdc`**。**`s_paintDbgMaxScroll`** と **`ClampScrollYToMaxScroll`** の連続は **`SetDbgMaxScrollAndClampScrollY`**（phase1 / vmSplitRefine）。**unified** では同 **+ T45** を **`RunUnifiedMaxScrollClampAndT45`**（`UnifiedScrollLayoutForT45`）に集約（§7.7 D）。 |
| **C — D2D プレフィル（`outHud`）** | `WindowsRendererState::dbgHud*` 各フィールド（左列テキスト、スクロールバンド、vmSplit バンド等） | **`Win32_LegacyStacked_ApplyD2dHudPrefill`**（`ComputeLayoutMetrics` 内、`outHud != nullptr` 時のみ）。`ComputeLayoutMetrics` 本体は `outHud->` を直接書かない。 | Prefill（legacy）経路。vmSplit 用の文字列コピーは引き続きファイル先頭スクラッチ（`s_paintDbgT14VmSplit*`）を読む。 |
| **D — T45 / T46 / T52** | `Win32_T45_ApplyWindowedScrollInfo` 経由で **`s_t46LastSi*`**；**`s_paintDbgLayoutMetricsFromPaintValid`** | **`ComputeLayoutMetrics`** 後半（スクロール範囲確定後） | `ScrollLog` / `FormatScrollDebugOverlay` が **読む**（§7.3）。unified 経路の入口は **`RunUnifiedMaxScrollClampAndT45`**（→ **`Win32_T45_ApplyWindowedScrollInfo`**、T46 は同関数内）。**T52** 確定は **`MarkPaintLayoutMetricsFromPaintValid`**。 |
| **E — GDI ラスタ（永続グローバルではない）** | `DrawTextW`、`SetTextColor`、`OffsetViewportOrgEx`（`s_paintScrollY` を参照） | **`PaintStackedLegacy`** | フレームバッファ上の可視出力。状態テーブルでは **B に含めない**。 |

---

## 8. 更新履歴

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
| 2026-04-06 | **§5 追記**: `Win32DebugOverlayLegacyStacked.cpp` / `internal.h` の役割を表に追加 |
| 2026-04-06 | **§7.2 追記**: `internal.h` の公開面を縮小（struct + `Win32_LegacyStacked_*` のみ；`extern` は各 .cpp） |
