# ページ式 HUD — 受け入れ確認パック

前提: **縦積み HUD の通常運用には戻らない**。GDI で `Win32_HudPaged_PaintGdi` が本文を描画し、D2D/D3D 本体描画と二重にならない構成（`Win32HudPaged.h` 方針）。

ページ順は **コード上の `s_hudPagedIndex` / `kHudPagedPageTitles`** と一致（1/7 … 7/7）。

**関連（実装の依存関係）**: ページ式とレガシー縦積み・T37 との **コード上の分界**は `docs/HUD_LEGACY_CODE_DEPENDENCY.md`。  
**関連（維持・削減の優先順位）**: `docs/HUD_LEGACY_MAINTENANCE_PRIORITIES.md`。

---

## 実確認ログ（リポジトリ作業セッション）

| 日付 | 実施内容 | 結果 |
|------|----------|------|
| 2026-04-06 | **MSBuild** `MainApp.vcxproj` **Debug \| x64** | 成功（警告のみ、既存） |
| 2026-04-06 | **MainApp.exe** 起動スモーク（最小化、約 3 秒後に終了） | クラッシュなし（プロセス生存を確認後終了） |
| **2026-04-06** | **T18 同一セッション抜き差し（一部）**（Debug\|x64・**同一プロセス**・キーで T18） | **接続中**の **HUD** で `HID=yes`・**family** / **parser** / **support** / **product** / **why** を目視（本機: **XInput**・VID **0x0F0D** / PID **0x006D**）。**USB 抜き差し**および **管理者 `Disable-PnpDevice` は未実施**のため、**非接続→再接続**の **HUD 遷移**は**未記録**（手順は **T18** 節に追記） |
| **2026-04-09** | **ローカル目視**（Debug\|x64、`MainApp.exe` 前面、`←` `→` でページ送り、各ページキャプチャ確認） | T14・T15・T16・T17・T18・T20 のタイトル帯・本文が期待どおり表示。接続デバイス: **DS4（HID）1 台**（T18）。クライアント約 **645×498** で右端が一部切れる箇所あり |
| **2026-04-09** | **T17 追加**（T17 ページで **F6** 候補循環・**Enter** 適用、画面キャプチャ／コード照合） | **Windowed→Borderless** 適用後に `cand=act=Borderless` と本文を目視確認。他遷移は下記 **T17 実確認サマリ** 参照 |
| **2026-04-09** | **T15 / T16**（T15 で **↑↓** プリセット、`→` で T16、キャプチャ目視） | T15 の `[n/8]`・desired/nearest/delta/exact が **↑↓** で変化。T16 は **T14 選択由来**の target を表示（T15 プリセットと同一数字にならないのは**実装どおり**） |
| **2026-04-09** | **T18 / T20**（ページ送りで T18・T20、キャプチャ目視） | T18: DS4 接続時に family/parser/support/product/why が読める。T20: HUD のマクロ行が **Debug\|x64** と一致（`BUILDINFO` と同一関数由来） |
| **2026-04-10** | **残余受け入れ**（Debug\|x64・**Release\|x64** ビルド、キー操作） | **T14** `↑↓` 選択追従。**T17** Fullscreen 適用後 `cand=act=Fullscreen`。**T20** `config=Release`。**T18** 抜き差し・**T17** マルチモニタは**未実施** |
| **2026-04-15** | **T19/T20** 手動確認（**Debug\|x64**・同一セッション） | T19 **5/7** 到達・表示 **OK**。T20 **7/7** 到達・**`config=Debug`** **platform=x64** **pagedHUD=on** 整合 **OK**。**`[BUILDINFO]`** と矛盾なし |

**限界**: **マルチモニタ・プライマリ切替・非接続コントローラ**などは、引き続きローカルで必要に応じて実施する。

### ローカル目視の推奨順（次回）

**T14 ↑↓**・**T17 Fullscreen（HUD）**・**T20 Release\|x64** は **2026-04-10** まで実施済み。残りは下記。

| 順 | ページ / 内容 |
|----|----------------|
| 1 | T17: **マルチモニタ**・**プライマリ切替**・**Fullscreen→Windowed 復帰**の cand/act を**手動で**静止画確認（自動キャプチャは遷移途中を含み得る） |
| 2 | T15 / T16: **全 DPI・全モニタ**、T16 の **T15 nearest フォールバック**経路 |
| 3 | T18: **同一セッション USB 抜き差し**（**HUD** で接続→非接続→再接続）。**2026-04-06** 時点で **接続中 HUD** のみ実機確認済み（下記 **T18**）。**XInput 主体**・**ブリッジ経路変更**・複数デバイス |
| 4 | T20: **ARM64** 等レア構成 |
| 5 | 全ページ共通: **GDI/D2D 二重描画**の再確認（必要時） |

（T19 は受け入れ候補版のため、必要に応じて入力網羅を追加。）

---

## 記号

| 記号 | 意味 |
|------|------|
| [ ] | 未確認 / 要再確認 |
| [x] | 確認済み（手動または記録済み） |
| (code) | 実装・表示ロジックはリポジトリ内で妥当 |

| 区分 | 意味 |
|------|------|
| **確認済み** | 実装・表示・または手動で一度以上問題なしと記録したもの |
| **条件付き確認済み** | コード上・開発時の限定的な確認まで。本番相当の網羅は未了 |
| **未確認** | 未実施または未記録 |

---

## T14 — Display mode list

**何を見るページか**  
選択モニタの表示モード一覧と、キーボードでページ内スクロール・選択ができるか。

**最低限の確認項目**

- [x] タイトル帯に `T14 — Display mode list` が出る
- [x] モード行が読め、↑↓でスクロール・選択が追従する（T14 専用ロジック）
- [x] ページ切替（エッジ）で他ページへ移れる

**確認済み**  
- (code) `kHudPagedPageTitles[0]` と `Win32_HudPaged_FillT14PageBody` により本文が生成。既定ページとして T14 が選択可能。  
- **2026-04-06**: Debug\|x64 ビルド・`MainApp.exe` 起動スモーク成功。  
- **2026-04-09**: 目視でタイトル・`modes:` 行（複数解像度・選択 `>`）・`←→ page / ↑↓ select` を確認。`menuOpen` なし状態で **←→ によるページ送り**を実施。  
- **2026-04-10**: **↑↓** により選択 `>` が `[0]`→（↓×4）`[4]`→（↑×1）`[3]` に移ることを目視（`menuOpen` なし）。

**条件付き確認済み**  
- （空）

**未確認**  
- マルチモニタ・プライマリ変更後の一覧、**一覧が空**・列挙失敗などの**異常系**の目視。

---

## T15 — Nearest resolution vs desired

**何を見るページか**  
希望解像度に対する最近似モード（列挙キャッシュ）の説明。

**最低限の確認項目**

- [x] タイトル `T15 — Nearest resolution vs desired`
- [x] 本文に nearest / delta 等の要約が出る（`Win32_HudPaged_FillT15PageBody`）
- [x] Clamp 行数で本文が切れすぎない（現状 5 行・76 文字）
- [x] **↑↓** でプリセットが変わり、`desired` / `nearest` / `delta` / `exact` が追従する（`menuOpen` なし・T15 ページ）

**確認済み**  
- (code) T15 本文生成・Clamp パラメータが `MainApp.cpp` のページ分岐に存在。  
- **2026-04-09**: 目視で `desired` / `nearest` / `delta` / `exact` が表示されることを確認（例: 1280×720 一致時 `delta: 0 / 0`、`exact: 1`）。右端で **hz=** などが欠ける場合あり（狭いクライアント幅）。  
- **2026-04-09**: **↑↓** により `[n/8]` と `desired:` が変化することを確認（例: 初期 `[0/8] 1280x720` → ↓×3 で `[3/8] 1920x1080` → ↑×2 で `[1/8] 1366x768`）。各段階で `nearest` / `delta` / `exact` が更新される。

**T15 と T16 の関係（受け入れ上の期待）**  
- T15 は **希望プリセット → 列挙キャッシュ上の nearest** のみ（`Win32_T15_ApplyDesiredPresetAndRecompute`）。  
- T16 の **mode source が `T14 selected`** のとき、target は **T14 一覧の選択行**由来（T15 のプリセットと**別軸**）。そのため **T15 を動かしても T16 の `target mode (resolved)` が同じ数字になるとは限らない** — **矛盾ではなく役割分担**。横断の「nearest」は T14 行の `*`（T15 nearest との対応）で見る。

**条件付き確認済み**  
- 解像度ポリシーと HUD の横断レビュー記録は**未作成**（変更時は `docs/t35_display_mode_policy.md` と併読推奨）。

**未確認**  
- **全 DPI・全モニタ**でのプリセット追従。T14 の `*` と T15 `nearest` の対応を**目視で網羅**（必要時）。

---

## T16 — Window metrics

**何を見るページか**  
クライアントサイズ・再作成回数などウィンドウメトリクス。

**最低限の確認項目**

- [x] T16 セクション本文が表示される（`Win32_T16_AppendPaintSection`）
- [x] Clamp（8 行・76 文字）で過剰行が抑えられる
- [x] `mode source`・`target mode (resolved)`・クライアント行が**読める**（**Windowed**・通常クライアント高）

**確認済み**  
- (code) ページ index 2 で `Win32_T16_AppendPaintSection` が呼ばれ、Clamp 済み。  
- **2026-04-09**: 目視で `--- T16 window (windowed) ---`、`mode source: T14 selected`、クライアント／ターゲット解像度行が読めることを確認（一部行は右端で切れる場合あり）。  
- **2026-04-09**: T15 でプリセットを変えた**直後**に T16 を開き、`mode source: T14 selected`・`T14 list index`・`selected mode (T14 list)` と **T15 の desired が一致しない**ことを確認（**上記 T15 節の通り、別軸**）。

**条件付き確認済み**  
- リサイズ時の数値更新は実装上想定（**全遷移の手動ログは未作成**）。

**未確認**  
- **フルスクリーン／ボーダーレス**時の T16 短縮表示（`compactT64` / `compactT59` 等）と **DPI 極端値**。`mode source` が **T15 nearest フォールバック**になる経路の目視（該当時）。

---

## T17 — Presentation

**何を見るページか**  
windowed / borderless / fullscreen 等のプレゼンテーション状態と cand/act 相当の表示。

**最低限の確認項目**

- [x] T17 本文が表示され、**主要**プレゼンテーション遷移でモード切替と整合（**Windowed→Borderless** を目視）
- [x] **Fullscreen** 適用後、HUD 上で **cand=act=Fullscreen** を目視（**2026-04-10**、Debug\|x64・F6 循環＋Enter）
- [x] Clamp（8 行・76 文字）で要約が収まる（**Windowed / Borderless** で目視）

**確認済み**  
- (code) `Win32_T17_AppendPaintSection` がページ index 5 で呼ばれ、ステータス行と cand/act ラベルが GDI パスと整合する構成。  
- **2026-04-09**: **Windowed** で `--- T17 presentation ---`、F6/Enter の説明行、`cand=Windowed act=Windowed` が一致していることを目視確認。  
- **2026-04-09**: **Windowed→Borderless** 適用後、上帯 `cand=Borderless act=Borderless`、本文 `last key affecting T17: Enter (apply)`、`cycle seq` / `apply seq` 更新を目視確認。  
- **(code)** `Win32_T17_ShouldSkipNoopApply` + `Win32_T17_ApplyCurrentPresentationMode`: **候補と最終適用が同じ**で実際に再適用が不要なとき **APPLY SKIP no-op**（`OutputDebugStringW`）。`candidate==lastApplied` でも **CDS が掛かったままの Fullscreen から Windowed/Borderless に戻す**場合は no-op にしない。

**T17 実確認サマリ（2026-04-09）** — 各ケースの cand/act・本文・apply / skip

| ケース | cand / act（期待） | 本文・ログ | apply / skip |
|--------|-------------------|------------|--------------|
| **Windowed→Windowed**（同一モードで Enter） | 変更なしで一致のまま | **(code)** `ShouldSkipNoopApply` true なら skip。skip 経路では `InvalidateRect` が呼ばれないため、**HUD の数値が即座に更新されない**ことがある（次の再描画まで古い `apply seq`）。**noop の検証は** デバッグ出力 `[T17] APPLY SKIP no-op` または **次のキー操作後の再描画**が確実。 | **skip**（重い再生成なし） |
| **Windowed→Borderless** | 適用後 **cand=act=Borderless** | 目視で一致、`last key... Enter (apply)` | **apply**（再作成） |
| **Borderless→Windowed** | F6 を 2 回で候補 Windowed、Enter で **cand=act=Windowed** | **操作は実施**。キャプチャタイミングによっては **Enter 前** に `cand=Windowed` / `act=Borderless` の瞬間が写る。適用完了の静止画は**手元で再確認推奨**。 | **apply**（CDS 状況に応じて desktop reset 含む） |
| **Windowed→Fullscreen** | Enter 後 **Fullscreen** | **2026-04-10** 目視: 適用直後 **cand=Fullscreen act=Fullscreen**。 | **apply**（CDS + recreate、失敗時は Borderless フォールバック） |
| **Fullscreen→Windowed** | Enter 後 **Windowed** | F6 で候補 Windowed → Enter は**実施**。自動キャプチャでは **cand=Windowed / act=Fullscreen** の**途中**を捉えることがある。**cand=act=Windowed** の静止画は**手動推奨**。 | **apply**（CDS_RESET 等の経路あり） |
| **同一モードで再 Enter**（例: Borderless 済みで再 Enter） | **(code)** Borderless かつ `GetWindowRect` がモニタ矩形と一致なら **skip** | 上記 no-op と同様の HUD 更新の注意 | **skip** |

**条件付き確認済み**  
- **Fullscreen→Windowed** の**完了直後**（`cand=act=Windowed`）の静止画は、**手動**で確実（自動キャプチャは遷移途中を含み得る）。  
- **monitor 差し替え・プライマリ変更**は未実施。

**未確認**  
- **マルチモニタ**・**プライマリ切替**後の T17 表示・適用。  
- **DWM / GPU** 組み合わせの網羅回帰（必要時のみ）。

---

## T18 — Controller identify (HID / XInput)

**何を見るページか**  
スロット、HID、VID/PID、推定 family、パーサ、製品名、T18 の why 要約。

**最低限の確認項目**

- [x] 接続コントローラの識別情報が論理と一致（**DS4（HID）接続時**に目視）
- [x] **同一セッション起動**のまま T18 で **接続中**に **family** / **parser** / **support** / **product** / **why** を **HUD** で読める（**2026-04-06**: **XInput**・HORI 系 VID/PID **0x0F0D/0x006D**。**DS4** は **2026-04-09** 済み）
- [ ] **同一セッション**で **USB 抜き差し（または管理者 `Disable-PnpDevice` / `Enable-PnpDevice`）**のあと、**非接続表示→再接続表示**が **HUD** で追従する
- [ ] PS4 ブリッジ等の経路変更後もタイトル・本文が更新される

**同一セッション USB 抜き差し — ローカル受け入れ手順（推奨）**  
1. **Debug\|x64** の `MainApp.exe` を起動し、**T18（4/7）** を表示する（`→` で T14→…→T18）。  
2. **接続中**: 本文の `HID=`・`family=`・`parser`/`support`・`product`・`why:` を **HUD** で確認する。  
3. **切断**: デバイスの **USB を抜く**、または **管理者** PowerShell で当該コントローラーの **InstanceId** に対し `Disable-PnpDevice -Confirm:$false` を実行する。  
4. **T18 のまま**、表示が更新されるまで待つ（変化しない場合は **キー1つ**や **マウス移動**で **WM_PAINT** を発生させる）。**HUD** で **`HID=no`**（およびスロット・VID/PID が期待どおり）になることを確認する。  
5. **再接続**: USB を挿す、または `Enable-PnpDevice -Confirm:$false`。再度 **接続中** と同様の行が戻ることを **HUD** で確認する。  
6. （任意）**Sysinternals DebugView** などで **`[T18]`** 行（`hid_found`・`rationale`・`device_path(full)`）がスナップショット変化に合わせて出ることを確認する。起動直後の Raw HID 列挙ログは **`Win32InputGlue_LogRawInputHidGameControllersClassified`** 経路；**抜き差し瞬間**は **`[T18]` 差分ログ**を主とし、**HIDgen** は T18 主系外の汎用 HID 用。

**確認済み**  
- (code) `Win32_HudPaged_FillT18PageBody` および T18 状態の参照がページ描画に接続。タイマー・WM_PAINT で更新される設計。  
- **2026-04-09**: **DS4（VID/PID 0x054C/0x05C4）** 接続下で `family=PlayStation`、`parser=Ds4KnownHid`、製品名・why 行が表示されることを目視確認（XInput slot=-1 の状態）。  
- **2026-04-09（再確認）**: HUD で **family** / **parser** / **support**（行末は右端で切れる場合あり）/ **product** / **why**（`DS4 verified table` / `known HID report map`）が **読める**ことを目視確認。文書の **DS4 既定表示**（verified + known HID map）と一致。  
- **2026-04-06**: **同一プロセス**・**同一セッション**で T18 を表示。**本機**の **XInput 接続コントローラー（VID 0x0F0D / PID 0x006D）** について **HUD** のみで **接続中**の識別行（`HID=yes`・`XInput slot=0`・**family** / **parser** / **support** / **product** / **why**）を確認（**`[T18]` デバッグ行・HID 抜き差し瞬間ログは未採取**）。

**条件付き確認済み**  
- **同一セッション内の 接続→（抜き差し）→非接続→再接続**について: **2026-04-06** の自動検証環境では **管理者権限がなく** `Disable-PnpDevice` を実行できず、**物理 USB 抜き差し**も未実施。**HUD** および **`[T18]`** による **非接続→再接続**の**連続記録**は**ない**（上記手順はローカル完了用）。

**未確認**  
- **同一セッション**での **非接続 HUD**（`HID=no` 等）および **再接続後**の **接続 HUD** の**目視**（**2026-04-06 未記録**）。**DS4 USB** 専用の抜き差しは**未実施**（今回の実機は **XInput HORI**）。  
- **複数デバイス**の網羅目視。  
- **PS4 ブリッジ経路変更直後**のタイトル・本文更新（**[ ]** の項目は継続）。

---

## T19 — Input state (logical / pad L+R / analog)

**何を見るページか**  
共通 logical（8 行）、pad L+R（圧縮 2 列）、analog（3 行）を **標準ウィンドウサイズで同時に**読む。

**最低限の確認項目**

- [ ] **keyboard**: common logical（South … DPad）がキー入力と一致
- [ ] **PS4**: common logical + pad L+R（L1 … Sq/Tri）が入力と一致
- [ ] **XInput**: 同上
- [ ] L2/R2 の **r** は表示用安定化バイト（タイマー差分は analog と同量子化）
- [ ] **analog**: LS/RS/LT/RT の値・バーが一致し、**本文下端で切れない**
- [ ] **WM_TIMER**: 論理+pad 変化は即時再描画、analog のみの変化は `kT19AnalogThrottleMs` で間引き（`Win32_WndProc_OnXInputPollTimer`）

**確認済み**  
- (code) logical / pad L+R / analog の本文生成、論理+pad 即時 invalidation、analog のみ間引き、Clamp（40 行・96 文字）、標準サイズ向け**縦圧縮レイアウト**が `MainApp.cpp` に実装済み。  
- **表示目標**: 標準 HUD サイズで **3 ブロック同時表示**まで調整済み（受け入れ候補版）。  
- **2026-04-06**: 同一ビルド・起動スモーク成功（**入力デバイスを伴う目視は本セッション未実施**）。
- **2026-04-13**: **Debug\|x64**、**5/7** の T19 で目視。**OK（注記あり）**。keyboard は **Backspace / Tab / Up / Down** で common logical の応答を確認（**Enter** は **T17 apply** と競合、**Left / Right** はページ送りと競合のため、今回の最小確認では主対象外）。**analog** は **LS** の値変化を確認。

**条件付き確認済み**  
- PS4 ブリッジ経路・pad L+R の開発時確認、keyboard/XInput の論理合成経路（**網羅テストは未記録**）。

**未確認**  
- 極端に狭いクライアント幅、新デバイス種別、長時間プレイでの表示ずれ。

**詳細（実装）** — `MainApp.cpp` の `Win32_HudPaged_FillT19*` / T19 タイマー分岐。

---

## T20 — Build / Debug flags

**何を見るページか**  
ビルド種別・プラットフォーム・診断マクロのマニフェスト（起動時 `[BUILDINFO]` と整合）。

**最低限の確認項目**

- [x] タイトル `T20 — Build / Debug flags`
- [x] 本文が `[BUILDINFO]` と同内容のマクロ一覧（**Debug\|x64** で起動中 HUD 本文として目視）
- [x] Release/Debug 切替で表示が変わる（**Release\|x64** で `config=Release` を目視）

**確認済み**  
- (code) `Win32_FormatBuildDebugManifest` が **T20 本文**と **`Win32_EmitBuildInfoLogOnce`（`[BUILDINFO]`）** の**両方**で呼ばれ、**同一のマニフェスト文字列**が生成される（`MainApp.cpp`）。  
- **2026-04-09**: **Debug\|x64** で HUD 上に `config=Debug`、`platform=x64`、`pagedHUD=on`（表記は行幅で切れる場合あり）、`WIN32_HUD_USE_PAGED_HUD=1`、列挙 PS4/論理/T18 系マクロが **0**、`__DATE__` / `__TIME__` 行を目視。**BUILDINFO 対応**は **(code) 同一関数** + **本HUD の行が起動時ログと一致する前提**で受け入れ（同一バイナリでは本文の差は出ない）。  
- **2026-04-10**: **Release\|x64** をビルドし、T20 で **`config=Release`**・`platform=x64`・同一マクロ列を目視（`__DATE__` / `__TIME__` はそのビルドの値）。  
- **2026-04-13**: **Debug\|x64**、**7/7** の T20。**OK**。HUD の `config=Debug` / `platform=x64` / `pagedHUD=on` 等が **`[BUILDINFO]`** と整合することを確認。

**条件付き確認済み**  
- （空）

**未確認**  
- **ARM64** 等レア構成での表示。新マクロ追加時の一覧更新漏れ。

---

## 全ページ共通

- [x] ページ切替（左右エッジまたは既定ショートカット）で **7 ページ**が循環する（**2026-04-09**: `←` `→` で T14↔T15↔…↔T20 を実施）
- [ ] GDI ページ式 HUD 有効時、D2D 側で同じ本文を二重描画していない
- [x] 標準参照解像度（640×480 論理相当）でタイトル・メニュー帯・本文が読める（**2026-04-09**: 約 **645×498** クライアントで概ね可読。一部タイトル・本文が右端で欠ける）

**確認済み**  
- (code) `kHudPagedCount == 7`、`Win32_HudPaged_AdvancePage`、GDI 単一路線（`WindowsRenderer.cpp` / `Win32DebugOverlay` 側の二重防止コメント）。  
- **2026-04-09**: 前面ウィンドウでページ送りし、優先順ページ（T17・T15・T16・T18・T20・T14）の表示を確認。

**条件付き確認済み**  
- （空）

**未確認**  
- GDI/D2D/D3D × 全ページの**マトリクス回帰**（必要時のみ）。

---

## 更新履歴

| 日付 | 内容 |
|------|------|
| 2026-04-06 | 初版（ページ式 HUD 受け入れパック統合、T19 詳細は本書へ集約） |
| 2026-04-06 | 初回棚卸し: 各ページに 確認済み / 条件付き確認済み / 未確認 を追記 |
| 2026-04-06 | **実確認**: MSBuild Debug\|x64・MainApp.exe 起動スモークを記録。条件付きを整理し未確認を絞り込み。ローカル目視の推奨順を追加 |
| **2026-04-09** | **ローカル目視**: T14–T20（優先順）のタイトル・本文・ページ送りを実施。HUD 受け入れチェックを更新。一時キャプチャは `docs/_hud_smoke_*/`（gitignore） |
| **2026-04-09** | **T17 深掘り**: F6/Enter で主要遷移を実施し **W→Borderless** を目視確認。**noop skip**・Fullscreen 系はコード照合と受け入れ注意を `T17 実確認サマリ` に反映 |
| **2026-04-09** | **T15/T16**: T15 の **↑↓** 追従と T16 本文を実確認。T15 プリセットと T16 target の**軸の違い**を文書化 |
| **2026-04-09** | **T18/T20**: T18 の DS4 行を再目視。T20 と **`[BUILDINFO]`** の **同一関数**由来を明記（**Release\|x64** は **2026-04-10** に追記） |
| **2026-04-10** | **残余**: T14 **↑↓**、T17 **Fullscreen** HUD、T20 **Release\|x64**。T18 抜き差し・T17 マルチモニタは未実施 |
| **2026-04-13** | **T19/T20** 手動確認（**Debug\|x64**）: T19 **OK**（keyboard は Backspace/Tab/Up/Down 主、Enter は T17 apply・Left/Right はページ送りと競合の注記、analog **LS** 確認）、T20 **OK**（HUD と **`[BUILDINFO]`** 整合） |
