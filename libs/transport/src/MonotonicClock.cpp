// SPDX-License-Identifier: MIT
// Added for DAS Kai (2026-11-05). See MODIFICATIONS.md.
#include <das/transport/MonotonicClock.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif
#elif defined(__APPLE__)
#include <time.h>
#else
#error "DAS Kai supports macOS and Windows only"
#endif

namespace das::transport {

#if defined(_WIN32)
namespace {

// The same arithmetic as OBS's util_mul_div64 (libobs/util/util_uint64.h),
// which os_gettime_ns() uses, so both sides round to the same nanosecond.
std::uint64_t mulDiv64(const std::uint64_t value, const std::uint64_t mul,
                       const std::uint64_t div) noexcept {
#if defined(_MSC_VER) && defined(_M_X64) && (_MSC_VER >= 1920)
  unsigned __int64 high {};
  const unsigned __int64 low = _umul128(value, mul, &high);
  unsigned __int64 remainder {};
  return _udiv128(high, low, div, &remainder);
#else
  const auto remainder = value % div;
  return (value / div) * mul + (remainder * mul) / div;
#endif
}

// Read once at load time, not on the audio thread's first call.
const std::uint64_t counterFrequency = [] {
  LARGE_INTEGER frequency {};
  QueryPerformanceFrequency(&frequency);
  return static_cast<std::uint64_t>(frequency.QuadPart);
}();

} // namespace

std::int64_t monotonicNowNs() noexcept {
  LARGE_INTEGER counter {};
  QueryPerformanceCounter(&counter);
  return static_cast<std::int64_t>(
      mulDiv64(static_cast<std::uint64_t>(counter.QuadPart), 1'000'000'000ULL, counterFrequency));
}
#else
std::int64_t monotonicNowNs() noexcept {
  return static_cast<std::int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
}
#endif

} // namespace das::transport
