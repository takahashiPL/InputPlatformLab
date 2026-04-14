# 1. 目的

本書は、InputPlatformLab において **`docs/architecture.md` の Pack-out / reuse boundary** を、**現状コードに対する設計メモとして一段進める**ための **再開用足場**である。

- **実装変更・ビルド変更・挙動変更は行わない**（本書は **docs のみ**の追加）。
- **`WM_INPUT` / `WM_TIMER` / `WM_PAINT` / `InvalidateRect`**、および **T19 / T20 の受け入れ済みページの意味**（`docs/HUD_PAGED_ACCEPTANCE.md`）は **変えない・再解釈しない**。
- **危険線**の位置づけは `docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md` と整合させる。

---

# 2. 今回までで固定された前提

- **legacy stacked HUD 分離フェーズは closed**。再オープンしない。
- **ページ式 HUD が通常運用の正**。
- **T76 は close**。**T77 は step24 foundation close 済み**（`docs/T77_FOUNDATION_CLOSE.md`）。foundation close をあいまいにしない。
- **小工事フェーズは一区切り**（`Win32InputGlue`、`ControllerClassification`、起動時ラベルダンプの置き場整理等。`docs/NEXT_THEME_RESTART_ENTRY.md` 参照）。
- **運用の次の足場** として `docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md` と `docs/HUD_PAGED_ACCEPTANCE.md`（2026-04-13 追記含む）がそろっている。
- **実行骨格** は `GetMessage` → `DispatchMessage` → **`WndProc`**。単一 `Update()` 入口は無い（`docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`）。
- **本書は時点固定の設計メモ** として読む（既存 docs に委ねる）。

---

# 3. いまの境界の読み方

`docs/architecture.md` では、zip / clone 時の分類として次が既に固定されている。

| 区分 | 意味（要約） |
|------|----------------|
| **reusable candidate** | 中立型・`InputCore.h` が束ねるヘッダ群・OS 非依存寄りの `.cpp`（HWND を引かない方針のもの） |
| **platform/win specific** | `include/platform/win/*`、`src/platform/win/*`、`Win32InputGlue.*` 等。Raw Input / XInput / レンダラ / GDI オーバーレイ |
| **app-specific glue / verification** | `MainApp.cpp`、T18 系 glue、`.rc` / `resources/`、ページ式 HUD 本文・検証フロー・`_DEBUG` ホットキー |

**入力 foundation のレイヤ表** （同ファイル「入力 foundation の整理」）とセットで読むと、**「中立 core」「platform/win」「巨大 app glue（MainApp）」**の三層になる。

**危険線（実装に触るときの太い線）**: `WndProc` 内の **`WM_INPUT` / `WM_TIMER` / `WM_PAINT` の順序と早期 return**、**`InvalidateRect` 条件**（T19 は論理+pad 即時・analog 間引き）、**D2D prefill と GDI オーバーレイの接続**。pack-out で「ファイルを動かす」作業は、**この線に寄るほど回帰コストが跳ねる**（`docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`）。

---

# 4. 次の pack-out / 再利用境界候補

## 候補 1 — reusable candidate と `*.cpp` の対応表（ポータブル pack 一覧）

| 観点 | 内容 |
|------|------|
| **切りたい境界** | どのヘッダ／TU が **HWND 非依存の portable foundation** かを **ファイル単位で列挙**し、`architecture.md` の Pack-out 表を **追跡しやすい** 形にする。 |
| **候補理由** | 小工事後も **中立と Win32 周辺のあいまいさ** が残りやすい。実装を動かさず **読み手の誤解を減らせる**。 |
| **危険線への近さ** | **低い**（docs のみ）。`WndProc` や `InvalidateRect` を直接は触らない。 |
| **着手向き** | **今すぐ設計メモ化向き**。次回 1 セッションで表を埋め切れる単位。 |

### 第一候補の具体化 — reusable candidate とファイル対応（2026-04-13 時点）

`InputCore.h` のコメントどおり、**単一入口が束ねる中立ヘッダ**と、**`#include "InputCore.h"` には載っていないが `docs/architecture.md` の Pack-out 行に含まれる部品**を、**同一の portable foundation 束**として読む。パスはリポジトリルートからの相対表記。

**確認条件（設計メモ）**: 各候補について **HWND / `Windows.h` / `WM_*`** をヘッダが要求しないか、`.cpp` がメッセージポンプや `InvalidateRect` に触れないかを見る。例外は **`EffectiveInputGuideArbiter.cpp` のみ**（`Windows.h` 利用。`architecture.md` 既述）。

| 候補名 | 主なファイル（`.h` / `.cpp`） | いまの責務（要約） | pack-out / 再利用境界としての自然さ | 危険線からの距離 | docs 固定向き / 実装の時期感 |
|--------|-------------------------------|-------------------|--------------------------------------|------------------|------------------------------|
| **A. VirtualInputNeutral** | `app/InputPlatformLab/MainApp/include/VirtualInputNeutral.h`<br>`app/InputPlatformLab/MainApp/src/VirtualInputNeutral.cpp` | 仮想スナップショットのリセット・ボタン/スティック読み取り、ポリシー型（`VirtualInputSnapshot` 周り）。Raw/XInput を知らない中立 API。 | `InputCore.h` が明示的にリンク対象として挙げる **最初の `.cpp`**。Portable pack の中核の一つ。ホストが自前バックエンドから `VirtualInputSnapshot` を埋めるときの **参照実装**になりやすい。 | **遠い**。`WM_INPUT` / `WM_TIMER` / `WM_PAINT` / `InvalidateRect` 非接触。 | **docs は今すぐ固定向き**（本表がその実体）。**物理移動は別タスク**（合意とビルド確認後）。 |
| **B. LogicalInputState** | `app/InputPlatformLab/MainApp/include/LogicalInputState.h`<br>`app/InputPlatformLab/MainApp/src/LogicalInputState.cpp` | 論理ボタン ID と論理入力状態（`LogicalInputState` 等）の更新・複合。メニュー/ガイド向けの論理層。 | Neutral と同列で **InputCore 束の第二 `.cpp`**。ゲームロジック寄りの「論理入力」境界として pack 単位が明確。 | **遠い**（同上）。 | **docs は今すぐ固定向き**。**実装の物理移動** は Arbiter より前に片付けるのが読みやすい（依存の上下が一方通行に近い）。 |
| **C. ControllerClassification** | `app/InputPlatformLab/MainApp/include/ControllerClassification.h`<br>`app/InputPlatformLab/MainApp/src/ControllerClassification.cpp` | VID/PID テーブル・HID 要約から family / parser / support を決定。`Win32_` 接頭辞の論理があるが、**ファイル先頭コメントどおり Win32 API 呼び出しなし**（命名の名残）。 | デバイス**分類だけ**を切り出す自然な単位。inventory（T18）由来の HID 要約から family/parser/support へ写す「関数寄りの束」。 | **遠い**（メッセージループ非接触）。T18 **ページ本文・accepted** とは別レイヤ（glue は app-specific）。 | **docs は今すぐ固定向き**。**実装移動** は inventory パイプとセットで読みやすいため **時期は A/B より慎重**（呼び出し側の見え方が変わりやすい）。 |
| **D. 共有型・メニュー試作ヘッダ束** | `app/InputPlatformLab/MainApp/include/CommonTypes.h`<br>`app/InputPlatformLab/MainApp/include/GamepadTypes.h`<br>`app/InputPlatformLab/MainApp/include/VirtualInputMenuSample.h`（**header-only**） | 固定幅エイリアス、ゲームパッド列挙、`VirtualInputConsumerFrame` とメニュー試作状態機械（`VirtualInputMenuSample*`）。 | **`.cpp` なし**の薄い束。Portable pack の **最下層〜試作 UI 方針のサンプル** としてまとめて持ち出しやすい。`InputCore.h` に直接列挙されている。 | **遠い**。 | **docs は今すぐ固定向き**。**実装変更は不要**に近い（分割の対象外）。 |
| **E. スロット・ガイド型（データモデル）** | `app/InputPlatformLab/MainApp/include/PlayerInputGuideTypes.h`<br>`app/InputPlatformLab/MainApp/include/PlayerInputSlots.h`（**データ面は header-only**） | ガイド表示・スロット索引・バインド解決に使う **型と定数**（HWND 非依存方針）。 | `architecture.md` の reusable 行で **明示**。**`EffectiveInputGuideArbiter.h`** の前提になるため、**契約の固定に効く**。 | **遠い**（描画・メッセージ非接触）。T19/T20 の **ページ本文** は app glue。本候補は **型の束**。 | **docs は今すぐ固定向き**。**型の意味を変える実装** は早い（T76/T77・effective owner の説明に波及しうる）。 |
| **F. EffectiveInputGuideArbiter** | `app/InputPlatformLab/MainApp/include/EffectiveInputGuideArbiter.h`<br>`app/InputPlatformLab/MainApp/src/EffectiveInputGuideArbiter.cpp` | T76/T77: スロット表、effective owner、staging、single live consume、`_DEBUG` trial ゲート等。`.cpp` は `Windows.h`（`GetTickCount` / `OutputDebugStringW` 等）。コメントどおり **WM_* は足さない** 方針。 | **Portable pack に含めるが「移植時は .cpp 内 OS 依存を差し替え」**（`architecture.md` 既述）。**ヘッダ契約** は reusable、**実装 TU** はホスト都合で厚くなりやすい。 | **中程度**。`WM_INPUT` 等には直接触れないが、**タイマー駆動の tick 契約** と **MainApp / glue からの呼び出し順** が、説明上 **T19/T20 の「どの入力がガイドの正か」** に接続しうる（**accepted 文言やページ意味は変更しない** 前提で読む）。 | **docs で API 境界・ファイル所属は今すぐ固定向き**。**`.cpp` の物理 pack-out や責務分割はまだ早い**（T77 foundation close 後の別合意）。 |

#### 表の読み方 — 最初に埋めるべき候補（1 つ）

**候補 A（VirtualInputNeutral）** を最初に「表の意味で」埋める。**他プロジェクトが真似するならこの TU からリンクする** のが説明コストが最も低い。`InputCore.h` の実装列挙順とも一致する。

**候補 A（VirtualInputNeutral）— API 束ね（1 段具体化）**

| 項目 | 内容 |
|------|------|
| **主な公開型 / 関数（入口）** | **型**: `VirtualInputSnapshot`（1 フレーム分の仮想パッド）、`VirtualInputPolicyHeld` / `VirtualInputPolicyMenuEdges`、`KeyboardActionState`。`VirtualInputConsumerFrame` は `VirtualInputMenuSample.h` 側の型で、本 TU から組み立て・マージする。**関数**: `VirtualInput_ResetDisconnected`、`VirtualInput_IsButtonDown`、`VirtualInput_WasButtonPressed` / `VirtualInput_WasButtonReleased`、トリガ・スティック向け getter、`VirtualInputPolicy_*`（move の DPad 優先・クランプ）、`VirtualInputConsumer_BuildFrame`（prev/curr スナップショットから）、`VirtualInputConsumer_BuildFrameFromKeyboardState`、`VirtualInputConsumer_MergeKeyboardController`。 |
| **主ファイル** | `app/InputPlatformLab/MainApp/include/VirtualInputNeutral.h`、`app/InputPlatformLab/MainApp/src/VirtualInputNeutral.cpp` |
| **依存** | `GamepadTypes.h`（列挙・`GamepadButtonId` 等）、`VirtualInputMenuSample.h`（→ `CommonTypes.h`、Consumer 型）。`#include` チェーンに **HWND / `Windows.h` は無い**。 |
| **Win32 非依存として見なせるか** | **見なせる**（ヘッダ・`.cpp` とも API レベルで Win32 型・呼び出しなし）。コメントが Raw Input / オートリピートに言及するのは **ホスト側の前提説明**であり、本 TU が `WM_INPUT` 等に依存する意味ではない。 |
| **pack-out 時に先に固定すべき前提（設計メモ）** | ホストが毎フレーム `VirtualInputSnapshot` を埋めること。`leftDir` / `rightDir` / deadzone フラグは **ホストまたは上流**で既に解決済みであることを期待する読み方（本 TU はポリシーとエッジ検出に専念）。`VirtualInputPolicy_*` のルール（Confirm=South pressed 等）は **ヘッダコメントの固定仕様**として読み、変更は T19/T20 **accepted や `InvalidateRect` 経路とは別合意**。Consumer の **パッド優先マージ**（`MergeKeyboardController`）の意味を文書どおり持つ。 |
| **まだ触らない方がよい周辺** | **`WndProc` / `WM_TIMER` で prev/curr を切る責務**、`InvalidateRect`、T19/T20 **ページ本文**、Raw/XInput から `VirtualInputSnapshot` へ写す **platform / MainApp glue**。候補 A は **その先の中立コア**として切り出す対象で、メッセージループや accepted 文言の再解釈と混同しない。 |

**候補 A の docs 上の準備完了条件**: 上表が **「型・関数の入口」「依存」「ホスト契約」** について読み手の誤読を残さないこと。`InputCore.h` の「リンクする `.cpp`」記述と矛盾しないこと。

**実装（物理 pack-out 等）に入る前の確認事項**: **D（GamepadTypes / MenuSample / CommonTypes）を同じ portable 束として一緒に持ち出す**前提でビルド対象を数えること。**挙動・API を変えない**こと。**危険線**（`WM_INPUT` / `WM_TIMER` / `WM_PAINT` / `InvalidateRect` / T19・T20 accepted）に手を伸ばす作業と **同一コミットにしない**こと。

#### いまは触らない方がよい候補（設計メモの追補は可／実装・呼び出し契約は別）

- **候補 F（EffectiveInputGuideArbiter）の `.cpp` 分割・移動・OS 分岐の増殖** — T77 foundation close 済み域に接続し、タイマー・`_DEBUG` 経路の説明責任が重い。**本書の表でパスと責務を固定する分にはよい**。
- **候補 C を inventory（T18）パイプと一体で `MainApp` から引き剥がす試み** — 分類ロジック本体は中立でも、呼び出し側は app glue / platform に跨るため、**実装は A/B の整理より後** が安全。

---

## 候補 2 — `T18InventorySnapshotGlue` / `T18PageBodyFormatGlue` と paint 経路の責務ライン

| 観点 | 内容 |
|------|------|
| **切りたい境界** | スナップショット・分類根・compact 行（glue）と、`WM_PAINT` 以降の **表示直前（オーバーレイ）** のあいだで、**データの流れと「ここでは描かない」** を文章で固定する。 |
| **候補理由** | `ENGINE_LOOP_MAPPING` / `WNDPROC_MESSAGE_RESPONSIBILITY_MAP` でも言及がある。T18 本文素材は pack-out 上 **app-specific** と分類済みだが、読み手の迷いが残りやすい。 |
| **危険線への近さ** | **設計メモのみなら中程度以下**。実装移動は **T18 表示・accepted** に波及しうるため **実装は別タスク**。 |
| **着手向き** | **設計メモは今すぐ向き**。**コード移動は後回し**。 |

## 候補 3 — `Win32InputGlue` と `MainApp.cpp` の入力ポンプ境界

| 観点 | 内容 |
|------|------|
| **切りたい境界** | Glue が担う **登録・調査** と、`WndProc` 側の **`WM_INPUT` / タイマー内処理** の **呼び出し関係** を、**関数名レベルで対応表化** する（移設先は決めない）。 |
| **候補理由** | 小工事で Glue が厚くなった。**次の衝突起因は「どこがメッセージ中枢か」の誤読** になりやすい。 |
| **危険線への近さ** | **高い**（`WM_INPUT` / `WM_TIMER` 直結）。**設計メモは可能だが、実装の pack-out は最後に回す** のが安全。 |
| **着手向き** | **設計メモは中程度**。**実装 pack-out は後回し**。 |

## 候補 4 — `MainApp.cpp` の将来シェル（プレースホルダー）と責務ラベルだけの地図

| 観点 | 内容 |
|------|------|
| **切りたい境界** | `WindowsAppShell` / `WindowsInputBackend` 等の **プレースホルダー** と、**現状どの責務が MainApp に残っているか** を **ラベルだけ** で 1 枚にする（分割コミットはしない）。 |
| **候補理由** | `architecture.md` 既述の「巨大 TU」に対する **読み手向け索引** になる。 |
| **危険線への近さ** | **メモのみ低〜中**。実際の抽出は **WndProc 全体** に触れうるため **実装は高リスク**。 |
| **着手向き** | **設計メモは今すぐ向き**。**物理移動は後回し**。 |

---

# 5. 第一候補として再開するなら何か

**候補 1（reusable candidate と `*.cpp` の対応表を完成させる）** を第一候補とする。

- **理由**: **危険線から最も遠く**、**pack-out の目的（何を clone 候補にするか）に直結** する。T19/T20 の accepted や `InvalidateRect` 条件を **書き換えず** に進められる。
- **`docs/NEXT_THEME_RESTART_ENTRY.md` 候補 B** の精神に合い、**実装に落とす前の言語化** に最も向く。

---

# 6. いま触らない方がよい範囲

次は **意図的に触らない**（別タスクでも **合意と受け入れ手順の再確認のうえ**）。

- **`WndProc` の `WM_INPUT` / `WM_TIMER` / `WM_PAINT` の分岐・順序・早期 return** の変更。
- **`InvalidateRect` / `RedrawWindow` 条件** の変更（T19 の論理即時・analog 間引きを含む）。
- **T19 / T20 の本文生成・文言・accepted 意味** の変更。
- **レンダラ・オーバーレイ実験**（t32 / t33 / t37 系）の **コード着手**（`docs/roadmap.md` の保留どおり、**`WM_PAINT` 近傍は重い**）。
- **legacy stacked HUD 分離フェーズの再オープン**。

---

# 7. 次回の最初の着手単位

**1 セッションで完結させる** 単位として、次を推奨する。

1. **§4「候補 1」直下の具体化表** を基準に、迷いが残る TU へのラベル付け・`docs/decisions.md` への 1 行メモ・本書への「未決：要次回」を足す（**`docs/architecture.md` の Pack-out 行とも突き合わせる**。**実装変更はしない**）。
2. 表を書くとき **`HWND` / `Windows.h` の有無** を確認条件として明記し、**迷う TU は「platform/win」または「app glue」側に寄せてラベルだけ付ける**（中身の移動はしない）。
3. 疑問が残る場合は **`docs/decisions.md` に 1 行**、または **本書に「未決：要次回」** とだけ残す。

---

## 参照

- `docs/architecture.md`（Pack-out / reuse boundary、入力 foundation）
- `docs/NEXT_THEME_RESTART_ENTRY.md`（候補 B）
- `docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`（危険線、T19/T20 経路）
- `docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`（メッセージループとモジュールの位置づけ）
- `docs/roadmap.md`
- `docs/HUD_PAGED_ACCEPTANCE.md` / `docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md`

