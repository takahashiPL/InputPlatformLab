# T35 — 表示モード方針（Windowed / Borderless / Fullscreen）

**実装の正**はコード（`MainApp.cpp` の T16/T17、`WindowsRenderer.cpp`）です。本稿は **T35 として採用する方針を固定**したものです（実装変更が入る場合は本稿と [worklog.md](worklog.md) を更新する）。

**関連**: [T34 完了記録](t34_t35_display_and_render.md) — T34 は **完了**。T17 のウィンドウログ（`targetPhys` / `client` / `outer`）は **レンダラの committed 解像度とは別軸**（下記 §3）。

---

## 0. T34 の位置づけ（完了・維持）

- **Borderless** かつ **T14 Enter 確定の committed 幅・高さが有効**なとき、**committed W×H** のオフスクリーン RT に D3D/D2D 描画し、**スワップチェーンのバックバッファ（クライアント全面）へ線形拡大合成**してから Present する経路を **本プロジェクトの正式な Borderless レンダリング経路**とする（640×480 / 4096×2160 等で create / draw / composite を検証済み）。
- **Windowed** では **T34/T36 オフスクリーンは使わない**（直接バックバッファ描画のみ）。**Fullscreen** では **T36（実験）** が committed オフスクリーンを試す（T34 とは別）。

---

## 1. 1 フレームの共通経路

すべてのモードで **`WM_PAINT` → `Win32_MainView_PaintFrame`** が入口です。

| 順 | 処理 | 備考 |
|----|------|------|
| 1 | `Win32_RefreshRendererGridDebugParams` | `gridDebug*`、Borderless では T34 用フラグ |
| 2 | `WindowsRenderer_Frame(..., presentationMode)` | T35: **Borderless** のみ T34。**Fullscreen** は T36 実験（committed オフスクリーン）を試行。モード切替時に `[T35] offscreen enabled/disabled` を 1 回ログ。 |
| 3 | `Win32DebugOverlay_Paint`（GDI） | 同一クライアント HDC（D3D の上に重ねる） |

**スワップチェーン**: `Init` / `Resize` で **クライアント幅×高さ**に `ResizeBuffers`。**Present** は `WindowsRenderer_Frame` 内。

---

## 2. モード別の正式方針（固定）

### 2.1 Windowed

| 観点 | 方針（固定） |
|------|----------------|
| **Window / client** | T14 選択モード（なければ T15 最近傍）を **クライアント物理サイズ**にし、`AdjustWindowRectExForDpi` で **外枠**。`fillMonitorPhysical = FALSE`。 |
| **Swapchain** | **常にクライアントと同寸法**（`Resize` で追従）。 |
| **Offscreen** | **使用しない**。バックバッファへ直接描画。 |
| **Present** | 直接描画のあと **1 回** `Present`（T34 分岐なし）。 |
| **GDI** | **クライアント座標系**のまま（`Win32DebugOverlay` 既定）。 |

### 2.2 Borderless

| 観点 | 方針（固定） |
|------|----------------|
| **Window / client** | 選択モニタの **monitor_rect 全面**に `WS_POPUP` + `fillMonitorPhysical`。**CDS でデスクトップ解像度は変えない**。 |
| **Swapchain** | **常にクライアント全面**（モニタ作業領域のピクセル数）。**T14 committed とは別寸法になり得る**。 |
| **Offscreen** | **committed が有効なとき T34 を使用**（committed W×H の RT → バックバッファへ拡大合成）。committed が無効なら直接描画。 |
| **Present** | T34 時は合成後に **1 回** `Present`；非 T34 時も **1 回**。 |
| **GDI** | **クライアント実ピクセル**上に描画。T34 の「仮想レンダリング解像度」と **ピクセル対応は取らない**（オーバーレイは実クライアント基準のまま）。 |

### 2.3 Fullscreen

| 観点 | 方針（固定） |
|------|----------------|
| **Window / client** | **CDS 成功時**: `ChangeDisplaySettingsEx(..., CDS_FULLSCREEN)` で **T15 最近傍**のモードに合わせ、外枠をその **物理幅×高さ**でモニタ左上に配置。**CDS 失敗時**: Borderless と同様に **monitor_rect 全面** fill。 |
| **Swapchain** | **常にその時点のクライアントと同寸法**（CDS 成功時は多くの環境で **CDS モードのピクセル数**と一致）。 |
| **Offscreen** | **T35 既定**: 直接描画。**T36（実験）**: committed 有効時のみ、T14 committed サイズのオフスクリーン RT に描きバックバッファへ合成（ログ `[T36][RT]`）。失敗時は直接描画。T34（Borderless）とは **別リソース**。 |
| **Present** | 直接描画または T36 合成のあと **1 回** `Present`。 |
| **GDI** | **クライアント座標系**（全面クライアント HDC）。 |

### 2.4 Fullscreen — CDS とレンダリング解像度（固定整理）

| 項目 | 方針 |
|------|------|
| **CDS** | 指定モニタの **表示モード**（幅・高さ・bpp・Hz）を変える。 |
| **クライアント** | 再生成後、**CDS に合わせた物理ピクセル**でクライアントを取る（`outerUsesCdsModeSize` 分岐）。 |
| **スワップチェーン** | **クライアントにバインド** → **通常は CDS モードの解像度＝バックバッファのピクセル数**。 |
| **T14 committed と CDS** | **別概念**。ウィンドウ寸法は **T15 最近傍 + CDS** が主。レンダラ実験として **T36** が committed 解像度のオフスクリーンを試す（CDS/outer/client のロジックは変えない）。 |
| **CDS 失敗時** | デスクトップは不変 → **Borderless 相当の全面クライアント**へ。レンダリングは **そのクライアント**への直接描画。 |

---

## 3. T17 ログとレンダラ（別軸の固定ルール）

次は **矛盾ではなく責務の分離**として扱う。

| ログ・状態 | 意味（固定） |
|------------|----------------|
| **`targetPhys`**（例: `s_t16LastTargetPhysicalW/H`） | **ウィンドウ再生成・fill 用**に解決した目標クライアント寸法。T15 最近傍など **T14 のリスト選択と一致しない**ことがある。 |
| **`client` / `outer`** | **実際の**クライアント矩形・ウィンドウ外枠（`GetClientRect` / `GetWindowRect`）。 |
| **T34 committed 解像度** | **T14 Enter で確定した幅・高さ**。オフスクリーン描画とグリッド分母の主たる入力。 |

→ **Borderless で `targetPhys` と committed が違っても正常**（ウィンドウ方針ログとレンダラ実験解像度は別軸）。

---

## 4. Borderless で T34 を採用する理由（利点 / 留意点）

| 利点 | 留意点 |
|------|--------|
| T14 で選んだ **committed 解像度**で D3D/D2D の描画ピクセルを固定できる。 | 全面への **拡大**のコストと、線の見え方がモニタサイズに依存する。 |
| スワップチェーンは **全面のまま**維持でき、モニタ fill と独立して検証しやすい。 | GDI は **フルクライアント**のため、オーバーレイは **仮想 committed 座標と 1:1 にはならない**。 |
| T17 の **ウィンドウ方針**と **描画解像度**を概念分離できる。 | DXGI/D2D フォールバック経路の保守が必要。 |

---

## 5. 将来の変更（本稿の外）

次は **別タスク**で検討する（本稿の固定方針を変えるときは文書更新が前提）。

- T17 ログを **T14 committed** などと **数値上揃える** UI/ログ改善。
- **T36** を正式方針に昇格するか（現状は Fullscreen のみの **実験**、`[T36][RT]`）。
- GDI を **仮想解像度に合わせてスケール**するか。

---

## 6. 関連ファイル

| ファイル | 内容 |
|----------|------|
| `MainApp.cpp` | T16/T17、`Win32_RefreshRendererGridDebugParams` |
| `WindowsRenderer.cpp` | T34 オフスクリーン・合成・Present |
| `Win32DebugOverlay.cpp` | GDI オーバーレイ |
