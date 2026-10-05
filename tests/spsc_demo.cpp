#include "spsc_queue.hpp"
#include <cstdint>
#include <cstdio>
#include <thread>

int main() {
    constexpr std::uint64_t count = 10'000'000;
    SpscQueue<std::uint64_t, 1024> q;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i) {
            while (!q.push(i)) { }          // queue full: keep trying
        }
    });

    std::uint64_t expected = 0;
    bool ok = true;
    std::thread consumer([&] {
        std::uint64_t v;
        while (expected < count) {
            if (q.pop(v)) {
                if (v != expected) { ok = false; break; }
                ++expected;
            }
        }
    });

    producer.join();
    consumer.join();

    std::printf("%s: received %llu items\n", ok ? "OK" : "FAILED",
                static_cast<unsigned long long>(expected));
    return ok ? 0 : 1;
}
