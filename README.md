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
`MainApp.cpp` の `Win32_FillVirtualInputFromDs4StyleHidReport` 参照。概要:
- byte5: DPad hat（下位ニブル）, Share `0x10`, Circle `0x20`, Options `0x40`, R3 仮説 `0x80`（観測補強中・verified 本文へ未昇格）
- byte6: L1 仮説 `0x01`, R1 `0x02`, L2 `0x04`, R2 `0x08`, Square `0x10`, Cross `0x20`, L3 `0x40`, Triangle `0x80`
- byte7: PS `0x01`
- byte8/9: L2/R2 アナログ
- 調査用 WM_INPUT 冗長ログは `kPs4HidVerboseRawLog`（既定 `false`）。slot=99 は `[PS4VIchg]`（肩・StSel・PS 等）に加え `[PS4Bridge]`（L1R1/L2R2/L3R3/PS + raw b5/b6/b8/b9 要約）、単発確認は `[PS4DS4ISO]`（DPad hat 中立時の L1/R3/L3/Tri 等）。

## 入力レイヤーと検証度（MainApp.cpp）
アプリは次の 3 経路に分かれる（T18 の `parser` / `support` に反映）。
1. **XInput** — `XInputGetState` 系。`ControllerParserKind::XInput` + **verified**（Microsoft 公式 API 経路）。
2. **Known Raw HID（DS4）** — VID/PID が `kControllerHidProductTable` の DS4 行に一致するときのみ `Win32_FillVirtualInputFromDs4StyleHidReport` で橋渡し。**verified**。
3. **Generic HID fallback** — 上記以外のゲームパッド HID。`[HIDgen]` の要約ログ（500ms スロットル）のみ。ビットマップは未固定のため **tentative**。

**verified** … 実機でマップまたは API 契約を固定したもの（現状は XInput と DS4 USB/BT のテーブル行）。  
**tentative** … VID/PID や名称の受け皿のみ。PS5 / Nintendo / Microsoft HID ワイルドカード等は今後テーブル行を足して昇格させる。
