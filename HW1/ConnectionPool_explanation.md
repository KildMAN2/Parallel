# `ConnectionPool_Impl.h` — Explained

A blocking connection pool that hands out `std::shared_ptr<Connection>`s with
a **custom deleter**, so connections automatically return to the pool when
the last shared owner is destroyed.

---

## C++ Syntax Breakdown

### 1. The Inheritance Header

```cpp
class ConnectionPool : public ConnectionPoolAbstract {
public:
    explicit ConnectionPool(size_t poolSize) : ConnectionPoolAbstract(poolSize) {
        ...
    }
```

- `: public ConnectionPoolAbstract` — public inheritance from the abstract
  base supplied in `Connection.h`.
- `: ConnectionPoolAbstract(poolSize)` — calls the **base-class constructor**,
  which stores `poolSize` and initializes the `pool` queue.

---

### 2. Pre-Creating Connections

```cpp
for (size_t i = 0; i < poolSize; ++i) {
    pool.push(std::unique_ptr<Connection>(new Connection(static_cast<int>(i))));
}
```

- `pool` is inherited: `std::queue<std::unique_ptr<Connection>>`.
- `new Connection(i)` allocates a `Connection` on the heap.
- `std::unique_ptr<Connection>(...)` wraps the raw pointer so memory is freed
  automatically if anything throws (and so each slot has clear single
  ownership while sitting in the pool).
- `static_cast<int>(i)` silences a signed/unsigned conversion warning —
  `size_t` is unsigned, `Connection`'s constructor takes `int`.

> Note: this uses the **C++11-compatible** form `std::unique_ptr<Connection>(new Connection(...))`.
> `std::make_unique` (C++14) would also work — the spec allows C++14 for Ex4.

---

### 3. `borrowConnection` — Blocking Wait

```cpp
std::shared_ptr<Connection> borrowConnection() override {
    std::unique_lock<std::mutex> lock(_mutex);
    _available.wait(lock, [this]{ return !pool.empty(); });
    ...
}
```

- Locks the pool's mutex (RAII — released automatically).
- `_available.wait(lock, predicate)`:
  - if the pool is empty → releases the lock and sleeps;
  - on wakeup → re-acquires the lock and re-checks the predicate.
- The lambda `[this]{ return !pool.empty(); }` captures the surrounding
  object so it can call the inherited `pool.empty()`.

---

### 4. Transferring Ownership Out of the `unique_ptr`

```cpp
std::unique_ptr<Connection> conn = std::move(pool.front());
pool.pop();

Connection* raw = conn.release();
```

- `pool.front()` returns a reference to the front `unique_ptr`. `std::move`
  turns it into an rvalue so its contents can be **moved** into `conn`
  (the queue slot is left empty, then `pool.pop()` discards it).
- `conn.release()` gives up ownership and returns the raw `Connection*`.
  Critically, **the `Connection` is not deleted here** — we still need it.

We're handing the raw pointer to a `shared_ptr` next; that `shared_ptr`'s
custom deleter takes over lifetime management.

---

### 5. The Custom Deleter — The Heart of the Trick

```cpp
auto deleter = [this](Connection* c) {
    std::unique_lock<std::mutex> lk(_mutex);
    pool.push(std::unique_ptr<Connection>(c));
    _available.notify_one();
};

return std::shared_ptr<Connection>(raw, deleter);
```

Normally a `shared_ptr<Connection>` calls `delete` on its pointer when the
last copy is destroyed. The two-argument constructor lets us substitute
**any callable** as the cleanup action.

What this deleter does:

1. Locks the pool mutex.
2. Wraps the raw `Connection*` back into a `unique_ptr` and pushes it onto
   the queue. The `Connection` is **not destroyed** — it's recycled.
3. Notifies one waiting borrower that a slot is available.

The result: when the **last** `shared_ptr` referring to that connection goes
away (whether held by the original borrower or shared copies in other
threads), the connection automatically goes back into the pool. No manual
"release" call is needed by the user.

---

### 6. Why Capture `this` in the Deleter

```cpp
auto deleter = [this](Connection* c) { ... };
```

The deleter needs access to `_mutex`, `_available`, and `pool` — all member
variables. Capturing `this` makes those members reachable inside the lambda.

The lambda outlives the call to `borrowConnection` (it's stored inside the
`shared_ptr`'s control block), so the `ConnectionPool` object **must
outlive every borrowed connection** — otherwise the deleter would touch a
destroyed object. That's a natural constraint on a connection pool's
lifetime anyway.

---

### 7. `notify_one` vs `notify_all`

```cpp
_available.notify_one();
```

Each return pushes exactly **one** connection back, so waking exactly one
waiter is enough and avoids a thundering-herd of threads that would all
re-check the predicate and immediately go back to sleep.

---

### 8. Why `unique_ptr` in Storage, `shared_ptr` for Borrowing

- **In the pool** (`std::queue<std::unique_ptr<Connection>>`): only one
  owner at any moment — the pool itself. `unique_ptr` makes this exclusive
  ownership explicit and gives free exception safety.
- **Borrowed out** (`std::shared_ptr<Connection>`): callers may share the
  same connection across threads. Reference counting decides when the
  connection is no longer in use. The custom deleter hooks "reference count
  reaches zero" → "return to pool."

This split is the idiomatic C++ way to model "owned by a container, lent
out with shared, reference-counted access."

---

### 9. Why the Deleter Doesn't `delete` the Connection

Because we *want to keep it alive*. A connection is expensive to recreate
(per the problem statement); the whole point of the pool is to recycle the
same set of `Connection` objects forever. The pool's destructor will free
them when it itself is destroyed (each `unique_ptr` in the queue will
delete its `Connection`).
