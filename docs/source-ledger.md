# Source Ledger

## Scope

- Topic: OBS (libobs) audio timestamp handling and r8brain latency, as relied on by DAS Kai's sync design
- Date checked: 2026-11-04
- Research owner: Claude (DAS Kai fork)
- All sources are source code at pinned commits, read locally from the FetchContent checkout (`build/*/_deps/`).

## Sources

| Source ID | Title | URL / Path | Publisher | Access Date | Locator | Used For | Verification Status |
|---|---|---|---|---|---|---|---|
| S1 | libobs obs-source.c | https://github.com/obsproject/obs-studio/blob/c17423ce05899ecb93f678601b3feaa8a469b180/libobs/obs-source.c | OBS Project | 2026-11-04 | line 1452; `source_output_audio_data` | 70 ms smoothing of async audio timestamps | adopted |
| S2 | libobs obs-internal.h | .../libobs/obs-internal.h (same commit) | OBS Project | 2026-11-04 | line 1059 | timestamps within 2 s of now are used directly | adopted |
| S3 | libobs obs-audio.c | .../libobs/obs-audio.c (same commit) | OBS Project | 2026-11-04 | `add_audio_buffering` (log text line 393), `audio_buffer_insufficient` line 424 | late audio grows global buffering; sources holding < 1 tick are excluded | adopted |
| S4 | libobs obs.c | .../libobs/obs.c (same commit) | OBS Project | 2026-11-04 | line 1617 | default maximum buffering 45 ticks | adopted |
| S5 | libobs media-io/audio-io.h | .../libobs/media-io/audio-io.h (same commit) | OBS Project | 2026-11-04 | line 31 | mixing tick = 1024 frames | adopted |
| S6 | libobs util/platform-cocoa.m | .../libobs/util/platform-cocoa.m (same commit) | OBS Project | 2026-11-04 | line 43 | `os_gettime_ns` = CLOCK_UPTIME_RAW on macOS | adopted |
| S7 | r8brain-free-src README | https://github.com/avaneev/r8brain-free-src/blob/8fff6f3db26f14a8f5e8fb871000613673db5753/README.md | Aleksey Vaneev | 2026-11-04 | lines 138-152 | resampler removes initial latency; latency via `getInputRequiredForOutput()` | adopted |
| S8 | Headless libobs run | `docs/obs-harness-*.txt` (this repository) | measured here | 2026-11-04 | log lines "adding ... audio buffering", "exceeded TS_SMOOTHING_THRESHOLD" | behaviour of the installed OBS 32.2.2 engine matches S1/S3 | adopted |

## Adopted Evidence

### S1

- Claim supported: an async audio packet whose timestamp is less than 70 ms from the expected continuation is moved onto the continuation; larger differences are used as given.
- Exact locator: `#define TS_SMOOTHING_THRESHOLD 70000000ULL` (line 1452); in `source_output_audio_data`, `else if (diff < TS_SMOOTHING_THRESHOLD) { ... in.timestamp = source->next_audio_ts_min; }`
- Numeric values: 70,000,000 ns
- Caveats: applies per source; the continuation resets when the source is recreated.

### S2

- Claim supported: timestamps within 2 s of `os_gettime_ns()` are treated as direct (no timing adjustment).
- Exact locator: `#define MAX_TS_VAR 2000000000ULL` (line 1059); `if (uint64_diff(in.timestamp, os_time) < MAX_TS_VAR) { ... using_direct_ts = true; }`
- Numeric values: 2,000,000,000 ns. DAS Kai clamps its total offset to 1.2 s to stay inside it.

### S3

- Claim supported: when a source's audio is older than the mixing window, libobs adds buffering and logs it; a source with fewer than one tick of frames buffered is marked pending and left out of that decision.
- Exact locator: log text "adding %d milliseconds of audio buffering, total audio buffering is now %d milliseconds" (line 393); `audio_buffer_insufficient` (line 424): `if (source->audio_input_buf[0].size < size) { source->audio_pending = true; return true; }`
- Caveats: buffering only grows during an OBS session.

### S4 / S5

- Numeric values: `audio->max_buffering_ticks = 45;` and `#define AUDIO_OUTPUT_FRAMES 1024` -> 45 x 1024 / 48000 = 960 ms (the OBS log of the harness run prints "max buffering: 960 milliseconds").

### S6

- Claim supported: OBS timestamps on macOS use CLOCK_UPTIME_RAW; the plug-in uses the same clock (`clock_gettime_nsec_np(CLOCK_UPTIME_RAW)`).
- Exact locator: `return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);` (line 43)

### S7

- Claim supported: "the resampler removes the initial processing latency automatically"; "The overall input-to-output delay or latency can be obtained via the `CDSPResampler::getInputRequiredForOutput()` function."
- Measured here: 44.1 -> 48 kHz with `CDSPResampler24` reports a lead of 30.4 ms for 512-sample blocks (11.6 ms block + about 19 ms converter).

## Rejected Or Background Sources

| Source ID | Reason |
|---|---|
| — | none |
