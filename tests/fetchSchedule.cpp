#include "../include/fetchSchedule.h"
#include <cassert>
using namespace std::chrono;
int main() {
    const FetchSchedule::Clock::time_point start{};
    const auto cycle = seconds(600);
    FetchSchedule schedule{start, start};
    schedule.finish(false, start + seconds(2), cycle);
    assert(schedule.nextFetch == start + seconds(32));
    schedule.finish(false, start + seconds(34), cycle);
    assert(schedule.nextFetch == start + seconds(94));
    schedule.finish(false, start + seconds(96), cycle);
    assert(schedule.nextFetch == start + seconds(216));
    schedule.finish(false, start + seconds(218), cycle);
    assert(schedule.nextFetch == start + cycle && schedule.retries == 0);
    schedule.finish(false, start + seconds(602), cycle);
    schedule.finish(true, start + seconds(634), cycle);
    assert(schedule.nextFetch == start + seconds(1200));
    schedule.finish(false, start + seconds(1790), cycle);
    assert(schedule.nextFetch == start + seconds(1800) && schedule.retries == 0);
    schedule.finish(true, start + seconds(3100), cycle);
    assert(schedule.nextFetch == start + seconds(3600));
}
