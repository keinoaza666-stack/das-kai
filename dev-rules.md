# DAS Kai の開発ルール(設計の約束)

コードを変える前に読むファイルです。ここには「守らないと別の所が壊れる約束」だけを書きます。ビルド手順・検証の記録・過去の経緯は `verify-map.md` に書きます。

約束を新しく見つけたら、その変更と同じコミットでここに書き足します。

## 1. 共有メモリの構造を変えたら、版と名前を一緒に変える

- 守ること: `RingHeader` の並び・大きさ・意味を変えたら、`kProtocolMajor` を上げ、`kObsAudioMappingName` の末尾の版(`.v2`)も上げる。DAW側とOBS側は別々に読み込まれるので、片方だけ新しい状態が必ず起きる
- 必ず通す入口: `libs/transport/include/das/transport/RingBuffer.h`
- 破ると: 古いOBS用プラグインが新しい構造を読み違える。テストでは検出できない(両側を同時にビルドするため)

## 2. 元のDawAudioStreamerと混線させない

- 守ること: 識別子はすべて元と別の値を使う。プロトコルの印(`KSTR`)、共有メモリ名(`KinuStream.OBS.v2`、POSIX名の接頭辞 `/kst-`)、送信ロックのファイル名(Windows では名前付きイベント `Local\KinuStream.Send.Owner.v1`)、Windows インストーラーの AppId、OBSのソースID(`kinu_stream_audio_source`)、プラグインID(`Kein`/`Knst`)、バンドルID(`com.keinoaza.kinustream*`)
- 必ず通す入口: `RingBuffer.h`、`NamedSharedMemory_posix.cpp`、`PluginProcessor.cpp`(`senderLockPath`)、`obs-plugin.cpp`、`plugins/*/CMakeLists.txt`
- 表示名を変える時(2026-11-04 に Kinu Stream → DAS Kai)は、画面・DAW・OBS に出る名前とバンドルのファイル名だけを変える。上の識別子と、保存データの形式名(`"KinuStream"`、`PluginProcessor.cpp`)は旧仮称のまま変えない。変えると、すでに DAW のプロジェクトや OBS のシーンに入れたものが外れる
- 破ると: 両方を入れたMacで、片方のOBSソースがもう片方の音を拾う、またはDAW側の「1台に1個」の判定が相手を巻き込む

## 3. 送信の処理は StreamSender だけに書く

- 守ること: サンプルレート変換・共有メモリへの書き込み・クロックモデルの公開・補正値の公開は `das::audio::StreamSender` の中だけで行う。プラグインはパラメータを渡して呼ぶだけにする
- 必ず通す入口: `libs/audio-core/src/StreamSender.cpp`
- 破ると: シミュレーション(`sync` テスト)が実際のプラグインと違う処理を試すことになり、テストが通っても実機で壊れる

## 4. クロックモデルの「フレーム位置」は入力側の時間で数える

- 守ること: `StreamSender` がクロックに渡すフレーム位置は「準備時の書き込み位置 + 入力サンプル数 × 48000 / DAWのサンプルレート」。実際に書き込んだフレーム数で数えない。変換器の遅れは `leadNs` に入れて受信側に伝える
- 必ず通す入口: `StreamSender::process`
- 破ると: 変換器の遅れ(44.1kHzで約19ミリ秒)の分だけ、OBSに渡す時刻がずれる。`sync` テストの「timestamps off from DAW time」で落ちる

## 5. OBSに渡す時刻を変える時は、70ミリ秒以上の切れ目と、1024サンプル以上のまとめ送りをセットにする

- 守ること: 手動補正・自動補正のオンオフ・待ち時間の変更・仕切り直しで時刻の並びが変わる時は、`needsTimestampBreak_` を立てる。受信側は、OBSが前の時刻の続きとみなせなくなるまで(差が80ミリ秒以上)出力を止め、再開の最初は3ブロック(1440サンプル)を1回で送る
- 必ず通す入口: `StreamReceiver::poll` の末尾(時刻の切れ目とまとめ送りの処理)
- 破ると: 70ミリ秒未満の変化はOBSに無視される(`TS_SMOOTHING_THRESHOLD`)。まとめ送りがないと、OBSが待ち時間を足さずに先頭を削り、補正が最大21ミリ秒足りなくなる(実エンジンで再現済み)。`sync` テストの「offset changes」と、`das_kai_obs_harness` の手動補正の行で分かる

## 6. クロック情報が読めないことと、送信側が止まったことを混ぜない

- 守ること: `readClock` が失敗した時は直前に読めたクロック情報を使う。送信側が止まったかどうかは、書き込み位置が `kProducerTimeoutNs` の間動いていないことだけで判断する
- 必ず通す入口: `StreamReceiver::poll` の先頭(`lastClock_`)
- 破ると: 読み取りが書き込みと競合するたびに受信を最初からやり直し、約 86 ms 途切れる。`sync` テストの「clock read failure」で落ちる

## 7. 編集画面は共有メモリに触らない

- 守ること: 画面に出すOBSの状態は、音声処理の中で原子変数に写した値だけを読む
- 必ず通す入口: `DasSendProcessor::sendBlock` の末尾と `obsStatus()`
- 破ると: ホストが別スレッドで `releaseResources` を呼んだ瞬間に、解放済みのメモリを読む

## 8. クロックの推定では、ホストの呼び出し方の癖を「クロックのずれ」にしない

- 守ること(`FrameClockEstimator`):
  - 点は、区間(250ミリ秒以上、かつ最近の呼び出し間隔の3倍以上)の中の**「まとまりの最初の呼び出し」の中央値**にする。まとまりの最初とは、直前の呼び出しから半ブロック以上あいた呼び出し(実時間のホストでは毎回)。平均はまとめ処理とうなってのこぎり波になり(8×512 で +1400ppm に固定)、最小はまとまりの大きさが不規則だと動く(15〜28ppm ずれた)。まとまりの最初は、ハードウェアが次に必要とするブロックなので、まとまりの大きさに関係なく同じ位置に来る。中央値なので、時々早い呼び出しや、前後に広がる揺れにも引っ張られない
  - 段差は「新しい4点がすべて、古い点の直線から見て、古い点の振れ幅全体の1.5倍より外の同じ側にある」時だけとする。振れ幅は中央値ではなく最大−最小で見る(中央値だと、のこぎり波の戻りを毎回段差と取り違えた)
  - 段差を見つけたら、古い点を段差の分だけ動かし、新しい4点は**捨てる**。点を直線の上に書き直さない(書き直した点は残差がゼロになり、間違った傾きを「確か」に見せた)。段差では受信側を仕切り直さない(epoch を進めない)
  - 公開する傾きは、新しい4点を除いた古い点から求める。傾きは自身の誤差と「最後に合わせ終わった値」(最初は0、幅 100ppm)の重み付きで決め、誤差が 20ppm 未満になるまで「合わせ終わった」と表示しない
  - 点そのものの傾き(重み付けの前)が上限(1500ppm)に届いたら、新しい4点だけ残して捨て、学び直す。重み付けの後の傾きで判定すると上限に届かず、溜め込みの傾きが30秒残った。判定は8点以上ある時だけ(3点だと 0.75ms の揺れで上限に届き、捨て続けて永久に合わなかった)
  - 遅れの判定(再アンカー)は 150ms にホストのまとめの幅を足した値。早い側は 100ms 続いた時だけ。再アンカー直後の最初の区間は捨てる
- 必ず通す入口: `FrameClockEstimator::observe` と `addPoint`
- 破ると: Cubase の ASIO-Guard のように呼び出しが前後するホストで、クロックのずれの推定が上限に張り付き、OBS 側が数秒おきに仕切り直して音が途切れる(2026-11-04、Cubase 15 の実機で発生。不規則な大きさのまとめ処理で旧版が同じ症状を再現)。`sync` テストの「host:」の行(39 型)で落ちる。この型は増やすことはあっても減らさない。既知の限界はテストに2つ(20ms の溜め直しが3秒おき=最大約 24〜26ppm ずれる)と、3回目の検品で見つかった未対応の型(下の「未対応」)

- 未対応(3回目の検品、模擬ホストで確認・実機での頻度は不明): ①まとめの中のブロック処理に半ブロック以上かかる重いセッション(全部が「まとまりの最初」と判定されて合わない) ②処理ブロックとデバイスのバッファが 1 に近い比率で違う(960/1024、1000/1024) ③1秒前後の周期で経路が切り替わる ④1回のバッファを小分けにして呼び、その割合が変わる ⑤数秒かけて数 ms 動く段差(傾きが 65〜200ppm 引っ張られる) ⑥0.1〜0.3 秒の停止や一度の大きな遅れで待ち時間が恒久的に延びる(位置は変わらない)。実際の DAW の記録(`das_kai_live_monitor`)を取ってから、どれが現実に起きるかで手を付ける

## 9. 受信側の「遅れ」と「止まった」は、送信側の書き込みの間隔と、その後に音が届いたかで判断する

- 守ること: 送信側が書いているかの判断(`writingNow`)と「止まった」の判断(`producerAlive`)は、最近の書き込みの最大間隔(`writeGapNs_`)に合わせて延ばす。音が足りなくなった時はすぐ待ち時間を延ばさず、その後に新しい音が届いた時だけ、遅れた分(足りなかった量と待った時間の大きい方)+10ms だけ延ばす(`lateSuspected_`)
- 必ず通す入口: `StreamReceiver::poll` の遅れ(underrun)の処理
- 破ると: まとめて書き込むホストで、書き込みの合間を「止まった」と誤認して待ち時間を延ばさず、まとめ書きのたびに途切れる(待ち時間の表示は変わらないので画面から気づけない)。逆に、DAW が止まる時に「遅れ」と取り違えて待ち時間を最大 180ms 延ばしていた(実時間ホストで発見)

## 10. 模擬テストだけで「動く」と言わない

- 守ること: クロック・送受信・時刻に触れたら、`ctest` と `das_kai_obs_harness` に加えて、実際の DAW に挿して再生し、`das_kai_live_monitor`(OBS は閉じておく)で実際の呼び出し方と受信の様子を測る
- 必ず通す入口: `tests/live_monitor.cpp`
- 破ると: 模擬は自分が想定した呼び出し方しか試さない。2026-11-04 の不具合は、模擬・pluginval・OBS 実エンジンがすべて合格のまま、Cubase に挿すとすぐ出た

## 11. Setup と配布用 ZIP は、名前・作業用フォルダー・権限の扱いをそろえる

- 守ること: バンドルのファイル名を変えたら、`installer/macos/SetupCore.swift` の `components`、`cmake/CreateMacPackage.cmake`、`scripts/install.sh`、`installer/macos/README-macOS.txt`、`README.md` を同じコミットで変える。Setup の作業用フォルダー(`.daskai-setup-`)とロック(`com.keinoaza.daskai.setup-<uid>.lock`)は、元の DawAudioStreamer の Setup(`.das-setup-`、`org.dawaudiostreamer`)と別の名前にしておく。Setup は管理者権限を求めず、権限・隔離属性(quarantine)・Gatekeeper に触らない(macOS の確認は利用者が自分で通す)。配布用 ZIP は、コミットして GitHub に送ったソースから `das_kai_package` で作る
- 必ず通す入口: `installer/macos/SetupCore.swift`、`cmake/CreateMacPackage.cmake`
- 破ると: 名前がずれると、Setup が「ファイルが足りない」で止まるか、古い版を消せずに2つ並ぶ。作業用フォルダーの名前が同じだと、片方の Setup が失敗して残したフォルダーで、もう片方も動かなくなる。ZIP とコミットがずれると、SOURCE.txt が中身と違うソースを指す(AGPL/GPL の対応ソースにならない)。`ctest -R setup`(仮のホームでの入れる・消す・巻き戻し・元の Setup との共存)と `setup_contract`(名前・URL・権限の約束)、`das_kai_package` の最後の「展開した ZIP での試験」で分かる

## 12. DAW 側の時刻は、OBS と同じ時計を OBS と同じ計算で読む(Mac と Windows)

- 守ること: DAW 側で時刻を取る所は、すべて `das::transport::monotonicNowNs()` を通す。`clock_gettime_nsec_np` や `QueryPerformanceCounter` を直接呼ばない。Windows では OBS の `os_gettime_ns()` と同じく、パフォーマンスカウンターを `util_mul_div64` と同じ計算(128 ビットの掛け算と割り算)でナノ秒にする
- 必ず通す入口: `libs/transport/src/MonotonicClock.cpp`
- 破ると: OBS に渡す時刻が OBS の時計とずれ、音の置き場所がずれる。片側だけの試験では分からない(Mac の試験は同じ時計を両側で使うため)。OBS のソースが計算を変えたら、ここも合わせる(`obs_headers-src/libobs/util/platform-windows.c`)

## 13. Windows のインストーラーと OBS の関数一覧

- 守ること: `installer/windows/DASKai.iss` の AppId は変えない(変えると更新のたびに別のアプリとして2つ並ぶ)。本家の AppId も使わない(dev-rules 2)。入れる場所を変えたら、`README.md`、`installer/windows/README-Windows.txt`、`tools/windows/check-installer.ps1` を同じコミットで変える。OBS 用プラグインが libobs の関数を新しく呼ぶ時は、`plugins/obs-source/obs-import.def` に足す
- 必ず通す入口: `installer/windows/DASKai.iss`、`plugins/obs-source/obs-import.def`
- 破ると: 一覧にない関数は Windows のリンクで止まる(Mac では止まらない)。場所がずれると、入れたのに見つからない・消し残る。GitHub Actions の Windows の確認(まっさらな Windows に入れて、読み込んで、消す)で分かる

## 変更の進め方

- 不具合を直す時は、直す前に失敗するテストを足す
- コミットの前に、テスト全部(`ctest`)を流す。時刻やOBSの扱いに触れたら `das_kai_obs_harness` も流す
- 外部リポジトリのコード・依存を取り込む時は、`~/.claude/skills/_shared/external-code-intake.md` の安全確認を先に行い、判定を報告してから動かす。取り込んだものはコミットIDで固定する
