#include "arena.hpp"
#include "spsc_queue.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <new>
#include <thread>

struct Packet {
    std::uint64_t id;
    std::uint64_t timestamp;
    std::uint32_t length;
    std::uint32_t flags;
    std::uint8_t  payload[40];
};
static_assert(sizeof(Packet)==64, "Unexpected padding");

constexpr std::uint64_t kBatch      = 4096; //number of packets per batch
constexpr std::uint64_t kBatches    = 2000; // number of batches
constexpr std::size_t   kQueueSize  = 1024;

int main() {
    // each shared variable on its own cache line, so the two threads don't
    // fight over a line just because the variables sit next to each other
    alignas(kCacheLine) Arena arena(kBatch*sizeof(Packet)+64);
    alignas(kCacheLine) SpscQueue<Packet*, kQueueSize> q;
    alignas(kCacheLine) std::atomic<std::uint64_t> batches_done{0};

    alignas(kCacheLine) std::uint64_t checksum = 0;
    alignas(kCacheLine) std::uint64_t bad = 0;

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&] {
        for (std::uint64_t b = 0; b < kBatches; ++b) {
            if (b > 0) {
                // wait until the consumer has fully finished batch b-1
                while (batches_done.load(std::memory_order_acquire) != b) { }
                arena.reset();
            }
            for (std::uint64_t i = 0; i < kBatch; ++i) {
                void* mem = arena.alloc(sizeof(Packet), alignof(Packet));
                Packet* p = new (mem) Packet;          // placement new
                p->id        = b * kBatch + i;
                p->timestamp = i;
                p->length    = sizeof(p->payload);
                p->flags     = 0;
                for (unsigned j = 0; j < sizeof(p->payload); ++j)
                    p->payload[j] = static_cast<std::uint8_t>(p->id + j);
                while (!q.push(p)) { }                 // queue full: spin
            }
        }
    });

    std::thread consumer([&] {
        for (std::uint64_t b = 0; b < kBatches; ++b) {
            for (std::uint64_t i = 0; i < kBatch; ++i) {
                Packet* p;
                while (!q.pop(p)) { }                  // queue empty: spin
                if (p->payload[0] != static_cast<std::uint8_t>(p->id)) ++bad;
                checksum += p->id;
                for (unsigned j = 0; j < sizeof(p->payload); ++j)
                    checksum += p->payload[j];
            }
            // every read of this batch is done: hand the memory back
            batches_done.store(b + 1, std::memory_order_release);
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(end-start).count();
    double msgs = static_cast<double>(kBatch * kBatches);

    std::printf("messages:  %.0f\n", msgs);
    std::printf("time:      %.3f s\n", secs);
    std::printf("rate:      %.2f M msgs/s\n", msgs / secs / 1e6);
    std::printf("checksum:  %llu\n", static_cast<unsigned long long>(checksum));
    std::printf("corrupted: %llu\n", static_cast<unsigned long long>(bad));
    return bad == 0 ? 0 : 1;
}
