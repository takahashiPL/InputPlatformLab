# アーキテクチャ — ディレクトリ構成と拡張方針

この文書は、MainApp プロジェクト（`app/InputPlatformLab/MainApp/`）のレイアウトの意味と、将来のプラットフォーム追加の考え方をまとめたものです。

---

## 現在のフォルダ構成

```
MainApp/
├── MainApp.vcxproj          # Visual Studio プロジェクト（ビルドの正）
├── include/                 # 公開インターフェース（型・宣言の集約）
│   ├── platform/
│   │   └── win/             # Windows / Win32 専用のヘッダ
│   └── …                    # OS 非依存のヘッダ（論理入力・分類など）
├── src/                     # 実装本体
│   ├── platform/
│   │   └── win/             # Windows 専用の .cpp（レンダラ・GDI オーバーレイ）
│   └── …                    # OS 非依存の .cpp
└── resources/               # .rc / .ico（メニュー・アイコン・ダイアログ）
```

### `include/` の役割

- **他の翻訳単位や将来のモジュールから参照される「型定義・宣言」**を置く場所です。
- 中立層（`GamepadTypes.h`、`VirtualInputNeutral.h`、`VirtualInputMenuSample.h`、`ControllerClassification.h` など）は **Win32 HWND を含まない**設計に寄せています。
- `InputCore.h` は、これら中立ヘッダの **単一入口**としてのコメント付きインクルード集約です（実装は対応する `.cpp` をリンク）。

### `src/` の役割

- **実装の詳細**（関数本体・静的ヘルパ・状態）です。
- 中立ロジック（`VirtualInputNeutral.cpp`、`ControllerClassification.cpp`）は OS API に依存しない形にします。
- Windows 固有のコードは **`src/platform/win/`** に分離します。

### `include/platform/win/` と `src/platform/win/`

- **ファイル名に `Windows` または `Win32` を含む**ソース・ヘッダを、ここに配置するルールです（例: `WindowsRenderer.*`、`Win32DebugOverlay.*`）。
- `WindowsAppShell.h` / `WindowsDisplayBackend.h` / `WindowsInputBackend.h` は **将来の集約先のプレースホルダ**（コメントで責務を記載）で、現状の多くの処理は `MainApp.cpp` 内にあります。

### `resources/`

- リソースコンパイラが参照する `.rc` とアイコン（`.ico`）です。
- `MainApp.rc` 内の `ICON "…"` は **`.rc` と同じディレクトリ**を前提にしています。

### プロジェクト共通の設定

- `MainApp.vcxproj` の `AdditionalIncludeDirectories` に `include` と `include\platform\win` が入っており、`#include "framework.h"` のように **短い名前**でインクルードできます。
- ソースは **UTF-8（BOM 付き）**で保存すると MSVC の C4819 を避けやすいです（`README.md` 参照）。

---

## `platform/` の意味 — OS 依存の隔離

- **`platform/` 以下は「OS・ランタイムに依存するコード」**の置き場です。
- 上位レイヤー（論理入力 `VirtualInputConsumerFrame`、メニュー試作 `VirtualInputMenuSample` など）は **HWND や XInput の型を知らない**前提で設計されています。
- Windows では **Raw Input / XInput / WM_* メッセージ**が `MainApp.cpp` および `platform/win` の実装に集約され、そこから **`VirtualInputSnapshot`** に変換されて中立層へ渡ります。

---

## 新プラットフォーム追加の考え方（例: PS4 ホスト向け）

本リポジトリは現状 **Windows デスクトップ専用**です。PlayStation 4（またはその他ホスト）向けの実行ファイルを追加する場合、**ディレクトリの鏡像**で分けると追跡しやすいです。

### ディレクトリ案

```
include/
└── platform/
    ├── win/                 # 既存の Windows
    └── ps4/                 # 例: PS4 の公開ヘッダ（実際の SDK 名に合わせる）
        └── Ps4InputBackend.h   # 仮名: ホスト側の入力・フレーム取得の宣言

src/
└── platform/
    ├── win/
    │   WindowsRenderer.cpp
    │   └ …
    └── ps4/                 # 例: PS4 ホスト向け実装
        └── Ps4InputBackend.cpp
```

- **名前は例**です。公式 SDK のディレクトリ規約・命名に合わせてください。
- 重要なのは、**上位（MainApp の「論理」部分）が依存するのは中立型だけ**であることです。

### 実装手順（推奨フロー）

1. **中立型の固定**  
   `VirtualInputSnapshot`、`VirtualInputConsumerFrame`、`KeyboardActionState` など、既存の入力の「境界」を確認する。

2. **プラットフォームの「入口」**を定義する  
   - Windows では `WM_INPUT` / タイマー上の XInput などが `VirtualInputSnapshot` を更新している。  
   - PS4 側では、SDK が提供する「コントローラ状態」や「フレーム」に合わせて、**同じ `VirtualInputSnapshot` を埋める関数**を `platform/ps4` に実装する（例: `Ps4_FillVirtualInputSnapshot(...)`）。

3. **メインループの差し替え**  
   - Windows の `wWinMain` + `GetMessage` の代わりに、ホストのメインループ（またはコールバック）から、上記「入口」＋既存の `VirtualInputConsumer_*` / `VirtualInputMenuSample_Apply` を呼ぶ。

4. **描画・ウィンドウ**  
   - `WindowsRenderer_*` は Windows 専用。PS4 では別のレンダラ（または `platform/ps4` のラッパ）を用意し、**デバッグ UI が必要なら**ホスト側の描画 API に合わせてオーバーレイを実装する。

5. **上位を書き換えない条件**  
   - `MainApp.cpp` の「巨大」さは、将来の分割で `WindowsAppShell` / `WindowsInputBackend` などに移す想定がコメント済みです。  
   - PS4 追加時は、**`#ifdef` で OS を分岐するより**、ファイルを `platform/win` と `platform/ps4` に分け、**ビルドでどちらをリンクするか** を切り替える方が保守しやすいです。

### 注意

- **PS4 の SDK・NDA・提出規約**はこのリポジトリの範囲外です。実際の API 名・パスは公式ドキュメントに従ってください。
- 本プロジェクトの「PS4」は、主に **DS4 を PC の Raw Input 経由で扱う**文脈（VID/PID 分類など）で使われています。コンソール本体向けビルドとは別物です。

---

## 関連ドキュメント

- [API リファレンス（ファイル別・実行フロー）](api_reference.md)
- [T34 — Borderless オフスクリーン（完了）](t34_t35_display_and_render.md)
- [T35 — 表示モード方針（Windowed / Borderless / Fullscreen）](t35_display_mode_policy.md)
- [T37 — 仮想解像度とデバッグオーバーレイ実験](t37_virtual_overlay_experiment.md)
- ルートの [README.md](../README.md) にビルドパスと実機メモの一部があります。
