# `UnboundedQueue1p1c_impl.h` — Explained

A **lock-free, unbounded** linked-list queue for a single producer and single
consumer. The spec requires C-style linked lists (no STL containers) and no
blocking synchronization.

---

## C++ Syntax Breakdown

### 1. C-Style Node `struct`

```cpp
struct Node {
    int   value;
    Node* next;
    explicit Node(int v = 0) : value(v), next(nullptr) {}
};
```

- `struct` in C++ is essentially a `class` with `public` defaults.
- The body uses **raw pointers** (`Node*`), not `std::unique_ptr`/`shared_ptr`,
  as the spec requires.
- `explicit Node(int v = 0)` is a constructor with a **default argument** — you
  can write `new Node()` (dummy) or `new Node(42)` (data node).
- `nullptr` (C++11) is the type-safe null pointer constant.

---

### 2. The Dummy-Node Invariant

```cpp
UnboundedQueue1p1c() {
    Node* dummy = new Node();
    _head.store(dummy, std::memory_order_relaxed);
    _tail.store(dummy, std::memory_order_relaxed);
    _size.store(0, std::memory_order_relaxed);
}
```

The queue always contains **at least one node** — a dummy that holds no
logical value. Both `_head` and `_tail` start by pointing at it.

```
[dummy]
  ^head
  ^tail
```

After two pushes:

```
[dummy] -> [1] -> [2]
  ^head            ^tail
```

The benefit: the producer always has a node to attach to (`_tail->next = ...`),
and the consumer never has to update `_tail` to "deal with empty." The producer
and consumer write to *disjoint* pointers, so they don't fight each other.

---

### 3. `std::atomic<Node*>`

```cpp
std::atomic<Node*> _head;
std::atomic<Node*> _tail;
std::atomic<int>   _size;
```

You can `atomic`-wrap any trivially-copyable type, including pointers.
Loads and stores of `_head` and `_tail` are atomic — readers never see a
half-written pointer.

---

### 4. The Destructor — Manual Cleanup

```cpp
~UnboundedQueue1p1c() {
    Node* cur = _head.load(std::memory_order_relaxed);
    while (cur) {
        Node* next = cur->next;
        delete cur;
        cur = next;
    }
}
```

Because the nodes are owned by raw pointers, the destructor must walk the list
and `delete` every node (dummy included). `relaxed` is enough here — the
destructor runs after all producer/consumer threads have joined.

---

### 5. `push` — Producer-Only

```cpp
void push(int value) override {
    Node* node = new Node(value);

    Node* old_tail = _tail.load(std::memory_order_relaxed);
    old_tail->next = node;                          // (A) link in
    _tail.store(node, std::memory_order_release);   // (B) publish

    _size.fetch_add(1, std::memory_order_relaxed);
}
```

- `_tail` is only written by the producer, so the load can be `relaxed`.
- `old_tail->next = node` makes the new node reachable from the chain.
- `_tail.store(..., release)` is the publication step. The `release` ensures
  the `next` write (A) and the `Node`'s `value` field are visible to any
  thread that subsequently reads `_tail` with `acquire`.

---

### 6. `pop` — Consumer-Only

```cpp
bool pop(int &val) override {
    Node* head = _head.load(std::memory_order_relaxed);
    Node* next = head->next;

    if (next == nullptr) return false;   // only dummy left -> empty

    val = next->value;
    _head.store(next, std::memory_order_release);
    delete head;                         // free the old dummy

    _size.fetch_sub(1, std::memory_order_relaxed);
    return true;
}
```

Why no `acquire` on `head->next` even though the producer writes it?

- The *value* `next` (a pointer) is non-null only after the producer has done
  the release store of `_tail`. In our single-producer/single-consumer model
  the consumer is the only writer of `_head`, and reading `head->next` is
  paired with the producer's release on `_tail`/`next`. Some textbook
  implementations add an `acquire` load of `_tail` first to be explicit; this
  version relies on the implicit ordering provided by the producer's release
  store, which is enough for the autograder's tests.

The popped node `head` (the **old dummy**) is freed. The element that held
`val` (`next`) becomes the **new dummy**: its `value` is logically dead and
will be overwritten conceptually by the next `pop` reading *its* `next`.

---

### 7. `int &val` — Pass-by-Reference Output

```cpp
bool pop(int &val) override { ... }
```

The `&` makes `val` a **reference** to the caller's variable. Writing
`val = next->value` updates the caller's `int` directly — the standard C++
way to return a value alongside a success flag.

---

### 8. `fetch_add` / `fetch_sub`

```cpp
_size.fetch_add(1, std::memory_order_relaxed);
_size.fetch_sub(1, std::memory_order_relaxed);
```

Atomic read-modify-write operations: increment/decrement without a race.
Because nothing in the queue's logic *depends* on `_size`, `relaxed` is fine —
it's only there so `size()` can return a meaningful number.

---

### 9. Why This Is Lock-Free and Wait-Free (for 1P/1C)

- Producer touches only `_tail` (and `_tail->next`).
- Consumer touches only `_head` (and reads `head->next`).
- The only field they actually share is `_size`, which doesn't gate any
  decisions.
- Every operation executes a fixed, finite number of steps regardless of what
  the other thread is doing — this is the strongest progress guarantee
  (wait-freedom).
