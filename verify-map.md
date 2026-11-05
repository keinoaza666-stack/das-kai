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
- 配布用 ZIP: コミットして GitHub に送った後、`cmake --build build/release --target das_kai_package`。出力は `build/release/dist/DAS-Kai-<版>-macOS-AppleSilicon.zip` と `.sha256`。未コミットの変更があると止まる(試すだけなら環境変数 `DAS_ALLOW_UNCOMMITTED=1` で、名前に `-uncommitted` が付く)。最後に ZIP を展開し、中の Setup で入れる・入れ直す・消すを仮のホームで試す
- 配布前チェック: `python3 ~/.claude/skills/audio-plugin-dev/scripts/check_release_bundle.py <zip> <payload の .vst3> <payload の .component> --min-os 13.0 --version <版> --forbid <個人のメールアドレス>`。OBS 用の `.plugin` はこの道具の対象外なので、`lipo -archs`・`otool -l`(minos・LC_RPATH)・個人情報の文字列・`codesign --verify`・版を手で見る

### Windows

この Mac では Windows を動かせないので、GitHub Actions の Windows(仮想マシン、`windows-2025`)で作って確かめる。設定は `.github/workflows/windows.yml`。ブランチを GitHub に送るたびに動き、手で動かす時は Actions の「Windows」→「Run workflow」。

```bash
cmake -S . -B build -A x64 -DDAS_ISCC_EXECUTABLE="C:/Program Files (x86)/Inno Setup 6/ISCC.exe"
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target das_kai_package
```

- 作られるもの: `build/plugins/send-vst3/DasKai_artefacts/Release/VST3/DAS Kai.vst3`、`build/plugins/obs-source/Release/das-kai-obs.dll`、`build/dist/DAS-Kai-<版>-Windows-x64-Setup.exe` と `.sha256`
- 確かめる順: 試験 → DLL が Visual C++ ランタイムを読み込まないこと(`dumpbin /dependents`)→ pluginval(strictness 5、GUI の試験は除く)→ インストーラー → まっさらな Windows のコンテナーで、黙ってインストールし、入ったファイルを確かめ、入った VST3 を vst3_smoke で読み込み、アンインストールして消えたことを確かめる(`tools/windows/check-installer.ps1`)
- 合格したインストーラーは、その実行の成果物(Artifacts、14日)に残る。リリースに載せるのはこのファイル(作り直さない)
- 実際の Windows の DAW と OBS での確認はできない。未検証として報告する

## 検証ポイント

| 何を確かめるか | 手段 | 合格の基準 |
|---|---|---|
| 共有メモリ・時計情報・補正値の受け渡し | `ctest -R transport` | 書きかけの時計情報を読まない、上書きされた範囲を読まない |
| 補間の品質 | `ctest -R sync`(先頭) | 誤差 1k/10k/16k/20kHz で -100/-95/-90/-60 dB 未満 |
| 時計の推定 | `ctest -R sync` | +250ppm を ±5ppm で推定、先読みのまとめ処理後も ±10ppm。60ms の段差は 0.8 秒以内に直線ごと動かし、ずれの推定は 1ppm 以内しか変わらない |
| ホストの呼び出し方の型 | `ctest -R sync`(「host:」の行。ASIO-Guard 風のまとめ処理 4・8 ブロック、先読みの溜め込み 40ms 一気/45ms・100ms をゆっくり、溜め直し 20ms・40ms の繰り返し) | 決めた時間の後、ずれの推定が真値から 3〜10ppm 以内、表示に出る値が上限に張り付かない、30 秒以降の切れ目は待ち時間の調整 1 回まで(溜め直しは 1 回につき 1 回)、仕切り直し(epoch)は最初の 1 回だけ |
| 実時間ホストでの実機の呼び出し | OBS を閉じて `build/release/tests/das_kai_live_monitor 93 <trace.csv>` を起動し、1 秒後に `das_kai_realtime_host_artefacts/Release/das_kai_realtime_host "<DAS Kai.vst3>" 90 --device "Mac Studioのスピーカー" --buffer 256 [--guard 8]`(出力は無音、デバイスの今のサンプルレートのまま) | 仕切り直し 0、切れ目 0(待ち時間の調整を除く)、ずれの推定が落ち着く |
| 実際の DAW | DAW に挿して再生し、OBS を閉じて `das_kai_live_monitor` で記録(DAW の操作はユーザーの許可が要る) | 同上。書き込みの間隔と量から、DAW の実際の呼び出し方が分かる |
| クロックずれの吸収 | `ctest -R sync`(44.1k +250ppm 10分、48k -300ppm 5分) | 時刻の切れ目 0、仕切り直し 0、時刻のぶれ 0.5ms 未満、最初と最後の差 0.2ms 未満、1kHz の段差 1e-4 未満 |
| 手動補正・自動補正の切り替え | `ctest -R sync`(offset changes) | +45ms で 45ms 早める、AUTO オフで待ち時間ぶん遅くなる、切れ目は 80ms 以上、切れ目の直後はまとめ送り |
| DAW の停止と再開 | `ctest -R sync`(engine pause) | 再開後に合い方が戻る、待ち時間が初期値に戻る |
| 時計情報の読み取り失敗 | `ctest -R sync`(clock read failure) | 1 回失敗しても切れ目 0・仕切り直し 0 |
| 合計補正の上限 | `ctest -R sync`(offset ceiling) | 手動 1000ms を入れても合計 850ms で止まる |
| 実際の VST3 | `ctest -R vst3_smoke` | 名前・ベンダー、バイパスで無音を送る、2個目は送らない、補正値の公開、受信側で音と時刻が届く |
| VST3 規格 | `pluginval --strictness-level 5 --validate "<DAS Kai.vst3>"` | SUCCESS |
| AU 規格 | `auval -v aufx Knst Kein` | AU VALIDATION SUCCEEDED |
| 配布物の検査 | `check_release_bundle.py <bundle> --min-os 13.0 --version <版>`(VST3/AU)。OBS 用は `otool -l` で minos と LC_RPATH を手で見る | 全項目 ok。OBS 用は LC_RPATH なし |
| OBS の本物の音声エンジン | `build/release/tests/das_kai_obs_harness build/release/plugins/obs-source/das-kai-obs.plugin 120 1000 90` | ミックスの時刻の抜け 0、DAW時刻とのずれのぶれ 0.1ms 未満、最初と最後の差 0.2ms 未満、手動 +45ms が 45ms ±0.5 で効く |
| 画面と言語 | scratchpad の `snap_language.py`(pedalboard で開き、保存データの uiLanguage を書き換えて撮影)。`docs/screenshots/editor-ja.png`(既定)と `editor-en.png` | 既定は日本語、保存データに en があれば英語、読み込み後に保存し直したデータにも en が残る。崩れなし(pedalboard は書き出し扱いなので状態は「書き出し中」) |
| Setup の入れる・消す・巻き戻し | `ctest -R setup`(仮のホームで17項目。元の DawAudioStreamer のファイルと作業用フォルダーに触らないことを含む) | すべて PASS |
| Setup の約束(名前・URL・権限) | `ctest -R setup_contract` | OK。壊した写しで、作業用フォルダー名・xattr・バンドル名・URL・元のサイトへのリンクをそれぞれ検出することを確認済み(2026-11-05) |
| 配布用 ZIP | `das_kai_package`(展開した ZIP の中身で Setup の試験を流す)と配布前チェック | 全項目 ok、`__MACOSX` なし、SOURCE.txt が GitHub 上のコミットを指す |
| 実際の Setup.app | 展開した ZIP の Setup.app を開き、[インストール / 更新]・[アンインストール…]・英語切替を押す(Setup.app の操作だけ許可をもらう) | 「インストール完了」、入ったものが payload と `diff -rq` で一致、署名 ok、`.daskai-setup-*` が残らない、DAS Send の SHA-256 が変わらない |
| Windows のビルド・試験・インストーラー | GitHub Actions の「Windows」(上の節) | すべての段階が成功 |
| 実 DAW と実 OBS | ユーザーの手元(Cubase + OBS) | 自動化できない。未検証の項目として報告する |

## 既知の罠

- **Xcode なし環境ではプロジェクトに C 言語が必要**: JUCE の SheenBidi が .c なので、`project(... LANGUAGES C CXX)` でないと Makefile 生成時に `CMAKE_C_COMPILE_OBJECT` が無いと止まる(上流は Xcode 生成器前提)
- **OBS は 70ms 未満の時刻の変化を無視する**: `TS_SMOOTHING_THRESHOLD`(obs-source.c)。時刻を変える時は 80ms 以上の切れ目が要る(dev-rules 5)
- **OBS は 1024 サンプル未満しかたまっていない音源を、待ち時間を足す判断から外す**: `audio_buffer_insufficient`(obs-audio.c)。10ms ずつ送ると、切れ目の直後に先頭が削られ補正が最大 21.3ms 足りなくなる。実エンジンで 1 回目は偶然通り、2 回目で -23.5ms(期待 -44.85ms)になって発覚。切れ目の直後を 1440 サンプルまとめ送りにして解消
- **44.1kHz → 48kHz 変換は約 19ms ためる**: 最初の 1 ブロックでは何も出てこない。テストは数ブロック流してから判定する。この遅れは時計のフレーム位置には入れず `leadNs` で伝える(dev-rules 4)
- **起動直後にホストがまとめて処理すると、時計の傾きが引っ張られる**: 固まる前(4秒)は 50ms 以上外れた点で推定をやり直す。2次ループ(DLL)は揺れを拾って ±40ppm ぶれたので、指数重み付き最小二乗に替えた
- **ホストで読み込んだ VST3 のパラメータは 0〜1 の値だけ**: テストから値を入れる時は範囲(-100〜100ms)で正規化する。45ms は丸めで ±0.1ms ずれる
- **ビルドした AU の署名は古くなっていることがある**(リソース追加後): インストール時に署名し直す(`scripts/install.sh`)
- **時計情報の読み取りは書き込みと競合して失敗することがある**(seqlock の再試行 16 回を使い切る。検品で 1 読みあたり約 5e-7〜3e-6 [推定])。失敗を「送信側が消えた」と扱うと約 86ms 途切れていた。直前の時計情報を使い、送信側の生存は書き込み位置の変化だけで判断する
- **画面のフォントは macOS 標準のヒラギノ丸ゴ ProN と Menlo**(埋め込みなし)。丸ゴは W4 の1種類だけなので太字指定は効かない。明るい配色の確認は環境変数 `DAS_KAI_UI_THEME=light`(開発用)で撮る
- **JUCE の Shift+ドラッグ微調整は `setVelocityModeParameters` の4番目を true にしないと効かない**(罠一覧の例が false だった。2026-11-04 に罠一覧を訂正)
- **swiftc は `/usr/lib/swift` を絶対パスの rpath として入れる**: Setup.app のライブラリはすべて `/usr/lib/swift/...` の完全な場所で指定されていて使われないが、配布前チェックが不合格にする(上流の Setup.app にも入っている)。`-no-stdlib-rpath` で外す
- **この Mac で作ったファイルには `com.apple.provenance` が付く**: `ditto -c -k --sequesterRsrc` で固めると、全ファイルに `__MACOSX/._*` が付く(上流は CI で作るので付かない)。`--norsrc --noextattr --noacl` で固める。署名は拡張属性に頼らないので、展開後の `codesign --verify` は通る
- **ZIP の日本語ファイル名には UTF-8 の印(フラグ)が付かない**(`ditto` の仕様、上流も同じ)。Mac で展開すれば正しい名前になる。Windows で開くと文字化けしうるが、Mac 専用なので許容
- **配布前チェックの道具は OBS 用の `.plugin` を調べない**: 手で見る(ビルドの節)
- **Windows では OBS の `util/threading.h` が OBS 専用の `pthread.h`(deps/w32-pthreads)を求める**: OBS 用プラグインが使うのは `os_set_thread_name` だけなので、Windows ではヘッダーを読まずにその関数だけを宣言する(`obs-import.def` にも載せる)
- **まっさらな Windows のコンテナーは Windows PowerShell 5.1**: BOM のない UTF-8 のスクリプトを ANSI として読み、日本語が化けて構文が壊れる。コンテナーで動かすスクリプトは `utf8BOM` で書き出す
- **GitHub の Windows ランナーで Docker が動いていないことがある**(同じ版のイメージで、動いた回と動かない回があった)。確認の前に `Start-Service docker` して、応答するまで待つ。まっさらな Windows のイメージ(`windows/server:ltsc2025`)の取得は約 11 分かかる
- **版を上げる時は `project(DasKai VERSION ...)` だけを変える**: プラグイン・OBS 用・Setup・インストーラーの版はすべてここから取る。以前は `DAS_COMPONENT_VERSION` がキャッシュに残り、既存のビルドフォルダーでは古い版のままになっていた(0.2.0 に上げる時に発見して直した)。配布前チェックの `--version` で止まる
- **Windows で作った `.sha256` は行末が CRLF になっていた**(CMake の `file(WRITE)`)。Mac の `shasum -c` が「<名前>\r」を探して失敗する。`file(CONFIGURE ... NEWLINE_STYLE UNIX)` で LF にする。0.2.0 のリリースの値ファイルは LF のものに差し替えた(値は同じ)
- **配布物を作る段階は、コミットにないファイルがあると止まる**(SOURCE.txt とずらさないため)。CI でダウンロードするもの(pluginval など)は `RUNNER_TEMP` に置き、ソースのフォルダーに置かない
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

### 記録 2026-11-04(0.1.0、案B「墨と翠」・手動補正 ±100ms・表現の見直し)

- 画面: 黒緑の地に翡翠色、文字はヒラギノ丸ゴ、数字は Menlo。言語切り替えは右上の小さな「日本語 ・ EN」(選択側を翡翠色の下線)。構成(配置)は変えていない
- 手動補正: -100〜+100ms、0.1ms 刻み、Shift+ドラッグで 1/4 の速さ、数字欄で直接入力
- 表現: 見出し「タイミング補正」、状態「DAWの音を早めています / 遅らせています / 補正していません」
- `docs/screenshots/editor-ja.png`(暗)、`editor-en.png`(暗・英語)、`editor-ja-light.png`(明)で崩れなしを確認。言語切り替えで「日本語」が省略記号つきで切れていたのを固定幅に直して再確認
- `ctest` 5/5、pluginval strictness 5 SUCCESS、auval SUCCEEDED、配布物の検査 VST3/AU 全項目 ok
- 配置済みの SHA-256(先頭16桁): VST3 `cc0aa82ec1e6a273` / AU `e08b3f1434f85e85` / OBS `5bb9a40881ee8478`(OBS 用は前回と同じ)
- ユーザー実機: OBS 側で「音声入力キャプチャ」を追加していて待機中のままだった件は、DAS Kai のソースを追加して解決済み(ユーザー申告)

### 記録 2026-11-04(0.1.0、Cubase で「時計のずれ」が上限に張り付く件)

- ユーザー実機: Cubase 15(ASIO-Guard 有効 = Defaults.xml の `Prefetching=1`、無音時の処理停止は無効 = UserPreferences.xml の `SuspendWhenSilenceFlags=0`)、オーディオインターフェース Lyra 1。Stereo Out に挿した DAS Kai の時計のずれが +2004ppm(旧上限 2000ppm に当たった値)から動かず、しばらくして -15ppm に急に落ちた。共有メモリの時計の番号(epoch)は 5007 で、この日の送信約 22 分ぶんの間に約 5000 回仕切り直していた
- 見逃した理由: 模擬テストの DAW は毎回きっちり等間隔で呼んでいた。pluginval・auval・pedalboard・OBS 実エンジンの確認も、送る側の呼び出し方は自分で決めていた。実 DAW で実時間に再生する確認をしていなかった(dev-rules 10)
- 原因(模擬で再現): 推定は呼び出し1回ずつ直線を当てはめていた。ASIO-Guard のように呼び出しが段差状に前倒しされると(溜め直し・まとめ処理・ゆっくりした先読みの溜め込み)、段差が傾きに入って上限に張り付く。張り付いている間、受信側の速さの補正(±300ppm)が追いつかず数秒おきに仕切り直す(20ms の溜め直しが3秒おき: 2 分で 181 回途切れた)
- 修正: 推定を 250ms ごとの平均で当てはめ、外れた区間は保留、同じ側に2区間で段差として直線ごと移動(dev-rules 8)。上限は 1500ppm(500ppm にすると OBS 実エンジンの 1000ppm 確認で 12ms ずれたため)。上限に張り付いている間は「合わせ終わった」にしない。揺れの大きいホストでは平均の期間と合わせ終わるまでの時間を最大4倍に。受信側は書き込みの合間を最近の間隔で判断し(dev-rules 9)、遅れで待ち時間を延ばすのは前回から書き込みが進んだ時だけ(止まる時に 180ms 延ばしていたのを実時間ホストで発見して修正)。OBS のログに `[DAS Kai]` の行(受信開始・停止・段差・遅れ・1分ごとの状態)
- `ctest` 5/5。sync の「host:」9 型すべて合格(ずれの推定は真値から 0.9〜7.3ppm、30 秒以降の途切れは溜め直しの回数ぶんだけ)。engine pause の遅れ扱いは 1 回(修正前 4 回)
- OBS 実エンジン 2 分 +1000ppm(`docs/obs-harness-120s-1000ppm-offset90s.txt` を上書き): 推定 1000.0ppm、時刻の抜け 0、ずれ 0.145ms(ぶれ最大 0.009ms)、最初 0.147 / 最後 0.146ms、手動 +45ms は -44.851ms(期待 -44.855ms)。OBS ログに `[DAS Kai]` の行が出る
- 実時間ホスト(`das_kai_realtime_host`、Mac Studio のスピーカー、48kHz、出力は無音)+ `das_kai_live_monitor`、各 90 秒: 通常の実時間 256 = 仕切り直し 0・途切れ 0・ずれ -3.3〜-3.7ppm。先読み 8×256 = 開始 1 秒の待ち時間調整 2 回のみ、30 秒以降 -3.1〜-6.6ppm。先読み 4×1024 = 開始 1 秒の調整 3 回と段差 1 回のみ、30 秒以降 -6.6〜+3.7ppm。停止時の遅れ扱いは各 1 回
- 旧版(0.1.0 初版)を同じ先読み 8×256 で動かすと開始直後に -1996ppm まで振れたが、30 秒以降は落ち着いた。ユーザーの Cubase の「長く張り付く・約 5000 回の仕切り直し」は再現できていない = 実際の Cubase の呼び出し方は、この先読みの真似より荒い
- 記録の再生(`das_kai_replay_trace`)は、実時間ホストの記録 3 本で実測と一致(途切れ 0/2/3 対 実測 0/3/4、ずれ 1〜3ppm 以内)。Cubase で `das_kai_live_monitor` の記録を1本取れば、旧版のままの Cubase でも新しいコードでの結果をこれで出せる
- pluginval strictness 5 SUCCESS、auval SUCCEEDED(配置後)、配布物の検査(配置後の VST3/AU)全項目 ok、OBS 用 arm64・minos 13.0・LC_RPATH なし
- 配置済みの SHA-256(先頭16桁): VST3 `1cbfbf3c35e19a7b` / AU `1543d1d78abc81ab` / OBS `9b7bec15787d2442`
- 未検証: 実際の Cubase・Studio One での新版(computer-use の許可が下りず、ユーザーのアプリは操作していない。起動中の Cubase は旧版のバイナリを読み込んだままなので、新版は Cubase の再起動後)。Windows(この Mac に仮想 Windows はなく、DAS Kai は Mac 版のみ)
- 測定時、ユーザーの Cubase の DAS Kai は約 26 分前から送信していなかった(Cubase は DAS Kai のバイナリを読み込んだまま、送信ロックは誰も持っていない = 挿していたインスタンスは外れていた)

### 記録 2026-11-04 続き(1回目の修正は独立検品で反証 → 推定を作り直し)

- 独立検品(verifier)の反証: 1回目の修正(区間平均+段差で直線ごと移動)は、段差のない一定のまとめ処理(6・8×512 @48k/44.1k、6・8×1024 @96k)で +1502ppm に張り付いた(旧版は -14.8ppm で正しかった)。区間平均がまとめ処理とうなり、ゆっくりした上りと約 2.5ms の戻りののこぎり波になり、戻りだけ段差として消されて上りが傾きに積もった。この版を一度配置してしまった(VST3 `1cbfbf3c35e19a7b`)
- 作り直しの途中で踏んだこと(どれも sync テストの「host:」で再現してから直した): ①段差の基準を中央値の振れ幅にすると、のこぎり波の戻りを毎回段差と取り違える → 振れ幅全体(最大−最小)の 1.5 倍に ②段差をまたいだ点が残ると傾きを約 100ppm 引っ張る → 新しい4点を新しい高さに置き直す ③数秒のデータの傾きを信じると、その傾きで外挿した段差判定が自己強化する(8×512 で +1446ppm) → 傾きを自身の誤差と前回値の重み付きに ④それでも 8×512 は平均が「2.1秒周期で +3ms 上って戻る」ので、どの統計でも傾き+段差と区別できない → 点を区間の**平均ではなく一番早い呼び出し**に変更(まとめ処理の最後のブロックは毎回同じ位置なので揺れない) ⑤一番早い呼び出しを基準にすると、まとめの最初のブロックが 160〜210ms「遅れ」に見えて再アンカーを繰り返した → 遅れの判定にまとめの幅を足す ⑥受信側の「止まった」の判定(200ms)が 213ms 間隔のまとめで切れていた → 書き込み間隔に合わせて延ばす ⑦遅れで待ち時間を 10ms ずつ延ばすと粗いホストで開始時に 13 回途切れた → 足りなかった分だけ延ばす(7 回に)
- テストの模擬の誤り: 「溜め直し」の模擬が、余分な音と呼び出しの前倒しを二重に数えていた(段差が実際の2倍で、先読みが際限なく積み上がる)→ 余分な音だけにした
- sync テストの「host:」は 26 型(まとめ 4〜8 ブロック × 256〜2048、44.1/48/96k、±300ppm、まとめ幅 150ms 超、溜め込み、溜め直し、まとめ+溜め直し、遅い側・早い側の段差、1.9ms の小さな段差)。全型で合わせ終わり、真値から 0.2〜12ppm、30 秒以降の途切れは待ち時間の調整1回まで(溜め直しは1回ごと)。213ms まとめ(5×2048)だけは「合わせ終わった」表示が時々外れる(推定値は ±8ppm)
- 推定器が「一番早い呼び出し」を基準にしたことで、OBS に渡す時刻はコールバックの平均の遅れ(ジッタの半分)ではなく、ほぼ遅れ 0 の時刻になった(44.1k +250ppm で 0.125ms、OBS 実エンジンで 0.033ms)。まとめ処理のホストでは、待ち時間(自動補正)がまとめの幅だけ増える
- 検品の合成記録3本(6×512、8×512、2048×4)を `das_kai_replay_trace <csv> <block>` で再生: -15.3 / -14.8 / -14.7ppm(真値 -15)、合わせ終わり
- 実時間ホスト(Mac Studio のスピーカー、各 90 秒): 実時間 256 = 途切れ 0・遅れ 0・ずれ -4.1〜-3.9ppm。先読み 8×256 / 4×1024 / 6×512 / 8×512 = 途切れは開始 30 秒以内の 3〜5 回のみ、30 秒以降 -2.8〜-4.8ppm、仕切り直し 0、停止時の遅れ扱い 0
- OBS 実エンジン 2 分 +1000ppm: 推定 999.9ppm、時刻の抜け 0、ずれ 0.033ms(ぶれ最大 0.074ms)、最初 0.045 / 最後 0.030ms、手動 +45ms は -44.965ms(期待 -44.967ms)
- `ctest` 5/5、pluginval strictness 5 SUCCESS、auval SUCCEEDED、配布物の検査(配置後)全項目 ok、OBS 用 minos 13.0・LC_RPATH なし
- 配置済みの SHA-256(先頭16桁): VST3 `774009268dd7ed54` / AU `cef3971cb1d614b4` / OBS `23c288b1f3ee24a4`
- 未検証: 実際の Cubase の呼び出し方(記録がまだない)。新版の実 Cubase・Studio One での動き

### 記録 2026-11-04 続き2(2回目の独立検品を受けた3版目)

- 2回目の検品で確認できたこと: 不規則な大きさのまとめ処理(1〜16×512)を旧版に流すと、ユーザーの Cubase と同じ症状(+2004ppm 張り付き、約 0.1 秒ごとの再アンカー = 1日で約5000回と合う)が出る。実際の Cubase の呼び出し方の最有力候補
- 2回目の検品の反証と対処: ①点を直線上に書き直すと残差ゼロで「確かな間違った傾き」になる → 書き直さず捨てる ②50ms 以上の段差で受信側の待ち時間を初期値に戻すと途切れが増える → 段差では仕切り直さない ③時々早い呼び出し・前後に広がる揺れ・不規則なまとめで最小値が動く → 点を「まとまりの最初の呼び出しの中央値」に ④待ち時間が 10〜20ms ずつしか伸びない → 遅れた分(足りない量と待った時間の大きい方)+10ms ⑤ゆっくりした溜め込みで重み付け後の傾きが上限に届かず、溜め込みの傾きが 30 秒残る → 点そのものの傾きで上限を判定 ⑥文書の食い違い(「10ミリ秒ずつ」等)を修正
- sync テストの「host:」は 39 型(2回目の検品の型 13 を追加: 不規則なまとめ 2〜6/1〜8/1〜16、+80ppm、時々早い呼び出し 50/20/5ms、溜め込み+-80/+60ppm、実時間と先読みの切り替え 7/20 秒、まとめの大きさの変更、前後に広がる揺れ)。全型で真値から 0.3〜3ppm(不規則なまとめは 0.5〜0.8ppm、溜め込みは 1.5ppm、揺れの広がりは 0.6ppm)、まとめ処理の待ち時間は増えない(例 29→29ms)
- 既知の限界(テストに明記): 20ms の溜め直しが3秒おきに続くと推定が最大約 24ppm ずれる。それに 8×1024 のまとめが重なると推定は合う(-16.8ppm)が「合わせ終わった」表示にならない。どちらも OBS の音は段差のたびに途切れる
- 実時間ホスト(Mac Studio のスピーカー、各 90 秒): 実時間 256 = 途切れ 0・ずれ -2.8〜-3.9ppm。先読み 8×256 / 4×1024 / 6×512 / 8×512 = 途切れは開始時の 3〜4 回のみ(30 秒以降 0)、遅れの扱い各 1 回、待ち時間はそのまま、30 秒以降 -1.3〜-6.2ppm、仕切り直し 0
- OBS 実エンジン 2 分 +1000ppm: 推定 999.9ppm、時刻の抜け 0、ずれ 0.151ms(ぶれ最大 0.036ms)、最初 0.155 / 最後 0.153ms、手動 +45ms は -44.846ms(期待 -44.849ms)
- `ctest` 5/5、pluginval strictness 5 SUCCESS、auval SUCCEEDED、配布物の検査(配置後)全項目 ok、OBS 用 minos 13.0・LC_RPATH なし
- 配置済みの SHA-256(先頭16桁): VST3 `2086e58517f2a3d0` / AU `5283348c24e9644e` / OBS `7abde3effb0c3971`
- 未検証: 実際の Cubase の呼び出し方と、新版の実 Cubase・Studio One での動き

### 記録 2026-11-04 続き3(3回目の独立検品)

- 3回目の検品(模擬ホスト)で確認された失敗: ①まとめの中のブロック間隔が半ブロック以上(重い処理)→ 全部「まとまりの最初」扱いになり合わない ②処理ブロックとデバイスのバッファの比が 1 に近い(960/1024・1000/1024 で合わない) ③1秒周期の経路切り替え ④バッファの小分けの割合が変わる ⑤数秒かけた段差 ⑥0.1〜0.3 秒の停止で待ち時間が恒久的に延びる。崩せなかったもの: 先頭が数個しかない大きなまとめ(2×8192 等)、不規則なまとめ(1〜32×256、1〜16×1024)、サンプルレート変更、小さいバッファ、独自タイマーの先読み、1000 万回超の呼び出しで確保 0
- 直したもの: 上限での学び直しを3点で発動して捨て続けていた(480/512 で合わない)→ 8点以上ある時だけ、新しい4点は残す。480/512・496/512 は合うようになった(23〜30 秒、±0.1ppm)。960/1024・1000/1024・0.55 ブロック間隔は未対応のまま
- `ctest` 5/5(「host:」39 型)、OBS 実エンジン 2 分 +1000ppm: 999.9ppm・抜け 0・ずれ 0.151ms(ぶれ最大 0.028ms)・手動 +45ms は -44.847ms(期待 -44.849ms)、pluginval SUCCESS、auval SUCCEEDED、配布物の検査 ok
- 配置済みの SHA-256(先頭16桁): VST3 `b44b343c2c5f7b90` / AU `cc74efd17a01c40c` / OBS `7abde3effb0c3971`
- 判断: 想定した呼び出し方の型を足すたびに別の型で崩れる。実際の Cubase の記録(`das_kai_live_monitor`)を取り、`das_kai_replay_trace` で再生して、現実に起きる型から直す

### 記録 2026-11-04 続き4(クロックのずれの色分け・「時計」→「クロック」)

- ユーザー指示: ppm が正常範囲は緑、異常は赤、間は黄。「時計」ではなく「クロック」表記に統一
- 色の境目(PluginEditor.cpp の `kClockNormalPpm` / `kClockAbnormalPpm`): 緑 = ±100ppm 以内(ふつうのインターフェースの範囲)、黄 = 100〜300ppm、赤 = 300ppm 超(受信側の速さの補正 `StreamReceiver::kMaximumCorrection` を超え、OBS 側が途切れやすい)。表示している整数で判定(+100 は緑)。合わせている最中は判定せず灰色
- 表記: 画面「クロックのずれ」「クロックを合わせています…」、README と dev-rules も「クロック」に統一(日付つきの記録は当時のまま)
- 画面確認用の開発専用の上書き: 環境変数 `DAS_KAI_UI_PREVIEW_PPM=<ppm>` で、その値を合わせ終わった状態として表示する(`DAS_KAI_UI_THEME` と同じ扱い)
- `docs/screenshots/editor-ja.png`(緑 -15)、`editor-en.png`(緑)、`editor-ja-light.png`(明・黄 +180)、`editor-ja-clock-yellow.png`、`editor-ja-clock-red.png`(+450)、`editor-ja-clock-locking.png`、`clock-colour-boundaries.png`(+100 緑 / +101 黄 / +300 黄 / -301 赤)
- `ctest` 5/5、pluginval strictness 5 SUCCESS、auval SUCCEEDED、配布物の検査(配置後)全項目 ok
- 配置済みの SHA-256(先頭16桁): VST3 `49e7b6601545723f` / AU `7b0db6bdcecdbc42` / OBS `7abde3effb0c3971`(OBS 用は前回と同じ)

### 記録 2026-11-05(赤の理由の訂正・README に「クロックのずれが大きい時」を追加)

- 訂正: 続き4 と README で、赤(300ppm 超)の理由を「OBS 側が速さの補正で追いつける範囲を超える」と書いたのは不正確。受信側は送り側が出すクロックの傾き(`clock.periodNs`)をそのまま読む速さに使い(`nominalStep`)、`kMaximumCorrection`(±300ppm)はその上に足す位置ずれの補正だけを抑える(StreamReceiver.cpp の `nominalStep + correction`)。本当のずれは推定の上限 `kMaxDeviation`(±1500ppm)まで追える(OBS ハーネス 1000ppm で途切れ 0: `docs/obs-harness-120s-1000ppm-offset90s.txt`)。300ppm が効くのは推定が実際から外れた時で、外れが 300ppm を超えると埋めきれず途切れる。赤の意味は「ふつうの機材ではまず出ない値 = 推定の外れなら OBS 側で埋めきれない大きさ」とした。境目の値(300)と画面の挙動は変えていない
- `PluginEditor.cpp` の `kClockAbnormalPpm` のコメントを同じ意味に直した(行数は変えず)。再ビルドした VST3 は配置済みと同じ SHA-256 `49e7b6601545723f` で、配置し直しは不要
- README に「クロックのずれが大きい時」を追加: 灰色の間は待つ / 落ち着いていて途切れなければ本当のずれ(外部クロック設定を確認)/ 上下する・途切れる・ログに段差や待ち時間の延長が続くなら読み違え → 無効化して戻す(バイパスでは合わせ直さない: `prepareToPlay` → `StreamSender::prepare` で推定を初期化するのは無効化から戻した時)、ASIO-Guard やドロップアウト保護を弱める、OBS のログと記録の道具で知らせる
- 未確認: Cubase 15 / Studio One 6 の画面での設定の場所(Steinberg と PreSonus のヘルプの記載に基づく)、実 Cubase で無効化して戻した時の合わせ直し
- 公開前の確認(2026-11-05): 全体の再ビルド直後の `ctest` で `vst3_smoke` が1回失敗した(出力を絞っていたため失敗の内容は取れていない)。その後は単独10回、全体1回(5/5)、CPU を8本の空回しで埋めた状態で5回、すべて合格。負荷下の時刻のずれは最大 8.6ms(合格の上限 15ms)。実時間で動かす試験なので、ビルド直後の負荷で上限を超えた可能性が高いが、原因は未特定。再発したら `--output-on-failure` で内容を取る

### 記録 2026-11-05(Setup.app と配布用 ZIP)

- 依頼: 「インストーラーがないから作って。本家のように簡単にインストールできるように」
- 本家の方式: ZIP の中に Setup.app(Swift・AppKit)を入れる。ボタン1つで、ホームフォルダーの3か所に管理者権限なしで入れる。失敗した時は前の版に戻す。フォークした時に削っていたものを戻し、DAS Kai 用に改変した(変更点は MODIFICATIONS.md 2026-11-05、約束は dev-rules 11)
- 本家から変えた点: バンドル名と文言、日本語を先に表示、ロックと作業用フォルダーの名前を本家と別にする、ヘルプのリンク先を DAS Kai のリポジトリにする。ZIP を作る手順は、Apple Silicon のみ・未コミットなら止める・JUCE が組み込むライブラリのライセンス文も入れる・拡張属性なしで固める・展開した ZIP で Setup の試験を流す、とした。swiftc には `-no-stdlib-rpath` を付けた
- 確認の結果:
  - `ctest` 7/7 合格。setup は17項目で、本家から引き継いだ16項目に「元の DawAudioStreamer のファイルと作業用フォルダーに触らない」を足した
  - 契約の試験が本当に違反を見つけるかを、わざと壊した写しで確かめた。作業用フォルダー名・xattr・バンドル名・URL・元のサイトへのリンクの5通りをすべて検出し、壊していない写しは合格した。最初は URL を部分一致で見ていて `das-kai2` を見逃したので、URL の区切りまで見るように直した
  - 試しの ZIP: 展開した中身で Setup の試験 17 項目と、実物の3つのバンドルでの入れる・入れ直す・消すが合格。配布前チェックは ZIP・VST3・AU のすべてで ok。最初は Setup.app の rpath `/usr/lib/swift` で不合格になったので、`-no-stdlib-rpath` で外した。OBS 用は手で見て ok(arm64、minos 13.0、rpath なし、個人情報 0、署名 ok、0.1.0)。`__MACOSX` は 0
  - 実際の Setup.app: ZIP を展開して開き、Setup.app の操作だけ許可をもらって押した。インストールすると「インストール完了」と出て、入ったものが payload と `diff -rq` で一致し、署名 ok、作業用フォルダーは残らず、DAS Send の SHA-256 は変わらなかった。アンインストールすると DAS Kai の3つが消え、DAS Send は残った。英語に切り替えて入れ直すと、確認前と同じ SHA-256(VST3 49e7b660…、AU 7b0db6bd…、OBS 7abde3ef…)に戻った
  - 画面で、手順2の「設定は不要です」が途中で折り返していたので、文を短くした
- 未検証:
  - ダウンロードした ZIP(隔離属性が付く)を開いた時に macOS に止められ、「このまま開く」で通す流れ。利用者の操作が要るので、ここでは試していない → 同日確認済み(下の「Windows 版・Mac のダウンロード後の確認」)
  - DAW が、隔離属性の付いたプラグインを読み込む時の動き。本家と同じ方式で、Setup は隔離属性に触らない → 同日確認済み(macOS 26 では止められない)
  - OBS が起動している時に Setup が止める動き。OBS は起動していないので試していない

### 記録 2026-11-05(Windows 版・Mac のダウンロード後の確認)

- 依頼: 「Windows 版はない？」「Kinu Tune は仮想で動作確認した。同じ方式でいいし、インストーラーも作れるでしょ」。あわせて「なんで Mac でインストールできるか試さない？」
- Windows 版: フォークした時に削った Windows 用の部分のうち必要なものを本家から戻し、DAS Kai に合わせた(MODIFICATIONS.md 2026-11-05 Windows version、dev-rules 12・13)。GitHub Actions の Windows で確かめた(`.github/workflows/windows.yml`)
- 仮想 Windows の結果(実行 41983831034、コミット ef5c7f4、イメージ windows-2025-vs2026 20261030.294.1):
  - ctest 5/5 合格(audio_core、transport、transport_ipc、sync 56.8 秒、vst3_smoke)
  - Visual C++ ランタイム(VCRUNTIME・MSVCP・ucrtbase)を読み込まない。VST3 が読み込むのは Windows 標準の DLL(kernel32、user32、gdi32、shell32、ole32、oleaut32、winmm、d2d1、dxgi、d3d11、dcomp、imm32、comctl32、dwmapi)、OBS 用は obs.dll と kernel32.dll だけ。最初は「VST3 は kernel32.dll だけ」と書いたが、記録の行を途中で切って読んだ誤り(0.2.0 の実行で全文を見て訂正)
  - pluginval 1.0.4(SHA-256 で固定)strictness 5、GUI の試験を除いて SUCCESS
  - インストーラー `DAS-Kai-0.1.0-Windows-x64-Setup.exe`、SHA-256 `275189268c95cf42bf32e7c9e9ba94338498edd6ce36a82641ebb371237b0fac`。成果物を手元に落として値の一致を確認
  - まっさらな Windows(`windows/server:ltsc2025`、Visual C++ ランタイムなし)で、黙ってインストール → 終了コード 0、すべてのファイル(日本語名の README を含む)と登録あり → 入った VST3 を vst3_smoke で読み込み、クロック周期 20833.1 ns(48 kHz)、受け取り側の待ち時間 49 ms、時刻のずれ最大 2.08 ms、音量 0.25 → アンインストールで VST3・OBS 用・アプリのフォルダー・登録がすべて消えた
  - 合格までに4回失敗した。OBS の `pthread.h`、pluginval の置き場所、コンテナーのスクリプトの文字コード、Docker が動いていない。どれも上の「既知の罠」に書いた
- Mac のダウンロード後の確認(別のセッションが行い、syspolicyd の記録で裏取り): リリースの ZIP に Chrome と同じ隔離属性を付け、アーカイブユーティリティで展開した。macOS の判定は「拒否」で、Setup.app を開くと止められた。利用者が「このまま開く」を押した後に入れると、入ったプラグインには許可済みの印(02c1)が付いた。そのまま、VST3 は das_kai_realtime_host で 4 秒・752 ブロック、AU は auval で SUCCEEDED、OBS 用は das_kai_obs_harness で受信できた。syspolicyd はプラグインを検査したが(evaluateScanResult 2)、確認の画面は出さなかった。止められるのは最初の Setup.app の1回だけ(macOS 26、この Mac)
- Setup の画面(別のセッション、コミット e28518d): 操作のボタンを右下、ヘルプを左下に置いた。インストールかアンインストールが終わったら、右下の「完了」だけを出す。v0.1.0 の ZIP は前の画面のまま
- 未検証:
  - 実際の Windows の DAW と OBS。特に、Windows の OBS 本体で読み込まれることと、時刻が OBS と合うこと。仮想 Windows には OBS がない
  - Windows で、署名のないインストーラーを開いた時の SmartScreen の画面の実際の文言
  - Windows のプラグイン画面の見た目(フォントは Yu Gothic UI と Consolas。入っていない文字は JUCE が別のフォントで出す)
  - macOS 13〜15 で、ダウンロードした Setup とプラグインを止める挙動

### 記録 2026-11-05(0.2.0 を公開)

- ユーザー指示: 「普通に公開して」(プレリリースの印なし)。0.2.0 = Windows 版 + Setup の画面の変更 + 版の番号の修正
- 版の番号: `DAS_COMPONENT_VERSION` がキャッシュに残り、`project(VERSION)` を 0.2.0 にしても既存のビルドフォルダーではプラグインが 0.1.0 のままになる作りだった。毎回 `project(VERSION)` から取るように直し、VST3・AU・OBS 用が 0.2.0 になることを確認
- コミット c856db3(main、タグ v0.2.0)から、Mac の ZIP は手元で、Windows のインストーラーは GitHub Actions(実行 41993091408)で作った
  - Mac: ctest 7/7、配布前チェックは ZIP・VST3・AU で全項目 ok、OBS 用は手で ok(arm64、minos 13.0、rpath なし、個人情報 0、署名 ok、0.2.0)、pluginval strictness 5 SUCCESS、`__MACOSX` 0。ZIP の SHA-256 `8979f020721c0d51548187fa5e59825af9d0497ccbd5dd1332f6ce808dcc52cb`
  - Windows: ctest 5/5、ランタイムの確認、pluginval 5 SUCCESS、まっさらな Windows で入れる・読み込む(時刻のずれ最大 2.73 ms)・消すが合格。インストーラーの SHA-256 `56d08cf8411fbca4491b854f984b9f58fe5130c8aefe4b185ce62b3a759829e5`
- 公開: https://github.com/keinoaza666-stack/das-kai/releases/tag/v0.2.0 (Latest)。4つのファイルを GitHub から落とし直し、`shasum -c` で2つとも一致を確認。Windows の値ファイルは CRLF で照合に失敗したので、LF のものに差し替えた(上の「既知の罠」)
- v0.1.0 はプレリリースのまま残した
- 未検証は前の記録と同じ: 実際の Windows の DAW と OBS、SmartScreen の画面、Windows のプラグイン画面の見た目、macOS 13〜15、新しいクロックの推定の実際の Cubase での動き

