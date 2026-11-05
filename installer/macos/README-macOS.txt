DAS Kai — macOS
===============
macOS 13 以降・Apple Silicon(M シリーズの Mac)専用です。Intel の Mac では動きません。
OBS は 32 系に対応しています。

English instructions follow the Japanese section.

DAS Kai は、DAW の音を仮想オーディオデバイスなしで OBS に送るプラグインです。
DawAudioStreamer(yoruhinot 作)を改造したもので、元のソフトとは別物です。
DAS Kai についての問い合わせは、元の作者ではなく下の配布元へお願いします。

入れ方
------
1. ZIP 全体を展開し、DAW と OBS を終了します。
2. 「DAS Kai Setup.app」を開き、[インストール / 更新]を押します。
   初めて開く時に macOS に止められたら、下の「macOS に止められた時」を見てください。
3. 「インストール完了」と出たら、DAW と OBS を開き直します。
4. DAW のマスターの最後に「DAS Kai」を1個挿します(VST3 か AU)。
5. OBS の「ソース」の[＋]から「DAS Kai（DAW）」を追加します。設定は不要です。
6. DAW を再生し、プラグイン画面の OBS 欄が「受信中」になることを確かめます。

更新とアンインストールにも Setup.app を使うので、残しておいてください。
ターミナルの操作も、管理者のパスワードも要りません。
元の DawAudioStreamer(DAS Send)が入っていても、そのまま残ります。両方入れたままで構いません。

macOS に止められた時
--------------------
DAS Kai は Apple の公証(Apple がアプリを検査して登録する手続き)を受けていません。
そのため、初めて開く時に macOS が止めます。
1. 一度 Setup.app を開こうとします。「開けません」と出たら[完了]を押します。
2. [システム設定]→[プライバシーとセキュリティ]を開き、下の方にある
   「"DAS Kai Setup"」の横の[このまま開く]を押します。
3. 確認が出たら[開く]を押します。Mac のログインパスワードを求められることがあります。
DAW がプラグインを読み込む時に同じように止められた場合も、同じ画面で[このまま開く]を押します。
この手順は、下の配布元から落とした ZIP に限ってください。
「マルウェア」や「壊れている」という警告が出た時は続けず、配布元でお知らせください。
組織で管理されている Mac では、管理者の許可が要ることがあります。
https://support.apple.com/ja-jp/102445

完了しなかった時
----------------
「インストール完了」と出るまでは、DAW で探す手順に進まないでください。
・書き込めない: [Finder で表示]から[情報を見る]→[共有とアクセス権]を確かめます。
  自分のアカウントに書き込みの権限がなければ、その Mac の管理者に相談してください。
  Setup が勝手に権限を変えたり、管理者の権限を求めたりすることはありません。
・ファイルが足りない/確かめられない: ZIP をダウンロードし直し、全体を展開します。
・元に戻すためのファイルが残っている: 消さずに[詳細をコピー]して、お知らせください。
・後片付けの警告: 入れる/消す処理は終わっていますが、一時ファイルが残っています。
  もう一度実行する前に、警告の詳細をお知らせください。

入る場所(このユーザーだけ)
--------------------------
~/Library/Audio/Plug-Ins/VST3/DAS Kai.vst3
~/Library/Audio/Plug-Ins/Components/DAS Kai.component
~/Library/Application Support/obs-studio/plugins/das-kai-obs.plugin

DAW に出ない時は、DAW を開き直して VST3/AU を再スキャンしてください。
OBS に出ない時は、OBS を開き直してください。
macOS のセキュリティ機能は切らないでください。

削除
----
DAW と OBS を終了し、Setup.app の[アンインストール…]を押します。
「アンインストール完了」と出れば終わりです。DAW のプロジェクト・OBS のシーン・音声の設定は残ります。
その後、Setup.app と ZIP はゴミ箱へ移して構いません。

使い方・困った時: https://github.com/keinoaza666-stack/das-kai#readme
配布元:          https://github.com/keinoaza666-stack/das-kai/releases
知らせる時は、macOS・DAW・OBS の版を添えてください。
コピーした詳細に個人の名前やパスが入っていないか、送る前に確かめてください。

---

DAS Kai — macOS
===============
macOS 13 or later, Apple Silicon (M-series Macs) only. It does not run on Intel Macs.
Works with OBS 32.

DAS Kai sends your DAW's audio to OBS without a virtual audio device. It is a
modified version of DawAudioStreamer by yoruhinot and a separate product: send
questions about DAS Kai to the page below, not to the original author.

Install
-------
1. Extract the whole ZIP and close your DAW and OBS.
2. Open "DAS Kai Setup.app" and click Install / Update.
   If macOS blocks it the first time, see "If macOS blocks opening" below.
3. When it says "Installation complete", reopen your DAW and OBS.
4. Insert one "DAS Kai" at the end of your DAW master (VST3 or AU).
5. In OBS Sources, click + and add "DAS Kai (DAW)". No settings are needed.
6. Play your DAW and check that the OBS row in the plugin window shows OK.

Keep Setup.app: it also updates and uninstalls. No Terminal commands and no
administrator password are needed. An installed original DawAudioStreamer
(DAS Send) is left as it is; both can stay installed.

Setup opens in Japanese; click English at the top to switch.

If macOS blocks opening
-----------------------
DAS Kai is not notarized by Apple, so macOS blocks it the first time.
1. Try to open Setup.app once. If it says it cannot be opened, click Done.
2. Open System Settings > Privacy & Security and click Open Anyway next to
   "DAS Kai Setup" near the bottom.
3. Confirm with Open. macOS may ask for your login password.
If your DAW is blocked from loading the plugin, use Open Anyway on the same page.
Only do this for a ZIP downloaded from the page below. Do not continue after a
malware or damaged-app warning; report it there instead. Managed Macs may need
their administrator's approval.
https://support.apple.com/102445

If Setup does not complete
--------------------------
Do not look for the plugin in your DAW until Setup says "Installation complete".
- Cannot write: click Show in Finder, then Get Info > Sharing & Permissions. If
  your account cannot write there, ask the Mac's administrator. Setup never
  changes permissions or asks for administrator access itself.
- Missing or unverifiable files: download the ZIP again and extract all of it.
- Recovery files remain: do not delete them. Copy the details and report them.
- Cleanup warning: installing or removing finished, but temporary files remain.
  Report the details before running Setup again.

Installed for this user only
----------------------------
~/Library/Audio/Plug-Ins/VST3/DAS Kai.vst3
~/Library/Audio/Plug-Ins/Components/DAS Kai.component
~/Library/Application Support/obs-studio/plugins/das-kai-obs.plugin

If DAS Kai is missing in your DAW, reopen it and rescan VST3/AU plugins. If it
is missing in OBS, reopen OBS. Do not turn off macOS security features.

Uninstall
---------
Close your DAW and OBS, open Setup.app and click Uninstall…. Wait for
"Uninstall complete". DAW projects, OBS scenes and audio settings are kept.
Setup.app and the ZIP can then go to the Trash.

Guide & help: https://github.com/keinoaza666-stack/das-kai#readme
Downloads:    https://github.com/keinoaza666-stack/das-kai/releases
When reporting, include your macOS, DAW and OBS versions, and check copied
details for personal names or paths first.
