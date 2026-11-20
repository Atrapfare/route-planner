#pragma once

// Calls fn(worker, i) for i = 0..count-1 on several threads, which take the
// next index from a shared counter. worker (0..threads-1) tells fn which
// thread it is on, so it can use that thread's planner. Returns when all are
// done.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace rp {

template <typename F> void parallel_for(std::size_t threads, std::size_t count, F&& fn) {
    threads = std::max<std::size_t>(1, std::min(threads, count));
    std::atomic<std::size_t> next{0};
    const auto run = [&](std::size_t worker) {
        for (std::size_t i; (i = next.fetch_add(1)) < count;) {
            fn(worker, i);
        }
    };
    if (threads == 1) {
        run(0);
        return;
    }
    std::vector<std::jthread> pool;
    for (std::size_t w = 1; w < threads; ++w) {
        pool.emplace_back(run, w);
    }
    run(0);
}

} // namespace rp
