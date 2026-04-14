# 1. 目的

本書は、InputPlatformLab において **安全なコード小工事フェーズを一区切り**としたあと、**次に何を「実務テーマ」として位置付けるか**を棚卸しし、**再開時の第一候補と着手単位**を 1 箇所に固定するための **再開入口ドキュメント**である。

- **コード変更・ビルド設定変更・挙動変更・新機能追加は行わない**（本書は **docs のみ**の追加）。
- **本書以外の**既存ドキュメントの本文編集は **同じセッションでは行わない**（参照リンクの列挙に留める）。
- **危険線**（`WM_INPUT` / `WM_TIMER` / `WM_PAINT` の分岐・順序、`InvalidateRect` 条件、**T19 / T20 の受け入れ済みページの accepted 意味**）を **あいまいにしない**。

---

# 2. 今回までで固定された前提

以下は、直近の小工事・既存 docs と整合する **運用上の固定**として本書でも用いる。

- **legacy stacked HUD 分離フェーズは closed**。再オープンしない。
- **ページ式 HUD が通常運用の正**（受け入れの一次情報は `docs/HUD_PAGED_ACCEPTANCE.md`）。
- **T76 は close**。**T77 は step24 foundation close 済み**（`docs/T77_FOUNDATION_CLOSE.md`。foundation close をあいまいにしない）。**Go(1)**（trial の routing 近接化）は `EffectiveInputGuideArbiter.cpp` のみ **2 コミット**（`fdd33b0`・`57cf06f`）で一区切り、**3 手目は見送り**（現況は `docs/roadmap.md` に同期済み。過大な前進とはみなさない）。
- **Win32 ホストの実行骨格**は `GetMessage` → `DispatchMessage` → **`WndProc`**。単一の `Update()` 相当入口は無い（`docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`）。
- **`WndProc` 経路の責務と危険線**は `docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md` で現状固定されている。
- **実装側の整理**（直近まで）:
  - **`Win32InputGlue`**: Raw Input / XInput 周辺の小整理・重複削除。
  - **`ControllerClassification`**: ゲームパッド系 **表示ラベル**（論理ボタン ID 名・ファミリ別ラベル・左スティック方向ラベル等）の集約。
  - **起動時のゲームパッドボタンラベル表ダンプ**（`OutputDebugStringW`）は **`MainApp.cpp` 側**に置き、分類モジュールから I/O 副作用を外した。
  - **候補 A（`VirtualInputNeutral`）・候補 B（`LogicalInputState`）**: **第 1 回 extraction unit は完了**（`include/input/core` / `src/input/core`、**`vcxproj` / `filters` のみ**、**挙動変更なし**・**危険線非接触**。`VirtualInputNeutral` の第 1 回移動は **`241a077` 参照**）。地図は **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md`**（**A/B 行**・候補 A/B の「第 1 回 unit」・「まだ触らない周辺」）。
  - **候補 E（`PlayerInputGuideTypes.h` / `PlayerInputSlots.h`）**・**F（`EffectiveInputGuideArbiter`）**: **型契約**および **API 境界・説明責務**を、同ドキュメントの **候補 E / 候補 F 節**まで **docs 固定済み**（**`.cpp` の物理 pack-out や分割は別合意**）。

---

# 3. 小工事フェーズをここで止める理由

- **`ControllerClassification` に素直に入る中立ラベル移動**は、**追加で触っても得が薄く、T16/T17・guide・T19 表示に近いラベルに踏み込みやすい**段階に来ている。
- これ以上の「1 ブロック整理」は **体裁寄り・境界あいまい**になりやすく、**危険線への接近コスト**に見合わない。
- 「**無理に 1 ブロックをひねり出さない**」ことと「**foundation / 受け入れ済みページを崩さない**」という制約を両立するなら、**コードより先に「次の実務テーマ」を決める**方が安全である。

---

# 4. 次の実務テーマ候補


**現況（2026-04 時点）**: 候補 **A/B** は **第 1 回 unit 一区切り**、**E/F** は **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md` で docs 固定済み**。同書 **§4 候補 2〜4**（T18 glue の paint 非接觨境界・`Win32InputGlue` と `MainApp.cpp` の索引・`MainApp.cpp` の責務ラベル地図）の **docs 追補も済み**。T77 Go(1) は **2 手で一区切り・3 手目見送り**（§2 参照）。ここからの主眼は **pack-out 軸で次の docs 先行を 1 本に絞る判断**（**危険線**は **`WM_INPUT` / `WM_TIMER` / `WM_PAINT` / `InvalidateRect` / T19・T20 accepted** を別扱いし、意味は再定義しない）。

## 候補 A — ページ式 HUD（T19/T20）中心の **回帰確認・手動検証手順**の実務化

| 項目 | 内容 |
|------|------|
| **何を触るか** | **主手順**は **`docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md`**。**受け入れ・記録**は **`docs/HUD_PAGED_ACCEPTANCE.md`**（2026-04-13 追記含む）および **`docs/roadmap.md`** の短文まで **反映済み**。残るのは **運用の追随**（手順と一次情報の差分が出たときの **最小追記**）に近い。**「次の第一候補から手順を書き起こす」段階ではない**。 |
| **なぜ今か** | 足場は既にあるため、**再開直後の唯一の最優先**という位置づけは下げる。それでも **T19/T20 の accepted 意味**は **`HUD_PAGED_ACCEPTANCE.md` に立ち返る**必要がある（危険線は変えない） |
| **危険線** | **手順書の記述**が誤った解釈を誘発するリスクはあるが、**コードを直接触らない**なら即時の実行時リスクは低い。記述は **`HUD_PAGED_ACCEPTANCE.md` と矛盾しない**こと。 |
| **着手向き** | **必要時のみの短い docs 追随**。全体の優先は **§5（第一候補・候補 4 突合）** と整合させる。 |

## 候補 B — **VirtualInputNeutral** / **LogicalInputState**（pack-out・**第 1 回 unit 済み**）

| 項目 | 内容 |
|------|------|
| **何を触るか** | **第 1 回 extraction は完了**（候補 A・B とも `input/core` 配置済み）。設計メモは **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md`**（**A/B 行**・候補 A/B の「第 1 回 unit」・「まだ触らない周辺」）。**第 2 段**（docs 横断の現況同期や追加移動）は **再棚卸しで優先を決めてから**。 |
| **なぜ今か** | **「進行中の第一候補」ではない**。読み手は E/F まで進んだ **境界メモ**とあわせ、**次にどの線を動かすか**を §5 で揃える段階にある。 |
| **危険線** | 設計・追記だけなら低いが、**実装に落とすと WndProc 近傍に寄りやすい**。本文では **「実装は別タスク」**と明記する。 |
| **着手向き** | **§5 の再棚卸しの結果として**位置付ける。**実装タスクは別途**（危険線をあいまいにしない）。 |

## 候補 C — **ロードマップ / 決定ログ**の同期（`roadmap.md`・`decisions.md`）

| 項目 | 内容 |
|------|------|
| **何を触るか** | 「小工事フェーズ終了」「次は検証手順 or 境界設計」など、**プロジェクトの意思**を **短文で更新**。 |
| **なぜ今か** | 外部・未来の自分への **再開コスト削減**。 |
| **危険線** | **低**（docs のみ）。 |
| **着手向き** | **再棚卸し（§5）と一体**の **短い追記**に留めるのがよい。 |

## 候補 D — **レンダラ / オーバレイ実験**（t32 / t33 / t37 系）の優先度整理

| 項目 | 内容 |
|------|------|
| **何を触るか** | 既存の実験メモを **「次に着手する価値があるか」**で並べ替え、**ページ式 HUD 正との関係**を書く。 |
| **なぜ今か** | 描画経路は **危険線が太い**ため、**いつコードを触るか**を先に合意した方が安全。 |
| **危険線** | **高**（`WM_PAINT` / `InvalidateRect` / D2D・GDI 接続）。 |
| **着手向き** | **後回し**。**§5** の再棚卸しのあとで優先を見直す。 |

---

# 5. 第一候補として再開するなら何か

**第一候補（1 本固定）**: **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md` の §4 候補 4**（`MainApp.cpp` の責務ラベル・プレースホルダー地図）を軸に、**`docs/architecture.md`（Pack-out 行）** と **`docs/roadmap.md`** を **短文で突合**し、読み手向けの索引を 1 枚に揃える **docs 先行の 1 セッション**とする。

- **理由**: §4 **候補 2・3** は同書で **docs 追補済み**。pack-out 軸の次の喉のどころは、**巨大 TU の読み方**を `architecture` / `roadmap` と矛盾なく固定すること。**実装 pack-out**・**`WndProc` 改変** は対象外（T77 は §2 のとおり。過大な前進とはみなさない）。
- **次点**: 候補 2・3 は **必要時の読み返しのみ**（一次情報の上書きはしない）。候補 C〜D・曖昧 TU の優先は **本テーマのあと** に短く見直す。
- **危険線の置き場所は変えない**: **`WM_INPUT` / `WM_TIMER` / `WM_PAINT` の分岐・順序**、**`InvalidateRect` 条件**、**T19 / T20 の受け入れ済みページの accepted 意味** は **`WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`** と **`HUD_PAGED_ACCEPTANCE.md`** を一次情報とし、**あいまいにしない**（本テーマは **ラベルと参照** に留め、意味の再解釈ではない）。
- **まずやらないこと**: 候補 D のような **描画パイプライン実装**、および **合意前の Arbiter `.cpp` pack-out** に踏み込まない。

---

# 6. いま触らない方がよい範囲

次を **意図的に触らない**（必要なら別タスクで **設計・合意のうえ**）:

- **`WndProc` 内の `WM_INPUT` / `WM_TIMER` / `WM_PAINT` の分岐・順序**の変更。
- **`InvalidateRect` の条件・頻度・ページ別ロジック**の変更。
- **T19 / T20 の本文生成・accepted 意味**の変更（文言・構造・意味の再解釈を含む）。
- **owner / guide / slot / consume** の意味論に直結する変更。
- **T16 / T17 の表示モード・ウィンドウ操作**の文脈に踏み込んだ「ついで整理」。
- **legacy stacked HUD 分離フェーズの再オープン**。

---

# 7. 次回の最初の着手単位

**1 セッションで完結させる**単位として、次を推奨する。

1. **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md`** の **§4 候補 4**（`MainApp.cpp` の責務ラベル・プレースホルダー地図）を読み、**`docs/architecture.md`**・**`docs/roadmap.md`** の Pack-out 記述と **短文で矛盾がないか突合**する（**危険線**は **ラベルと参照** に留め、意味の再解釈はしない）。
2. 手順 1 の突合で **優先度の矛盾**が残る場合のみ、**短文**でどちらを正とするか決め、`decisions` / `roadmap` に **1 行**残す。
3. T19/T20 については **`docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md`** に従い、必要なら **`docs/HUD_PAGED_ACCEPTANCE.md`** へ **最小限の追記**にとどめる。**accepted 意味に触る用語は一次情報どおり**に保つ。
4. **実装変更は行わない**（本書の再開単位では）。未決が残る場合は **decisions / roadmap に 1 行**残す程度に留める。

## 参照（既存 docs）

- `docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`
- `docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`
- `docs/HUD_PAGED_ACCEPTANCE.md`
- `docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md`
- `docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md`
- `docs/T77_FOUNDATION_CLOSE.md`
- `docs/architecture.md`
- `docs/roadmap.md` / `docs/decisions.md`
