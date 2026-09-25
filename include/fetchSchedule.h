#pragma once
#include <algorithm>
#include <chrono>

// Retries belong to the current slot and never move the regular cadence.
struct FetchSchedule {
    using Clock = std::chrono::steady_clock;
    Clock::time_point nextFetch;
    Clock::time_point slot;
    unsigned retries = 0;

    void finish(bool success, Clock::time_point now, std::chrono::seconds cycle) {
        const auto nextSlot = slot + cycle;
        if (!success && retries < 3 && now < nextSlot) {
            nextFetch = std::min(now + std::chrono::seconds(30 * (1 << retries)), nextSlot);
            ++retries;
            if (nextFetch < nextSlot) return;
        }
        retries = 0;
        slot = nextSlot;
        while (slot <= now) slot += cycle;
        nextFetch = slot;
    }
};
