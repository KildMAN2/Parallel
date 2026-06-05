# `BoundedQueue_impl.h` — Explained

---

## C++ Syntax Breakdown

### 1. The Underscore Prefix (`_capacity`, `_buf`, etc.)
Just a naming convention — the leading `_` marks a variable as a **private member** of the class.  
It has no special meaning in C++; it's purely for readability to distinguish members from local variables.

---

### 2. Class Inheritance and `override`

```cpp
class BoundedQueue : public BoundedQueueAbstract {
```
- `: public BoundedQueueAbstract` means `BoundedQueue` **inherits** from `BoundedQueueAbstract`
- It must implement every `virtual` method declared in the base class

```cpp
int size() override { ... }
```
- `override` tells the compiler: "this method is replacing a virtual method from the base class"
- If you typo the name or signature, the compiler gives an error instead of silently creating a new method

---

### 3. The Constructor — `explicit` and Initializer List

```cpp
explicit BoundedQueue(int capacity)
    : _capacity(capacity),
      _buf(std::make_unique<int[]>(capacity)),
      _head(0), _tail(0), _count(0)
{}
```

- **`explicit`** — prevents the compiler from automatically converting an `int` to a `BoundedQueue` by accident
- **`: member(value), ...`** — the **member initializer list**; this is how you initialize member variables in C++. It runs *before* the constructor body `{}`
- **`std::make_unique<int[]>(capacity)`** — allocates an array of `capacity` ints on the heap and wraps it in a smart pointer. No manual `new`/`delete` needed

---

### 4. `std::unique_ptr<int[]>`

```cpp
std::unique_ptr<int[]> _buf;
```
- A **smart pointer** — automatically frees the memory when the object is destroyed
- `int[]` means it holds an array of ints (uses `delete[]` internally, not just `delete`)
- Access elements with `_buf[i]` just like a regular array

---

### 5. `std::mutex` and `std::unique_lock`

```cpp
std::mutex _mutex;

std::unique_lock<std::mutex> lock(_mutex);  // inside a method
```
- A **mutex** is a lock that only one thread can hold at a time — prevents two threads from modifying the queue simultaneously
- `unique_lock` acquires the mutex on construction and **automatically releases it** when the variable goes out of scope (RAII)
- You don't need to manually unlock — it happens automatically at the end of the function

---

### 6. `condition_variable` and `wait` with a Lambda

```cpp
std::condition_variable _not_empty;

_not_empty.wait(lock, [this]{ return _count > 0; });
```

- A **condition variable** lets a thread sleep until some condition becomes true
- `.wait(lock, predicate)` does three things:
  1. Checks the predicate (`_count > 0`)
  2. If **false** → releases the mutex and puts the thread to sleep
  3. When woken up → re-acquires the mutex and checks the predicate again (loop)
- **`[this]`** is a **lambda capture** — it gives the anonymous function `{ return _count > 0; }` access to the object's members (`this` = pointer to the current object)

In plain English:
```
wait until _count > 0, sleeping in the meantime
```

---

### 7. `notify_one()`

```cpp
_not_empty.notify_one();
```
- Wakes up **one** thread that is sleeping in `.wait()` on that condition variable
- Used after a push (signals: "there's now something to pop") and after a pop (signals: "there's now a free slot to push into")

---

## Overview
A thread-safe, fixed-capacity FIFO queue using a **circular buffer**. Blocking semantics: `push` waits when full, `pop` waits when empty.

---

## Data Layout — Circular Buffer

```
Index:  0    1    2    3    4
       [10] [20] [30] [ ] [ ]
              ↑              ↑
            _head          _tail
```

- `_head` — index of the next element to **read** (pop)
- `_tail` — index of the next **empty slot** to write (push)
- Both wrap around modulo `_capacity` — no shifting, O(1) operations

---

## Member Variables

| Variable | Type | Purpose |
|---|---|---|
| `_capacity` | `const int` | Max elements; fixed at construction, never changes |
| `_buf` | `unique_ptr<int[]>` | Heap-allocated array; all memory allocated once in constructor |
| `_head` | `int` | Read index |
| `_tail` | `int` | Write index |
| `_count` | `int` | Current number of elements |
| `_mutex` | `std::mutex` | Protects all shared state |
| `_not_empty` | `condition_variable` | Threads waiting to `pop` sleep here |
| `_not_full` | `condition_variable` | Threads waiting to `push` sleep here |

---

## `push(int v)` — Insert an element

```cpp
void push(int v) {
    std::unique_lock<std::mutex> lock(_mutex);                   // 1. acquire lock
    _not_full.wait(lock, [this]{ return _count < _capacity; }); // 2. wait if full
    _buf[_tail] = v;                                             // 3. write value
    _tail = (_tail + 1) % _capacity;                            // 4. advance tail (wraps)
    ++_count;                                                    // 5. update count
    _not_empty.notify_one();                                     // 6. wake a waiting pop
}
```

The lambda in `wait` guards against **spurious wakeups** — the thread only proceeds when the condition is genuinely true.

---

## `pop()` — Remove an element

```cpp
int pop() {
    std::unique_lock<std::mutex> lock(_mutex);                   // 1. acquire lock
    _not_empty.wait(lock, [this]{ return _count > 0; });         // 2. wait if empty
    int val = _buf[_head];                                       // 3. read value
    _head = (_head + 1) % _capacity;                            // 4. advance head (wraps)
    --_count;                                                    // 5. update count
    _not_full.notify_one();                                      // 6. wake a waiting push
    return val;
}
```

---

## `size()` — Thread-safe count

```cpp
int size() {
    std::unique_lock<std::mutex> lock(_mutex);
    return _count;
}
```

Holds the lock while reading `_count` to prevent a data race with concurrent pushes/pops.

---

## Synchronization Flow

```
Producer (push)                Consumer (pop)
──────────────                 ──────────────
lock mutex                     lock mutex
wait if _count == _capacity    wait if _count == 0
  └─ sleeps on _not_full         └─ sleeps on _not_empty
write to _buf[_tail]           read from _buf[_head]
++_count                       --_count
notify _not_empty  ──────────► wakes sleeping pop
unlock                         notify _not_full ──► wakes sleeping push
                               unlock
```

---

## Key Design Decisions

- **`unique_ptr<int[]>`** — RAII ownership; no manual `delete[]`, no memory leak
- **Circular buffer** — O(1) push/pop with no reallocation; satisfies the "all memory allocated at creation" requirement
- **`condition_variable::wait` with predicate** — correctly handles spurious wakeups without extra `while` loops
- **`notify_one` not `notify_all`** — only one thread can proceed (only one slot freed/filled), so waking one is sufficient and more efficient
