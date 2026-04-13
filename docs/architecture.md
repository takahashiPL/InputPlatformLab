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

## 入力 foundation の整理（レイヤ・転用・Debug）

**目的**: T76/T77 完了後の読み手向け。挙動は変えず、境界と転用しやすさを文章化する。

### レイヤの読み方（現状の配置）

| 区分 | 役割 | 代表（現状） |
|------|------|----------------|
| **platform/win** | HWND・WM_INPUT・Raw Input・XInput・タイマー・GDI/D2D/D3D・ウィンドウサイズ | `src/platform/win/*`、`MainApp.cpp` 内の WndProc / 入力ポンプの大半 |
| **input/core 相当** | `VirtualInput*` / `LogicalInput*`、プレイヤースロット、arbiter、列挙・定数（HWND 非依存） | `VirtualInputNeutral.*`、`LogicalInput*`、`InputCore.*`、`PlayerInputGuideTypes.h`、`PlayerInputSlots.h`、`EffectiveInputGuideArbiter.*` |
| **app / debug** | 検証 HUD・T18/T19/T20 ページ・`_DEBUG` ホットキー | `MainApp.cpp` の T18/T19/T20、`Win32_DebugTryApplyT77Slot1TrialHotkeys`（F8–F11）、デバッグオーバーレイ |
| **app / glue** | core と Win32 を1プロセスでつなぐ巨大 TU（分割予定コメントあり） | `MainApp.cpp`（エントリ・ループ・dispatch） |

大規模なファイル移動はしない。**どの層として読むか**を固定する。

### reusable / app-specific

- **転用しやすい候補（reusable candidate）**: 中立型と実装（`VirtualInput*`、`LogicalInput*`、`PlayerInputGuideTypes.h`、`PlayerInputSlots.h` のデータ面）、`EffectiveInputGuideArbiter` の**ヘッダ契約**、OS 非依存の処理。
- **InputPlatformLab 固有（app-specific）**: T18 行の文言、ページ式 HUD 本文、**F8/F9/F10/F11**、T17 連携、`OutputDebugStringW` ログ。製品では `_DEBUG` ブロックが落ちる想定。

### Debug-only（foundation verification）

- **F8 / F9 / F10 / F11**: `_DEBUG` のみ。本番の入力経路・既定 consume ではない。T77 single live / slot-indexed trial の検証用。
- **trial 観測**（`·tr=`、`lv=`、`InputGuideArbiter_DebugLogSlot1TrialObsIfChanged` など）: 検証・説明用。Release では無効想定。
- **T18 観測補助**: lab HUD 用。本番 UI 仕様ではない。

### `#ifdef` / プラットフォーム依存の方針

- 新しい Win32 API は `platform/win` または `MainApp.cpp` の境界へ寄せる（既存の巨大 TU は段階的に）。
- core ヘッダ（`PlayerInputGuideTypes.h` 等）は HWND / `Windows.h` を引かない方針。
- `EffectiveInputGuideArbiter.cpp` の `Windows.h` は時刻・ログ等。条件付きで OS 分岐を増やさないことを優先（将来 platform 側へ分離）。

---

## Pack-out / reuse boundary（repo hygiene）

zip や clone を見たときの**分類**。大規模移動はしない前提で、何を持ち出し候補にするかだけ固定する。

| 区分 | 中身の例 |
|------|-----------|
| **reusable candidate（portable foundation）** | `PlayerInputGuideTypes.h`、`PlayerInputSlots.h`（データモデル）、`InputCore.h` が束ねる中立ヘッダ（`VirtualInputNeutral.*`、`LogicalInputState.*`、`GamepadTypes.h`、`ControllerClassification.*`、`VirtualInputMenuSample.*` など）、`EffectiveInputGuideArbiter.h` + 実装 `.cpp`（移植時は TU内の Win32 依存を別 OS へ差し替え） |
| **platform/win specific** | `include/platform/win/*`、`src/platform/win/*`（レンダラ・オーバーレイ・Win32 HUD ページヘルパ・`Win32InputGlue.*`（Raw 登録・XInput probe・デバイス文字列・GetRawInputDeviceList 集約・先頭接続スロット・WM_INPUT HID 要約・T76 inventory 間引き・XUser 接続4スロット・T18 inventory survey（Raw 列挙順の先頭 HID 行）など） |
| **app-specific glue / verification** | `MainApp.cpp` / `MainApp.h`、`.rc` / `resources/`、InputPlatformLab 固有の T18/T19/T20 文言・検証フロー・`_DEBUG` ホットキー |
| **generated / local-only** | `.vs/`、`Debug/` / `Release/` / `x64/` / `x86/`、`ipch/`、`*.obj` `*.pdb` `*.exe` など（ルート [`.gitignore`](../.gitignore) 参照）。**コピー対象にしない** |

### 他プロジェクトへ持ち出す最小イメージ

1. **Portable pack**: 上表の reusable candidate に対応する `include/*.h` と中立 `.cpp` をまとめ、ホスト側でビルドに追加する。  
2. **Windows adapter pack**（PC ゲームが Win32 の場合）: `platform/win` + `VirtualInputSnapshot` を埋める既存ブリッジを参考に、自アプリのメインループから呼ぶ。  
3. **持ち出し不要**: 生成物・`.vs`・ユーザー設定（`*.vcxproj.user`）。リポジトリを渡すなら `.gitignore` どおり除外されれば足りる。

---

## 関連ドキュメント

- [T77 — foundation close / pre-branch freeze](T77_FOUNDATION_CLOSE.md)
- [API リファレンス（ファイル別・実行フロー）](api_reference.md)
- [T34 — Borderless オフスクリーン（完了）](t34_t35_display_and_render.md)
- [T35 — 表示モード方針（Windowed / Borderless / Fullscreen）](t35_display_mode_policy.md)
- [T37 — 仮想解像度とデバッグオーバーレイ実験](t37_virtual_overlay_experiment.md)
- ルートの [README.md](../README.md) にビルドパスと実機メモの一部があります。
