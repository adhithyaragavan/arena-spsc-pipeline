# Custom Allocator & Lock-Free Concurrency

Systems SIG recruitment task: arena allocator, SPSC ring buffer, and an integrated low-latency pipeline in C++.

**Status:** Phases 1, 2 and 3 are done (basic tests written by AI, nothing else). Still to do: proper tests, ThreadSanitizer, and the bonus tasks.

## Environment
- macOS, Apple Silicon (arm64), 10 cores
- Apple clang, C++20
- CMake with Ninja
- Page size 16KB, cache line 128 bytes (checked it using `sysctl hw.pagesize hw.cachelinesize`)

## Build and run
```
cmake -S . -B build -G Ninja
cmake --build build
./build/main          # arena smoke test
./build/spsc_demo     # SPSC queue: 10M items, checks they arrive in order
./build/pipeline      # arena + SPSC pipeline
./build/baseline      # malloc + mutex queue
./build/isolation     # all four allocator/queue combinations
```

## Project layout
| Path | Contents                                                   |
|---|------------------------------------------------------------|
| `include/arena.hpp` | Phase 1: arena allocator                                   |
| `include/spsc_queue.hpp` | Phase 2: lock-free SPSC queue                              |
| `bench/pipeline.cpp` | Phase 3: arena + SPSC pipeline                             |
| `bench/baseline.cpp` | Phase 3: malloc + mutex queue baseline                     |
| `bench/isolation.cpp` | Phase 3: all four combinations in one program              |
| `tests/` | Test and demo programs (written using AI)                  |
| `CMakeLists.txt` | Build configuration                                        |

---

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

### Optimization: cached indices
My first version read the other thread's counter (with `acquire`) on **every** push and pop. That pulls a cache line from the other core on every message, which turned out to be expensive (see "Optimization" in Phase 3).

Now each side keeps a local copy of the other side's counter (`head_cache_` for the producer, `tail_cache_` for the consumer) and only reloads the shared counter when the queue looks full (producer) or empty (consumer).

Using a stale copy is safe: it can only make the queue look fuller or emptier than it really is. It can never let a thread read a slot that is not ready, because the earlier `acquire` load already covered every slot up to the cached value. Each cached copy sits on the same cache line as the counter its own thread writes, so it adds no extra traffic.

---

## Phase 3: Integrated pipeline

### What it does
A producer thread creates packets and a consumer thread processes them.
- The producer gets memory for each packet from the **arena** (no `malloc` or `new`), fills it with mock data, and pushes the pointer onto the **SPSC queue**.
- The consumer pops pointers, reads the packet, and adds to a running checksum.
- When a whole batch is finished, the arena is reset and reused for the next batch.

A packet (`Packet`) is plain data, exactly 64 bytes: `id`, `timestamp`, `length`, `flags`, and a 40-byte payload. It has no destructor, because the arena never runs destructors.

Settings: 2000 batches of 4096 packets (8,192,000 messages), queue size 1024.

### The hard part: when is it safe to reset the arena?
`reset()` makes every old pointer invalid. So the producer should not reset until the consumer has completely finished reading the batch.

"The queue is empty" is **not** enough. The consumer may have popped the last pointer but still be reading the packet.

My solution is an atomic counter, `batches_done`:
- After the consumer finishes reading every packet in batch `b`, it stores `b + 1` into `batches_done` with **release**.
- Before starting batch `b`, the producer waits until `batches_done == b`, reading it with **acquire**, then calls `reset()`.

Release/acquire means all the consumer's reads happen before the producer's new writes. I considered an end-of-batch marker and a return queue, but the counter is simpler and needs only one synchronisation point per batch.

The arena is only touched by the producer, so its offset needs no atomic.

### Baseline for comparison
Same packet, same message count, same checksum work, same queue size limit (1024):
- `malloc` for every packet, and `free` by the consumer after use
- a `std::queue` protected by a `std::mutex`
- both threads busy-wait, as in the lock-free version

### Benchmark method
- Release build (`-O3`), 8,192,000 messages per run
- `std::chrono::steady_clock`, timing the whole run from before the threads start until both have finished
- Several runs of each program; I report the median and the range
- The checksum is printed so the compiler cannot remove the work
- Both programs gave the same checksum (`33596207104000`) and `corrupted: 0` in every run, so they process identical data

### Results, first version (millions of messages per second)
11 runs of each:

| | Median | Min | Max |
|---|---|---|---|
| Baseline (malloc + mutex queue) | 3.49 | 3.13 | 9.53 |
| Pipeline (arena + SPSC queue) | 16.84 | 16.39 | 17.43 |

Median speedup was about **4.8x**.

Baseline runs, sorted: `3.13 3.19 3.32 3.36 3.45 3.49 3.67 | 8.65 8.66 9.08 9.53`

- The pipeline was very consistent (16.4 to 17.4).
- The baseline had **two modes**: 7 runs around 3.4 and 4 runs around 9.0, with nothing in between.
- My guess at the time was that the scheduler puts the two threads on different kinds of cores, and a lock passed between two spinning threads is sensitive to that. I did not test this.

### Isolation experiment
The first comparison changes two things at once, so I could not tell where the speedup came from. `bench/isolation.cpp` runs all four combinations of allocator and queue with identical producer/consumer code. Only the allocator and the queue change.

My first run of this program gave a surprise: the lock-free queue was **slower** than the mutex queue when both used the arena (about 16.5 vs 25 M msgs/s). That led to the optimization below. These are the final numbers, after it.

Median of 42 runs each (6 invocations x 7 runs), millions of messages per second:

| | mutex queue | SPSC queue |
|---|---|---|
| malloc/free | 16.4 | 25.5 |
| arena | 29.5 | **53.4** |

Time per message: 61 ns, 39 ns, 34 ns and 19 ns respectively.

- Replacing malloc/free with the arena saves about 27 ns per message (from the baseline).
- Replacing the mutex queue with the SPSC queue saves about 22 ns per message (from the baseline).
- Together they give **3.3x**. They do not add up fully, because a fixed part of each message (filling the payload, the checksum, handing the pointer to another core) is the same in all four versions.

### Optimization: what looked wrong and what fixed it
1. **Observation:** in the first isolation run, `arena + SPSC` (16.5) was slower than `arena + mutex queue` (25), and slower than `malloc + SPSC` (20).
2. **Suspect 1: my benchmark itself.** In `run()`, the variables `alloc`, `queue`, `checksum` and `bad` sat next to each other in one stack frame, while two threads wrote to them constantly. That can cause false sharing in the harness. I put each on its own cache line with `alignas(kCacheLine)`. This alone raised `arena + SPSC` from about 17.8 to 22.6.
3. **Suspect 2: the queue reloads the other thread's counter on every push and pop**, moving a cache line between cores on every message. I added the cached indices (see Phase 2). On top of the padding this raised `arena + SPSC` from 22.6 to about 53.
4. I applied the same padding to `pipeline.cpp` and `baseline.cpp`.


### Limitations
- One machine (Apple Silicon laptop, 10 cores), with other apps possibly running.
- Both threads busy-wait, which uses a full core while waiting. A design using condition variables would give different numbers.
- macOS has no real thread pinning, so thread placement is up to the scheduler.
- Each packet involves the same fill and checksum work in all versions, so the numbers show the *difference* in allocation and queue cost on top of that. With lighter packets the ratios would be bigger.
- **Unexplained:** `bench/baseline.cpp` still gives about 3.8 M msgs/s (3 runs after the padding change), while `malloc + mutex queue` inside `isolation.cpp` gives about 16. They should be the same logic. I have not found the reason. I trust the isolation table because all four rows use exactly the same code paths. Absolute numbers on this machine clearly depend on memory layout and thread placement.

### A test that passed but should not be trusted
I changed the `batches_done` store from `release` to `relaxed`. The output did not change (`corrupted: 0` every time). That does **not** make `relaxed` correct. The failure window is tiny: the producer's first writes after a reset go to the start of the arena, while the consumer's last reads are at the end of it, so corruption almost never shows up. Without the release/acquire pair there is no guarantee. I put the `release` back. ThreadSanitizer would be the right tool to catch this, and I have not run it yet.

---

## Failures and fixes

| Problem | Cause | Fix |
|---|---|---|
| The lock-free queue was slower than the mutex queue with the arena | Cache-line traffic on every message (queue reloaded the other counter each time) and false sharing between harness variables | Cached indices in the queue, and `alignas(kCacheLine)` on the shared variables (see Optimization) |
| Baseline results were inconsistent (3.4 vs 9.0) | Not understood. Two performance modes. After padding, the same logic inside `isolation.cpp` was stable | Reported the median and range, and trust the isolation table (see Limitations) |

---

## What is still to do
- Proper tests for the arena and the queue (so far only basic ones written with AI)
- ThreadSanitizer run, including a deliberate `relaxed` bug to see it get caught
- Bonus: MPMC queue, huge pages (needs Linux), TSan in the build

