# Custom Allocator & Lock-Free Concurrency



Systems SIG recruitment task: arena allocator, SPSC ring buffer, adn an integrated low-latency pipeline in C++.

**Status:** Phase 1 and Phase 2 complete(basic tests written by ai nothing else).Phase 3( in progress). 

##Environment
- macOS, Apple Silicon(arm64), 10 cores
- Apple clang, C++20
- CMake with Ninja
- Page size 16KB, cache line 128 bytes (checked it using `sysctl hw.pagesize hw.cachelinesize` )

## Project layout
| Path | Contents                                  |
|---|-------------------------------------------|
| `include/arena.hpp` | Phase 1: arena allocator                  |
| `include/spsc_queue.hpp` | Phase 2: lock-free SPSC queue             |
| `tests/` | Test and demo programs (written using AI) |
| `CMakeLists.txt` | Build configuration                       |

## Phase 1: Arena allocator

### What it does
`Arena` asks the OS for one big block of memory using `mmap`. It hands out pieces by moving a pointer (the offset) forward. It never uses `malloc` or `new`.

### Key points
- **Getting memory:** `mmap` gives a big block from the OS. If it fails, it returns `MAP_FAILED`, not `nullptr`, so the constructor checks for that.
- **Alignment:** some data must start at an address that is a multiple of a number (8, 16, 64...). `alloc` rounds the address up to the next such multiple. This only works when the number is a power of two.
- **Align the address, not the offset:** I round the real address, because that is correct for any starting address.
- **Safe size check:** I compare against the space remaining instead of adding sizes together. Adding could overflow for a huge size and give a wrong answer.
- **Reset is O(1):** `reset()` just sets the offset back to 0. It does not free or clear anything. Old pointers become invalid.
- **Little waste:** there is no bookkeeping per allocation. The only waste is the small gap for alignment.
- **No copying:** copying an `Arena` would free the same memory twice, so copy is deleted.
- **Header-only:** the code is in the header so the compiler can inline `alloc`, making it fast.
- **Not thread-safe:** only one thread (the producer) uses it in the pipeline.

---


## Phase 2: Lock-free SPSC ring buffer

### How it works
A fixed-size circular queue. One thread only pushes (producer), and one thread only pops (consumer). The size `N` is a power of two.

- **Two counters:** `tail_` counts items pushed, `head_` counts items popped. They only go up.
- **Which slot:** `counter & (N - 1)`, which wraps around the array.
- **Full or empty:** items in queue = `tail - head`. Full if that equals `N`. Empty if `head == tail`.
- **No locks:** each counter is written by only one thread, so no lock or compare-and-swap is needed.

### Memory orders (in simple terms)
- The producer writes the item, **then** updates `tail_` with `release`. This means "everything I wrote before is now visible."
- The consumer reads `tail_` with `acquire`. This means "I can now see everything written before that update," so the item is safe to read.
- The consumer does the same in reverse with `head_`, so the producer knows a slot is free to reuse.
- A thread reading its **own** counter uses `relaxed`, because no other thread changes it.

| Operation | Order |
|---|---|
| push: read own `tail_` | relaxed |
| push: read `head_` | acquire |
| push: write `tail_` | release |
| pop: read own `head_` | relaxed |
| pop: read `tail_` | acquire |
| pop: write `head_` | release |

If the `tail_` write were `relaxed`, the consumer might see the new `tail_` before the item itself, and read garbage.

### False sharing
A CPU loads memory in chunks called cache lines (128 bytes on this Mac). If `head_` and `tail_` were in the same line, the two threads would keep stealing the line from each other (MESI protocol), which is slow. So I put each on its own line with `alignas`.

