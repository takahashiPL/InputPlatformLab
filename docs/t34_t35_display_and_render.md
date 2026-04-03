# T34 / T35 — 表示モードとレンダリング解像度

本稿は **T34（完了した実験）** の記録です。**T35（3 モードの方針・表・Fullscreen と CDS）** は **[t35_display_mode_policy.md](t35_display_mode_policy.md)** に集約しました。

T17 のウィンドウ再生成・ログ（`targetPhys` / `client` / `outer`）は **モード方針・枠サイズ**を表し、**T34 のオフスクリーン解像度**とは別軸として扱います。

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

## T35 — 設計メモの所在

**[t35_display_mode_policy.md](t35_display_mode_policy.md)** に、現状の 3 モードの表、描画経路、Borderless で T34 を正式採用する場合の利点/欠点、Fullscreen の CDS とレンダリング解像度の関係をまとめています。

---

## 関連ファイル

- 実装: `app/InputPlatformLab/MainApp/src/platform/win/WindowsRenderer.cpp`, `MainApp.cpp`
- 進捗メモ: [worklog.md](worklog.md)
- 段階計画: [roadmap.md](roadmap.md)
