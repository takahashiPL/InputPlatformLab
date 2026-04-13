# 1. 目的

本書は、ページ式 HUD の **T19（Input state）** と **T20（Build / Debug flags）** について、**人手で受け入れ状態を確認するための手順**を 1 か所にまとめたものである。

- **実装変更・挙動変更・新機能追加は行わない**（本書は **docs のみ**の追加）。
- **accepted 意味の一次情報**は `docs/HUD_PAGED_ACCEPTANCE.md`、**メッセージ経路の読み方**は `docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`、**再開入口**は `docs/NEXT_THEME_RESTART_ENTRY.md` を参照する。
- 本書は **検証手順の共有**であり、設計変更やリファクタ提案ではない。

---

# 2. 前提と固定事項

- **legacy stacked HUD 分離フェーズは closed**。再オープンしない。
- **ページ式 HUD が通常運用の正**（`Win32_HudPaged_IsEnabled()` が有効な構成を前提とする）。
- **T19 / T20 の受け入れ済みページの意味**を本文で再定義しない（矛盾がある場合は一次情報を正とする）。
- **T76 は close**。**T77 は step24 foundation close 済み**（foundation close をあいまいにしない）。
- **ページ番号（目安）**: タイトル帯の **5/7 が T19**、**7/7 が T20**（0 始まりインデックスでは T19=4、T20=6。ページ順は `MainApp.cpp` の `kHudPagedPageIndexT19` / `kHudPagedPageIndexT20` と `kHudPagedPageTitles` に一致）。

### 危険線の読み方（本ガイドでの位置づけ）

| 要素 | T19 / T20 確認時に押さえること |
|------|--------------------------------|
| **`WM_INPUT`** | キー・HID の**イベント受付**。論理入力の素材を更新する。**T19 の即時 `InvalidateRect` の主因ではない**（`WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md` の第6章「T19 に効く主経路」）。 |
| **`WM_TIMER`**（`TIMER_ID_XINPUT_POLL`） | **約 33ms 周期**。T19 表示中は **論理／pad 付加の差分で即 `InvalidateRect`**、**analog のみ**は **`kT19AnalogThrottleMs`（66ms）** で間引き（`Win32_WndProc_OnXInputPollTimer`）。 |
| **`InvalidateRect` → `WM_PAINT`** | **再描画が要求されない限り** HUD 本文は更新されない。analog 間引き中は**見た目の更新が遅れるのは受け入れ上想定内**。 |
| **`WM_PAINT`** | **実際に画面に描かれる本文**は paint 時に確定。D2D 本体と GDI オーバーレイの順序は `WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md` の描画パート（第4〜5章付近）。 |
| **T20 / `Win32_FormatBuildDebugManifest`** | **HUD 本文**（`Win32_HudPaged_FillT20PageBody`）と **起動時 `[BUILDINFO]`**（`Win32_EmitBuildInfoLogOnce`）は **同一関数**由来。実行中にマクロが変わるわけではない（再ビルドで変わる）。 |

---

# 3. T19 で何を確認するか

**T19 — Input state (logical / pad L+R / analog)** は、**1 ページ内で次の 3 ブロック**を読むページである（`HUD_PAGED_ACCEPTANCE.md` の T19 セクション）。

| ブロック | 内容 |
|----------|------|
| **common logical（8 行）** | South … DPad 等の**共通論理ボタン**表示。キーボード・割当に応じた入力と一致するか。 |
| **pad L+R（圧縮 2 列）** | L1/R1/L2/R2/L3/R3 および West/North 等の **pad 付加**表示。PS4 経路・XInput 等で期待と一致するか。 |
| **analog（3 行）** | LS/RS/LT/RT の数値・バー。**本文下端で切れない**こと。 |

**受け入れ上の骨格**: **論理／pad 付加の変化は即時再描画**、**analog のみの変化は 66ms 間引き**（`kT19AnalogThrottleMs`）。

---

# 4. T19 の手動確認手順

1. **ビルド・起動**  
   - 通常は **Debug\|x64** の `MainApp.exe`（`HUD_PAGED_ACCEPTANCE.md` の記述に沿う）。  
   - メインウィンドウを**前面**に置く。

2. **T19 ページを開く**  
   - **メニューが開いていない**状態（`menuOpen` なし）で、**← →** によりページ送り。  
   - タイトルが **T19 — Input state**（表記は `kHudPagedPageTitles` 依存）であり、**5/7** 相当であることを確認。

3. **keyboard（論理）**  
   - キーボードで **論理ボタンに割り当てられたキー**を押す／離す。  
   - **common logical** の行が**入力と一致**して更新されることを目視。

4. **ゲームパッド（論理 + pad L+R）**  
   - **XInput** または **PS4 HID** 等、環境に応じたデバイスで **デジタルボタン**（面ボタン・肩・スティッククリック・DPad 等）を操作。  
   - **common logical** と **pad L+R** が**期待と一致**することを目視。  
   - **L2/R2**: 受け入れ上、**r** 表示は**表示用安定化バイト**であり、タイマー差分は analog と同様の量子化を前提とする（`HUD_PAGED_ACCEPTANCE.md`）。

5. **analog（スティック・トリガー）**  
   - スティックを大きく動かし、**LT/RT** を押す。  
   - **analog ブロック**の数値・バーが**おおむね入力と一致**し、**ページ下端で analog が欠落しない**ことを目視。  
   - **スティックだけ**を連続で動かす場合、**66ms 未満での更新が画面に現れない**ことがある（間引き）。**論理／pad を一度動かす**と即時更新が走り、スナップショットが揃う。

6. **（任意）デバッグログ**  
   - `kHudPagedPaintDebugLog` が **true** のビルドでは、`[HUDPAINT] timer page=T19 ...` が出る（通常の Debug ビルドでは **false**）。出る場合は **logicalChanged / analogThrottled / invalidateIssued** の意味を第9章で読む。

---

# 5. T19 の OK / NG 判定

### OK の目安

- **論理または pad 付加**を変えた直後（数タイマー周期以内）、**HUD の該当行が入力と整合**して更新される。  
- **analog だけ**を変え続けたとき、**完全にリアルタイムではない**が、**約 66ms 以上開けて変化**すれば追随する（間引き後に更新）。  
- **3 ブロック**が**同時に読める**標準ウィンドウサイズで、**analog が本文外に落ちて読めない**状態にならない（右端が狭いクライアントは `HUD_PAGED_ACCEPTANCE.md` の未確認事項）。

### NG の兆候（調査・記録のトリガ）

- **キー／パッドを操作しているのに**、論理／pad 行が**一切変わらない**（`WM_TIMER` 停止・別ページ表示・入力経路の異常等を疑う。**コード変更は本書外**）。  
- **論理は即時なのに**、**pad 付加だけ**が常にずれる／更新されない（差分判定やスナップショットの不整合の可能性）。  
- **analog だけ**が**数秒以上**更新されない（間引き以上の停滞。別要因の切り分けが必要）。  
- **T19 以外のページ**を表示しているつもりがページインデックスのずれで**別ページを見ている**（タイトル帯の **n/7** を必ず確認）。

---

# 6. T20 で何を確認するか

**T20 — Build / Debug flags** は、**現在の exe がどのビルド設定・マクロで組み立てられたか**を **1 本のマニフェスト**として読むページである。

- **HUD 本文**は `Win32_HudPaged_FillT20PageBody` が **`Win32_FormatBuildDebugManifest`** で生成した文字列を載せる（`MainApp.cpp` コメントどおり）。  
- **起動直後の `[BUILDINFO]`** ログも **同一関数**由来である（`HUD_PAGED_ACCEPTANCE.md` の T20 セクション、`WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md` の T20 セクション）。

確認の主眼は **`config=`（Debug/Release）**、**`platform=`**、**ページ式 HUD 関連マクロ**、**`__DATE__` / `__TIME__`** 等が **HUD とログで矛盾しない**ことである。

---

# 7. T20 の手動確認手順

1. **ビルド・起動**  
   - **Debug\|x64** でビルドし起動（ベースライン）。  
   - **Release\|x64** でも 1 度繰り返すと、`config=Release` の確認になる（`HUD_PAGED_ACCEPTANCE.md` 2026-04-10 記録と同種）。

2. **T20 ページを開く**  
   - **← →** で **7/7**（**T20 — Build / Debug flags**）を表示。

3. **HUD 本文を読む**  
   - **`config=`**、**`platform=`**、**`pagedHUD=`** 等が**そのビルド**と整合するか目視。  
   - **`__DATE__` / `__TIME__`** が**直近ビルド**と整合するか目視。

4. **起動ログと照合（同一セッション・同一バイナリ）**  
   - デバッガ出力または DebugView で **起動直後の `[BUILDINFO]`** ブロックを探す。  
   - **HUD の本文**と **マクロ行の集合**が**同一マニフェスト**として読めるか（行の折り返しの違いはあってよいが、**矛盾する値がない**こと）。

---

# 8. T20 の OK / NG 判定

### OK の目安

- **Debug** ビルドで **`config=Debug`**、**Release** ビルドで **`config=Release`**。  
- **HUD のマクロ一覧**と **`[BUILDINFO]`** に **同じ `Win32_FormatBuildDebugManifest` 由来**の内容がある（同一バイナリでは実行中に内容が変わらない）。  
- タイトルが **T20 — Build / Debug flags** である（`HUD_PAGED_ACCEPTANCE.md` の最低限項目）。

### NG の兆候

- **同一バイナリ**なのに、**HUD と `[BUILDINFO]`** で **`config` や主要マクロ**が食い違う（**あり得ない想定**。記録用にビルド ID・時刻・取得元を残す）。  
- **T20 本文が空**、または **明らかに別ページの本文**が出ている（ページインデックスの取り違え）。  
- **古い exe** を起動している（エクスプローラの更新日時と `__TIME__` の突合）。

---

# 9. 関連ログと読み方

| ログ・出力 | いつ | 何を見るか |
|------------|------|------------|
| **`[BUILDINFO]`** | **起動直後**（`Win32_EmitBuildInfoLogOnce`） | **T20 と同じ**マニフェスト。T20 確認時の**照合元**。 |
| **`[HUDPAINT] timer page=T19 ...`** | **`kHudPagedPaintDebugLog == true`** のビルドのみ | **logicalChanged / analogChanged / analogThrottled / invalidateIssued**。論理即時・analog 間引きの**実際の判定結果**を追うときに使う（通常ビルドでは出ない）。 |
| **その他 `OutputDebugStringW`** | 入力・T17 等 | T19/T20 の**受け入れ必須ではない**。迷ったら **T19 はタイマーと `WM_PAINT`**、**T20 はマニフェスト一致**に立ち返る。 |

---

# 10. 今回触っていない範囲

- **ソースコード・ビルド設定・リソース・既存 docs の本文**は**変更していない**（本ファイルの **新規追加のみ**）。  
- **T14〜T18・T17 単体**の詳細手順は `docs/HUD_PAGED_ACCEPTANCE.md` に委ねる。  
- **レガシー縦積み HUD**・**WM_VSCROLL レガシー経路**の検証は本書のスコープ外（ページ式が正）。  
- **実装の改修・定数変更・`InvalidateRect` 条件の変更**は行わない（検証で問題が見つかった場合は、**別タスクで**一次情報と照合して判断する）。

---

## 参照

- `docs/HUD_PAGED_ACCEPTANCE.md`（T19 / T20、ページ順・最低限項目）
- `docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`（T19 の章、T20 の章、危険線まとめ）
- `docs/NEXT_THEME_RESTART_ENTRY.md`（候補 A・着手単位）
- `docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`（メッセージループとエンジン対応）
