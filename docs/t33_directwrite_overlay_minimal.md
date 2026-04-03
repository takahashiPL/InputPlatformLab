# T33: DirectWrite / D2D による 1 行オーバーレイ（最小実験）

## 完了条件（実装）

- `WindowsRenderer_Frame` 内で **clear（D3D）→ D2D `DrawText` 1 行 → Present 1 回**。
- D3D11 デバイスと **IDXGIDevice 経由で共有**する `ID2D1Device` / `ID2D1DeviceContext`、および `IDWriteFactory` / `IDWriteTextFormat` を `WindowsRenderer_Init` で作成。失敗時は従来どおり **clear + Present のみ**（ログ `[T33] D2D/DWrite init failed`）。
- 各フレーム、スワップチェーンのバックバッファから **IDXGISurface** を取得し `CreateBitmapFromDxgiSurface` でターゲット化（**Resize 後も**都度表面から生成。D2D デバイスは再作成しない）。
- **ログ**: `[T33] D2D/DWrite ok`、`[T33] first EndDraw ok`、`[T33] after resize: ...`（初回 resize 時 1 回）、既存 `[D3D11] resize ok` 等。

## MainApp との切り替え

- `WIN32_MAIN_T33_HIDE_GDI_OVERLAY`（既定 `0`）: `1` にすると **GDI `Win32DebugOverlay_Paint` を抑止**し、D3D+D2D のみで見た目を比較可能。

## 未実施（後段）

- 長文・スクロール・既存 GDI の置換、C4244 の修正。
