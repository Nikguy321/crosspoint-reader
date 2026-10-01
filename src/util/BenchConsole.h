#pragma once

// USB serial bench console (dev builds with CROSSPOINT_BENCH_CONSOLE=1): lets a
// computer on the USB cable press buttons, touch the screen, take screenshots,
// read the device state and add books to the SD card. Protocol and verbs are
// documented in scripts/x4bench.py; the pure parts live in lib/BenchConsole.

#if CROSSPOINT_BENCH_CONSOLE

#include <BenchProtocol.h>

#include <cstddef>
#include <cstdint>

namespace BenchConsole {

// HWCDC receive queue: one whole PUT chunk plus its header line, so a chunk can
// arrive while the main loop is busy without the USB ISR dropping bytes.
constexpr size_t RX_BUFFER_BYTES = bench::PUT_CHUNK_MAX + 512;

enum Result : uint8_t {
  NONE = 0,
  USER_ACTIVITY = 1 << 0,       // a command was accepted: counts like a button press
  REQUEST_SLEEP = 1 << 1,       // CMD:SLEEP: the caller sleeps (the live screen on power; on it, a redraw)
  REQUEST_DEEP_SLEEP = 1 << 2,  // CMD:SLEEP deep: deep sleep even on power (from the live screen too)
  REQUEST_REDRAW = 1 << 3,      // CMD:REDRAW: the live sleep screen redraws now
};

// Once per main-loop pass, after input was updated. exclusiveStorage is set
// while USB Drive owns the card (file verbs refuse); lastActivityMs is the
// main loop's inactivity-timer origin, reported by STATE.
uint8_t poll(bool exclusiveStorage, unsigned long lastActivityMs);

// A USB host (SOF frames, not just VBUS) is attached; it counts as gone only
// after 2 s of continuous absence (bench::HostPresence). The inactivity
// auto-sleep is held off while this is true.
bool hostPresent();

}  // namespace BenchConsole

#endif  // CROSSPOINT_BENCH_CONSOLE
