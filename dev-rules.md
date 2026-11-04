# DAS Kai の開発ルール(設計の約束)

コードを変える前に読むファイルです。ここには「守らないと別の所が壊れる約束」だけを書きます。ビルド手順・検証の記録・過去の経緯は `verify-map.md` に書きます。

約束を新しく見つけたら、その変更と同じコミットでここに書き足します。

## 1. 共有メモリの構造を変えたら、版と名前を一緒に変える

- 守ること: `RingHeader` の並び・大きさ・意味を変えたら、`kProtocolMajor` を上げ、`kObsAudioMappingName` の末尾の版(`.v2`)も上げる。DAW側とOBS側は別々に読み込まれるので、片方だけ新しい状態が必ず起きる
- 必ず通す入口: `libs/transport/include/das/transport/RingBuffer.h`
- 破ると: 古いOBS用プラグインが新しい構造を読み違える。テストでは検出できない(両側を同時にビルドするため)

## 2. 元のDawAudioStreamerと混線させない

- 守ること: 識別子はすべて元と別の値を使う。プロトコルの印(`KSTR`)、共有メモリ名(`KinuStream.OBS.v2`、POSIX名の接頭辞 `/kst-`)、送信ロックのファイル名、OBSのソースID(`kinu_stream_audio_source`)、プラグインID(`Kein`/`Knst`)、バンドルID(`com.keinoaza.kinustream*`)
- 必ず通す入口: `RingBuffer.h`、`NamedSharedMemory_posix.cpp`、`PluginProcessor.cpp`(`senderLockPath`)、`obs-plugin.cpp`、`plugins/*/CMakeLists.txt`
- 表示名を変える時(2026-11-04 に Kinu Stream → DAS Kai)は、画面・DAW・OBS に出る名前とバンドルのファイル名だけを変える。上の識別子と、保存データの形式名(`"KinuStream"`、`PluginProcessor.cpp`)は旧仮称のまま変えない。変えると、すでに DAW のプロジェクトや OBS のシーンに入れたものが外れる
- 破ると: 両方を入れたMacで、片方のOBSソースがもう片方の音を拾う、またはDAW側の「1台に1個」の判定が相手を巻き込む

## 3. 送信の処理は StreamSender だけに書く

- 守ること: サンプルレート変換・共有メモリへの書き込み・時計モデルの公開・補正値の公開は `das::audio::StreamSender` の中だけで行う。プラグインはパラメータを渡して呼ぶだけにする
- 必ず通す入口: `libs/audio-core/src/StreamSender.cpp`
- 破ると: シミュレーション(`sync` テスト)が実際のプラグインと違う処理を試すことになり、テストが通っても実機で壊れる

## 4. 時計モデルの「フレーム位置」は入力側の時間で数える

- 守ること: `StreamSender` が時計に渡すフレーム位置は「準備時の書き込み位置 + 入力サンプル数 × 48000 / DAWのサンプルレート」。実際に書き込んだフレーム数で数えない。変換器の遅れは `leadNs` に入れて受信側に伝える
- 必ず通す入口: `StreamSender::process`
- 破ると: 変換器の遅れ(44.1kHzで約19ミリ秒)の分だけ、OBSに渡す時刻がずれる。`sync` テストの「timestamps off from DAW time」で落ちる

## 5. OBSに渡す時刻を変える時は、70ミリ秒以上の切れ目と、1024サンプル以上のまとめ送りをセットにする

- 守ること: 手動補正・自動補正のオンオフ・待ち時間の変更・仕切り直しで時刻の並びが変わる時は、`needsTimestampBreak_` を立てる。受信側は、OBSが前の時刻の続きとみなせなくなるまで(差が80ミリ秒以上)出力を止め、再開の最初は3ブロック(1440サンプル)を1回で送る
- 必ず通す入口: `StreamReceiver::poll` の末尾(時刻の切れ目とまとめ送りの処理)
- 破ると: 70ミリ秒未満の変化はOBSに無視される(`TS_SMOOTHING_THRESHOLD`)。まとめ送りがないと、OBSが待ち時間を足さずに先頭を削り、補正が最大21ミリ秒足りなくなる(実エンジンで再現済み)。`sync` テストの「offset changes」と、`das_kai_obs_harness` の手動補正の行で分かる

## 6. 時計情報が読めないことと、送信側が止まったことを混ぜない

- 守ること: `readClock` が失敗した時は直前に読めた時計情報を使う。送信側が止まったかどうかは、書き込み位置が `kProducerTimeoutNs` の間動いていないことだけで判断する
- 必ず通す入口: `StreamReceiver::poll` の先頭(`lastClock_`)
- 破ると: 読み取りが書き込みと競合するたびに受信を最初からやり直し、約 86 ms 途切れる。`sync` テストの「clock read failure」で落ちる

## 7. 編集画面は共有メモリに触らない

- 守ること: 画面に出すOBSの状態は、音声処理の中で原子変数に写した値だけを読む
- 必ず通す入口: `DasSendProcessor::sendBlock` の末尾と `obsStatus()`
- 破ると: ホストが別スレッドで `releaseResources` を呼んだ瞬間に、解放済みのメモリを読む

## 変更の進め方

- 不具合を直す時は、直す前に失敗するテストを足す
- コミットの前に、テスト全部(`ctest`)を流す。時刻やOBSの扱いに触れたら `das_kai_obs_harness` も流す
- 外部リポジトリのコード・依存を取り込む時は、`~/.claude/skills/_shared/external-code-intake.md` の安全確認を先に行い、判定を報告してから動かす。取り込んだものはコミットIDで固定する
