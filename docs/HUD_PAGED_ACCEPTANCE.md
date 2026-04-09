# ページ式 HUD — 受け入れ確認パック

前提: **縦積み HUD の通常運用には戻らない**。GDI で `Win32_HudPaged_PaintGdi` が本文を描画し、D2D/D3D 本体描画と二重にならない構成（`Win32HudPaged.h` 方針）。

ページ順は **コード上の `s_hudPagedIndex` / `kHudPagedPageTitles`** と一致（1/7 … 7/7）。

---

## 実確認ログ（リポジトリ作業セッション）

| 日付 | 実施内容 | 結果 |
|------|----------|------|
| 2026-04-06 | **MSBuild** `MainApp.vcxproj` **Debug \| x64** | 成功（警告のみ、既存） |
| 2026-04-06 | **MainApp.exe** 起動スモーク（最小化、約 3 秒後に終了） | クラッシュなし（プロセス生存を確認後終了） |
| **2026-04-09** | **ローカル目視**（Debug\|x64、`MainApp.exe` 前面、`←` `→` でページ送り、各ページキャプチャ確認） | T14・T15・T16・T17・T18・T20 のタイトル帯・本文が期待どおり表示。接続デバイス: **DS4（HID）1 台**（T18）。クライアント約 **645×498** で右端が一部切れる箇所あり |
| **2026-04-09** | **T17 追加**（T17 ページで **F6** 候補循環・**Enter** 適用、画面キャプチャ／コード照合） | **Windowed→Borderless** 適用後に `cand=act=Borderless` と本文を目視確認。他遷移は下記 **T17 実確認サマリ** 参照 |

**限界**: **マルチモニタ・プライマリ切替・非接続コントローラ**などは、引き続きローカルで必要に応じて実施する。

### ローカル目視の推奨順（次回）

優先順（T17→T15→…）の **T17 は主要ケースまで実施済み**。残りは下記。

| 順 | ページ / 内容 |
|----|----------------|
| 1 | T14: **↑↓** でのモード一覧スクロール・選択追従の手動確認 |
| 2 | T17: **Fullscreen 系**の静止画で cand/act を最終確認（独占表示時は前面ウィンドウ／矩形に注意）。**monitor 差し替え** |
| 3 | T18: 非接続・再接続・複数デバイス・XInput 主体 |
| 4 | T20: **Release** ビルドで本文が `config=Release` 等に変わること |
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
- [ ] モード行が読め、↑↓でスクロール・選択が追従する（T14 専用ロジック）
- [x] ページ切替（エッジ）で他ページへ移れる

**確認済み**  
- (code) `kHudPagedPageTitles[0]` と `Win32_HudPaged_FillT14PageBody` により本文が生成。既定ページとして T14 が選択可能。  
- **2026-04-06**: Debug\|x64 ビルド・`MainApp.exe` 起動スモーク成功。  
- **2026-04-09**: 目視でタイトル・`modes:` 行（複数解像度・選択 `>`）・`←→ page / ↑↓ select` を確認。`menuOpen` なし状態で **←→ によるページ送り**を実施。

**条件付き確認済み**  
- （空）

**未確認**  
- **↑↓** によるスクロール・選択追従の手動確認。マルチモニタ・プライマリ変更後の一覧、空リスト・異常系の目視。

---

## T15 — Nearest resolution vs desired

**何を見るページか**  
希望解像度に対する最近似モード（列挙キャッシュ）の説明。

**最低限の確認項目**

- [x] タイトル `T15 — Nearest resolution vs desired`
- [x] 本文に nearest / delta 等の要約が出る（`Win32_HudPaged_FillT15PageBody`）
- [x] Clamp 行数で本文が切れすぎない（現状 5 行・76 文字）

**確認済み**  
- (code) T15 本文生成・Clamp パラメータが `MainApp.cpp` のページ分岐に存在。  
- **2026-04-09**: 目視で `desired` / `nearest` / `delta` / `exact` が表示されることを確認（例: 1280×720 一致時 `delta: 0 / 0`、`exact: 1`）。右端で **hz=** などが欠ける場合あり（狭いクライアント幅）。

**条件付き確認済み**  
- 解像度ポリシーと HUD の横断レビュー記録は**未作成**（変更時は `docs/t35_display_mode_policy.md` と併読推奨）。

**未確認**  
- 希望解像度 **↑↓ プリセット切替**直後の再描画・数値の目視（全 DPI・全モニタ）。

---

## T16 — Window metrics

**何を見るページか**  
クライアントサイズ・再作成回数などウィンドウメトリクス。

**最低限の確認項目**

- [x] T16 セクション本文が表示される（`Win32_T16_AppendPaintSection`）
- [x] Clamp（8 行・76 文字）で過剰行が抑えられる

**確認済み**  
- (code) ページ index 2 で `Win32_T16_AppendPaintSection` が呼ばれ、Clamp 済み。  
- **2026-04-09**: 目視で `--- T16 window (windowed) ---`、`mode source: T14 selected`、クライアント／ターゲット解像度行が読めることを確認（一部行は右端で切れる場合あり）。

**条件付き確認済み**  
- リサイズ時の数値更新は実装上想定（**全遷移の手動ログは未作成**）。

**未確認**  
- フルスクリーン／ボーダーレス境界、DPI スケール極端値での表示の目視。

---

## T17 — Presentation

**何を見るページか**  
windowed / borderless / fullscreen 等のプレゼンテーション状態と cand/act 相当の表示。

**最低限の確認項目**

- [x] T17 本文が表示され、**主要**プレゼンテーション遷移でモード切替と整合（**Windowed→Borderless** を目視）
- [ ] **Fullscreen** の適用・復帰を **静止画で** cand/act まで最終確認（自動キャプチャでは前面ウィンドウがずれることがある）
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
| **Windowed→Fullscreen** | Enter 後 **Fullscreen** | **自動スクリーンキャプチャ**では独占表示やフォーカス移動で **別ウィンドウの矩形**が撮れることがある。HUD での cand/act まで**未の静止画として残す**。 | **apply**（CDS + recreate、失敗時は Borderless フォールバック） |
| **Fullscreen→Windowed** | Enter 後 **Windowed** | 上記と同様、**フルスクリーン復帰時の目視は手動で最終確認推奨**。 | **apply**（CDS_RESET 等の経路あり） |
| **同一モードで再 Enter**（例: Borderless 済みで再 Enter） | **(code)** Borderless かつ `GetWindowRect` がモニタ矩形と一致なら **skip** | 上記 no-op と同様の HUD 更新の注意 | **skip** |

**条件付き確認済み**  
- **Fullscreen** 適用・復帰の **HUD 上の cand/act** は、**手動で前面に `InputPlatformLab` を置いて**確認するのが確実（自動化のキャプチャ限界）。  
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
- [ ] PS4 ブリッジ等の経路変更後もタイトル・本文が更新される

**確認済み**  
- (code) `Win32_HudPaged_FillT18PageBody` および T18 状態の参照がページ描画に接続。タイマー・WM_PAINT で更新される設計。  
- **2026-04-09**: **DS4（VID/PID 0x054C/0x05C4）** 接続下で `family=PlayStation`、`parser=Ds4KnownHid`、製品名・why 行が表示されることを目視確認（XInput slot=-1 の状態）。

**条件付き確認済み**  
- （空）

**未確認**  
- 非接続・再接続・複数デバイス切替の**網羅**、XInput 主体・全パーサ種別の目視。ブリッジ経路を変えた直後の更新確認。

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
- [ ] Release/Debug 切替で表示が変わる

**確認済み**  
- (code) `Win32_FormatBuildDebugManifest` と T20 本文、起動時 `[BUILDINFO]` の**同一内容**が実装されている。  
- **2026-04-09**: **Debug\|x64** で `config=Debug`、`platform=x64`、`WIN32_HUD_USE_PAGED_HUD=1` および列挙マクロが **0** である行を HUD 上で目視（`__DATE__` / `__TIME__` はビルド時刻に依存）。

**条件付き確認済み**  
- **Release** ビルドでの T20 本文（`config=Release` 等）は**未目視**。ビルド後に 1 回確認推奨。

**未確認**  
- **Release** ビルドでの表示。新マクロ追加時の一覧更新漏れ、ARM64 等レア構成での目視。

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
