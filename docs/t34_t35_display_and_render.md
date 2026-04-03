# T34 / T35 — 表示モードとレンダリング解像度

本稿は **T34（完了した実験）** と **T35（今後の整理対象）** を分けて記録するための 1 枚です。T17 のウィンドウ再生成・ログ（`targetPhys` / `client` / `outer`）は **モード方針・枠サイズ**を表し、**T34 のオフスクリーン解像度**とは別軸として扱います。

---

## T34 — 完了内容（Borderless のみ）

**目的**: T14 で Enter 確定した **committed 解像度**を、スワップチェーンのクライアント（モニタ全面など）とは独立した **オフスクリーン RT** で描画し、**バックバッファへ拡大合成して Present** する実験を成立させる。

**対象**: **Borderless** かつ **committed 幅・高さが有効**なときのみ（`MainApp` の `Win32_RefreshRendererGridDebugParams` がフラグを立てる）。

**パイプライン（概要）**:

1. **オフスクリーン**: `committed W×H` の `ID3D11Texture2D` + RTV を確保（ログ `[T34][RT] offscreen create`）。
2. **描画**: D3D でクリア → D2D で **デバッググリッド**（任意）と **T33 の 1 行**をオフスクリーンの DXGI 表面へ（ログ `[T34][RT] draw offscreen`）。
3. **合成**: スワップチェーンのバックバッファ（**クライアント＝従来どおり全面サイズ**）を D3D でクリアし、D2D `DrawBitmap` でオフスクリーンを全クライアント矩形へ線形拡大（ログ `[T34][RT] composite to backbuffer client=`）。
4. **Present**: 従来どおり。
5. **GDI**: `WM_PAINT` の後段で **変更なし**（D3D の上にデバッグ文字列を載せる）。

**位置づけ**:

| 要素 | 解像度・座標の基準 |
|------|-------------------|
| グリッド分母・ラベル | MainApp が毎フレーム設定する `gridDebug*`（committed / client フォールバック） |
| D3D/D2D 実ピクセル（T34） | オフスクリーンは **committed**、最終表示は **swapchain client** へスケール |
| GDI オーバーレイ | **クライアント論理ピクセル**（従来どおり） |

**実装の置き場**: `WindowsRenderer.cpp`（オフスクリーン・合成）、`WindowsRenderer.h`（状態フィールド）、`MainApp.cpp`（Borderless + committed 時のフラグ設定）。

---

## T35 — スコープ（未完了・設計整理）

**目的**: **Windowed / Borderless / Fullscreen** それぞれについて、次を **モード方針として一貫**させる（T17 のログとレンダラの挙動を矛盾なく説明できる状態にする）。

整理する観点（各モードごとに「現状」「理想」「T34 との関係」を決める）:

| 観点 | 決めることの例 |
|------|----------------|
| **window / client size** | 外枠・クライアントを誰が決めるか（T16/T17、`AdjustWindowRectExForDpi`、fill monitor 等） |
| **swapchain size** | `ResizeBuffers` の幅・高さ＝クライアントに固定するか、別ルールにするか |
| **offscreen render size** | committed を常に使うか、モード別に使い分けるか、T34 を Borderless 限定のままにするか拡張するか |
| **present path** | 直接バックバッファ描画のみ / T34 合成のみ / モードで切替 |
| **GDI overlay path** | 常にクライアント座標か、仮想解像度と揃えるか |

**現時点の明示**:

- **T34 は Borderless 実験段階**に留まる。Windowed / Fullscreen では **オフスクリーン合成は行わない**（従来のスワップチェーン直接描画）。
- T17 の **apply ログ**（`targetPhys` / `client` / `outer`）は **ウィンドウ・デスクトップ方針**であり、**T34 の committed オフスクリーン解像度と一致しない**ことがある（意図した分離）。

---

## 関連ファイル

- 実装: `app/InputPlatformLab/MainApp/src/platform/win/WindowsRenderer.cpp`, `MainApp.cpp`
- 進捗メモ: [worklog.md](worklog.md)
- 段階計画: [roadmap.md](roadmap.md)
