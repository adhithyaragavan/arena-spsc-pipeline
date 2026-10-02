#pragma once
#include <sys/mman.h>
#include <cstddef>
#include <cstdint>
#include <new>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

class Arena {
public:
    explicit Arena(std::size_t capacity):capacity_(capacity) {
        void *p = mmap(nullptr,capacity,PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,-1,0);
        if (p == MAP_FAILED) throw std::bad_alloc(); //doesnt give nullptr
        base_  = static_cast<std::byte*>(p);
    }
    ~Arena() {
        if (base_) munmap(base_,capacity_);
    }
    Arena(const Arena&)            = delete; // to prevent double-munmap
    Arena& operator=(const Arena&) = delete;

    // returns null pointer if no space or alignment is not a power of two

    void* alloc(std::size_t size, std::size_t alignment) {
        if (alignment == 0|| ((alignment) & (alignment-1)) != 0) return nullptr;

        //aligns the real adress not only offset.
        auto cur = reinterpret_cast<std::uintptr_t>(base_)+offset_;
        auto aligned = (cur + alignment -1) & ~(std::uintptr_t(alignment)-1);
        std::size_t padding = aligned - cur;

        //overflow-safe bounds check (no offset+padding +size addition to check because it may exceed size_t capacity)
        std::size_t remaining = capacity_ - offset_;
        if (padding > remaining || size > remaining - padding) return nullptr;

        offset_ += padding + size;
        return reinterpret_cast<void*>(aligned);
    }
    void reset() { offset_ = 0; }

    std::size_t used()      const { return offset_; }
    std::size_t capacity()  const { return capacity_;}

private:
    std::byte* base_ = nullptr;
    std::size_t capacity_;
    std::size_t offset_ = 0;
};