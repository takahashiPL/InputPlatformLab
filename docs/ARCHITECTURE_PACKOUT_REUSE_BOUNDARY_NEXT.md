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

1. **`docs/architecture.md` の Pack-out 表** と **`InputCore.h` が束ねる中立ヘッダ** を基準に、**reusable candidate を「ファイルパス付き」で 1 表に埋める**（追記先は **本書への追補** でも **別 1 枚の表** でもよいが、**実装変更はしない**）。
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

