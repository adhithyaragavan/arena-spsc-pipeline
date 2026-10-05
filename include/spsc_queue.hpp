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
        if (t - head_cache_ == N) {
            // looks full: refresh my cached copy of head_ before giving up
            head_cache_ = head_.load(std::memory_order_acquire);
            if (t - head_cache_ == N) return false;
        }
        buf_[t & kMask] = v;
        tail_.store(t+1,std::memory_order_release);
        return true;
    }

    //Consumer thread only.
    bool pop(T& out) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_cache_) {
            // looks empty: refresh my cached copy of tail_ before giving up
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (h == tail_cache_) return false;
        }
        out = buf_[h & kMask];
        head_.store(h+1, std::memory_order_release);
        return true;
    }

private:
    // consumer side: head_ and its cached copy of tail_ share one cache line
    alignas(kCacheLine) std::atomic<std::size_t> head_{0}; //written by consumer
    std::size_t tail_cache_{0};                            //consumer-owned
    // producer side: tail_ and its cached copy of head_ share another line
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; //written by producer
    std::size_t head_cache_{0};                            //producer-owned
    alignas(kCacheLine) T buf_[N];
};


