# DAS Kai

DAW の音を、仮想オーディオデバイスなしで OBS へ。

DAS Kai は、DAW のマスターの音を OBS へ直接送る配信用プラグインです。オーディオインターフェースとパソコンのクロックのずれを吸収するので、長時間配信しても、DAW の音が OBS のほかの音から少しずつずれていくのを防ぎます。歌とオケのずれも、プラグインの画面で合わせられます。

DawAudioStreamer(yoruhinot 作)を改造したもので、元のソフトとは別物です。両方入れたままでも使えます。

## ダウンロード

| パソコン | ファイル |
|---|---|
| **Mac**(Apple Silicon・macOS 13 以降) | [DAS-Kai-0.2.0-macOS-AppleSilicon.zip](https://github.com/keinoaza666-stack/das-kai/releases/download/v0.2.0/DAS-Kai-0.2.0-macOS-AppleSilicon.zip) |
| **Windows**(10 / 11・64 ビット) | [DAS-Kai-0.2.0-Windows-x64-Setup.exe](https://github.com/keinoaza666-stack/das-kai/releases/download/v0.2.0/DAS-Kai-0.2.0-Windows-x64-Setup.exe) |

OBS Studio 32 で使えます。ほかの版は [Releases](https://github.com/keinoaza666-stack/das-kai/releases) にあります。

## 使い方

### 1. インストール

**Mac**

1. DAW と OBS を終了します。
2. ダウンロードした ZIP をダブルクリックして展開し、「DAS Kai Setup.app」を開きます。
3. [インストール / 更新]を押し、「インストール完了」と出たら[完了]を押します。

初めて開く時は、macOS に「開けません」と止められます。[完了]を押してから「システム設定」→「プライバシーとセキュリティ」を開き、下の方にある「DAS Kai Setup」の横の[このまま開く]を押します。確認が出たら[開く]を押します(パスワードを求められることがあります)。この許可は最初の1回だけです。

**Windows**

1. DAW と OBS を終了します。
2. ダウンロードしたインストーラーを開き、案内に従って進めます。「このアプリがデバイスに変更を加えることを許可しますか?」と出たら[はい]を押します。

「Windows によって PC が保護されました」と出た時は、[詳細情報]を押してから[実行]を押します。

### 2. DAW

DAW を開き、マスターの最後に「DAS Kai」を1個挿します(Mac は VST3 か AU、Windows は VST3)。見つからない時は、DAW でプラグインを再スキャンします。

### 3. OBS

1. OBS を開き、「ソース」の[＋]から「DAS Kai（DAW）」を追加します。設定は要りません。
2. DAW を再生し、DAS Kai の画面の OBS 欄が「受信中」になることを確かめます。

これで、DAW の音が OBS に届きます。

### 4. 歌とオケのずれを合わせる(必要な時だけ)

ブラウザなどで流したオケを聞きながら歌うと、配信では歌がオケより少し遅れて聞こえます。その分を、DAS Kai の「手動補正」で DAW の音を早めて合わせます。聞き方と機材が同じなら、一度測った値を使い回せます。

1. OBS の録画を「トラックを分けて保存」にし、オケと DAS Kai を別のトラックにします。
2. オケの拍に合わせて、マイクの前で手を数回たたいて録画します。
3. 録画を DAW などに読み込み、オケの拍と手拍子のずれをミリ秒で測ります。
4. 手拍子が遅れていれば、その値を「手動補正」に入れます。

## プラグイン画面の見方

画面は日本語で開きます。右上の「日本語 ・ EN」で英語に切り替えられます。

| 表示 | 意味と対処 |
|---|---|
| OBS 欄「受信中」 | OBS が音を受け取っています |
| OBS 欄「待機中」 | OBS に「DAS Kai（DAW）」がありません。OBS のソースに追加します |
| OBS 欄「1個のみ」 | DAS Kai が2個以上挿さっています。1個だけにします |
| OBS 欄「バイパス」 | DAS Kai がバイパスされています。OBS には無音が届きます |
| OBS 欄「書き出し中」 | DAW が書き出し中です。書き出しの間は OBS に送りません |
| OBS 欄「!」 | OBS、DAW の順に開き直します |
| クロックのずれ(緑の数字) | ふつうの範囲です。0 でなくて構いません |
| クロックのずれ(黄か赤の数字) | 下の「クロックのずれが黄や赤の時」を見ます |
| 「クロックを合わせています…」 | 起動した直後などに数秒〜数十秒出ます。待てば数字に変わります |
| タイミング補正 | OBS の中で DAW の音を何ミリ秒早めているか(自動 + 手動) |
| 自動補正 | DAW から OBS へ音を渡す時の待ち時間を、自動で差し引きます。ふつうはオンのまま使います |
| 手動補正 | -100〜+100 ミリ秒。プラスで DAW の音を早めます。Shift を押しながら動かすと細かく動き、ダブルクリックで 0 に戻ります |

補正を変えると、OBS の音が一瞬(0.1 秒ほど)途切れます。配信を始める前に合わせておきます。

## 困った時

### 音が二重に聞こえる

- DAS Kai はマスターに1個だけ挿します。
- OBS で、同じ音をデスクトップ音声などからも取り込んでいないか確かめます。
- 元の DawAudioStreamer(DAS Send)も挿している時は、どちらか一方だけにします。

### クロックのずれが黄や赤の時

数字が一つの値で落ち着いていて、OBS の音も途切れないなら、そのまま使えます。オーディオインターフェースのクロックが本当にずれているだけです。外部のクロック(ワードクロックなど)に合わせる設定になっていて、その必要がなければ「内部(Internal)」に戻します。

数字が大きく上下する時や、OBS の音が途切れる時は、次を試します。

1. DAS Kai を一度無効にして、有効に戻します(バイパスではなく無効化です)。クロックを最初から合わせ直します。
2. DAW の先読みの設定を弱めます。Cubase は「スタジオ」→「スタジオ設定」→「オーディオシステム」の ASIO-Guard、Studio One は「オーディオ設定」→「処理」のドロップアウト保護です。DAW の負荷が上がるので、様子を見ながら変えます。

### その他

- OBS のプレビューとモニター音は、少しずれて見えることがあります。配信と録画では揃っています。
- 音が途切れた時は、OBS のログ(「ヘルプ」→「ログファイル」)の `[DAS Kai]` で始まる行に、何が起きたかが残ります。

## 対応環境

- Mac: Apple Silicon、macOS 13 以降、VST3 か AU に対応した DAW
- Windows: Windows 10 / 11 の 64 ビット版、VST3 に対応した 64 ビットの DAW
- OBS Studio 32

## アンインストール

- Mac: DAW と OBS を終了し、DAS Kai Setup.app を開いて[アンインストール…]を押します。
- Windows: DAW と OBS を終了し、「設定」→「アプリ」→「インストールされているアプリ」で「DAS Kai」をアンインストールします。

DAW のプロジェクトと OBS のシーンは残ります。インストールされる場所は次のとおりです。

- Mac: `~/Library/Audio/Plug-Ins/VST3/DAS Kai.vst3`、`~/Library/Audio/Plug-Ins/Components/DAS Kai.component`、`~/Library/Application Support/obs-studio/plugins/das-kai-obs.plugin`
- Windows: `C:\Program Files\Common Files\VST3\DAS Kai.vst3`、`C:\ProgramData\obs-studio\plugins\das-kai-obs`、`C:\Program Files\Keinoaza\DAS Kai`

## ライセンス

DAS Kai は [DawAudioStreamer](https://github.com/yoruhinot/DawAudioStreamer)(yoruhinot 作)の改変版です。ライセンスは [LICENSE](LICENSE)、第三者ソフトウェアの表示は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)、元のソフトからの変更点は [MODIFICATIONS.md](MODIFICATIONS.md) を参照してください。

ソースからビルドする時の手順は [verify-map.md](verify-map.md) にあります。
