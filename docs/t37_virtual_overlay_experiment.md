# T37 — 仮想レンダリング解像度とデバッグオーバーレイ（実験）

## 問題整理（GDI）

| 観点 | 内容 |
|------|------|
| **実クライアント座標** | `Win32DebugOverlay_Paint` は `GetClientRect` の幅で `DrawTextW`（メニュー + T14 本文）。1 クライアントピクセルあたりの文字密度は **物理解像度**基準。 |
| **committed 仮想解像度** | T34/T36 では D3D/D2D のグリッド・T33 は **committed W×H** のオフスクリーンに描き、バックバッファへ拡大合成。画面上の「セル」密度は **committed** に追従。 |
| **見え方の差** | オーバーレイ本文は **フルクライアント**向けに折り返し、グリッドは **仮想解像度**で描かれるため、**同じ T14 テキストでも行長・文字の相対サイズが一致しない**。 |

## 実験内容（最小）

- Borderless（T34）または Fullscreen（T36）で **オフスクリーン合成が有効**なときだけ、`Win32_FillMenuSamplePaintBuffers` で得た **T14 本文バッファ**を `IDWriteTextLayout`（折り返し幅 = committed 仮想幅の DIP）で描画し、合成済みバックバッファへ **グリッドと同様のスケール**で重ねる。
- **メニュー行・[scroll] 帯**は従来どおり GDI。
- DWrite 経路が失敗した場合は **T14 本文を GDI で描画**（フォールバック）。
- Windowed は **従来どおり**（T37 フラグは立たない）。

ログ: `[T37] overlay body DWrite virtual=WxH clientDip=...`（初回成功時 1 回）、失敗時 `[T37] fallback: ...`。
