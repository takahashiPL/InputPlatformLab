# 1. 目的

本書は、`MainApp.cpp` の **`WndProc` と主要な `WM_*` 経路の責務**を、**現状実装として固定**する。次の「小さく安全な整理」の前に、**どのメッセージが何を起こし、T19 / T20 の受け入れ済みページにどう効くか**を文章で共有するためのドキュメントである。

- **実装変更・動作変更・新機能追加は行わない**（本書は **docs のみ**）。
- **legacy stacked HUD 分離に言及して再開する話はしない**（分離フェーズは closed）。
- **T19 / T20 の accepted 状態を崩す提案は書かない**（受け入れの一次情報は `docs/HUD_PAGED_ACCEPTANCE.md`）。
- 本書は **設計メモ・現状固定**であり、**コード変更提案そのものではない**。

---

# 2. 前提と固定事項

以下はプロジェクト運用と整合する前提として本書でも固定する。

- **legacy stacked HUD 分離フェーズは closed**。再オープンしない。
- **ページ式 HUD が通常運用の正**。
- **T19 / T20 の受け入れ済みページ**の意味を崩さない整理にする（T19 は論理+pad の即時 `InvalidateRect` と analog の間引き等。T20 は `[BUILDINFO]` と同一マニフェスト由来の表示等）。
- **T76 は close**。**T77 は step24 foundation close 済み**（foundation close を暧味にしない。`docs/T77_FOUNDATION_CLOSE.md`）。
- **Win32 ホスト上では `GetMessage` → `DispatchMessage` → `WndProc` が骨格**であり、**イベント駆動とタイマー起点のポーリングが混在**する（詳細なエンジン対応は `docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`）。
- **`MainApp.cpp` に単一の `Update()` 相当入口は無い**（処理は `WndProc` の分岐とその先の静的関数に分散）。

---

# 3. 先に結論

- **`WndProc` はメインウィンドウのメッセージディスパッチの中枢**である。コメントどおり、**入力の主経路は `WM_INPUT`（キー・HID）と `WM_TIMER`（XInput 周期・論理入力 tick・メニュー consumer）**、**描画の主経路は `WM_PAINT`**。
- **T19**は、**`WM_TIMER` 内での論理／pad／analog の差分判定**と、条件を満たしたときの **`InvalidateRect` → `WM_PAINT` → ページ本文再生成**の連鎖が受け入れ上重要である（analog は `kT19AnalogThrottleMs` で間引き）。
- **T20**は、**表示は主に `WM_PAINT` 経路のページ式 HUD 本文生成**に依存し、本文は **`Win32_FormatBuildDebugManifest`（`Win32_HudPaged_FillT20PageBody` 経由）**で組み立てられる。**起動時の `[BUILDINFO]`** も同一マニフェスト生成関数由来であり、**「ログと HUD の一致」が受け入れの読み方**である。
- **`InvalidateRect` の条件**、`WM_INPUT` / `WM_TIMER` / `WM_PAINT` のつながり、**D2D 左列 prefill と GDI オーバーレイ**の接続は、小工事でも**危険線**として見なすのが妥当である。

---

# 4. WndProc 全体の読み方

1. **トップレベル**: `wWinMain` 側の **`while (GetMessage …)`** がメッセージを取り出し、**`DispatchMessage` が `WndProc` を呼ぶ**。これがアプリの実行骨格である（**エンジンの単一 game loop 関数ではない**）。
2. **`WndProc`**: `switch (message)` で **メッセージ種別ごとに処理を分岐**する。多くの case は **薄いラッパ**で、実処理は **`Win32_WndProc_On*` 静的関数**に寄せられている。
3. **入力の二系統**:
   - **`WM_INPUT`**: Raw Input の**イベント受付**。キーは `KeyboardActionState` 等を更新。HID ゲームパッドはレイヤ関数へ。
   - **`WM_TIMER`（`TIMER_ID_XINPUT_POLL`）**: **周期処理**。XInput 系のデジタルエッジ、論理入力 tick、**統合メニュー consumer**、**T19 専用の差分と `InvalidateRect`** などがここに集約される（**FixedUpdate 相当に「近い」が、fixed-step 保証ではない**）。
4. **描画**: **`WM_PAINT` → `Win32_WndProc_OnPaint` → `Win32_MainView_PaintFrame`**。レンダラ（D3D/D2D）と **GDI オーバーレイ** が **同一 paint フレーム内**で順に実行される（**描画フェーズ寄り**）。
5. **関連モジュール**（本書での位置づけ）:
   - **`Win32InputGlue.*`**: 登録・列挙・スロット・`WM_INPUT` 調査など **Win32 / 入力プラットフォーム寄りのグルー**（`WndProc` 本体ではないが、`WM_INPUT` 経路と結びつく）。
   - **`T18InventorySnapshotGlue.*` / `T18PageBodyFormatGlue.*`**: **T18 本文・スナップショット・フォーマット**（**描画や状態参照の素材**）。`WndProc` のメッセージ種別表に「全部載せ」はしないが、**ページ式 HUD の本文生成**とセットで読む。

## 4.1 フェーズ語との対応（参照だけ固定）

本書では `WndProc` の **主責務**を一次情報として固定しつつ、再開時の読み筋としてだけ **variable-like / fixed-like / render** の語を使う。これは **意味の再定義ではなく参照名** である。

| フェーズ語 | 主な `WM_*` 経路 | 本書での扱い |
|------------|------------------|---------------|
| **variable-like** | `WM_INPUT` | 生入力とその周辺状態更新の主経路として参照する。**単一の `Update()` 相当入口ではない。** |
| **fixed-like** | `WM_TIMER` → `Win32_WndProc_OnXInputPollTimer` | 一定間隔処理として最も近い。**fixed-step 完了の意味では使わない。** |
| **render** | `WM_PAINT` → `Win32_WndProc_OnPaint` → `Win32_MainView_PaintFrame` | 描画フレームの主経路として参照する。**`InvalidateRect` 条件や paint 順序の意味は変えない。** |

---

# 5. 主な WM_* メッセージと責務

| メッセージ | 責務（現状解釈） | 呼び出し先（代表） | 備考 |
|------------|------------------|---------------------|------|
| **WM_ERASEBKGND** | 既定の背景消去を抑止し、D3D 表示を優先 | （`return 1`） | 描画スタックとの兼ね合い。 |
| **WM_COMMAND** | メニュー **About / Exit** など | `DialogBox` / `DestroyWindow` / `DefWindowProc` | アプリシェルの UI。 |
| **WM_INPUT** | **Raw Input**：HID ゲームパッド・キー | `Win32_WndProc_OnRawInput` | **入力イベント受付の主経路**。キーは論理状態・T17 Enter ラッチ等。 |
| **WM_TIMER** | **`TIMER_ID_XINPUT_POLL` のみ**処理 | `Win32_WndProc_OnXInputPollTimer` | XInput・論理 tick・メニュー・**T19 の差分と `InvalidateRect`**。**ポーリング混在の中心**。 |
| **WM_SIZE** | クライアントサイズに合わせレンダラリサイズ、レイアウト補助 | `Win32_WndProc_OnClientSize`、ログ、`InvalidateRect` | スワップチェーン・オーバーレイの土台。 |
| **WM_EXITSIZEMOVE** | サイズ移動終了後の再描画強制 | `RedrawWindow` | GDI 欠け対策（コメント意図）。 |
| **WM_MOVE** | 再描画要求 | `InvalidateRect` | オーバーレイ追随。 |
| **WM_WINDOWPOSCHANGED** | 再描画要求 | `InvalidateRect` | 同上。 |
| **WM_VSCROLL** | **レガシー縦スクロール**（ページ式 HUD 有効時は未処理） | `Win32_WndProc_OnVScroll`（条件により `false`） | **ページ式が正のときはネイティブスクロール経路が短絡されない**ように注意。 |
| **WM_MOUSEWHEEL** | ホイールでスクロール量更新・バー更新 | `Win32_WndProc_OnMouseWheel` | 変更時 `InvalidateRect`。 |
| **WM_PAINT** | **1 フレームの描画** | `Win32_WndProc_OnPaint` → `Win32_MainView_PaintFrame` | D2D prefill → `WindowsRenderer_Frame` → **`Win32DebugOverlay_Paint`（GDI）**。 |
| **WM_DESTROY** | タイマー停止、フルスクリーン復元、レンダラ終了、終了メッセージ | `KillTimer` 等 | ライフサイクル終端。 |

---

# 6. T19 に効く主経路

**T19（Input state: logical / pad L+R / analog）**は、受け入れドキュメントどおり **「論理+pad の変化は即時再描画、analog のみは間引き」**が重要である。現状コード上の読み方は次のとおり。

1. **`WM_INPUT`（キー・HID）**  
   - キー・HID の状態が更新され、**論理入力の素材**になる。  
   - **T19 専用の `InvalidateRect` はここでは主に張らない**イメージで読む（**即時性の判定はタイマー側とセット**）。

2. **`WM_TIMER`（`Win32_WndProc_OnXInputPollTimer`）**  
   - `Win32_XInputPollDigitalEdgesOnTimer` → `Win32_LogicalInputTick_AfterPadAndKeyboardCurrent` のあと、**ページが T19 のとき**に限り:
     - 論理表示スナップ・pad 付加・アナログ文字列を **現在値として取得**。
     - **論理／pad 付加の変化**があれば **即 `InvalidateRect`**（スナップショット更新とセット）。  
     - **analog のみの変化** は **`kT19AnalogThrottleMs` 未満であれば間引き**、経過後に `InvalidateRect`。  
   - これが **T19 の「差分判定 / InvalidateRect / あとで WM_PAINT」**の核である。

3. **`WM_PAINT`（`Win32_MainView_PaintFrame` → `Win32DebugOverlay_Paint` → ページ式 GDI）**  
   - **実際に HUD に描かれる論理／pad／analog** は、**paint 時の本文生成**で確定する。  
   - タイマーで `InvalidateRect` されなければ、**見た目は更新されない**（イベント駆動・間引きの意味で受け入れと一致）。

4. **危険線（T19 関連）**  
   - **`kT19AnalogThrottleMs`・論理スナップ・pad 付加比較** を変更すると、受け入れ記載の「即時／間引き」が崩れやすい。  
   - **`WM_INPUT` と `WM_TIMER` のどちらだけを見ても T19 は説明しきれない**（**混在が前提**）。

---

# 7. T20 に効く主経路

**T20（Build / Debug flags）**は、受け入れどおり **HUD 本文が `[BUILDINFO]` と同一のマニフェスト**であることが重要である。

1. **本文生成**  
   - `Win32_HudPaged_FillT20PageBody` が **`Win32_FormatBuildDebugManifest` を呼ぶ**だけの薄いラッパである。  
   - 起動時 **`Win32_EmitBuildInfoLogOnce`** も **同じ `Win32_FormatBuildDebugManifest`** で `[BUILDINFO]` を出す。

2. **`WM_PAINT`**  
   - ページ式 HUD が **T20 を表示しているとき**、**paint 経路で本文バッファが再構築され GDI に載る**。  
   - **T19 のような専用の差分 `InvalidateRect` ブロックは T20 には無い**（**表示更新は他ページと同様、ウィンドウの再描画要求に依存**）。**ビルド種別が変わるのは主に再ビルド後**であり、実行中にマクロが変わるわけではない。

3. **読み方の整理**  
   - T20 は **「表示・マニフェストの一致性」ページ**であり、**入力ポーリングや analog 間引きの対象ではない**。  
   - **T18 glue** と同様、**「データ／文字列の組み立て」と「paint で載せる」**の関係で理解するとよい（T18 はデバイス状態・T20 はビルドマクロ）。

4. **危険線（T20 関連）**  
   - **`Win32_FormatBuildDebugManifest` の内容・呼び出し元の分岐** を変えると、**`[BUILDINFO]` と HUD の一致**という受け入れ読み方を崩しやすい。

---

# 8. いま危ない変更箇所

**挙動が変わりやすい境界**というのが妥当である（**小工事でも触る前に受け入れ手順とセット** が望ましい）。

- **`WndProc` 内の `WM_INPUT` / `WM_TIMER` / `WM_PAINT` の順序・早期 return・`break` の有無**。
- **phase 語（variable-like / fixed-like / render）を使った読み替えで、`WM_*` の主責務を書き換えること**。
- **`InvalidateRect` / `RedrawWindow` を張る条件**（T19、T17 no-op skip の注意、サイズ移動系）。
- **`Win32_WndProc_OnXInputPollTimer` 内の T19 差分判定と `kT19AnalogThrottleMs`**。
- **`Win32_MainView_PaintFrame` 内の D2D prefill、`WindowsRenderer_Frame`、`Win32DebugOverlay_Paint` の順序と suppress フラグ**（二重描画・本文欠け）。
- **ページ式 HUD が有効なときの `WM_VSCROLL` の取り扱い**（実装は **paged 時にネイティブなスクロール処理へ入らない** 経路がある）。
- **キー入力と T17（Enter ラッチなど）** が **`WM_INPUT` 経路に依存** する部分。

---

# 9. いま安全に触れやすい箇所

**メッセージのタイミングや `InvalidateRect` に依存しない** が、相対的に触りやすい（**それでも回帰確認は必要**）。

- **`Win32InputGlue.*`** のうち、**登録・列挙・ログ・デバイス一覧** など、**`VirtualInputMenu` や T19 差分に直結しない** 調査用コード。
- **`T18InventorySnapshotGlue.*` / `T18PageBodyFormatGlue.*`** のうち、**単純な文字列・スナップショット整形** で、**呼び出し契約が明確** な部分（**呼び出し側のタイミングは変えない**）。
- **本書や `ENGINE_LOOP_MAPPING_...` の追記** など **docs のみ** の整理。
- **デバッグログの文言**（**条件分岐・タイミングを変えない** 場合に限る）。

---

# 10. 今回の整理で変えていないこと

- **ソースコード・ビルド設定・リソース・挙動: 一切変更していない**（**`docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md` の追加のみ**）。
- **T19 / T20 の受け入れ内容を上書きしない**（解釈は `docs/HUD_PAGED_ACCEPTANCE.md` に委ねる）。
- **foundation close や post-foundation 史実を暧味にしていない**。
- 本書は **実装変更提案ではない**。

---

**相互参照:** `docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`（ループのエンジン対応）、`docs/HUD_PAGED_ACCEPTANCE.md`（ページ別受け入れ）、`docs/T77_FOUNDATION_CLOSE.md`（T77 foundation）、`docs/HUD_LEGACY_CODE_DEPENDENCY.md`（レガシー経路の位置づけ・本書は legacy 分離再開を言及しない）。
