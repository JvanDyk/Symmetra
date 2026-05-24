#include "AntiDebug.h"
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <chrono>
#include <atomic>

#ifdef _WIN32
  #include <windows.h>
  #include <intrin.h>
#else
  #include <fstream>
  #include <string>
  #include <sys/ptrace.h>
  #include <unistd.h>
#endif

namespace symm::protect {

// ── Windows anti-debug ────────────────────────────────────────────────────────

#ifdef _WIN32

static bool win_isDebuggerPresent() {
    // Method 1: standard API
    if (IsDebuggerPresent()) return true;

    // Method 2: PEB.NtGlobalFlag at offset 0xBC on 64-bit Windows (0x70 set by debuggers)
    ULONG ntGlobalFlag = *(ULONG*)((PBYTE)__readgsqword(0x60) + 0xBC);
    if (ntGlobalFlag & 0x70) return true;

    // Method 3: CheckRemoteDebuggerPresent
    BOOL remote = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    if (remote) return true;

    // Method 5: parent process check — debuggers are often the parent
    // (simplified: skip for now, would require NtQueryInformationProcess)

    return false;
}

static bool win_timingCheck() {
    // RDTSC-based timing: a breakpoint causes measurable slowdown
    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    // Dummy work: won't be optimized away due to volatile
    volatile uint64_t x = 0;
    for (volatile int i = 0; i < 100; i++) x += i * i;

    QueryPerformanceCounter(&t1);
    double elapsed = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart * 1e6; // microseconds

    // Normal: <500µs. Under debugger with breakpoints: >10ms
    return elapsed > 5000.0;
}

bool detectDebugger() {
    return win_isDebuggerPresent() || win_timingCheck();
}

bool timingCheck() {
    return win_timingCheck();
}

#else

// ── Linux anti-debug ──────────────────────────────────────────────────────────

static bool linux_tracerPid() {
    // Read /proc/self/status — TracerPid line is non-zero under a debugger
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("TracerPid:", 0) == 0) {
            int pid = 0;
            sscanf(line.c_str() + 10, "%d", &pid);
            return pid != 0;
        }
    }
    return false;
}

static bool linux_ptrace() {
    // ptrace(PTRACE_TRACEME) fails with EPERM if already being traced
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) return true;
    // Undo the TRACEME so normal execution continues
    ptrace(PTRACE_DETACH, 0, 0, 0);
    return false;
}

static bool linux_timingCheck() {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile uint64_t x = 0;
    for (volatile int i = 0; i < 100; i++) x += (uint64_t)i * i;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long elapsed_us = (t1.tv_sec - t0.tv_sec) * 1000000L +
                      (t1.tv_nsec - t0.tv_nsec) / 1000L;
    return elapsed_us > 5000L;
}

bool detectDebugger() {
    return linux_tracerPid() || linux_ptrace() || linux_timingCheck();
}

bool timingCheck() {
    return linux_timingCheck();
}

#endif

// ── Watchdog thread ───────────────────────────────────────────────────────────

static std::atomic<bool> g_watchdogRunning{false};

void startWatchdog() {
    g_watchdogRunning = true;
    std::thread([] {
        while (g_watchdogRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (detectDebugger()) {
                std::_Exit(0);
            }
        }
    }).detach();
}

} // namespace symm::protect
