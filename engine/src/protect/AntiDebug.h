#pragma once
#include <cstdint>

namespace symm::protect {

// Multi-layer anti-debugging.
// Called once at startup before any useful work begins.
// Returns true if a debugger is detected (caller should exit).
bool detectDebugger();

// Timing-based check: if a breakpoint is set, execution slows measurably.
// Returns true if execution time between two points exceeds threshold.
bool timingCheck();

// Start a background watchdog thread that periodically checks for debuggers.
// If one is detected mid-run, calls std::exit(1).
void startWatchdog();

} // namespace symm::protect
