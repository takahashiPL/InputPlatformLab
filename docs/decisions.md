# 設計判断の記録（decisions）

本ファイルは、**他タイトルへの流用・移植・長期保守**のときに迷いを減らすため、**再利用や境界に効く設計上の決定**だけを残す場所です。日々の実装メモや作業ログはここには書きません。

## 何を書くか

次のようなテーマで、「なぜそうしたか」と「どこまで効くか」を短く残します。

- **責務分離**（モジュール間で何を知らないか）
- **プラットフォーム境界**（OS 固有と中立層の切り分け）
- **描画方式**（API や合成の方針）
- **入力の共通境界**（論理入力・デバイス固有の変換の置き場所）

※ PC 上のコントローラ入力と、コンソール本体向けのプラットフォーム対応は別問題として、境界の決め方だけ記録の対象にできます。

## 記録項目テンプレート

各エントリは次の項目を埋める想定です（複数行可）。

| 項目 | 内容 |
|------|------|
| **判断** | 採用した方針（一文でも可） |
| **理由** | 代替案と比較して決め手になった点 |
| **影響範囲** | 触るファイル・レイヤー・ビルド切り替えの有無など |

---

| **判断** | **T34** を Borderless の **正式レンダリング経路**とする（T14 committed のオフスクリーン → 全面バックバッファへ合成 → Present）。**T35** で Windowed / Borderless / Fullscreen ごとの window·client / swapchain / offscreen / present / GDI を文書上固定する。 |
| **理由** | 実機で committed 640×480 / 4096×2160 の create/draw/composite を確認済み。T17 のウィンドウログ（targetPhys 等）とレンダラ解像度は **別軸**のまま説明可能。 |
| **影響範囲** | `WindowsRenderer.cpp`（T34）、`MainApp.cpp`（T17・フラグ）、`docs/t35_display_mode_policy.md` / `t34_t35_display_and_render.md`。Windowed・Fullscreen はオフスクリーン不使用を方針として明記。 |

---

（以下にエントリを追記してください。）

---

| **判断** | `MainApp.cpp` の **variable-like / fixed-like / render** の区分は、まず **docs 上の読み方として固定**し、**実装分離完了とは言わない**。 |
| **理由** | `WM_INPUT` / `WM_TIMER` / `WM_PAINT` は現況で主経路として読めるが、`WndProc` 分散・`InvalidateRect` 条件・accepted 意味を崩さずにコード側完成まで進めるには、別テーマの複数手が必要。 |
| **影響範囲** | `docs/ENGINE_LOOP_MAPPING_UNITY_UNREAL_MAINAPP.md`、`docs/WNDPROC_MESSAGE_RESPONSIBILITY_MAP.md`、`docs/NEXT_THEME_RESTART_ENTRY.md`、`docs/roadmap.md`。必要なら本表を追記対象にする。コード変更なし。 |

---

| **判断** | **T77 次段**は、まず **条件整理のみ**を行い、**単一 pad 環境では `2P=XInput0` を normal live に上げない**。**複数 pad かつ別 identity が成立する場合だけ**、将来の再検討候補とする。 |
| **理由** | 直近の Go(2) 最小ケースでは、**2P=Keyboard live**、**2P=XInput0 dry-run 維持**、**absent / recovery 後の 2P Keyboard live 維持**まで受け入れ済み。ここで non-kb live を一般化すると、**1P owner / guide**、**2P=Keyboard live**、**cross-player family change**、**trial/debug semantics** を壊す危険があるため、先に **条件だけ**を固定する。 |
| **影響範囲** | `docs/T77_FOUNDATION_CLOSE.md`、`docs/NEXT_THEME_RESTART_ENTRY.md`、`docs/roadmap.md`。**コード変更なし**。`2P=XInput0` live 実装、non-kb live 一般化、3P/4P 展開、route / consume / staged / live の再設計は **次段以降**。 |
---

| **判断** | **T77 次段**で必要な「複数 pad」判定の source of truth に、現状の `boundDeviceIdentity` を使わない。**physical-unique な device key は inventory 側で持つ**ことを前提条件とする。 |
| **理由** | `boundDeviceIdentity` は **slot が何に lock したか**を表す宣言であり、`XInputUser` / `HidPathHash` など **別キー空間**を跨いで同一 physical pad を統合できない。`absent / recovery` や XInput/HID 二重顔で **二重カウント**の危険があるため、**単一 pad では `2P=XInput0` を normal live にしない**線を安全に守れない。 |
| **影響範囲** | `docs/T77_FOUNDATION_CLOSE.md`、`docs/NEXT_THEME_RESTART_ENTRY.md`、`docs/roadmap.md`。将来の inventory 形状（physical-unique key / 複数デバイス表現）の前提整理。**コード変更なし**。`2P=XInput0` effective-normal-live は **未着手維持**。 |

