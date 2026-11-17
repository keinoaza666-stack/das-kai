// SPDX-License-Identifier: MIT
// Modified for DAS Kai (2026-11-04): always-write ring, own test name.
// 2026-11-17: the producer can run as another CPU architecture (a DAW opened
// with Rosetta is x86_64 while OBS is arm64), and the clock model and controls
// cross the boundary too. DAS_IPC_PRODUCER_ARCH=x86_64 and
// DAS_IPC_CONSUMER_ARCH=arm64 start each side through /usr/bin/arch (a child
// started without it runs as its parent does, whatever that is).
#include <das/transport/NamedSharedMemory.h>
#include <das/transport/RingBuffer.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr std::uint32_t kCapacity = 256;
constexpr std::uint16_t kChannels = 2;
constexpr std::uint32_t kTotalFrames = 20000;

// Values with a fraction, a sign and more than 32 bits, so a field read at the
// wrong place or width cannot match by accident.
constexpr das::transport::ClockSnapshot kClock {7, 1234.5, 987654321012345LL, 20833.25};
constexpr das::transport::ProducerControl kControl {false, -45'000'000LL, 19'000'000LL};

std::wstring widen(const std::string_view text) {
  return {text.begin(), text.end()};
}

int runProducer(const std::wstring& name) {
  const auto bytes = das::transport::requiredBytes(kCapacity, kChannels);
  auto memory = das::transport::NamedSharedMemory::open(name, bytes);
  das::transport::RingBuffer ring(memory.storage());
  if (!memory.isOpen() || !ring.isCompatible()) return 10;
  ring.publishClock(kClock);
  ring.setProducerControl(kControl);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  std::array<float, kChannels> frame {};
  for (std::uint32_t sequence = 0; sequence < kTotalFrames;) {
    frame.fill(static_cast<float>(sequence));
    // The ring overwrites when full; wait for space so this stays lossless.
    if (ring.availableToWrite() > 0 && ring.write(frame, 1) == 1) ++sequence;
    else std::this_thread::yield();
    if (std::chrono::steady_clock::now() >= deadline) return 11;
  }
  return 0;
}

int runConsumer(const std::wstring& name) {
  const auto bytes = das::transport::requiredBytes(kCapacity, kChannels);
  auto memory = das::transport::NamedSharedMemory::open(name, bytes);
  das::transport::RingBuffer ring(memory.storage());
  if (!memory.isOpen() || !ring.isCompatible()) return 20;

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  std::array<float, kChannels> frame {};
  for (std::uint32_t sequence = 0; sequence < kTotalFrames;) {
    if (ring.read(frame, 1) == 0) {
      std::this_thread::yield();
    } else {
      for (const auto sample : frame)
        if (sample != static_cast<float>(sequence)) return 21;
      ++sequence;
    }
    if (std::chrono::steady_clock::now() >= deadline) return 22;
  }
  // Published before the first frame, so both are complete by now.
  das::transport::ClockSnapshot clock;
  if (!ring.readClock(clock) || clock.epoch != kClock.epoch ||
      clock.anchorFrame != kClock.anchorFrame || clock.anchorTimeNs != kClock.anchorTimeNs ||
      clock.periodNs != kClock.periodNs)
    return 23;
  const auto control = ring.producerControl();
  if (control.autoCompensation != kControl.autoCompensation ||
      control.manualOffsetNs != kControl.manualOffsetNs || control.leadNs != kControl.leadNs)
    return 24;
  return 0;
}

// architecture: nullptr runs the child as this process does; otherwise the name
// /usr/bin/arch takes (macOS), e.g. "x86_64" for the Rosetta slice.
pid_t startChild(const char* executable, const char* role, const std::string& name,
                 const char* architecture = nullptr) {
  const auto process = fork();
  if (process == 0) {
    if (architecture != nullptr && *architecture != '\0') {
      const auto option = std::string("-") + architecture;
      execl("/usr/bin/arch", "arch", option.c_str(), executable, role, name.c_str(), nullptr);
    } else {
      execl(executable, executable, role, name.c_str(), nullptr);
    }
    _exit(127);
  }
  return process;
}

int runCoordinator(const char* executable) {
  const auto name = std::string("Local\\KinuStream.IpcTest.") +
                    std::to_string(static_cast<unsigned long long>(getpid()));
  const auto wideName = widen(name);
  const auto bytes = das::transport::requiredBytes(kCapacity, kChannels);
  auto memory = das::transport::NamedSharedMemory::create(wideName, bytes);
  if (!memory.isOpen() || memory.alreadyExisted()) {
    std::cerr << "could not create named shared memory\n";
    return 1;
  }
  if (!das::transport::RingBuffer::initialize(memory.storage(), kCapacity, kChannels)) {
    std::cerr << "could not initialise the shared ring\n";
    return 2;
  }

  const auto producer = startChild(executable, "--producer", name,
                                   std::getenv("DAS_IPC_PRODUCER_ARCH"));
  const auto consumer = startChild(executable, "--consumer", name,
                                   std::getenv("DAS_IPC_CONSUMER_ARCH"));
  if (producer <= 0 || consumer <= 0) return 3;
  int producerStatus {};
  int consumerStatus {};
  if (waitpid(producer, &producerStatus, 0) != producer ||
      waitpid(consumer, &consumerStatus, 0) != consumer ||
      !WIFEXITED(producerStatus) || !WIFEXITED(consumerStatus) ||
      WEXITSTATUS(producerStatus) != 0 || WEXITSTATUS(consumerStatus) != 0) {
    std::cerr << "inter-process transfer failed\n";
    return 4;
  }
  return 0;
}
} // namespace

int main(const int argc, char** argv) {
  if (argc == 3 && std::string_view(argv[1]) == "--producer")
    return runProducer(widen(argv[2]));
  if (argc == 3 && std::string_view(argv[1]) == "--consumer")
    return runConsumer(widen(argv[2]));
  return runCoordinator(argv[0]);
}
