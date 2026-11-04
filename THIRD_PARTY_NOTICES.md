# 第三者ソフトウェアと権利表示

> Kinu Stream(このフォーク)について: 上流 DawAudioStreamer の表示をそのまま引き継いでいます。Windows版とインストーラーはこのフォークに含まれないため、Inno Setup の節は削除しました。下の「licenses フォルダへ収録」「インストーラーへ収録」は上流の配布物についての記述で、このフォークは配布物を作りません(配布する時の注意は `LICENSE`)。変更点は `MODIFICATIONS.md` を参照してください。

配布ソースアーカイブには、バイナリのビルドに使用した次の固定revisionを収録します。

## JUCE

- 上流：<https://github.com/juce-framework/JUCE>
- revision：`3af3ce009f6a02f6fa651008fffb5b41743a9fab`（JUCE 8.0.10）
- ライセンス：JUCE Framework modulesはAGPLv3とJUCE商用ライセンスのデュアルライセンス

この無料配布版はJUCE商用ライセンスを前提とせず、JUCEをリンクするDAS Sendを
AGPL-3.0-only条件で提供します。対応ソースには開発者向け確認用のDAS Engineも含み、同じ条件で
提供します。JUCE自身の`LICENSE.md`と、使用するJUCE依存物の
ライセンスをインストーラーと対応ソースへ収録します。配布バイナリに組み込まれるHarfBuzz、
SheenBidi、libpng、Independent JPEG Group JPEG、zlibの表示も`licenses`へ収録します。

## Steinberg VST3 SDK

JUCE 8.0.10に同梱されたVST3 SDKヘッダーを、VST3プラグインのビルドに使用します。対応ソースと
`VST3-SDK-LICENSE.txt`を配布物へ収録します。

VST is a registered trademark of Steinberg Media Technologies GmbH.

## r8brain-free-src

- 上流：<https://github.com/avaneev/r8brain-free-src>
- revision：`8fff6f3db26f14a8f5e8fb871000613673db5753`
- ライセンス：MIT License
- Copyright (c) 2013-2026 Aleksey Vaneev

Sample rate converter designed by Aleksey Vaneev of Voxengo.

ライセンス全文は`LICENSES/MIT-r8brain.txt`に収録します。

## SIMDe

- 上流：<https://github.com/simd-everywhere/simde>
- revision：`71fd833d9666141edcd1d3c109a80e228303d8d7`（0.8.2）
- ライセンス：MIT License

macOS版OBSプラグインのApple Silicon対応にSIMDeヘッダーを使用します。
ライセンス全文はmacOS配布物の`licenses/SIMDe`に収録します。

## OBS Studio

- 上流：<https://github.com/obsproject/obs-studio>
- revision：`c17423ce05899ecb93f678601b3feaa8a469b180`（OBS Studio 32.1.2）
- ライセンス：GPL-2.0-or-later

OBS音声ソースはこのrevisionの公開APIヘッダーでビルドし、実行時に利用者がインストールした
OBS Studioへ動的リンクします。OBS Studio本体は同梱しません。参照revisionの`COPYING`と
対応ソースを配布物へ収録します。


## 製品名・商標

DawAudioStreamerは、OBS Project、Discord Inc.、Steinberg Media Technologies GmbH、
Elgato、VB-Audioの公式製品ではなく、各社からの承認、提携、保証を意味しません。
OBS、Discord、JUCE、VST、ASIO、Elgato、VB-Audioなどの名称および商標は各権利者に帰属します。
本ソフトウェアはDiscordの非公開API、非公開イベント、DLL注入を使用しません。

---

# Third-Party Software and Attributions

The distributed source archive includes the following pinned revisions used to build the binaries.

## JUCE

- Upstream: <https://github.com/juce-framework/JUCE>
- Revision: `3af3ce009f6a02f6fa651008fffb5b41743a9fab` (JUCE 8.0.10)
- License: JUCE Framework modules are dual-licensed under AGPLv3 and the JUCE commercial license

This free distribution does not rely on a JUCE commercial license; it provides DAS Send, which links JUCE, under the AGPL-3.0-only terms. The corresponding source also includes DAS Engine, used for developer verification, provided under the same terms. JUCE's own `LICENSE.md` and the licenses of the JUCE dependencies in use are included in the installer and the corresponding source. Attributions for HarfBuzz, SheenBidi, libpng, Independent JPEG Group JPEG, and zlib, which are embedded in the distributed binaries, are also included under `licenses`.

## Steinberg VST3 SDK

The VST3 SDK headers bundled with JUCE 8.0.10 are used to build the VST3 plugin. The corresponding source and `VST3-SDK-LICENSE.txt` are included in the distribution.

VST is a registered trademark of Steinberg Media Technologies GmbH.

## r8brain-free-src

- Upstream: <https://github.com/avaneev/r8brain-free-src>
- Revision: `8fff6f3db26f14a8f5e8fb871000613673db5753`
- License: MIT License
- Copyright (c) 2013-2026 Aleksey Vaneev

Sample rate converter designed by Aleksey Vaneev of Voxengo.

The full license text is included in `LICENSES/MIT-r8brain.txt`.

## SIMDe

- Upstream: <https://github.com/simd-everywhere/simde>
- Revision: `71fd833d9666141edcd1d3c109a80e228303d8d7` (0.8.2)
- License: MIT License

SIMDe headers are used for Apple Silicon support in the macOS OBS plugin. The full license text is included in `licenses/SIMDe` of the macOS distribution.

## OBS Studio

- Upstream: <https://github.com/obsproject/obs-studio>
- Revision: `c17423ce05899ecb93f678601b3feaa8a469b180` (OBS Studio 32.1.2)
- License: GPL-2.0-or-later

The OBS audio source is built against the public API headers of this revision and dynamically links at runtime to the OBS Studio the user has installed. OBS Studio itself is not bundled. The `COPYING` of the referenced revision and the corresponding source are included in the distribution.


## Product Names and Trademarks

DawAudioStreamer is not an official product of OBS Project, Discord Inc., Steinberg Media Technologies GmbH, Elgato, or VB-Audio, and does not imply any endorsement, affiliation, or warranty from them. Names and trademarks such as OBS, Discord, JUCE, VST, ASIO, Elgato, and VB-Audio belong to their respective owners. This software does not use Discord's private APIs, private events, or DLL injection.
