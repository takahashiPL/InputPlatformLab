# T35 — 表示モード方針（Windowed / Borderless / Fullscreen）

設計整理用の 1 枚です。**実装の正**はコード（`MainApp.cpp` の T16/T17、`WindowsRenderer.cpp`）です。本稿は現状の描画経路を整理し、今後決めるべき論点を明示します。

**関連**: [T34 実験の記録（オフスクリーン合成）](t34_t35_display_and_render.md) — T34 はレンダラ実験、T17 の `targetPhys` / `client` / `outer` とは別軸。

---

## 1. 現状の描画経路（共通）

すべてのモードで **`WM_PAINT` → `Win32_MainView_PaintFrame`** が 1 フレームの入口です。

| 段 | 処理 | 備考 |
|----|------|------|
| 1 | `Win32_RefreshRendererGridDebugParams` | グリッド用 `gridDebug*`、Borderless 時は T34 用フラグ |
| 2 | `WindowsRenderer_Frame` | D3D clear →（T34 時はオフスクリーン経由）→ D2D グリッド+T33 → Present |
| 3 | `Win32DebugOverlay_Paint`（GDI） | 同一クライアント HDC 上にデバッグ文字（スクロール含む） |

**スワップチェーン**: `WindowsRenderer_Init` / `Resize` で **クライアント幅×高さ**に `ResizeBuffers`。**Present** は `WindowsRenderer_Frame` 内。

---

## 2. モード別の現状（表）

### 2.1 Windowed

| 観点 | 現状 |
|------|------|
| **Window / client** | T14 選択モード（なければ T15 最近傍）を **クライアント物理サイズ**にし、`AdjustWindowRectExForDpi` で **外枠**を算出。`fillMonitorPhysical = FALSE`。通常ウィンドウ枠。 |
| **Swapchain** | クライアントと一致（`Resize` で追従）。 |
| **Offscreen** | **なし**。バックバッファへ直接 D2D。 |
| **Present** | 直接描画パス（T34 分岐なし）。 |
| **GDI** | クライアント HDC。ドキュメント高さ・スクロールは `Win32DebugOverlay` 既定。 |

### 2.2 Borderless

| 観点 | 現状 |
|------|------|
| **Window / client** | 選択モニタの **monitor_rect 全面**に `WS_POPUP` + `fillMonitorPhysical`。**CDS でデスクトップ解像度は変えない**（コメントどおり）。`createPhysicalW/H` = キャッシュ矩形の幅・高さ。 |
| **Swapchain** | クライアント＝全面（例: 2539×1440）。**T14 の committed とは別**。 |
| **Offscreen** | **T34**（Borderless かつ committed 有効時）: **committed W×H** の RT にグリッド/T33 を描き、**バックバッファへ拡大合成**。無効時は直接描画。 |
| **Present** | T34 時は合成後に 1 回、`Present(1,0)`。非 T34 時は従来どおり。 |
| **GDI** | 全面クライアント上。仮想解像度（T34）とは **ピクセル密度が一致しない**可能性あり。 |

**T17 のログの意味（未整理の論点）**:

- **`targetPhys`**（`s_t16LastTargetPhysicalW`）: `Win32_T17_BuildFillMonitorConfig` 内では **T15 最近傍モード**由来の `physW×physH`（`GetTargetClientSizeFromNearestMode`）が入ることが多く、**T14 のリスト選択と別物**になり得る。
- **`client`**: `GetClientRect` の **実クライアント**（全面）。
- **`outer`**: 実枠の外周。

→ **T34 の committed 解像度**は **Enter 時に保存した T14 選択**と一致させるが、**上記 T17 ログはウィンドウ枠・nearest 解像度**を示すため、**数値が一致しないことがある**（意図とズレの整理は T35 の残課題）。

### 2.3 Fullscreen

| 観点 | 現状 |
|------|------|
| **Window / client** | **CDS成功時**: `ChangeDisplaySettingsEx(..., CDS_FULLSCREEN)` で **T15 最近傍**の `width×height` にデスクトップ解像度を合わせたうえで、**外枠をその物理サイズ**でモニタ左上に配置（`outerUsesCdsModeSize`）。**CDS 失敗時**: Borderless と同様に **fullMonitor_rect** の fill。 |
| **Swapchain** | クライアント＝その時点の **クライアント矩形**（CDS 成功時は多くの環境で **CDS モードと同じピクセル数**）。 |
| **Offscreen** | **なし**（T34 は Borderless のみ）。 |
| **Present** | 直接描画パス。 |
| **GDI** | 全面クライアント HDC。 |

### 2.4 Fullscreen — CDS と「レンダリング解像度」の関係（整理）

| 項目 | 内容 |
|------|------|
| **CDS が何を変えるか** | 指定モニタの **表示モード**（`DEVMODE` の幅・高さ・bpp・Hz）。デスクトップの論理解像度が変わる。 |
| **アプリのクライアント** | 再生成後、`CreateWindow` / `fillMonitorPhysical` で **そのモードに合わせた物理ピクセル**でクライアントが取れる（実装は `outerUsesCdsModeSize` 分岐）。 |
| **スワップチェーン** | **クライアントにバインド**されるため、**通常は CDS で選んだ解像度＝バックバッファのピクセル数**（同一モードに収まる前提）。 |
| **T34 / Offscreen** | 現状 **未使用**。将来「T14 committed と必ず一致させる」場合は **offscreen を committed サイズにし、合成してもよい**が、**CDS 済みモードと二重に解像度を変える意味**を整理する必要がある（T35）。 |
| **CDS 失敗時** | デスクトップは変わらず、**Borderless 相当の fill** → クライアントは **monitor_rect 全面**に近い。レンダリングは **そのクライアント**に対する直接描画。 |

---

## 3. T35 方針案（決めること）

| 論点 | 候補の方向 |
|------|------------|
| **ログの意味** | `targetPhys` / `client` / `outer` を **どの解像度（T14 / T15 / T16）と必ず紐づけるか**を文書化。 |
| **Borderless** | **T34 を正式採用するか**（下節）。採用する場合、T17 のログを **committed または「表示用レンダリング解像度」** に揃えるか。 |
| **Windowed** | オフスクリーンを導入するか、**常に client = 論理ウィンドウ**のままにするか。 |
| **Fullscreen** | **CDS モードと T14 committed の関係**（必ず一致させるか、nearest のみか）。オフスクリーンの要否。 |
| **GDI** | 全面モードで **仮想解像度（T34）と見た目を揃える**か、**クライアント実ピクセルのまま**か。 |

---

## 4. Borderless で T34 を正式採用する場合の利点 / 欠点

| 利点 | 欠点 |
|------|------|
| **T14 で選んだ解像度**で D3D/D2D の描画解像度を固定でき、**実験・検証が明確**になる。 | **スケール拡大**のコストと、**ブラー／線の太さ**がモニタサイズに依存する。 |
| スワップチェーンは **従来どおり全面**のままにできる（モニタ変更と独立）。 | **GDI は依然フルクライアント**のため、**オーバーレイは「仮想解像度」と一致しない**（ドキュメント座標・CALCRECT の意味の整理が必要）。 |
| T17 の **ウィンドウ方針（fill monitor）**と **レンダリング解像度**を **概念分離**しやすい。 | **CreateBitmapFromDxgiSurface** 二段や **GPU 差**によるフォールバック経路の保守。 |
| デバッググリッドの **cell 物理意味**と **描画ピクセル**を近づけられる。 | **正式採用**には、パフォーマンス・失敗時のポリシー・ログの一本化が必要。 |

---

## 5. 関連ファイル

| ファイル | 内容 |
|----------|------|
| `MainApp.cpp` | T16/T17 再生成、`Win32_RefreshRendererGridDebugParams`、T34 フラグ |
| `WindowsRenderer.cpp` | T34 パス、`Present` |
| `Win32DebugOverlay.cpp` | GDI オーバーレイ |
| [t34_t35_display_and_render.md](t34_t35_display_and_render.md) | T34 の短い記録 |

---

*本稿は「設計メモ」であり、未決定の項目は実装変更時に [worklog.md](worklog.md) へ追記する想定です。*
