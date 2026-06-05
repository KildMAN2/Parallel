# `BoundedQueue1p1c_impl.h` — Explained

A **lock-free** bounded queue for a *single producer* and *single consumer*.
The spec forbids mutexes/condition variables, so synchronization is built from
`std::atomic` loads/stores with carefully chosen memory ordering.

---

## What is `std::atomic`?

Imagine two threads doing `x++` on a shared `int x`. That single line is
actually **three** machine instructions: *load x → add 1 → store x*. If
thread A is between the load and the store when thread B does the same thing,
one increment gets lost. The result is a **data race** — undefined behavior
in C++.

`std::atomic<T>` is a wrapper that promises:

1. **Indivisible reads and writes.** A load reads a value that some thread
   fully wrote — never a half-old, half-new mix.
2. **Read-modify-write as a unit** (`fetch_add`, `compare_exchange`, …) —
   the load, modify, and store happen as one uninterruptible step.
3. **A say in how the CPU reorders memory operations around it** (the
   `memory_order_*` argument).

Without `atomic`, the compiler is free to assume no other thread touches `x`
and can cache it in a register, reorder accesses, or skip them entirely.
`atomic` switches that off for the wrapped variable.

---

## Line-by-Line Walkthrough

### File header

```cpp
/* Written by Yariv Aridor, 2022 */

#pragma once
```

- `/* ... */` — C-style block comment.
- `#pragma once` — non-standard but universally supported header guard.
  Tells the preprocessor: include this file at most once per translation
  unit, even if multiple files `#include` it. Prevents "class redefined"
  errors.

```cpp
#include "BoundedQueue1p1c.h"   // the abstract base class
#include <atomic>               // std::atomic, memory_order_*
#include <memory>               // std::unique_ptr
```

- `"..."` looks first in the current directory (your own headers).
- `<...>` looks in the standard library / system include paths.

---

### Class declaration

```cpp
class BoundedQueue1p1c : public BoundedQueueAbstract_1p1c {
public:
```

- `class Name : public Base` — `BoundedQueue1p1c` **inherits publicly** from
  `BoundedQueueAbstract_1p1c`. "Public inheritance" = "is-a"; outside code
  can treat a `BoundedQueue1p1c*` as a `BoundedQueueAbstract_1p1c*`.
- `public:` — everything below this line is callable from outside the class.

---

### The constructor

```cpp
explicit BoundedQueue1p1c(int capacity)
    : _capacity(capacity + 1),
      _buf(new int[capacity + 1]),
      _head(0),
      _tail(0)
{}
```

| Token | Meaning |
|---|---|
| `explicit` | Stops the compiler from silently converting an `int` into a `BoundedQueue1p1c`. Without it, `BoundedQueue1p1c q = 5;` would compile; with it, you must write `BoundedQueue1p1c q(5);`. |
| `BoundedQueue1p1c(int capacity)` | Function header — takes one `int` parameter. |
| `: _capacity(capacity + 1), ...` | **Member initializer list**. The *only* place you can initialize `const` members and references. Each `name(value)` constructs member `name` from `value`. Runs **before** the `{}` body. |
| `_capacity(capacity + 1)` | Stores `capacity + 1` — the "one empty slot" trick. |
| `_buf(new int[capacity + 1])` | `new int[N]` allocates an array of `N` ints on the heap and returns an `int*`. That raw pointer constructs the `std::unique_ptr<int[]>`, which now owns the memory. |
| `_head(0), _tail(0)` | Initialize the two atomic indices to zero. |
| `{}` | Empty constructor body — all the real work happened in the initializer list. |

---

### The private members

```cpp
private:
    const int              _capacity;
    std::unique_ptr<int[]> _buf;
    std::atomic<int>       _head;     // written only by consumer
    std::atomic<int>       _tail;     // written only by producer
};
```

- `private:` — only the class's own member functions can touch these.
- `const int _capacity` — cannot change after construction. Must be set in
  the initializer list.
- `std::unique_ptr<int[]> _buf`:
  - Smart pointer with **single ownership** — no copying, only moving.
  - The `[]` in `<int[]>` says "I'm managing an *array*, so use `delete[]`
    on cleanup," not the wrong `delete`.
  - Index it as `_buf[i]` exactly like a raw array. When `_buf` is
    destroyed, the array is freed automatically — no manual `delete[]`,
    no leaks.
- `std::atomic<int> _head` / `_tail`:
  - Two integer indices read/written by **two different threads** with no
    mutex. The `atomic` wrapper makes those concurrent accesses safe.
  - Protocol: the consumer is the **only writer** of `_head`; the producer
    is the **only writer** of `_tail`. Each thread *reads* both.

---

### `size()`

```cpp
int size() override {
    int head = _head.load(std::memory_order_acquire);
    int tail = _tail.load(std::memory_order_acquire);
    return (tail - head + _capacity) % _capacity;
}
```

- `override` — tells the compiler this is supposed to override a `virtual`
  function in the base. Typos give an error instead of silently creating a
  new function.
- `_head.load(order)` — atomic read.
- `(tail - head + _capacity) % _capacity` — "distance modulo N." If
  `tail >= head` it's just `tail - head`. If `tail` has wrapped past
  `head`, the difference would be negative; adding `_capacity` and then
  `% _capacity` makes it the correct positive distance.

`size()` is a **snapshot**: between the two `load`s the producer or consumer
can run, so it's "the size at some point during the call," not at a single
instant. The spec doesn't require more.

---

### `pop(int &val)`

```cpp
bool pop(int &val) override {
    int head = _head.load(std::memory_order_relaxed);
    int tail = _tail.load(std::memory_order_acquire);

    if (head == tail) return false;

    val = _buf[head];
    _head.store((head + 1) % _capacity, std::memory_order_release);
    return true;
}
```

- `int &val` — `val` is a **reference** to the caller's variable. Writing
  `val = ...` updates the caller's `int` directly. Idiomatic C++ way to
  return a value alongside a success flag.
- `_head.load(relaxed)` — only the *consumer* writes `_head`, and the
  consumer is the one calling `pop`. Reading your own variable is not a
  synchronization point, so we use the cheapest atomic (no fences).
- `_tail.load(acquire)` — the *producer* writes `_tail`. We need `acquire`
  so that everything the producer did **before** its `release` store of
  `_tail` (in particular, writing `_buf[tail]`) is visible to us **after**
  this load.
- `if (head == tail) return false;` — empty test. No sleeping, no spinning;
  just report "queue empty" per the Ex2 spec.
- `val = _buf[head];` — copy out the data. Safe because of the acquire.
- `_head.store(..., release)` — advance the head. `release` tells the
  producer "if you observe this new `_head`, slot `head` is no longer being
  read."

---

### `push(int v)`

```cpp
bool push(int v) override {
    int tail = _tail.load(std::memory_order_relaxed);
    int next_tail = (tail + 1) % _capacity;

    if (next_tail == _head.load(std::memory_order_acquire)) return false;

    _buf[tail] = v;
    _tail.store(next_tail, std::memory_order_release);
    return true;
}
```

Mirror image of `pop`:

- `_tail.load(relaxed)` — producer reading its own variable, no sync needed.
- `next_tail = (tail + 1) % _capacity` — where tail will move *if* the push
  succeeds.
- `_head.load(acquire)` — consumer's variable, so we use `acquire`. If
  `next_tail == head`, the buffer is full → return `false`.
- `_buf[tail] = v;` — **first** write the data into the slot.
- `_tail.store(next_tail, release)` — **then** publish the new tail. The
  `release` is the publication step: it guarantees the data write above
  happens-before any thread that subsequently reads `_tail` with `acquire`.

If the CPU were allowed to reorder these two writes, the consumer could
read the new `_tail`, follow it to `_buf[tail]`, and grab whatever garbage
was there before the data write. The `release`/`acquire` pair forbids that.

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
