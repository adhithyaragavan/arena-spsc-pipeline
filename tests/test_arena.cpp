#include "arena.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

// assert() is disabled in Release builds (NDEBUG), so use our own check.
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

static bool is_aligned(const void* p, std::size_t a) {
    return reinterpret_cast<std::uintptr_t>(p) % a == 0;
}

static void test_alignment() {
    Arena a(1 << 20);
    for (std::size_t al : {1, 2, 4, 8, 16, 64, 128, 4096}) {
        void* p = a.alloc(3, al);              // odd size forces padding next time
        CHECK(p != nullptr);
        CHECK(is_aligned(p, al));
    }
}

static void test_large_alignment() {
    // Alignments bigger than the page size. The mmap base is only page-aligned,
    // so aligning just the offset would give a misaligned address here.
    Arena a(1 << 23);
    CHECK(a.alloc(1, 1) != nullptr);               // move off the start
    for (std::size_t al : {1u << 15, 1u << 16, 1u << 18, 1u << 20}) {
        void* p = a.alloc(1, al);
        CHECK(p != nullptr);
        CHECK(is_aligned(p, al));
    }
}

static void test_no_overlap() {
    Arena a(1 << 16);
    std::vector<std::pair<unsigned char*, std::size_t>> blocks;
    for (std::size_t i = 1; i <= 100; ++i) {
        auto* p = static_cast<unsigned char*>(a.alloc(i, 8));
        CHECK(p != nullptr);
        std::memset(p, static_cast<int>(i), i);   // unique fill pattern per block
        blocks.push_back({p, i});
    }
    // if any block overlapped another, its pattern would have been overwritten
    for (auto& [p, n] : blocks)
        for (std::size_t j = 0; j < n; ++j)
            CHECK(p[j] == static_cast<unsigned char>(n));
}

static void test_overflow() {
    Arena a(128);
    CHECK(a.alloc(100, 1) != nullptr);
    std::size_t before = a.used();
    CHECK(a.alloc(100, 1) == nullptr);            // does not fit
    CHECK(a.used() == before);                    // failed alloc changes nothing
    CHECK(a.alloc(SIZE_MAX, 1) == nullptr);       // would wrap around if added naively
    CHECK(a.alloc(SIZE_MAX - 8, 8) == nullptr);
    CHECK(a.used() == before);
    CHECK(a.alloc(28, 1) != nullptr);             // exact fit still works
    CHECK(a.used() == 128);
    CHECK(a.alloc(1, 1) == nullptr);              // now truly full
}

static void test_padding_counts_toward_capacity() {
    Arena a(64);
    CHECK(a.alloc(1, 1) != nullptr);
    // 63 bytes left, but a 64-aligned block of 63 bytes cannot fit after padding
    CHECK(a.alloc(63, 64) == nullptr);
}

static void test_bad_alignment() {
    Arena a(1024);
    CHECK(a.alloc(8, 0) == nullptr);
    CHECK(a.alloc(8, 3) == nullptr);
    CHECK(a.alloc(8, 24) == nullptr);
    CHECK(a.used() == 0);
}

static void test_reset() {
    Arena a(1024);
    void* first = a.alloc(64, 16);
    CHECK(first != nullptr);
    a.alloc(64, 16);
    CHECK(a.used() > 0);
    a.reset();
    CHECK(a.used() == 0);
    CHECK(a.alloc(64, 16) == first);              // memory is reused from the start
}

static void test_zero_size() {
    Arena a(1024);
    void* p = a.alloc(0, 8);
    CHECK(p != nullptr);
    CHECK(is_aligned(p, 8));
    CHECK(a.used() == 0);                         // no bytes consumed
}

static void test_fresh_memory_is_zero() {
    Arena a(1 << 16);
    auto* p = static_cast<unsigned char*>(a.alloc(4096, 8));
    CHECK(p != nullptr);
    for (std::size_t i = 0; i < 4096; ++i) CHECK(p[i] == 0);
}

static void test_capacity_reported() {
    Arena a(4096);
    CHECK(a.capacity() == 4096);
    CHECK(a.used() == 0);
}

int main() {
    test_alignment();
    test_large_alignment();
    test_no_overlap();
    test_overflow();
    test_padding_counts_toward_capacity();
    test_bad_alignment();
    test_reset();
    test_zero_size();
    test_fresh_memory_is_zero();
    test_capacity_reported();
    std::puts("arena: all tests passed");
    return 0;
}
