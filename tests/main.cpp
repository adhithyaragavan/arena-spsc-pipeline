#include "arena.hpp"
#include <cstdio>

int main() {
    Arena a(1024);                       // calls mmap
    void* p = a.alloc(16, 8);            // actually use it
    printf("got pointer: %p\n", p);
    return 0;
}

