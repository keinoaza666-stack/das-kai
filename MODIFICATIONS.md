# Modifications

DAS Kai is a modified version of DawAudioStreamer by yoruhinot
(https://github.com/yoruhinot/DawAudioStreamer), forked from commit
`576c5b5c7fd395a3d4829a5708decd9b06301586` (release v0.4.5, 2026-10-14).
AGPL-3.0 section 5(a) / GPL-3.0 section 5(a): this file records that the work
was modified and when.

## 2026-11-04 (Keinoaza) — 0.1.0 (working name "Kinu Stream")

- Renamed the product and every identifier so it installs next to
  DawAudioStreamer without sharing anything with it: plug-in name, vendor,
  bundle IDs, AU/VST3 codes, OBS source ID, shared-memory names, protocol magic.
- Transport protocol v2: the ring always writes (overwrites when nobody reads),
  carries a clock model (seqlock), the user's sync controls and the OBS side's
  status. Added absolute-range reads.
- Added `libs/sync`: clock estimator (weighted least-squares line through
  block timestamps), Kaiser-windowed sinc interpolator, and `StreamReceiver`,
  which absorbs DAW/system clock drift by adaptive resampling and gives OBS
  timestamps of when the DAW processed the audio.
- Added `StreamSender` to `libs/audio-core`; the VST3/AU plug-in now sends
  through it. Bypass sends silence instead of stopping; offline exports are
  not sent.
- Plug-in window: the Discord card is replaced by a sync panel (total shift,
  automatic part, manual offset slider, clock drift).
- OBS plug-in: reads through `StreamReceiver`.
- macOS only: removed the Windows code, Discord bridge, developer engine app,
  installers, website and CI workflows.
- Build: enabled the C language for the Makefile generator; deployment target
  and architecture set before `project()`.
- Tests: adapted transport tests to the new ring; added the sync simulation,
  a real-time VST3 smoke test through `StreamReceiver`, and a headless libobs
  harness.

## 2026-11-04 (Keinoaza) — fixes after review

- OBS side: a clock read that loses the race with the writer reuses the last
  good snapshot instead of restarting the stream (was an ~86 ms dropout).
- Total shift placed earlier in OBS is capped at 850 ms; the manual offset
  range is -500..+500 ms (OBS buffers at most 960 ms).
- Plug-in window shows the shift OBS actually applies and "APPLYING..." until
  a new setting takes effect; "1 per user" instead of "1 per Mac".
- Per-file modification notices, `libs/sync/LICENSE`, distribution notes in
  `LICENSE`; the plug-in no longer names the upstream URL as its website.

## 2026-11-04 (Keinoaza) — renamed to DAS Kai

- Product name changed from the working name "Kinu Stream" to "DAS Kai"
  (plug-in, OBS source, bundle file names, OBS module `das-kai-obs`).
  Internal identifiers keep the old working name so existing DAW projects and
  OBS scenes stay attached (see dev-rules.md, rule 2).
- Plug-in window: Japanese labels by default, switchable to English; the
  choice is saved with the DAW project.

## 2026-11-04 (Keinoaza) — look and controls

- Plug-in window: own colours ("sumi and jade") and macOS system fonts
  (Hiragino Maru Gothic ProN, Menlo) to set it apart from upstream; the
  language switch is a small "日本語 ・ EN" text toggle. Layout unchanged.
- Manual offset range -100..+100 ms in 0.1 ms steps; Shift-drag for fine
  adjustment now works (the swap-mode flag was off).
- Wording: "timing compensation", "DAW audio earlier / later".

## 2026-11-05 (Keinoaza) — clock estimator for hosts that process ahead

- Clock estimator rebuilt: one point per segment of at least 250 ms, taken
  from the median of the calls that start a burst; steps in the host's timing
  move the line instead of tilting the slope; the slope is blended with a
  prior and relearnt at the limit (Cubase with ASIO-Guard pinned the old
  estimator at its +2004 ppm limit).
- `StreamSender` starts a new epoch only when the model re-anchors.
- `StreamReceiver`: producer timeout adapts to the host's write gaps; late
  audio grows the delay only once newer audio confirms it.
- OBS plug-in: rate-limited `[DAS Kai]` lines in the OBS log.
- Plug-in window: clock drift coloured by range; "clock" wording.
- Tests and tools: host call-pattern simulations, a real-time host, a live
  monitor and a trace replayer.

## 2026-11-05 (Keinoaza) — Setup app and distribution ZIP

- Brought back upstream's macOS Setup app (`installer/macos`, removed at the
  fork) and adapted it: DAS Kai bundle names and texts, Japanese first, its
  own lock and work-folder names so it never blocks or is blocked by the
  original DawAudioStreamer Setup, help links to the DAS Kai repository.
- `cmake/CreateMacPackage.cmake` (from upstream's
  `CreateMacPreviewPackage.cmake`): Apple Silicon only, takes the build
  outputs directly, refuses uncommitted sources, adds the license texts of the
  libraries JUCE embeds, zips without extended attributes, and runs Setup's
  tests on the extracted ZIP.
- Tests: upstream's Setup tests (`tests/setup_tests.swift`) plus a check that
  the original's files and work folders are left alone, and
  `tests/setup_contract.cmake`.
