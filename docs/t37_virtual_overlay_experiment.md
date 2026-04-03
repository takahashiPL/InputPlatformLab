# T37 — 仮想レンダリング解像度とデバッグオーバーレイ（実験）

## 問題整理（GDI）

| 観点 | 内容 |
|------|------|
| **実クライアント座標** | `Win32DebugOverlay_Paint` は `GetClientRect` の幅で `DrawTextW`（メニュー + T14 本文）。1 クライアントピクセルあたりの文字密度は **物理解像度**基準。 |
| **committed 仮想解像度** | T34/T36 では D3D/D2D のグリッド・T33 は **committed W×H** のオフスクリーンに描き、バックバッファへ拡大合成。画面上の「セル」密度は **committed** に追従。 |
| **見え方の差** | オーバーレイ本文は **フルクライアント**向けに折り返し、グリッドは **仮想解像度**で描かれるため、**同じ T14 テキストでも行長・文字の相対サイズが一致しない**。 |

## 実験内容（最小）

- Borderless（T34）または Fullscreen（T36）で **オフスクリーン合成が有効**なときだけ、`Win32_FillMenuSamplePaintBuffers` で得た **T14 本文バッファ**を `IDWriteTextLayout`（折り返し幅 = committed 仮想幅の DIP）で描画し、合成済みバックバッファの **本文サブ矩形**へ重ねる（**T37-1** でスケール・原点・クリップを補正）。
- **メニュー行・[scroll] 帯**は従来どおり GDI。
- DWrite 経路が失敗した場合は **T14 本文を GDI で描画**（フォールバック）。
- Windowed は **従来どおり**（T37 フラグは立たない）。

失敗時 `[T37] fallback: ...`。

## T37-1（縮尺・配置補正）

- **本文エリア**: 上端はグリッドラベル + T33 行の下（`WIN32_RENDERER_DEBUG_GRID_64PX` 時は約 108 DIP）、下端は GDI の `[scroll]` 帯を避けるため下余白を確保。
- **スケール**: `min(bodyW/vWdip, bodyH/vHdip)` を **clamp**（既定 **0.42 … 1.85**）し、低 committed で全面に引き伸ばしすぎない / 高 committed で極小化しすぎないようにする。
- **クリップ**: 上記本文矩形に `PushAxisAlignedClip`。
- パラメータが変わったときのみ `[T37] layout scale=...` / `origin=(x,y)` / `clip=WxH` を出力。
