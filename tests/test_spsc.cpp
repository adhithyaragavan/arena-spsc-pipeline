#include "spsc_queue.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

// assert() is disabled in Release builds (NDEBUG), so use our own check.
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

static void test_empty_pop_fails() {
    SpscQueue<int, 8> q;
    int v = -1;
    CHECK(!q.pop(v));
    CHECK(v == -1);                               // failed pop leaves output alone
}

static void test_fifo_order() {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 5; ++i) CHECK(q.push(i));
    for (int i = 0; i < 5; ++i) {
        int v = -1;
        CHECK(q.pop(v));
        CHECK(v == i);
    }
    int v;
    CHECK(!q.pop(v));
}

static void test_full_then_push_fails() {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 8; ++i) CHECK(q.push(i));  // all N slots usable
    CHECK(!q.push(99));                            // full
    int v;
    CHECK(q.pop(v));
    CHECK(v == 0);
    CHECK(q.push(99));                             // one slot freed -> push works again
    CHECK(!q.push(100));
}

static void test_wraparound() {
    SpscQueue<int, 4> q;
    int next_in = 0, next_out = 0;
    // push 3, pop 3, many times: indices wrap around the 4-slot array repeatedly
    for (int round = 0; round < 1000; ++round) {
        for (int i = 0; i < 3; ++i) CHECK(q.push(next_in++));
        for (int i = 0; i < 3; ++i) {
            int v = -1;
            CHECK(q.pop(v));
            CHECK(v == next_out++);
        }
    }
}

static void test_interleaved_near_full_and_empty() {
    // exercises the cached-index refresh paths in both push and pop
    SpscQueue<int, 4> q;
    int next_in = 0, next_out = 0;
    for (int i = 0; i < 4; ++i) CHECK(q.push(next_in++));
    CHECK(!q.push(next_in));                       // cached head says full, real head too
    int v;
    CHECK(q.pop(v)); CHECK(v == next_out++);
    CHECK(q.push(next_in++));                      // cached head is stale: must refresh
    while (next_out < next_in) {
        CHECK(q.pop(v));
        CHECK(v == next_out++);
    }
    CHECK(!q.pop(v));                              // cached tail says empty, real tail too
    CHECK(q.push(next_in++));
    CHECK(q.pop(v));                               // cached tail is stale: must refresh
    CHECK(v == next_out++);
}

struct Big { std::uint64_t a, b, c, d; };

static void test_struct_payload() {
    SpscQueue<Big, 4> q;
    for (std::uint64_t i = 0; i < 4; ++i) CHECK(q.push(Big{i, i + 1, i + 2, i + 3}));
    for (std::uint64_t i = 0; i < 4; ++i) {
        Big b{};
        CHECK(q.pop(b));
        CHECK(b.a == i && b.b == i + 1 && b.c == i + 2 && b.d == i + 3);
    }
}

static void test_two_threads_in_order() {
    constexpr std::uint64_t count = 2'000'000;
    SpscQueue<std::uint64_t, 64> q;                // small queue: lots of full/empty cycling
    std::uint64_t received = 0;
    bool in_order = true;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i)
            while (!q.push(i)) { }
    });
    std::thread consumer([&] {
        std::uint64_t v;
        while (received < count) {
            if (q.pop(v)) {
                if (v != received) { in_order = false; return; }
                ++received;
            }
        }
    });
    producer.join();
    consumer.join();
    CHECK(in_order);
    CHECK(received == count);
}

int main() {
    test_empty_pop_fails();
    test_fifo_order();
    test_full_then_push_fails();
    test_wraparound();
    test_interleaved_near_full_and_empty();
    test_struct_payload();
    test_two_threads_in_order();
    std::puts("spsc: all tests passed");
    return 0;
}
