# InputPlatformLab

Windowsアプリの入力基盤試作プロジェクト。

## 目的
- キーボード入力の取得
- 各国キーボード差を考慮した表示用キーラベル対応
- ゲームコントローラーの識別
- キーボード / コントローラーの仮想入力統合
- モニター解像度一覧取得と表示モード切替

## 環境
- C++
- Visual Studio 2026
- Windows

ビルド設定の正は `app/InputPlatformLab/MainApp/MainApp.vcxproj` とする。

## 方針
- 1マイルストーン = 1ブランチ = 1検証可能単位
- AIで雛形生成、人間が理解・修正・検証する

## DS4 USB（Sony VID 054C）ボタンマップ（実機確認済み）
`MainApp.cpp` の `Win32_FillVirtualInputFromDs4StyleHidReport` 参照。実測ログと整合する要点:
- **L1** = `byte6 & 0x01`（例: `rawB6=0x01`、`[PS4DS4ISO] L1Only`、`VirtualInput slot=99` の `L1R1=10`）
- **R3** = `byte5 & 0x80`（例: `rawB5=0x88` = DPad hat8 + R3、`[PS4DS4ISO] R3Only`、`L3R3=01`）
- byte5: DPad hat（下位ニブル; 無入力寄りは `8`）、Share `0x10`, Circle `0x20`, Options `0x40`
- byte6: R1 `0x02`, L2 `0x04`, R2 `0x08`, Square `0x10`, Cross `0x20`, L3 `0x40`, Triangle `0x80`
- byte7: PS `0x01`
- byte8/9: L2/R2 アナログ

slot=99（XInput 不在時）: `[PS4VIchg]`（肩・StSel・PS 等）、`[PS4Bridge]`（L1R1/L2R2/L3R3/PS + raw b5/b6/b8/b9）、単発の `[PS4DS4ISO]`（DPad hat 中立時のみ L1/R3/L3/Tri 系）。

調査用 WM_INPUT 冗長ログは `kPs4HidVerboseRawLog`（既定 `false`）。  
T18 の短い調査ログ（いずれも `MainApp.cpp` で既定 `false`。`true` にしたビルドのみ該当行が出る）: `kPs4IsoSkipDebugLog` → `[PS4ISOskip]`、`kPs4IsoProbeDebugLog` → `[PS4ISOprobe]`、`kPs4DPadProbeDebugLog` → `[PS4DPadProbe]`、`kPs4RawComboProbeDebugLog` → `[PS4RawCombo]`、`kPs4BridgeResetDebugLog` → `[PS4BridgeReset]`。

### DPad 混在時の isolate 抑止（`[PS4ISOskip]`）
`[PS4ISOskip]` は **同一フレームの `s_ps4LastReportB5` 下位ニブル（hatLo）が 8 以外**のときにだけ出る（`kPs4IsoSkipDebugLog = true` 時）。**skip 分岐の不具合ではなく**、条件「hat 非中立」のときだけログが出る。

**T18 実測（WM_INPUT 生レポート）:** DPad を押したまま **L1 / △ / L3 / R3** を足したコンボでも、`[PS4RawCombo]` / `[PS4Bridge]` / `[PS4ISOprobe]`（調査ログを有効にしたビルド）では **`b5=08`（hatLo=8）かつ該当ボタンが立つ・`dpad=0000`** になることがある。これは slot=99 の更新順ではなく、**HID レポート側で中立 hat になりうる**ためである（**DPad 単独**では hat と dpad 展開は一致する）。そのため **コンボ時に `[PS4ISOskip]` が出ない**のは **skip 実装不具合ではなく**、**そのフレームの生レポートが中立 hat だから**（skip 条件を満たさない）と見てよい。

**今後の整理案（未実装・本体挙動は現状のまま）**
- **A. Raw 忠実のまま据え置き** — `Win32_FillVirtualInputFromDs4StyleHidReport` は hat をそのまま解釈。コンボ時の hat=8 はデバイス側の報告として文書化のみ。
- **B. 診断専用の effectiveHat carry（既定 off）** — ログや診断パスだけで「直前の非中立 hat」を保持するなど、**本番マッピングは変えない**前提のオプション。

手動実測（`kPs4IsoSkipDebugLog`）: hat 非中立が **同一エッジ**で取れる操作なら `[PS4ISOskip] tag=… hatLo=…` が出る。
- **DPad + L1** → `tag=L1Only`（他も同様）
- **DPad + R3** → `tag=R3Only`
- **DPad + L3** → `tag=L3Only`
- **DPad + Tri（△）** → `tag=TriOnly`

## 入力レイヤーと検証度（MainApp.cpp）
アプリは次の 3 経路に分かれる（T18 の `parser` / `support` に反映）。
1. **XInput** — `XInputGetState` 系。`ControllerParserKind::XInput` + **verified**（Microsoft 公式 API 経路）。
2. **Known Raw HID（DS4）** — VID/PID が `kControllerHidProductTable` の DS4 行に一致するときのみ `Win32_FillVirtualInputFromDs4StyleHidReport` で橋渡し。**verified**。
3. **Generic HID fallback** — 上記以外のゲームパッド HID。`[HIDgen]` の要約ログ（同一 Raw デバイスかつ VID/PID・usage・payload 長が前回と同じ場合は 500ms に 1 行まで）のみ。ビットマップは未固定のため **tentative**（ログ文言は「tentative bucket; no verified map」で、固定マッピングを主張しない）。

**verified** … 実機でマップまたは API 契約を固定したもの（現状は XInput と DS4 USB/BT のテーブル行）。  
**tentative** … VID/PID などの**受け皿**に過ぎない。テーブル行は family の寄せ先の目印であり、実機未確認機種向けにボタンマップを増やしていない。

### T18: コントローラー識別（メイン画面デバッグ表示）
WM_PAINT で **1 台分**を表示: Raw Input 列挙で最初に見つかった **HID ゲームパッド**（`Win32_HidTraitsLookLikeGamepad`）の **vid/pid・product・device path**、および **先頭接続の XInput スロット**。フィールドは **family**（推定）、**parser**、**support**、上記 **vid/pid**、**slot**、**product/path**。`raw HID: not found` のときは Raw 側に該当デバイスが無く **vid/pid は n/a**（XInput のみ接続など）。DS4 以外でも **None / GenericHid / tentative** などが破綻なく並ぶ想定。変更時のみ `[T18] hid_found=…` が OutputDebugString に 1 行出る。
