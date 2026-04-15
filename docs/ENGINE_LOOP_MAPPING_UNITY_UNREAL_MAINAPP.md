# 1. 目的

本書は、InputPlatformLab の **現状実装**（Win32 ホスト上の **メッセージループを中心としたイベント処理とポーリングの混在**）を、ゲームエンジンである **Unity の Update / FixedUpdate / 描画フレーム** および **Unreal の Tick / Physics Sub-step / OnPaint（Slate）** と **横に並べて読むための対応表**として整理する。

- **再設計の前提を固定する**（「コード上のどこが、どの処理区分に近いか」を曲解のない形で共有する）。
- **Unity や Unreal そのものではない**ことを前提に、**近い / 相当 / 完全一致ではない** を本文・表の両方で区別する。
- **実装変更・動作変更・新機能追加・リファクタは行わない**。本書は docs のみの追加である。

---

# 2. 前提と固定事項

以下は本リポジトリの運用前提として **本書でも固定**する（プロジェクトの史実と矛盾しない）。

- **legacy stacked HUD 分離フェーズは closed**。再オープンしない。
- **ページ式 HUD が通常運用の正**（レガシー経路のみは保守上の経路に留まる。詳細は `docs/HUD_LEGACY_CODE_DEPENDENCY.md` 等）。
- **T19 / T20 の受け入れ済みページ**の意味（例: T19 の論理変化は即時 `InvalidateRect`、アナログは間引き等）を **崩さない**整理にする。受け入れの一次情報は `docs/HUD_PAGED_ACCEPTANCE.md`。
- **T76 は close**。
- **T77 は step24 foundation close 済み**（foundation close を暧味にしない）。`docs/T77_FOUNDATION_CLOSE.md` を参照。
- **post-foundation は step6 `82aa49f` まで完了済み**という整理（T18 本文フォーマットの glue 分離等）を崩さない。
- 本タスクは **docs 追加のみ**（上記の禁止事項と整合する）。

---

# 3. 先に結論

- **MainApp.cpp は「1 本のゲームループ関数」ではない。** `wWinMain` の `GetMessage` ループが骨格で、実処理は **`WndProc` に分散**している。Unity の `Update()` や Unreal の `AActor::Tick` の **単一入口に相当するものは存在しない**（**完全一致ではない**）。
- **入力**は **イベント（`WM_INPUT`）** と **タイマー起点のポーリング（`WM_TIMER` / XInput 周期）** の **混在**。**「メッセージイベントのみ」でも「ポーリングのみ」でもない**。
- **描画**は主に **`WM_PAINT` → `Win32_MainView_PaintFrame`**。D3D/D2D の `WindowsRenderer_Frame` と **GDI オーバーレイ**が **同一 paint コールバック内**で順に走る。**Unity の一般的な PaintFrame API と同一視はしない**（**意味は「OS が発火する描画フレーム」に近い**）。
- **`WM_TIMER`（約 33ms）** は **一定間隔の処理**に**近い**が、**アプリ全体が FixedUpdate 的な fixed-step シミュレーションであるとは言い切れない**（タイマー精度、メッセージ滞留、`InvalidateRect` の発行タイミング、ページ別ロジックなどが混在するため）。
- **物理エンジン相当のサブステップループは無い。** 「物理相当の一定刻み更新」に**相当する処理は無い**（N/A に近い）とみなすのが現状に忠実である。
- **現状は Win32 ホスト + メッセージループ中心のイベント処理 + ポーリングの混在**である（英語でいう *event driven* に近い側面と、タイマー起点の周期処理が同居する）。

## 3.1 現況判定（2026-04 時点）

- **描画フレーム**として読む主経路は、**`WM_PAINT` → `Win32_WndProc_OnPaint` → `Win32_MainView_PaintFrame`**。
- **固定寄り更新**として読む主経路は、**`WM_TIMER`（`TIMER_ID_XINPUT_POLL`）→ `Win32_WndProc_OnXInputPollTimer`**。
- **可変更新寄り**として読む主経路は、**`WM_INPUT`** とその周辺状態更新だが、**単一の `Update()` 相当入口にはなっていない**。
- したがって、**区分の docs 整理は可能だが、`MainApp.cpp` の実装が Variable / Fixed / Render に分離完了しているとは言わない**。
- 次に安全に進めるなら、**区分の意味を docs で固定する段階**までで止め、`WndProc` の分岐・順序や `InvalidateRect` 条件には触れない。

## 3.2 今回ここで止める線

- **docs 上の区分名をそろえること**までを今回の守備範囲とする。
- **`MainApp.cpp` が Variable / Fixed / Render に実装分離完了した**とは言わない。
- **`WndProc` の分岐・順序、`InvalidateRect` 条件、T19/T20 accepted の意味**に踏み込む変更は **別テーマ**に分ける。

---

# 4. 対応表（Unity / Unreal / MainApp.cpp）

**機械的な 1 対 1 の対応**として読むことはせず、**どの意味で近いか**を同じ行で並べる。**太字は MainApp 側の主たるフック**。

| 項目 | Unity（近い捉え方） | Unreal（近い捉え方） | MainApp.cpp（現状） |
|------|---------------------|----------------------|---------------------|
| **可変更新** | フレームごとの `Update()`（`Time.deltaTime` は可変） | `Tick(DeltaSeconds)`（既定は可変デルタ） | **単一の可変更新関数（Update 相当）は無い。** `WM_INPUT` / **`WM_TIMER`（`Win32_WndProc_OnXInputPollTimer`）** / メニュー・状態更新が **メッセージ処理に分散**し、**「毎フレームちょうど一度」という単一ループ像ではなくイベント発生単位**。**近いが完全一致ではない。** |
| **固定更新** | `FixedUpdate()`（`fixedDeltaTime` ベースの離散刻み） | 物理統合の固定ステップ（`Substep` / physics tick と混同禁止） | **`WM_TIMER`（`TIMER_ID_XINPUT_POLL`、約 33ms）** が **一定間隔処理**として**最も近い**。ただし **アプリ全体のシミュレーション刻みとしての fixed-step 保証は読み取らない**。**相当候補** / 完全一致ではない。** |
| **描画** | カメラ・レンダパイプライン（`OnRenderObject` 等も含む広義） | レンダリングパス全体（ワールド）。UI は別系統（Slate） | **`WM_PAINT` → `Win32_WndProc_OnPaint` → `Win32_MainView_PaintFrame`** 内で **`WindowsRenderer_Frame`**（D3D/D2D）→ **`Win32DebugOverlay_Paint`（GDI）**。**OS paint にぶら下がる描画フレーム**として Unity/Unreal の「ゲームスレッド tick」とは **分離**。**近いのは「描画フェーズ」寄り。** |
| **入力イベント受付** | 新 Input System / 旧 Input のイベント、UI イベント等 | 入力デバイス・Enhanced Input、Slate のルーティング等 | **`WM_INPUT` → `Win32_WndProc_OnRawInput`**（Raw Input、HID レイヤ）。**メッセージイベント起点**。**近い。** |
| **入力ポーリング** | スクリプト内での `Input.GetKey` 的参照、又はポーリングバックエンド | Tick での状態問い合わせに近いパターンもあり得る | **`Win32_WndProc_OnXInputPollTimer` 内の XInput 系処理**（デジタルエッジ等）が **周期ポーリングに近い**。キー状態の保持・統合もタイマー側と組み合わさる。**ポーリング寄り**。**近い。** |
| **HUD / Debug UI 本文生成** | IMGUI / UI Toolkit / uGUI のレイアウト・テキスト組み立て | Slate の `OnPaint` 内での文字列組み立て等（ワールドと混同禁止） | **ページ式 HUD 有効時:** `Win32DebugOverlay_Paint` → **`Win32_HudPaged_PaintGdi`**。本文バッファは **`Win32_HudPaged_FillT18PageBody` 等**（`switch (s_hudPagedIndex)`）で組み立て。D2D 左列は **`Win32_HudPaged_PrefillD2d`**（`Win32_DebugOverlay_PrefillHudLeftColumnForD2d` 経由）。**Slate OnPaint や uGUI に完全一致ではないが「表示直前のテキスト組み立て」に近い。** |
| **物理相当の一定刻み更新** | 物理エンジンの離散ステップ | Physics sub-stepping（固定刻み） | **現実装に相当する物理ループは無い。** `WM_TIMER` は **サンプリング間隔**として**見た目は似る**が **物理サブステップの代替ではない**。**N/A に近い。** |
| **実行タイミングが OS イベント依存か** | PlayerLoop はエンジン制御だがプラットフォーム依存あり | ゲームスレッド／レンダスレッド構成等 | **強く OS メッセージに依存。** `GetMessage` → `DispatchMessage` → **`WndProc`**。**メッセージイベントが骨格**。**はい（依存）。** |
| **フレームレート低下時の進み方** | `Update` が遅延すると delta が大きくなる（可変）。`FixedUpdate` は追いつき回数調整 | 可変 Tick と固定物理の関係は設定依存 | **メッセージキューが溜まると遅延・合間実行。** **`WM_TIMER` は厳密な実時間ロックではない**。描画は **`InvalidateRect` の発行タイミング**（T19 は論理即時／アナログ間引き）。**「溜まった分を固定回数で追いつく」型の保証は読み取らない**。**可変寄り、ページ別特例。** |
| **実装上の注意点** | PlayerLoop の各フェーズ順序に依存 | Tick グループ・World 種別・Slate パイプラインの差 | **単一ループではなく分散。** **Raw Input とタイマー起点ポーリングの二系統。** 描画は **`WM_PAINT` バッチ**。**`Win32_MainView_PaintFrame` は Unity API の PaintFrame ではない。** **Unreal の Tick / Physics / Slate OnPaint を混同しない。** |

### 4.1 関連モジュールの所在を固定

| モジュール | 本対応表での位づけ |
|------------|----------------------|
| `Win32InputGlue.*` | Raw Input 登録、デバイス列、XInput スロット調査、HID 要約、`WM_INPUT` 調査用ヘルパ、T76 スロットル等。**入力グルー／調査**。**ゲームループそのものではない。** |
| `T18InventorySnapshotGlue.*` | T18 スナップショット完成、分類根と理由の文字列、デバッグ行、短い HUD 用文字列。**データ整形・デバッグ表示素材**。**Update 相当の単一 tick ではない。** |
| `T18PageBodyFormatGlue.*` | T18 ページ本文の compact 行・`swprintf` 束ね。**HUD 本文生成の一部**（`Win32_HudPaged_FillT18PageBody` から利用）。**描画コールバック内で呼ばれる想定の表示用ロジック。** |

---

# 5. 現在の MainApp.cpp の読み方

1. **プロセス入口:** `wWinMain` の **`while (GetMessage ...)`** がアイドル時も含めた **トップレベルのメッセージループ**。
2. **ウィンドウ処理:** `WndProc` 冒頭コメントどおり、**入力は `WM_INPUT` と `WM_TIMER`、描画は `WM_PAINT`** が主（サイズ・スクロール・ホイール等も関連）。
3. **生入力:** `WM_INPUT` → `Win32_WndProc_OnRawInput`（キーは `KeyboardActionState`、HID はレイヤ関数へ）。
4. **周期処理:** `WM_TIMER`（`TIMER_ID_XINPUT_POLL`）→ `Win32_WndProc_OnXInputPollTimer`（XInput 系 → 論理入力 tick → メニュー consumer。T19 ページ時は `InvalidateRect` のルールに沿って再描画を要求）。
5. **描画:** `WM_PAINT` → `Win32_MainView_PaintFrame`（レンダラ `WindowsRenderer_Frame` の後に **GDI オーバーレイ**。ページ式 HUD は `Win32DebugOverlay_Paint` が `Win32_HudPaged_PaintGdi` に分岐）。
6. **「ゲームロジック 1 関数」探しをしない:** 状態更新は **複数のメッセージハンドラ**に分散。**Unity/Unreal の単一 Tick モデルで読むと脚が合わない**のが正常。

---

# 6. どこが FixedUpdate 相当で、どこがそうではないか

**FixedUpdate に「相当」候補として最も近いのは `WM_TIMER` 経路**（`Win32_WndProc_OnXInputPollTimer`）。**理由は、意図した周期（約 33ms）で XInput 系の更新が走る**からである。

**ただし「そうではない／言い切れない」理由**も並列に置く。

- タイマーは **OS のタイマーであり実時間と 1:1 でロックされない**（高負荷・メッセージキュー滞留時に遅延・合間実行）。
- **キー・HID の主要経路は `WM_INPUT`** であり、**固定刻みと非同期**。
- **描画・HUD 更新は `WM_PAINT` と `InvalidateRect` に依存**し、**論理更新と描画更新が常に同周期とは限らない**（T19 のアナログ間引きはその例）。
- **物理や決定論向けの sub-step ループは無い。**

まとめると: **「固定更新に似たサンプリング」はあるが、エンジンの FixedUpdate / Physics tick の意味での fixed-step ではない**（**近い / 相当候補 / 完全一致ではない**）。

---

# 7. 将来もし再編するなら、どの方向が自然か

本書で固定したのは **docs 上の読み方**までであり、ここから先は **別テーマの設計方向**として読む。

- **概念上分離するなら**、エンジン用語でいう **VariableUpdate（可変刻みの状態更新）**、**FixedUpdate（エンジン意味での fixed-step とは別物だが「契約された刻み」の処理）**、**Render（描画）** である。**現状の MainApp.cpp は Unity そのものでも Unreal そのものでもないが、再編するなら VariableUpdate / FixedUpdate / Render の分離方向が自然**（**設計上の方向性の話であり、本書は実装変更提案ではない**）。
- Win32 では **メッセージハンドラはそのまま「OS が呼ぶ境界」**として残し、**内部で明示的なフェーズ関数に委ねる**形が読みやすい、という程度の示唆に留める（**断定しない**）。

---

# 8. 今回の整理で変えていないこと

- **コード・ビルド設定・リソース・動作: 一切変更していない**（**docs 追加のみ**）。
- **legacy / ページ式 / T19・T20 受け入れ / T76 / T77 foundation / post-foundation step6 の史実整理を上書きしていない**。
- 本書は **設計メモ**であり、**実装変更提案そのものではない**。

---

**結語（固定文）:** 現状の MainApp.cpp は Unity そのものでも Unreal そのものでもないが、再編するなら VariableUpdate / FixedUpdate / Render の分離方向が自然。ただし今回の docs は設計メモであり、実装変更提案そのものではない。
