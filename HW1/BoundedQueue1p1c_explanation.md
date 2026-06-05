# `BoundedQueue1p1c_impl.h` — Explained

A **lock-free** bounded queue for a *single producer* and *single consumer*.
The spec forbids mutexes/condition variables, so synchronization is built from
`std::atomic` loads/stores with carefully chosen memory ordering.

---

## C++ Syntax Breakdown

### 1. `#include <atomic>`

Brings in `std::atomic<T>` — a wrapper that gives a primitive type *thread-safe*
read/write operations with explicit control over memory ordering.

```cpp
std::atomic<int> _head;
std::atomic<int> _tail;
```

These two indices are read/written concurrently by the producer and consumer
without a lock.

---

### 2. The `+1` Capacity Trick

```cpp
explicit BoundedQueue1p1c(int capacity)
    : _capacity(capacity + 1),
      _buf(new int[capacity + 1]),
      ...
```

The buffer holds `capacity + 1` slots, but only `capacity` are usable.
The extra slot lets us distinguish **empty** from **full** without a separate
counter:

| State | Condition          |
|-------|--------------------|
| empty | `head == tail`     |
| full  | `(tail + 1) % cap == head` |

Without the spare slot, both states would look the same (`head == tail`), which
would force us to add a `_count` variable — and a counter shared between two
threads would itself need synchronization.

---

### 3. `std::atomic::load` and `std::atomic::store`

```cpp
int head = _head.load(std::memory_order_acquire);
_head.store((head + 1) % _capacity, std::memory_order_release);
```

- `load(order)` — atomic read
- `store(value, order)` — atomic write

The `memory_order_*` argument controls **what the compiler and CPU are allowed
to reorder around this operation**.

---

### 4. Memory Ordering — The Important Part

C++ allows the compiler and CPU to reorder instructions for speed. On a queue,
that's dangerous: if the producer writes the *value* into the slot **after**
publishing the new tail, the consumer could read garbage. Memory ordering
tells the hardware "don't reorder across this point."

| Ordering           | Meaning                                                                                  |
|--------------------|------------------------------------------------------------------------------------------|
| `memory_order_relaxed` | Atomic but no ordering guarantees. Used when only the producer (or only the consumer) touches the variable. |
| `memory_order_acquire` | On a load: nothing that comes *after* this load can be moved *before* it.            |
| `memory_order_release` | On a store: nothing that comes *before* this store can be moved *after* it.          |

The classic **release/acquire pair**:

```cpp
// Producer:
_buf[tail] = v;                                      // (1) write data
_tail.store(next_tail, std::memory_order_release);   // (2) publish

// Consumer:
int tail = _tail.load(std::memory_order_acquire);    // (3) read pointer
val = _buf[head];                                    // (4) read data
```

If the consumer sees (2)'s new tail, it is **guaranteed** to also see (1)'s
data write. Without `release`/`acquire`, the CPU might reorder (1) after (2),
and the consumer could read uninitialized memory.

---

### 5. Why `relaxed` on the *Own* Index

```cpp
int head = _head.load(std::memory_order_relaxed);   // consumer reading its own _head
```

Only the **consumer** ever writes `_head`, so the consumer reading `_head` is
not a synchronization point — there's no race against itself. `relaxed` is
just an atomic load with no fences, which is the cheapest possible operation.
The producer reading `_head` needs `acquire` because it's reading a value
written by another thread (the consumer's release of a slot).

---

### 6. Modular Arithmetic — The Ring Buffer

```cpp
_head.store((head + 1) % _capacity, std::memory_order_release);
int next_tail = (tail + 1) % _capacity;
```

`% _capacity` wraps the index back to `0` when it falls off the end — turning
a fixed array into a circular ring.

---

### 7. Why `size()` Is Approximate

```cpp
int head = _head.load(std::memory_order_acquire);
int tail = _tail.load(std::memory_order_acquire);
return (tail - head + _capacity) % _capacity;
```

`head` and `tail` are read in two separate atomic operations; another thread
could push or pop between them. The returned value is a *snapshot* — accurate
at no single instant. For a queue spec like this, that's fine: the result is
only "the size at some point during the call."

The `+ _capacity` before the `%` keeps the result non-negative when `tail` has
wrapped past `head`.

---

### 8. Why This Is Lock-Free

There is no `mutex`, no `wait()`, no spinning on a flag. Each thread executes
a fixed number of atomic operations and returns. If `push` finds the queue
full it returns `false` immediately; same for `pop` on an empty queue. The
single-producer/single-consumer assumption means **the only contention is on
the two cache lines holding `_head` and `_tail`** — which is the absolute
minimum.

---

## Worked Example

`BoundedQueue1p1c(3)` → 3 usable slots, internal `_capacity = 4`. `H` marks
`_head`, `T` marks `_tail`.

### Step 0 — empty

```
buf: [ _ ] [ _ ] [ _ ] [ _ ]
       HT
size = (0 − 0 + 4) % 4 = 0
```

Consumer calls `pop`: `head == tail`, returns `false` immediately.
No sleeping, no spinning — the spec requires "return false if empty."

### Step 1 — `push(10)`

```cpp
tail = _tail.load(relaxed)            // 0
next_tail = 1
_head.load(acquire) == 0              // 1 != 0, NOT full
_buf[0] = 10
_tail.store(1, release)               // publishes the write of _buf[0]
```

```
buf: [10] [ _ ] [ _ ] [ _ ]
       H    T
```

### Step 2 — `push(20)`, `push(30)`

```
buf: [10] [20] [30] [ _ ]
       H              T
size = (3 − 0 + 4) % 4 = 3
```

### Step 3 — `push(40)` returns `false` (full)

```cpp
tail = 3
next_tail = (3 + 1) % 4 = 0
_head.load(acquire) == 0              // next_tail == head → FULL
return false
```

The spec for Ex2 says `push` returns `false` when full — no blocking, just
report it. This is the reason for the `+1` slot: it's the marker for "full"
without sharing a counter.

### Step 4 — `pop(val)` returns `10`

```cpp
head = _head.load(relaxed)            // 0  (consumer reads its own index)
tail = _tail.load(acquire)            // 3  (paired with producer's release in Step 1)
head != tail → not empty
val = _buf[0] = 10                    // safe because acquire above synced with release
_head.store(1, release)               // tells producer "slot 0 is now free"
```

```
buf: [10] [20] [30] [ _ ]
            H         T
```

### Step 5 — `push(40)` succeeds

```cpp
tail = 3
next_tail = 0
_head.load(acquire) == 1              // 0 != 1 → not full
_buf[0] = 40
_tail.store(0, release)
```

```
buf: [40] [20] [30] [ _ ]
            H    T
```

The tail wrapped to `0`, the queue holds `20, 30, 40` in FIFO order.

### Why no torn reads

In Step 4, the consumer reads `_buf[0]` *after* it has observed `_tail == 3`
via an `acquire` load. The producer's `release` store of `_tail` in Step 1
formed a synchronizes-with edge — the consumer is guaranteed to see every
write the producer made before that release, including `_buf[0] = 10`.

Without `acquire`/`release`, the CPU could reorder the producer's `_buf[0] = 10`
*after* its `_tail.store(1)`, and the consumer might read uninitialized memory.
