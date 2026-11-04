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
