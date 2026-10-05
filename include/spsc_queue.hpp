#pragma once
#include <atomic>
#include <cstddef>

//Apple Silicon and ARM uses 128 byte cache lines;x86 usually 64.
#if defined(__APPLE__) && defined(__aarch64__)
inline constexpr std::size_t kCacheLine = 128;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

template<typename T,std::size_t N>
class SpscQueue {
    static_assert(N>=2 && (N & (N-1)) == 0, "N must be a power of two");
    static constexpr std:: size_t kMask = N-1;
public:
    // Producer thread only.
    bool push(const T& v) {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_.load(std::memory_order_acquire);
        if (t-h == N) return false;
        buf_[t & kMask] = v;
        tail_.store(t+1,std::memory_order_release);
        return true;
    }

    //Consumer thread only.
    bool pop(T& out) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        if (t==h) return false;
        out = buf_[h & kMask];
        head_.store(h+1, std::memory_order_release);
        return true;
    }

private:
    alignas(kCacheLine) std::atomic<std::size_t> head_{0}; //written by consumer
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; //written by producer
    alignas(kCacheLine) T buf_[N];
};


