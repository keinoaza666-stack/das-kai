DAS Kai — Windows
=================
Windows 10 / 11 の 64 ビット版に対応しています。OBS は 32 系に対応しています。

English instructions follow the Japanese section.

DAS Kai は、DAW の音を仮想オーディオデバイスなしで OBS に送るプラグインです。
DawAudioStreamer(yoruhinot 作)を改造したもので、元のソフトとは別物です。
DAS Kai についての問い合わせは、元の作者ではなく下の配布元へお願いします。

入れ方
------
1. DAW と OBS を終了します。
2. 「DAS-Kai-<版>-Windows-x64-Setup.exe」を開き、案内に従って進めます。
   「このアプリがデバイスに変更を加えることを許可しますか?」と出たら[はい]を押します。
   初めて開く時に Windows に止められたら、下の「Windows に止められた時」を見てください。
3. 終わったら、DAW と OBS を開き直します。
4. DAW のマスターの最後に「DAS Kai」を1個挿します(VST3)。
5. OBS の「ソース」の[＋]から「DAS Kai（DAW）」を追加します。設定は不要です。
6. DAW を再生し、プラグイン画面の OBS 欄が「受信中」になることを確かめます。

元の DawAudioStreamer(DAS Send)が入っていても、そのまま残ります。両方入れたままで構いません。
新しい版は、新しいインストーラーをそのまま実行すれば入れ替わります。

Windows に止められた時
----------------------
DAS Kai のインストーラーには、配布元を証明する署名がありません。
そのため、初めて開く時に「Windows によって PC が保護されました」と出ることがあります。
[詳細情報]を押し、[実行]を押すと進めます。
この手順は、下の配布元から落としたファイルに限ってください。
ウイルス対策ソフトが警告した時は続けず、配布元でお知らせください。

入る場所
--------
C:\Program Files\Common Files\VST3\DAS Kai.vst3          (DAW 用のプラグイン)
C:\ProgramData\obs-studio\plugins\das-kai-obs              (OBS 用のプラグイン)
C:\Program Files\Keinoaza\DAS Kai                           (このファイル・ライセンス・アンインストーラー)

DAW に出ない時は、DAW を開き直して VST3 を再スキャンしてください。
OBS に出ない時は、OBS を開き直してください。

削除
----
DAW と OBS を終了し、「設定」→「アプリ」→「インストールされているアプリ」で
「DAS Kai」を選んで[アンインストール]を押します。
DAW のプロジェクトと OBS のシーンは残ります。

使い方・困った時: https://github.com/keinoaza666-stack/das-kai#readme
配布元:          https://github.com/keinoaza666-stack/das-kai/releases
知らせる時は、Windows・DAW・OBS の版を添えてください。

---

DAS Kai — Windows
=================
Windows 10 / 11, 64-bit. Works with OBS 32.

DAS Kai sends your DAW's audio to OBS without a virtual audio device. It is a
modified version of DawAudioStreamer by yoruhinot and a separate product: send
questions about DAS Kai to the page below, not to the original author.

Install
-------
1. Close your DAW and OBS.
2. Open "DAS-Kai-<version>-Windows-x64-Setup.exe" and follow the steps.
   Click Yes when Windows asks whether to allow the app to make changes.
   If Windows blocks it the first time, see "If Windows blocks it" below.
3. When it finishes, reopen your DAW and OBS.
4. Insert one "DAS Kai" at the end of your DAW master (VST3).
5. In OBS Sources, click + and add "DAS Kai (DAW)". No settings are needed.
6. Play your DAW and check that the OBS row in the plugin window shows OK.

An installed original DawAudioStreamer (DAS Send) is left as it is; both can
stay installed. To update, run the newer installer.

If Windows blocks it
--------------------
The installer is not signed, so Windows may say "Windows protected your PC" the
first time. Click More info, then Run anyway. Only do this for a file
downloaded from the page below. Stop if your antivirus warns, and report it there.

Installed to
------------
C:\Program Files\Common Files\VST3\DAS Kai.vst3          (plugin for your DAW)
C:\ProgramData\obs-studio\plugins\das-kai-obs              (plugin for OBS)
C:\Program Files\Keinoaza\DAS Kai                           (this file, licenses, uninstaller)

If DAS Kai is missing in your DAW, reopen it and rescan VST3 plugins. If it is
missing in OBS, reopen OBS.

Uninstall
---------
Close your DAW and OBS, then in Settings > Apps > Installed apps choose
"DAS Kai" and Uninstall. DAW projects and OBS scenes are kept.

Guide & help: https://github.com/keinoaza666-stack/das-kai#readme
Downloads:    https://github.com/keinoaza666-stack/das-kai/releases
When reporting, include your Windows, DAW and OBS versions.
