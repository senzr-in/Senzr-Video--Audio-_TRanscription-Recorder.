// test_queues.cpp
// Unit + concurrency tests for ThreadSafeQueue<T>
// Run with: ./test_queues

#include "session_pipeline/queues.hpp"
#include "session_pipeline/models.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

using namespace session_pipeline;
using namespace std::chrono_literals;

// ── Helpers ───────────────────────────────────────────────────────────────────

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::cerr << "FAIL: " << msg << '\n'; return false; } } while(0)

// ── Tests ─────────────────────────────────────────────────────────────────────

bool test_basic_push_pop() {
    ThreadSafeQueue<int> q(5);
    q.try_push(1);
    q.try_push(2);
    q.try_push(3);
    auto v = q.pop(10ms);
    CHECK(v.has_value(), "pop should return value");
    CHECK(*v == 1, "FIFO order");
    std::cout << "[PASS] test_basic_push_pop\n";
    return true;
}

bool test_drop_oldest_when_full() {
    // maxsize=3; push 4 items — oldest (1) should be dropped
    ThreadSafeQueue<int> q(3);
    q.try_push(1);
    q.try_push(2);
    q.try_push(3);
    bool not_dropped = q.try_push(4);  // should drop 1
    CHECK(!not_dropped, "should report drop");
    auto v1 = q.pop(10ms);
    CHECK(v1.has_value() && *v1 == 2, "oldest dropped, next is 2");
    std::cout << "[PASS] test_drop_oldest_when_full\n";
    return true;
}

bool test_pop_timeout_returns_nullopt() {
    ThreadSafeQueue<int> q(5);
    auto v = q.pop(20ms);
    CHECK(!v.has_value(), "empty queue pop should timeout");
    std::cout << "[PASS] test_pop_timeout_returns_nullopt\n";
    return true;
}

bool test_try_pop_empty() {
    ThreadSafeQueue<int> q(5);
    auto v = q.try_pop();
    CHECK(!v.has_value(), "try_pop on empty returns nullopt");
    std::cout << "[PASS] test_try_pop_empty\n";
    return true;
}

bool test_shutdown_unblocks_pop() {
    ThreadSafeQueue<int> q(5);
    std::atomic<bool> unblocked{false};
    std::thread t([&] {
        auto v = q.pop(5000ms);  // long timeout
        (void)v;
        unblocked.store(true);
    });
    std::this_thread::sleep_for(30ms);
    q.shutdown();
    t.join();
    CHECK(unblocked.load(), "shutdown should unblock pop");
    std::cout << "[PASS] test_shutdown_unblocks_pop\n";
    return true;
}

bool test_drain_clears_queue() {
    ThreadSafeQueue<int> q(10);
    for (int i = 0; i < 5; ++i) q.try_push(i);
    q.drain();
    CHECK(q.empty(), "drain should empty queue");
    std::cout << "[PASS] test_drain_clears_queue\n";
    return true;
}

bool test_concurrent_producers_consumers() {
    constexpr int N_PRODUCERS  = 4;
    constexpr int N_CONSUMERS  = 4;
    constexpr int ITEMS_EACH   = 10000;
    constexpr int QUEUE_SIZE   = 512;

    ThreadSafeQueue<int> q(QUEUE_SIZE);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};

    std::vector<std::thread> producers, consumers;

    for (int p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < ITEMS_EACH; ++i) {
                q.try_push(i);
                ++produced;
            }
        });
    }

    for (int c = 0; c < N_CONSUMERS; ++c) {
        consumers.emplace_back([&] {
            while (!done.load() || !q.empty()) {
                auto v = q.pop(10ms);
                if (v) ++consumed;
            }
        });
    }

    for (auto& t : producers) t.join();
    // Give consumers time to drain
    std::this_thread::sleep_for(200ms);
    done.store(true);
    q.shutdown();
    for (auto& t : consumers) t.join();

    // Due to drop-oldest, consumed <= produced
    CHECK(consumed.load() > 0, "consumers must have received items");
    std::cout << "[PASS] test_concurrent_producers_consumers"
              << " produced=" << produced.load()
              << " consumed=" << consumed.load() << '\n';
    return true;
}

bool test_no_deadlock_on_rapid_start_stop() {
    for (int i = 0; i < 100; ++i) {
        ThreadSafeQueue<int> q(10);
        std::thread t([&] {
            for (int j = 0; j < 20; ++j) q.try_push(j);
        });
        t.join();
        q.drain();
        q.shutdown();
    }
    std::cout << "[PASS] test_no_deadlock_on_rapid_start_stop\n";
    return true;
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== ThreadSafeQueue Tests ===\n";
    int failed = 0;
    failed += !test_basic_push_pop();
    failed += !test_drop_oldest_when_full();
    failed += !test_pop_timeout_returns_nullopt();
    failed += !test_try_pop_empty();
    failed += !test_shutdown_unblocks_pop();
    failed += !test_drain_clears_queue();
    failed += !test_concurrent_producers_consumers();
    failed += !test_no_deadlock_on_rapid_start_stop();

    if (failed == 0)
        std::cout << "\nAll tests PASSED.\n";
    else
        std::cout << "\n" << failed << " test(s) FAILED.\n";
    return failed;
}
