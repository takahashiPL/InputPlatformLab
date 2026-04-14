# 1. 目的

本書は、InputPlatformLab において **安全なコード小工事フェーズを一区切り**としたあと、**次に何を「実務テーマ」として位置付けるか**を棚卸しし、**再開時の第一候補と着手単位**を 1 箇所に固定するための **再開入口ドキュメント**である。

- **コード変更・ビルド設定変更・挙動変更・新機能追加は行わない**（本書は **docs のみ**の追加）。
- 既存ドキュメントの本文編集も **本タスクでは行わない**（参照リンクの列挙に留める）。
- **危険線**（`WM_INPUT` / `WM_TIMER` / `WM_PAINT` の分岐・順序、`InvalidateRect` 条件、**T19 / T20 の受け入れ済みページの accepted 意味**）を **あいまいにしない**。

---

# 2. 今回までで固定された前提

以下は、直近の小工事・既存 docs と整合する **運用上の固定**として本書でも用いる。

- **legacy stacked HUD 分離フェーズは closed**。再オープンしない。
- **ページ式 HUD が通常運用の正**（受け入れの一次情報は `docs/HUD_PAGED_ACCEPTANCE.md`）。
- **T76 は close**。**T77 は step24 foundation close 済み**（`docs/T77_FOUNDATION_CLOSE.md`。foundation close をあいまいにしない）。
- **Win32 ホストの実行骨格**は `GetMessage` → `DispatchMessage` → **`WndProc`**。単一の `Update()` 相当入口は無い（`docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`）。
- **`WndProc` 経路の責務と危険線**は `docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md` で現状固定されている。
- **実装側の整理**（直近まで）:
  - **`Win32InputGlue`**: Raw Input / XInput 周辺の小整理・重複削除。
  - **`ControllerClassification`**: ゲームパッド系 **表示ラベル**（論理ボタン ID 名・ファミリ別ラベル・左スティック方向ラベル等）の集約。
  - **起動時のゲームパッドボタンラベル表ダンプ**（`OutputDebugStringW`）は **`MainApp.cpp` 側**に置き、分類モジュールから I/O 副作用を外した。
  - **`VirtualInputNeutral`**: **`241a077` で `include/input/core` へ第1回物理移動**（抽出ユニットの移動と参照更新のみ、**挙動変更なし**）。続きの読み手入口は **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md`**（**同表の「候補 A」行**と「まだ触らない周辺」）。
---

# 3. 小工事フェーズをここで止める理由

- **`ControllerClassification` に素直に入る中立ラベル移動**は、**追加で触っても得が薄く、T16/T17・guide・T19 表示に近いラベルに踏み込みやすい**段階に来ている。
- これ以上の「1 ブロック整理」は **体裁寄り・境界あいまい**になりやすく、**危険線への接近コスト**に見合わない。
- 「**無理に 1 ブロックをひねり出さない**」ことと「**foundation / 受け入れ済みページを崩さない**」という制約を両立するなら、**コードより先に「次の実務テーマ」を決める**方が安全である。

---

# 4. 次の実務テーマ候補

## 候補 A — ページ式 HUD（T19/T20）中心の **回帰確認・手動検証手順**の実務化

| 項目 | 内容 |
|------|------|
| **何を触るか** | **主手順**は **`docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md`**。**受け入れ・記録**は **`docs/HUD_PAGED_ACCEPTANCE.md`**（2026-04-13 追記含む）および **`docs/roadmap.md`** の短文まで **反映済み**。残るのは **運用の追随**（手順と一次情報の差分が出たときの **最小追記**）に近い。**「次の第一候補から手順を書き起こす」段階ではない**。 |
| **なぜ今か** | 足場は既にあるため、**再開直後の唯一の最優先**という位置づけは下げる。それでも **T19/T20 の accepted 意味**は **`HUD_PAGED_ACCEPTANCE.md` に立ち返る**必要がある（危険線は変えない） |
| **危険線** | **手順書の記述**が誤った解釈を誘発するリスクはあるが、**コードを直接触らない**なら即時の実行時リスクは低い。記述は **`HUD_PAGED_ACCEPTANCE.md` と矛盾しない**こと。 |
| **着手向き** | **必要時のみの短い docs 追随**。優先判断は **§5（VirtualInputNeutral pack-out 第2段 vs 追加の docs 整合）**に寄せる。 |

## 候補 B — **VirtualInputNeutral** の pack-out（再利用境界・第2段）

| 項目 | 内容 |
|------|------|
| **何を触るか** | 一次の設計メモは **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md` まで具体化済み**（**同表の「候補 A」行**・VirtualInputNeutral・「まだ触らない周辺」）。残るのは **pack-out 第2段の切り方**と、**その前に docs をさらに揃えるか**の判断。 |
| **なぜ今か** | `241a077` まで抽出ユニットの物理移動が入り、**再利用境界の読み手入口が 1 本に寄せられた**。第2段の前に **認知を合わせておく**と衝突が減る。 |
| **危険線** | 設計・追記だけなら低いが、**実装に落とすと WndProc 近傍に寄りやすい**。本文では **「実装は別タスク」**と明記する。 |
| **着手向き** | **§5 と一体**。**実装タスクは別途**（`WM_INPUT` / `WM_TIMER` / `WM_PAINT` / `InvalidateRect` の危険線をあいまいにしない）。 |

## 候補 C — **ロードマップ / 決定ログ**の同期（`roadmap.md`・`decisions.md`）

| 項目 | 内容 |
|------|------|
| **何を触るか** | 「小工事フェーズ終了」「次は検証手順 or 境界設計」など、**プロジェクトの意思**を **短文で更新**。 |
| **なぜ今か** | 外部・未来の自分への **再開コスト削減**。 |
| **危険線** | **低**（docs のみ）。 |
| **着手向き** | **今すぐ向き**だが、**本書と役割が被る**ため、**短い追記に留める**のがよい。 |

## 候補 D — **レンダラ / オーバレイ実験**（t32 / t33 / t37 系）の優先度整理

| 項目 | 内容 |
|------|------|
| **何を触るか** | 既存の実験メモを **「次に着手する価値があるか」**で並べ替え、**ページ式 HUD 正との関係**を書く。 |
| **なぜ今か** | 描画経路は **危険線が太い**ため、**いつコードを触るか**を先に合意した方が安全。 |
| **危険線** | **高**（`WM_PAINT` / `InvalidateRect` / D2D・GDI 接続）。 |
| **着手向き** | **後回し**。T19/T20 の手順・記録および pack-out 境界メモは **すでに先行**。**§5** の判断のあとで優先を見直す。 |

---

# 5. 第一候補として再開するなら何か

**VirtualInputNeutral の pack-out 第2段をどう進めるか**、**またはその前に docs をさらに揃えるか**の判断を第一候補とする。

- **理由**: `241a077` で `include/input/core` への第1回移動まで完了し、続きの地図は **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md`** に寄せてある。T19/T20 の手動確認は **`docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md`**・**`HUD_PAGED_ACCEPTANCE.md`（2026-04-13 反映）**・**`roadmap.md`** まで **docs 化・確認結果の反映まで進んでいる**ため、「手順をゼロから書く」優先度は下がっている。
- **危険線の置き場所は変えない**: **`WM_INPUT` / `WM_TIMER` / `WM_PAINT` の分岐・順序**、**`InvalidateRect` 条件**、**T19 / T20 の受け入れ済みページの accepted 意味**は **`WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`** と **`HUD_PAGED_ACCEPTANCE.md`** を一次情報とし、**あいまいにしない**。
- **まずやらないこと**: 候補 D のような **描画パイプライン実装**には、上記の地図と合意が付くまで踏み込まない。

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

1. **`docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md`** を読み、**VirtualInputNeutral**（**同ドキュメント内の表で「候補 A」とラベルされた行**）と **「まだ触らない周辺」**を再確認する。**pack-out 第2段のスコープ**を切るか、**手順・一次情報との差分が出たときの最小 docs 追随**を先にするかを決める。
2. T19/T20 については **`docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md`** に従い、必要なら **`docs/HUD_PAGED_ACCEPTANCE.md`** へ **最小限の追記**にとどめる。**analog 間引き・即時 `InvalidateRect` ・BUILDINFO 整合**など、**accepted 意味に触る用語は一次情報どおり**に保つ。
3. **実装変更は行わない**（本書の再開単位では）。未決が残る場合は **decisions / roadmap に 1 行**残す程度に留める。

---

## 参照（既存 docs）

- `docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`
- `docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`
- `docs/HUD_PAGED_ACCEPTANCE.md`
- `docs/T19_T20_MANUAL_VERIFICATION_GUIDE.md`
- `docs/ARCHITECTURE_PACKOUT_REUSE_BOUNDARY_NEXT.md`
- `docs/T77_FOUNDATION_CLOSE.md`
- `docs/architecture.md`
- `docs/roadmap.md` / `docs/decisions.md`
