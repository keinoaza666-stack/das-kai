// SPDX-License-Identifier: MIT
// Added for DAS Kai (2026-11-05). See MODIFICATIONS.md.
#pragma once

#include <cstdint>

namespace das::transport {

// The clock OBS stamps audio with (os_gettime_ns()), read the same way OBS
// reads it, so a DAW-side timestamp and an OBS-side one can be compared
// directly: CLOCK_UPTIME_RAW on macOS, QueryPerformanceCounter scaled to
// nanoseconds on Windows. Safe to call from the audio thread.
[[nodiscard]] std::int64_t monotonicNowNs() noexcept;

} // namespace das::transport
