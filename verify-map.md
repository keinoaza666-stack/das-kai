# DAS Kai verify-map

ビルド手順・検証ポイント・既知の罠・外部依存の正本。コードの約束は `dev-rules.md`。

## ビルド

```bash
cmake -S . -B build/release -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --config Release -j8
cd build/release && ctest
```

- 初回の configure で JUCE・r8brain・OBS ソース・SIMDe を GitHub から取得する(コミットID固定)。取得済みのものを使う時は `-DFETCHCONTENT_SOURCE_DIR_JUCE=<path>` などで指す
- OBS 用プラグインは `/Applications/OBS.app` の libobs.framework にリンクする。OBS が入っていないとビルドできない
- インストール: `scripts/install.sh`(削除 → コピー → 署名し直し、最後に SHA-256 を表示)

## 検証ポイント

| 何を確かめるか | 手段 | 合格の基準 |
|---|---|---|
| 共有メモリ・時計情報・補正値の受け渡し | `ctest -R transport` | 書きかけの時計情報を読まない、上書きされた範囲を読まない |
| 補間の品質 | `ctest -R sync`(先頭) | 誤差 1k/10k/16k/20kHz で -100/-95/-90/-60 dB 未満 |
| 時計の推定 | `ctest -R sync` | +250ppm を ±5ppm で推定、先読みのまとめ処理後も ±10ppm |
| クロックずれの吸収 | `ctest -R sync`(44.1k +250ppm 10分、48k -300ppm 5分) | 時刻の切れ目 0、仕切り直し 0、時刻のぶれ 0.5ms 未満、最初と最後の差 0.2ms 未満、1kHz の段差 1e-4 未満 |
| 手動補正・自動補正の切り替え | `ctest -R sync`(offset changes) | +45ms で 45ms 前に置く、AUTO オフで待ち時間ぶん後ろへ、切れ目は 80ms 以上、切れ目の直後はまとめ送り |
| DAW の停止と再開 | `ctest -R sync`(engine pause) | 再開後に合い方が戻る、待ち時間が初期値に戻る |
| 時計情報の読み取り失敗 | `ctest -R sync`(clock read failure) | 1 回失敗しても切れ目 0・仕切り直し 0 |
| 合計補正の上限 | `ctest -R sync`(offset ceiling) | 手動 1000ms を入れても合計 850ms で止まる |
| 実際の VST3 | `ctest -R vst3_smoke` | 名前・ベンダー、バイパスで無音を送る、2個目は送らない、補正値の公開、受信側で音と時刻が届く |
| VST3 規格 | `pluginval --strictness-level 5 --validate "<DAS Kai.vst3>"` | SUCCESS |
| AU 規格 | `auval -v aufx Knst Kein` | AU VALIDATION SUCCEEDED |
| 配布物の検査 | `check_release_bundle.py <bundle> --min-os 13.0 --version 0.1.0`(VST3/AU)。OBS 用は `otool -l` で minos と LC_RPATH を手で見る | 全項目 ok。OBS 用は LC_RPATH なし |
| OBS の本物の音声エンジン | `build/release/tests/das_kai_obs_harness build/release/plugins/obs-source/das-kai-obs.plugin 120 1000 90` | ミックスの時刻の抜け 0、DAW時刻とのずれのぶれ 0.1ms 未満、最初と最後の差 0.2ms 未満、手動 +45ms が 45ms ±0.5 で効く |
| 画面と言語 | scratchpad の `snap_language.py`(pedalboard で開き、保存データの uiLanguage を書き換えて撮影)。`docs/screenshots/editor-ja.png`(既定)と `editor-en.png` | 既定は日本語、保存データに en があれば英語、読み込み後に保存し直したデータにも en が残る。崩れなし(pedalboard は書き出し扱いなので状態は「書き出し中」) |
| 実 DAW と実 OBS | ユーザーの手元(Cubase + OBS) | 自動化できない。未検証の項目として報告する |

## 既知の罠

- **Xcode なし環境ではプロジェクトに C 言語が必要**: JUCE の SheenBidi が .c なので、`project(... LANGUAGES C CXX)` でないと Makefile 生成時に `CMAKE_C_COMPILE_OBJECT` が無いと止まる(上流は Xcode 生成器前提)
- **OBS は 70ms 未満の時刻の変化を無視する**: `TS_SMOOTHING_THRESHOLD`(obs-source.c)。時刻を変える時は 80ms 以上の切れ目が要る(dev-rules 5)
- **OBS は 1024 サンプル未満しかたまっていない音源を、待ち時間を足す判断から外す**: `audio_buffer_insufficient`(obs-audio.c)。10ms ずつ送ると、切れ目の直後に先頭が削られ補正が最大 21.3ms 足りなくなる。実エンジンで 1 回目は偶然通り、2 回目で -23.5ms(期待 -44.85ms)になって発覚。切れ目の直後を 1440 サンプルまとめ送りにして解消
- **44.1kHz → 48kHz 変換は約 19ms ためる**: 最初の 1 ブロックでは何も出てこない。テストは数ブロック流してから判定する。この遅れは時計のフレーム位置には入れず `leadNs` で伝える(dev-rules 4)
- **起動直後にホストがまとめて処理すると、時計の傾きが引っ張られる**: 固まる前(4秒)は 50ms 以上外れた点で推定をやり直す。2次ループ(DLL)は揺れを拾って ±40ppm ぶれたので、指数重み付き最小二乗に替えた
- **ホストで読み込んだ VST3 のパラメータは 0〜1 の値だけ**: テストから値を入れる時は範囲(-500〜500ms)で正規化する。45ms は丸めで ±0.1ms ずれる
- **ビルドした AU の署名は古くなっていることがある**(リソース追加後): インストール時に署名し直す(`scripts/install.sh`)
- **時計情報の読み取りは書き込みと競合して失敗することがある**(seqlock の再試行 16 回を使い切る。検品で 1 読みあたり約 5e-7〜3e-6 [推定])。失敗を「送信側が消えた」と扱うと約 86ms 途切れていた。直前の時計情報を使い、送信側の生存は書き込み位置の変化だけで判断する
- **OBS 用プラグインに絶対パスの rpath を入れない**: `SKIP_BUILD_RPATH ON`。`@rpath/libobs` は OBS.app 側の `@executable_path/../Frameworks` で解決される(上流の配布物と同じ)

## 外部依存

| 依存 | 固定 | 用途 |
|---|---|---|
| JUCE 8.0.10 | `3af3ce009f6a02f6fa651008fffb5b41743a9fab` | VST3/AU(AGPL-3.0 で利用) |
| r8brain-free-src | `8fff6f3db26f14a8f5e8fb871000613673db5753` | DAW のサンプルレート → 48kHz |
| obs-studio(ヘッダーとソース参照) | `c17423ce05899ecb93f678601b3feaa8a469b180`(32.1.2) | OBS 用プラグインのヘッダー |
| SIMDe | `71fd833d9666141edcd1d3c109a80e228303d8d7` | OBS ヘッダーが Apple Silicon で必要 |
| OBS.app の libobs.framework | 実行環境の OBS(確認時 32.2.2) | リンク先 |

上流 DawAudioStreamer の安全確認(2026-10-27、判定「注意点あり」)の記録: `~/plugins_dev/_intake/DawAudioStreamer_security-check_20261027.md`。フォーク元のコミット `576c5b5c7fd395a3d4829a5708decd9b06301586`。

## 検証の記録

検証結果の数値は下の「記録」に日付つきで追記する。

### 記録 2026-11-04(0.1.0、初回)

- `ctest` 5/5 合格(audio_core / transport / transport_ipc / sync / vst3_smoke)
- sync: 44.1k +250ppm 10分 = 切れ目 0、仕切り直し 0、時刻のぶれ最大 0.071ms、最初 1.510ms / 最後 1.512ms、推定 250.1ppm。48k -300ppm 5分 = ぶれ最大 0.051ms、推定 -299.9ppm。手動 +45ms で -43.505ms(前 1.532ms)、AUTO オフで +49ms(待ち時間 49ms)
- vst3_smoke: 時計の周期 20833.3ns、lead 30.4ms、受信側の待ち時間 49ms、レベル 0.250
- pluginval strictness 5: SUCCESS。auval `aufx Knst Kein`: SUCCEEDED(配置後にも再実行)
- 配布物の検査: VST3/AU とも arm64・minos 13.0・相対 rpath のみ・個人情報なし・署名有効・版 0.1.0。OBS 用は arm64・minos 13.0・LC_RPATH なし
- OBS 実エンジン(OBS.app 32.2.2 の libobs、2分、+1000ppm): `docs/obs-harness-120s-1000ppm-offset90s.txt` と `-offset60s.txt`。ミックスの時刻の抜け 0、DAW 時刻とのずれ 0.183 / 0.227ms(ぶれ最大 0.077 / 0.072ms)、最初と最後の差 0.017 / 0.092ms、手動 +45ms は -44.825(期待 -44.817)/ -44.766(期待 -44.773)ms。OBS ログ: 待ち時間 64ms → 切れ目 85ms を受理 → +42ms で 106ms
- まとめ送り修正前の記録: `docs/obs-harness-120s-1000ppm-before-batch-fix.txt`(手動 +45ms が -23.523ms しか効かなかった)
- 普段使いの OBS(32.2.2)の起動ログで `das-kai-obs` の読み込みを確認
- 配置済みの SHA-256(先頭16桁): VST3 `cd850c7d08973e0d` / AU `a1ccf45c071d84df` / OBS `2b0534db1849efbd`
- 未検証: Cubase と OBS での実際の配信・録画(音と映像の揃い方を含む)、ASIO-Guard 有効時の自動補正の量

### 記録 2026-11-04(0.1.0、検品後の修正)

- 検品(別文脈の verifier)で見つかった点を修正: 時計情報の読み取り失敗で約 86ms 途切れる、合計補正が OBS の上限 960ms に近づく、README の途切れの説明、改変注記と libs/sync のライセンス、画面が未反映の値を出す、「1 per Mac」
- 検品で合格していた点: 識別子が上流とすべて別、音声処理中に確保・ロック・時計以外のシステム呼び出しなし(8万回の呼び出しで計測)、seqlock(約 29 億回の読み取りで破損 0)、画面が共有メモリに触らない、受信側の切れ目とまとめ送り(ランダム試験 40 回で違反 0)
- `ctest` 5/5(sync に clock read failure と offset ceiling を追加。修正前に clock read failure が落ちることを確認済み)
- pluginval strictness 5: SUCCESS。auval(配置後): SUCCEEDED。配布物の検査: VST3/AU 全項目 ok、OBS 用 arm64・minos 13.0・LC_RPATH なし
- OBS 実エンジン 2分 +1000ppm(`docs/obs-harness-120s-1000ppm-offset90s.txt` を上書き): 時刻の抜け 0、ずれ 0.142ms(ぶれ最大 0.035ms)、最初 0.157 / 最後 0.135ms、手動 +45ms は -44.861ms(期待 -44.858ms)
- 配置済みの SHA-256(先頭16桁): VST3 `1bb32010e8df1aba` / AU `c104a966e81e3a92` / OBS `bc607251af6047ba`
- 未検証は前回と同じ(Cubase と OBS での実配信・録画、ASIO-Guard)

### 記録 2026-11-04(0.1.0、DAS Kai へ改名・画面の日本語化)

- 表示名を Kinu Stream → DAS Kai に変更(DAW の名前、OBS のソース名、バンドルのファイル名、OBS モジュール `das-kai-obs`、作業フォルダ `plugins_dev/das-kai`)。中身の識別子は変えていない(dev-rules 2)
- 画面: 日本語が既定、右上で English に切り替え。選んだ言語はプロジェクトの保存データ(`uiLanguage`)に入る。保存データ経由の読み込みと再保存を pedalboard で確認
- `ctest` 5/5、pluginval strictness 5 SUCCESS、auval SUCCEEDED(AudioUnit Name: DAS Kai)、配布物の検査 VST3/AU 全項目 ok、OBS 用 arm64・minos 13.0・LC_RPATH なし
- OBS 実エンジン 20 秒 +1000ppm: 時刻の抜け 0、ずれ 0.135ms(ぶれ最大 0.006ms)、手動 +45ms は -44.832ms(期待 -44.865ms)。ソースは `kinu_stream_audio_source` のまま「DAS Kai」として作成
- 旧名のバンドル 3 つ(Kinu Stream.vst3 / .component / kinu-stream-obs.plugin)はゴミ箱へ移動(同じ識別子のため)。普段使いの OBS 32.2.2 で `das-kai-obs` の読み込みと旧モジュールが無いことを確認
- 配置済みの SHA-256(先頭16桁): VST3 `e08abba32a67ca95` / AU `7116337f54f7ef82` / OBS `5bb9a40881ee8478`
- 未検証は前回と同じ(Cubase と OBS での実配信・録画、ASIO-Guard)
