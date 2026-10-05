#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <queue>
#include <thread>

struct Packet {
    std::uint64_t id;
    std::uint64_t timestamp;
    std::uint32_t length;
    std::uint32_t flags;
    std::uint8_t  payload[40];
};
static_assert(sizeof(Packet)== 64, "undexpected padding");

constexpr std::uint64_t kBatch = 4096;
constexpr std::uint64_t kBatches = 2000;
constexpr std::uint64_t kTotal = kBatch * kBatches;
constexpr std::size_t   kQueueSize = 1024;
constexpr std::size_t   kLine      = 128;   // cache line size on Apple Silicon

int main() {
    // keep the lock and its queue together, but away from the other variables
    alignas(kLine) std::mutex m;
    std::queue<Packet*> q;

    alignas(kLine) std::uint64_t checksum = 0;
    alignas(kLine) std::uint64_t bad = 0;

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&] {
        for (std::uint64_t id = 0; id < kTotal; ++id) {
            Packet* p = static_cast<Packet*>(std::malloc(sizeof(Packet)));
            p->id        = id;
            p->timestamp = id % kBatch;
            p->length    = sizeof(p->payload);
            p->flags     = 0;
            for (unsigned j = 0; j < sizeof(p->payload); ++j)
                p->payload[j] = static_cast<std::uint8_t>(p->id + j);

            for (;;) {                                   // push(waiting while full)
                std::lock_guard<std::mutex> lock(m);
                if (q.size() < kQueueSize) {
                    q.push(p);
                    break;
                }
            }
        }
    });
    std::thread consumer([&] {
        for (std::uint64_t n = 0; n < kTotal; ++n) {
            Packet* p = nullptr;
            for (;;) {                                   // pop (waiting while empty)
                std::lock_guard<std::mutex> lock(m);
                if (!q.empty()) {
                    p = q.front();
                    q.pop();
                    break;
                }
            }
            if (p->payload[0] != static_cast<std::uint8_t>(p->id)) ++bad;
            checksum += p->id;
            for (unsigned j = 0; j < sizeof(p->payload); ++j)
                checksum += p->payload[j];
            std::free(p);                                // per packet freeing
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();
    double msgs = static_cast<double>(kTotal);

    std::printf("messages:  %.0f\n", msgs);
    std::printf("time:      %.3f s\n", secs);
    std::printf("rate:      %.2f M msgs/s\n", msgs / secs / 1e6);
    std::printf("checksum:  %llu\n", static_cast<unsigned long long>(checksum));
    std::printf("corrupted: %llu\n", static_cast<unsigned long long>(bad));
    return bad == 0 ? 0 : 1;
}