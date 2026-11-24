// Sleep for mapped images: the host's Sleep, held back while the layer does host-only work (src/host_stall.h).
//
// The retail executable imports Sleep from api-ms-win-core-synch-l1-2-0.dll, and its startup hang check is a loop
// around Sleep(1000) (docs/retail_boot_chain.md, "The startup hang check"). Only Sleep is served: SleepEx, the
// condition-variable sleeps and the waits with a timeout stay the host's, and so does every clock.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>

#include "../src/host_stall.h"
#include "shims.h"

namespace {

void WINAPI guest_sleep(DWORD milliseconds) {
    // A yield and an endless sleep have no duration to stretch.
    if (milliseconds == 0 || milliseconds == INFINITE) {
        Sleep(milliseconds);
        return;
    }
    // Test-branch EXPERIMENT, off by default: LEONIDA_SLOW_STARTUP=1 lets a guest Sleep of 1 s or more last 2.1 s, so
    // the title's startup hang check (docs/retail_boot_chain.md section 5: 30 rounds of Sleep(1000); a round over 2 s
    // restarts the count) sees every round as a suspended console's. It is for learning what a boot does past the
    // point where its startup outlasts 30 s of host time (Wine, game files on a network share); it hides real hangs.
    static const bool slow_startup = [] {
        char v[8]{};
        return GetEnvironmentVariableA("LEONIDA_SLOW_STARTUP", v, sizeof(v)) && v[0] == '1';
    }();
    if (slow_startup && milliseconds >= 1000 && milliseconds < 2100) {
        static std::atomic<bool> said{false};
        if (!said.exchange(true))
            std::fprintf(stderr, "[rt] host stall: LEONIDA_SLOW_STARTUP=1: a guest Sleep of 1 s to 2.1 s lasts 2.1 s "
                                 "(experiment; the title's startup hang check never counts)\n");
        Sleep(2100);
        return;
    }
    auto& gate = rt::host_stall::gate();
    const rt::host_stall::Nanoseconds before = gate.mark();
    Sleep(milliseconds);
    const rt::host_stall::Nanoseconds held = gate.hold(before);
    // One line for the first hold a title's own timing could notice; the count goes on in silence.
    static std::atomic<bool> logged{false};
    if (held >= std::chrono::seconds(1) && !logged.exchange(true))
        std::fprintf(stderr,
                     "[rt] host stall: a guest Sleep(%lu) returned %lld ms late, held while the layer did host-only "
                     "work (logged once)\n",
                     static_cast<unsigned long>(milliseconds),
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(held).count()));
}

}  // namespace

#define HOST_STALL_SLEEP(dll) {dll, "Sleep", reinterpret_cast<void*>(&guest_sleep)}

SHIM_TABLE(host_stall,
    HOST_STALL_SLEEP("kernel32.dll"),
    HOST_STALL_SLEEP("kernelbase.dll"),
    HOST_STALL_SLEEP("api-ms-win-core-synch-l1-2-0.dll"),
    HOST_STALL_SLEEP("api-ms-win-core-synch-l1-2-1.dll"),
);
