# API リファレンス — ファイル別・実行フロー

この文書は、MainApp 周辺の主要な `.cpp` / `.h` の役割と、**初期化**と**毎フレーム（またはメッセージ駆動）の更新**に関わる API を整理したものです。

---

## 実行フロー（全体像）

```
┌─────────────────────────────────────────────────────────────────┐
│  wWinMain                                                       │
│    → InitInstance（モニタ列挙・CreateWindow・D3D初期化・Raw Input  │
│       登録・XInput タイマー開始）                                │
│    → メッセージループ（GetMessage / DispatchMessage）             │
└─────────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
   WM_INPUT              WM_TIMER              WM_PAINT
 （Raw Input）      （XInput ポーリング＋      （D3D clear/present
   ＋物理キー        仮想入力の統合 tick）      ＋ GDI オーバーレイ）
```

- **入力の経路は二つ**あります。  
  - **WM_INPUT**: キーボード（Raw）と HID ゲームパッド。DS4 既知 VID/PID では `VirtualInputSnapshot` への変換が走る。  
  - **WM_TIMER** (`TIMER_ID_XINPUT_POLL`): XInput のポーリング、スナップショットの差分、`VirtualInputConsumer` の統合、`Win32_UnifiedInputConsumerMenuTick`（メニュー試作・T17 キーなど）。

- **描画**は **WM_PAINT** で、`WindowsRenderer_RenderPlaceholder` の後に `Win32DebugOverlay_Paint`（GDI テキスト）がオプションで続きます。

---

## ファイル別の役割

### ルート（`app/InputPlatformLab/MainApp/`）

| ファイル | 役割 |
|----------|------|
| `MainApp.vcxproj` | ビルド定義。インクルードパス、`src/` / `include/` / `resources/` の参照。 |

---

### `include/` — 中立・宣言

| ファイル | 役割 |
|----------|------|
| `framework.h` | Windows 標準ヘッダの前処理。`targetver.h` → `windows.h` 等。 |
| `targetver.h` | SDK バージョン（`SDKDDKVer.h`）。 |
| `MainApp.h` | `resource.h` のみ。リソース ID 用。 |
| `Resource.h` | メニュー・ダイアログ・アイコン ID。 |
| `GamepadTypes.h` | `GameControllerKind`、`GamepadButtonId`、`GamepadLeftStickDir`（OS 非依存）。 |
| `VirtualInputMenuSample.h` | `VirtualInputConsumerFrame`、`VirtualInputMenuSampleState`、`VirtualInputMenuSample_Apply`（**inline header-only**）。 |
| `VirtualInputNeutral.h` | `VirtualInputSnapshot`、ポリシー、キーボード `KeyboardActionState`、`VirtualInputConsumer_BuildFrame*` 等の宣言。 |
| `InputCore.h` | 上記中立ヘッダの **単一入口**としてのコメント付きインクルード集約。 |
| `ControllerClassification.h` | `ControllerParserKind`、`ControllerSupportLevel`、`GameControllerHidSummary`、VID/PID テーブル解決、`Win32_ClassifyGameControllerKind` 等（名前に Win32 が付くが実装は中立寄り）。 |

---

### `include/platform/win/`

| ファイル | 役割 |
|----------|------|
| `WindowsRenderer.h` | D3D11 初期化・リサイズ・フレーム描画（clear/present）の **公開 API**。`WindowsRendererState` 保持。 |
| `Win32DebugOverlay.h` | GDI デバッグ描画・スクロールログ・スクロール位置設定の **公開 API**。 |
| `WindowsAppShell.h` | （現状）将来の `wWinMain` / `WndProc` 集約先の **プレースホルダ**（コメントのみ）。 |
| `WindowsDisplayBackend.h` | （現状）モニタ列挙・T14 表示などの **プレースホルダ**（コメントのみ）。 |
| `WindowsInputBackend.h` | （現状）Raw Input / XInput の **プレースホルダ**（コメントのみ）。 |

---

### `src/` — 実装

| ファイル | 役割 |
|----------|------|
| `MainApp.cpp` | **エントリ**（`wWinMain`）、`WndProc`、`InitInstance`、モニタ列挙（T14）、解像度マッチ（T15）、ウィンドウ再作成（T16）、表示モード（T17）、コントローラ識別（T18）、Raw Input / XInput / DS4 HID 橋渡し、仮想入力ログ、メニュー試作連携。 |
| `VirtualInputNeutral.cpp` | `VirtualInput_*` ヘルパ、`VirtualInputPolicy_*`、`VirtualInputConsumer_BuildFrame*` の実装。 |
| `ControllerClassification.cpp` | `Win32_HidTraitsLookLikeGamepad`、`Win32_ResolveHidProductTable`、ラベル関数、`Win32_ClassifyGameControllerKind` 等。 |

---

### `src/platform/win/`

| ファイル | 役割 |
|----------|------|
| `WindowsRenderer.cpp` | `WindowsRenderer_InitPlaceholder` / `Shutdown` / `OnResize` / `RenderPlaceholder`（D3D11 + DXGI）。 |
| `Win32DebugOverlay.cpp` | `Win32DebugOverlay_Paint`、`Win32DebugOverlay_ScrollLog`、`Win32DebugOverlay_MainView_SetScrollPos`、T17 ジャンプ用スクロール目標計算。 |

---

### `resources/`

| ファイル | 役割 |
|----------|------|
| `MainApp.rc` | メニュー、アクセラレータ、バージョン情報ダイアログ、文字列、アイコン参照。 |
| `MainApp.ico` / `small.ico` | アイコンリソース。 |

---

## 主要関数・型の簡易マニュアル

### 初期化（起動時）

| シンボル | 種別 | 引数・戻り値 | 意味 |
|----------|------|----------------|------|
| `wWinMain` | 関数 | 標準 Win32 エントリ。戻り値は `msg.wParam`。 | DPI 設定、`InitInstance`、メッセージループ。 |
| `InitInstance` | 関数 | `(HINSTANCE, int nCmdShow)` → `BOOL`。 | モニタキャッシュ、初回 `CreateWindow`、`WindowsRenderer_InitPlaceholder`、`RegisterRawInputDevices`、ログ、XInput タイマー `SetTimer`。 |
| `WindowsRenderer_InitPlaceholder` | 関数 | `(HWND, const WindowsRendererConfig&, WindowsRendererState*)` → `bool`。 | D3D11 デバイスとスワップチェーン作成。失敗時 `false`。 |
| `Win32_RegisterKeyboardRawInput` | static（MainApp.cpp） | `(HWND)` → `BOOL`。 | キーボード + ゲームパッド HID の Raw Input 登録。 |

---

### 毎フレーム／メッセージ駆動の更新

| シンボル | 種別 | 引数・戻り値 | 意味 |
|----------|------|----------------|------|
| `WndProc` | 関数 | 標準 Win32。 | `WM_INPUT` / `WM_TIMER` / `WM_PAINT` / スクロール等を分岐。 |
| `WM_TIMER` + `TIMER_ID_XINPUT_POLL` | | | `Win32_XInputPollDigitalEdgesOnTimer` → `Win32_UnifiedInputConsumerMenuTick`。 |
| `Win32_XInputPollDigitalEdgesOnTimer` | static | `(HWND)` | XInput の先頭接続スロットをポーリングし `VirtualInputSnapshot` を更新。 |
| `Win32_UnifiedInputConsumerMenuTick` | static | `(HWND)` | キーボード状態のエッジ検出、F6/F7 等、メニュー試作 `VirtualInputMenuSample`、T17 適用、Invalidate。 |
| `WM_INPUT` | | | HID ゲームパッドは `Win32_OnRawInputHidGamepadLayers`、キーボードは `PhysicalKeyEvent` とログ。 |
| `Win32_FillVirtualInputFromDs4StyleHidReport` | static | `(const BYTE* buf, UINT len, VirtualInputSnapshot& out)` → `bool` | DS4 形式 HID レポートを `VirtualInputSnapshot` に変換。 |
| `WindowsRenderer_RenderPlaceholder` | 関数 | `(WindowsRendererState*, HWND)` | バックバッファ clear と `Present`。 |
| `Win32DebugOverlay_Paint` | 関数 | `(HWND, HDC, const wchar_t* t17ModeLabelForOverlay)` | GDI でデバッグテキスト（スクロール・オーバーレイ）。 |
| `Win32_FillMenuSamplePaintBuffers` | 関数 | `(HWND, const RECT&, wchar_t* menuBuf, size_t, wchar_t* t14Buf, size_t)` | メイン画面用の文字列バッファを組み立て（`Win32DebugOverlay_Paint` から呼ばれる）。 |

---

### 中立層（OS 非依存）

| シンボル | 種別 | 引数・戻り値 | 意味 |
|----------|------|----------------|------|
| `VirtualInputSnapshot` | struct | | 1 フレーム分の論理ボタン・スティック状態。 |
| `VirtualInputConsumer_BuildFrame` | 関数 | `(prev, curr)` → `VirtualInputConsumerFrame` | パッドの前後スナップショットから移動・確定・キャンセル・メニューを抽出。 |
| `VirtualInputConsumer_BuildFrameFromKeyboardState` | 関数 | `(prevKs, currKs)` → `VirtualInputConsumerFrame` | キーボードのみ。 |
| `VirtualInputConsumer_MergeKeyboardController` | 関数 | `(kb, pad)` → `VirtualInputConsumerFrame` | キーボードとパッドのフレームをマージ。 |
| `VirtualInputMenuSample_Apply` | inline | `(state&, frame)` → `VirtualInputMenuSampleEvents` | 2x2 メニュー試作の状態遷移。 |

---

### 分類・レンダラ

| シンボル | 種別 | 引数・戻り値 | 意味 |
|----------|------|----------------|------|
| `Win32_ResolveHidProductTable` | 関数 | `(vid, pid, outParser, outSupport)` | VID/PID からパーサ種別と検証度を決定。 |
| `Win32_ClassifyGameControllerKind` | 関数 | `(traits, productName, devicePath, anyXInput)` → `GameControllerKind` | 表示用 family 推定。 |
| `WindowsRenderer_OnResizePlaceholder` | 関数 | `(state*, width, height)` | リサイズ時にスワップチェーンと RTV を再構築。 |
| `WindowsRenderer_ShutdownPlaceholder` | 関数 | `(state*)` | リソース解放。 |

---

### デバッグオーバーレイ（スクロール）

| シンボル | 種別 | 引数・戻り値 | 意味 |
|----------|------|----------------|------|
| `Win32DebugOverlay_ScrollLog` | 関数 | `(where, hwnd, before, after, contentHOverride, t17Override, contentHBase, extraBottomPadding)` | `[SCROLL]` デバッグ出力。 |
| `Win32DebugOverlay_MainView_SetScrollPos` | 関数 | `(hwnd, newY, logWhere)` | スクロールバー位置と内部 `s_paintScrollY` を更新。 |
| `Win32DebugOverlay_ScrollTargetT17WithTopMargin` | 関数 | `void` → `int` | T17 ブロックを上端に近づけるスクロール目標。 |
| `Win32DebugOverlay_ScrollTargetT17Centered` | 関数 | `(HWND)` → `int` | T17 をビュー中央付近に置くスクロール目標。 |

---

## 補足（命名について）

- `ControllerClassification` の一部に **`Win32_` プレフィックス**がありますが、実体は **OS 非依存のヘッダ／`.cpp`** にあります。歴史的な命名で、**Raw Input の属性構造体**を渡す前提の「分類」API です。

---

## 関連ドキュメント

- [アーキテクチャ（ディレクトリ構成・拡張方針）](architecture.md)
- ルートの [README.md](../README.md)（DS4 ビットマップの実機メモなど）
