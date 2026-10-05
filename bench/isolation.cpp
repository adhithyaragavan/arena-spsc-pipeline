#include "arena.hpp"
#include "spsc_queue.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <queue>
#include <thread>

struct Packet {
    std::uint64_t id;
    std::uint64_t timestamp;
    std::uint32_t length;
    std::uint32_t flags;
    std::uint8_t  payload[40];
};
static_assert(sizeof(Packet) == 64, "unexpected padding");

constexpr std::uint64_t kBatch     = 4096;
constexpr std::uint64_t kBatches   = 2000;
constexpr std::size_t   kQueueSize = 1024;

// allocator policies
struct MallocAlloc {
    void begin_batch(std::uint64_t) {}
    Packet* get() { return static_cast<Packet*>(std::malloc(sizeof(Packet))); }
    void release(Packet* p) { std::free(p); }
    void end_batch(std::uint64_t) {}
};

struct ArenaAlloc {
    Arena arena{kBatch * sizeof(Packet) + 64};
    std::atomic<std::uint64_t> done{0};
    void begin_batch(std::uint64_t b) {
        if (b > 0) {
            while (done.load(std::memory_order_acquire) != b) { }
            arena.reset();
        }
    }
    Packet* get() {
        return new (arena.alloc(sizeof(Packet), alignof(Packet))) Packet;
    }
    void release(Packet*) {}
    void end_batch(std::uint64_t b) { done.store(b + 1, std::memory_order_release); }
};

// queue policies
struct MutexQueue {
    std::mutex m;
    std::queue<Packet*> q;
    void push(Packet* p) {
        for (;;) {
            std::lock_guard<std::mutex> lock(m);
            if (q.size() < kQueueSize) { q.push(p); return; }
        }
    }
    Packet* pop() {
        for (;;) {
            std::lock_guard<std::mutex> lock(m);
            if (!q.empty()) { Packet* p = q.front(); q.pop(); return p; }
        }
    }
};

struct SpscWrap {
    SpscQueue<Packet*, kQueueSize> q;
    void push(Packet* p) { while (!q.push(p)) { } }
    Packet* pop() { Packet* p; while (!q.pop(p)) { } return p; }
};

// the shared workload
template <class A, class Q>
double run(std::uint64_t& checksum_out, std::uint64_t& bad_out) {
    // each shared object on its own cache line
    alignas(kCacheLine) A alloc;
    alignas(kCacheLine) Q queue;
    alignas(kCacheLine) std::uint64_t checksum = 0;
    alignas(kCacheLine) std::uint64_t bad = 0;

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&] {
        for (std::uint64_t b = 0; b < kBatches; ++b) {
            alloc.begin_batch(b);
            for (std::uint64_t i = 0; i < kBatch; ++i) {
                Packet* p = alloc.get();
                p->id = b * kBatch + i;
                p->timestamp = i;
                p->length = sizeof(p->payload);
                p->flags = 0;
                for (unsigned j = 0; j < sizeof(p->payload); ++j)
                    p->payload[j] = static_cast<std::uint8_t>(p->id + j);
                queue.push(p);
            }
        }
    });

    std::thread consumer([&] {
        for (std::uint64_t b = 0; b < kBatches; ++b) {
            for (std::uint64_t i = 0; i < kBatch; ++i) {
                Packet* p = queue.pop();
                if (p->payload[0] != static_cast<std::uint8_t>(p->id)) ++bad;
                checksum += p->id;
                for (unsigned j = 0; j < sizeof(p->payload); ++j)
                    checksum += p->payload[j];
                alloc.release(p);
            }
            alloc.end_batch(b);
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    checksum_out = checksum;
    bad_out = bad;
    double secs = std::chrono::duration<double>(end - start).count();
    return static_cast<double>(kBatch * kBatches) / secs / 1e6;
}

template <class A, class Q>
void bench(const char* name, int runs) {
    std::printf("%-22s", name);
    for (int r = 0; r < runs; ++r) {
        std::uint64_t cs, bad;
        double rate = run<A, Q>(cs, bad);
        std::printf(" %6.2f%s", rate, bad ? "!" : "");
    }
    std::printf("   (M msgs/s)\n");
    std::fflush(stdout);
}

int main() {
    constexpr int runs = 7;
    bench<MallocAlloc, MutexQueue>("malloc + mutex queue", runs);
    bench<MallocAlloc, SpscWrap  >("malloc + SPSC",        runs);
    bench<ArenaAlloc,  MutexQueue>("arena  + mutex queue", runs);
    bench<ArenaAlloc,  SpscWrap  >("arena  + SPSC",        runs);
    return 0;
}
