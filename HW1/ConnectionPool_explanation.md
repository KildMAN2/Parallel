# `ConnectionPool_Impl.h` — Explained

A blocking connection pool that hands out `std::shared_ptr<Connection>`s with
a **custom deleter**, so connections automatically return to the pool when
the last shared owner is destroyed.

---

## Background: What Are Smart Pointers?

A **smart pointer** is a tiny object that holds a raw pointer and runs its
destructor when the smart pointer itself goes out of scope. That destructor
does the cleanup (`delete`) — so you can never forget. This is called
**RAII** (Resource Acquisition Is Initialization).

C++ has three smart pointers in `<memory>`:

| Type | Ownership | Copyable? | Moveable? | Overhead |
|---|---|---|---|---|
| `std::unique_ptr<T>` | **exactly one** owner | ❌ no | ✅ yes | none (size of a raw pointer) |
| `std::shared_ptr<T>` | **many** owners | ✅ yes | ✅ yes | extra atomic ref-count |
| `std::weak_ptr<T>` | **non-owning** observer of a `shared_ptr` | ✅ yes | ✅ yes | similar to `shared_ptr` |

---

### `std::unique_ptr<T>`

```cpp
std::unique_ptr<Connection> p(new Connection(42));
p->use();          // works like a normal pointer
// no delete needed; ~unique_ptr() will delete the Connection
```

- **Only one** `unique_ptr` can own a given object. Trying to copy it is a
  compile error: `unique_ptr q = p;` ❌.
- You can **move** ownership: `unique_ptr q = std::move(p);` — afterwards
  `p` is empty (`nullptr`), `q` is the owner.
- `p.release()` gives up ownership and returns the raw pointer. `p` becomes
  `nullptr`, but the object is **not** deleted — the caller now has to manage
  it (or hand it to another smart pointer).
- `p.reset()` deletes the current object and (optionally) takes a new one.

Cost: zero. A `unique_ptr` is the same size as a raw pointer.

#### Example: move, release, reset

```cpp
#include <memory>
#include <iostream>

struct Conn {
    int id;
    Conn(int i) : id(i) { std::cout << "create " << id << "\n"; }
    ~Conn()           { std::cout << "destroy " << id << "\n"; }
};

int main() {
    std::unique_ptr<Conn> p(new Conn(1));
    std::cout << "p holds: " << (p ? p->id : -1) << "\n";

    // MOVE: ownership transfers; p becomes empty
    std::unique_ptr<Conn> q = std::move(p);
    std::cout << "after move, p empty? " << (p == nullptr) << "\n";
    std::cout << "q holds: " << q->id << "\n";

    // RELEASE: gives up ownership, returns raw pointer, does NOT delete
    Conn* raw = q.release();
    std::cout << "after release, q empty? " << (q == nullptr) << "\n";
    std::cout << "raw still alive: " << raw->id << "\n";
    delete raw;   // we must clean up manually now

    // RESET: deletes current, optionally takes a new one
    std::unique_ptr<Conn> r(new Conn(2));
    r.reset(new Conn(3));   // deletes Conn(2), now holds Conn(3)
    // r goes out of scope here → deletes Conn(3)
}
```

Output:

```
create 1
p holds: 1
after move, p empty? 1
q holds: 1
after release, q empty? 1
raw still alive: 1
destroy 1
create 2
create 3
destroy 2
destroy 3
```

Note how `release()` does **not** print `destroy 1` until we manually
`delete raw` — that's exactly the mechanism the connection pool uses to
hand the object off to a `shared_ptr`.

---

### `std::shared_ptr<T>`

```cpp
std::shared_ptr<Connection> a(new Connection(42));
std::shared_ptr<Connection> b = a;     // copying allowed; both own
std::shared_ptr<Connection> c = a;     // three owners now
// Connection is destroyed only when a, b, and c are ALL gone
```

`shared_ptr` implements **reference counting**. Every `shared_ptr` to the
same object shares a hidden **control block** on the heap that stores:

```
+----------------+
| use_count      |   ← how many shared_ptr objects point here
| weak_count     |   ← how many weak_ptr objects point here
| deleter        |   ← the callable used to clean up (default: delete)
| (the object,   |
|  if made with  |
|  make_shared)  |
+----------------+
```

Every time you copy a `shared_ptr`, `use_count` is **atomically incremented**.
Every time one is destroyed, `use_count` is atomically decremented. When the
last copy goes (`use_count` hits 0), the deleter runs and frees the object.

This is exactly the semantics we want for a "borrowed connection that
multiple threads may share."

#### Example: watching the ref-count change

```cpp
#include <memory>
#include <iostream>

struct Conn {
    int id;
    Conn(int i) : id(i) { std::cout << "create " << id << "\n"; }
    ~Conn()           { std::cout << "destroy " << id << "\n"; }
};

int main() {
    std::shared_ptr<Conn> a(new Conn(7));
    std::cout << "after a: use_count = " << a.use_count() << "\n";

    {
        std::shared_ptr<Conn> b = a;     // copy → +1
        std::cout << "after b: use_count = " << a.use_count() << "\n";

        std::shared_ptr<Conn> c = a;     // copy → +1
        std::cout << "after c: use_count = " << a.use_count() << "\n";
    } // b and c go out of scope → -2

    std::cout << "after scope: use_count = " << a.use_count() << "\n";
}   // a goes out of scope → -1 → destroy 7
```

Output:

```
create 7
after a: use_count = 1
after b: use_count = 2
after c: use_count = 3
after scope: use_count = 1
destroy 7
```

The `Conn` is destroyed only when the **last** `shared_ptr` is gone — not
when the first or second copy disappears. That "last owner cleans up"
guarantee is what the pool exploits.

#### Cost of `shared_ptr`

- **Two heap allocations** by default: one for the object, one for the
  control block. `std::make_shared<T>(args...)` fuses them into one
  allocation — cheaper and faster.
- Every copy/destroy does an **atomic** ref-count update. Cheap but not free.
- The object itself is `sizeof(2 * pointer)` — one for the object, one for
  the control block.

#### `use_count()`

`sp.use_count()` returns the current number of owners. Useful for debugging
(and the worked example below), but rarely needed in production code.

---

### Custom Deleters — the killer feature for this exercise

A `shared_ptr` doesn't have to call `delete`. You can hand it any callable:

```cpp
std::shared_ptr<int> p(new int(42), [](int* x) {
    std::cout << "custom cleanup\n";
    delete x;
});
```

When the last owner dies, your lambda runs instead of the default `delete`.
**This is the entire reason the connection-pool exercise is solvable
cleanly.** The deleter is stored in the control block, so even copies of
the `shared_ptr` made later still trigger the same custom cleanup.

For our pool, the "cleanup" isn't `delete` — it's "put the connection back
in the pool."

#### Example: a deleter that recycles instead of deleting

A mini-version of the connection-pool idea — a single "slot" instead of a
queue, but the same pattern:

```cpp
#include <memory>
#include <iostream>

struct Conn {
    int id;
    Conn(int i) : id(i) { std::cout << "create " << id << "\n"; }
    ~Conn()           { std::cout << "destroy " << id << "\n"; }
};

int main() {
    Conn* slot = new Conn(99);   // pre-created, lives outside any shared_ptr

    auto recycler = [&](Conn* c) {
        std::cout << "returning " << c->id << " to the slot (NOT deleting)\n";
        slot = c;   // "put it back"
    };

    {
        std::shared_ptr<Conn> borrowed(slot, recycler);
        slot = nullptr;             // the slot is now "empty" until returned
        std::cout << "using id=" << borrowed->id << "\n";
    } // borrowed dies → recycler runs → slot is non-null again

    std::cout << "slot has id=" << slot->id << " again\n";
    delete slot;                    // real cleanup at program end
}
```

Output:

```
create 99
using id=99
returning 99 to the slot (NOT deleting)
slot has id=99 again
destroy 99
```

Notice there is **no `destroy 99`** in the middle — the `shared_ptr`'s
death does not destroy the object, because the deleter chose recycling
instead. That's exactly the trick `ConnectionPool::borrowConnection` uses.

---

## Why This Combination — `unique_ptr` Inside, `shared_ptr` Outside

| Where | Smart pointer | Why |
|---|---|---|
| `pool` queue (idle connections) | `unique_ptr<Connection>` | Only one owner: the pool itself. No ref-counting overhead. Makes ownership unambiguous. |
| Borrowed connection (returned to caller) | `shared_ptr<Connection>` | Caller may copy and share it across threads. Pool returns it only when the last user is done. |

The hand-off pattern is:

```
[pool: unique_ptr]  ──borrow──►  [caller: shared_ptr]  ──last owner gone──►  [pool: unique_ptr again]
       ↑                                                                              │
       └──────────────────── custom deleter ◄─────────────────────────────────────────┘
```

---

## Line-by-Line Walkthrough

### File header

```cpp
/* Written by Sari Mansour, 2026 */

#pragma once

#include "Connection.h"           // ConnectionPoolAbstract, Connection, pool member
#include <memory>                 // std::unique_ptr, std::shared_ptr
#include <mutex>                  // std::mutex, std::unique_lock
#include <condition_variable>     // std::condition_variable
```

- `#pragma once` — header guard (include this file at most once per
  translation unit).
- `Connection.h` gives us the abstract base class (which already declares
  the `pool` member as `std::queue<std::unique_ptr<Connection>>`).

---

### Class declaration

```cpp
class ConnectionPool : public ConnectionPoolAbstract {
private:
    std::mutex              _mutex;
    std::condition_variable _available;
public:
```

- Inherits publicly from `ConnectionPoolAbstract`, so a `ConnectionPool*`
  can be used wherever a `ConnectionPoolAbstract*` is expected.
- `_mutex` — guards every access to the `pool` queue (the queue itself is
  **not** thread-safe).
- `_available` — condition variable used to put threads to sleep when the
  pool is empty, and to wake them when a connection is returned.

> The base class declares `pool` and `poolSize` as `protected`, so this
> derived class can use them directly.

---

### The constructor

```cpp
explicit ConnectionPool(size_t poolSize) : ConnectionPoolAbstract(poolSize) {
    for (size_t i = 0; i < poolSize; ++i) {
        pool.push(std::unique_ptr<Connection>(new Connection(static_cast<int>(i))));
    }
}
```

- `explicit` — disables implicit conversions like
  `ConnectionPool p = 5;` (would compile and silently create a pool of 5
  without `explicit`).
- `: ConnectionPoolAbstract(poolSize)` — calls the **base-class constructor**
  first, which stores `poolSize` and initializes the queue.
- The loop creates `poolSize` `Connection` objects on the heap (`new Connection(i)`),
  immediately wraps each in a `unique_ptr` (no manual `delete` needed), and
  pushes it into the inherited `pool` queue.
- `static_cast<int>(i)` is an explicit conversion from `size_t` (unsigned)
  to `int` (signed), silencing a warning. The `Connection` constructor
  expects `int id`.

> All connections are **pre-created** here. This is the entire reason a
> pool exists: connections are expensive, so we pay the cost once at startup
> and recycle them forever.

---

### `borrowConnection` — line by line

```cpp
std::shared_ptr<Connection> borrowConnection() override {
    std::unique_lock<std::mutex> lock(_mutex);
    _available.wait(lock, [this]{ return !pool.empty(); });
```

1. `override` — confirms this overrides the base's pure virtual.
2. `std::unique_lock<std::mutex> lock(_mutex)` — locks `_mutex`. RAII
   guarantees `_mutex` is released when `lock` goes out of scope (at the
   `}` of the function), even if an exception is thrown mid-way.
3. `_available.wait(lock, predicate)`:
   - if `pool.empty()` → atomically releases the lock and puts this thread
     to sleep on `_available`,
   - on wakeup → re-acquires the lock and re-checks the predicate (handles
     spurious wakeups automatically).
4. The lambda `[this]{ return !pool.empty(); }` **captures `this`** so it
   can read the inherited `pool` member.

```cpp
    std::unique_ptr<Connection> conn = std::move(pool.front());
    pool.pop();
```

5. `pool.front()` returns a reference to the front `unique_ptr`. A
   `unique_ptr` cannot be copied, so we must **move** it. `std::move(x)`
   doesn't actually move anything — it just casts `x` to an rvalue so the
   move constructor of `unique_ptr` is selected. After this line, the
   queue's front `unique_ptr` is empty, and `conn` owns the connection.
6. `pool.pop()` discards the now-empty `unique_ptr` from the queue.

   Visually, if the queue starts as:

   ```
   pool: [ uptr(C0) , uptr(C1) , uptr(C2) ]
           ^ front
   ```

   then `std::move(pool.front())` followed by `pool.pop()` leaves:

   ```
   pool: [ uptr(C1) , uptr(C2) ]
   conn: uptr(C0)
   ```

   The `Connection` object itself never moved in memory — only ownership
   changed hands from "the queue's first slot" to the local variable `conn`.

```cpp
    Connection* raw = conn.release();
```

7. `release()` does two things:
   - Returns the raw pointer (`Connection*`) currently held.
   - Sets the `unique_ptr` to `nullptr` so its destructor will **not** call
     `delete` on the object.

   The raw pointer is now "unmanaged" — *we* are responsible for cleanup
   until something else takes over. The next line hands it to a
   `shared_ptr`, which is the new manager.

   Tiny example contrasting `release` with the default destructor:

   ```cpp
   {
       std::unique_ptr<Conn> u(new Conn(1));
   } // ~unique_ptr → delete → "destroy 1"

   {
       std::unique_ptr<Conn> u(new Conn(2));
       Conn* raw = u.release();   // u is now nullptr, Conn(2) NOT deleted
       // ... here: raw is alive and orphaned ...
       delete raw;                // we must do this ourselves
   }
   ```

   In our code we never call `delete raw` — instead the very next line
   hands `raw` to `std::shared_ptr<Connection>(raw, deleter)`, and that
   shared_ptr takes over the cleanup responsibility.

```cpp
    auto deleter = [this](Connection* c) {
        std::unique_lock<std::mutex> lk(_mutex);
        pool.push(std::unique_ptr<Connection>(c));
        _available.notify_one();
    };
```

8. `auto` — let the compiler deduce the lambda's anonymous type.
9. `[this]` — capture the enclosing object's `this` pointer so the lambda
   can access `_mutex`, `_available`, and `pool` later.
10. `(Connection* c)` — the lambda takes the raw pointer that the
    `shared_ptr` is about to release.
11. Inside the lambda:
    - Lock the pool mutex.
    - Wrap `c` back into a `unique_ptr` and push it onto the pool.
      **The `Connection` object is not deleted** — it's *recycled*.
    - Notify one waiter that a connection is available.

    Example sequence — what the deleter looks like "from the outside":

    ```cpp
    ConnectionPool pool(1);                    // pool has C0 inside
    {
        auto sp = pool.borrowConnection();     // pool: [],  sp owns C0
        sp->use();
    } // sp dies → deleter runs → pool: [C0] again, NOT destroyed

    auto sp2 = pool.borrowConnection();        // immediately gets C0 back
    ```

    The same `Connection` object (`C0`) goes round and round between the
    pool and the callers for the whole lifetime of the program. No new
    `Connection` is ever constructed after the pool's constructor.

```cpp
    return std::shared_ptr<Connection>(raw, deleter);
}
```

12. `std::shared_ptr<Connection>(raw, deleter)` — the two-argument
    constructor: "manage this raw pointer, but instead of calling `delete`,
    call this deleter when the last owner is gone."
13. The returned `shared_ptr` starts with `use_count() == 1`. Copies bump
    the count; destruction decrements it. When it reaches zero, the deleter
    runs.

> Note: the function returns by **value**, but C++17 mandates copy elision,
> and earlier versions still optimize the return — no extra ref-count
> increment happens at the return point.

---

## What the User Code Looks Like

```cpp
ConnectionPool pool(3);                        // pre-creates 3 connections

void worker() {
    auto conn = pool.borrowConnection();       // blocks if pool empty
    conn->use();                               // do work
    // Optionally share with helper threads:
    std::thread helper([conn]{ conn->use(); }); // copies the shared_ptr
    helper.join();
    // When conn (and the helper's copy) go out of scope, the connection
    // automatically returns to the pool.
}
```

The user **never** calls a `release` or `returnConnection` method. The
deleter inside the `shared_ptr` does it automatically when the last owner
disappears.

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

---

## Worked Example

`ConnectionPool pool(2);` is constructed with two pre-built connections,
`C0` and `C1`. Two worker threads `T1` and `T2` and a third late-arriving
thread `T3` use it.

### Step 0 — initial state

```
pool queue: [ unique_ptr(C0) , unique_ptr(C1) ]
mutex:      free
waiters:    none
```

### Step 1 — `T1.borrowConnection()`

```cpp
lock mutex
_available.wait(...)              // pool not empty → returns immediately
conn = move(pool.front())         // takes ownership of C0's unique_ptr
pool.pop()
raw = conn.release()              // raw = C0*, unique_ptr now empty
return shared_ptr<Connection>(raw, deleter)
unlock
```

```
pool queue: [ unique_ptr(C1) ]
T1 holds:   shared_ptr(C0)        use_count = 1
```

### Step 2 — `T1` shares with `T2`

```cpp
// T1:
auto copy = sp_C0;                // pass to T2 via some channel
```

```
T1 holds:   shared_ptr(C0)        \
T2 holds:   shared_ptr(C0)        / use_count = 2
pool queue: [ unique_ptr(C1) ]
```

Note: even though *two* threads hold C0, the **pool still has only one
connection left**. The shared_ptr's refcount tracks active users, the pool
tracks availability.

### Step 3 — `T3.borrowConnection()` takes C1

```
pool queue: [ ]                 (empty!)
T3 holds:   shared_ptr(C1)      use_count = 1
```

### Step 4 — `T4.borrowConnection()` blocks

```cpp
lock mutex
_available.wait(lock, [this]{ return !pool.empty(); })
// pool IS empty → release mutex, sleep on _available
```

```
pool queue: [ ]
waiters:    T4 sleeping on _available
```

### Step 5 — `T1` finishes; its `shared_ptr` is destroyed

```cpp
// T1's local sp_C0 goes out of scope:
// use_count drops 2 → 1
// Deleter does NOT run yet, because T2 still holds a copy.
```

```
T2 holds:   shared_ptr(C0)      use_count = 1
pool queue: [ ]                 ← C0 still NOT returned
T4 still sleeping
```

This is the key behavior the spec asks for: a connection is returned only
when **every** thread that shares it is done.

### Step 6 — `T2` finishes; deleter fires

```cpp
// T2's copy goes out of scope:
// use_count drops 1 → 0
// Custom deleter runs:
lk(_mutex)
pool.push(unique_ptr<Connection>(C0))      // C0 NOT deleted; recycled
_available.notify_one()                    // wakes T4
```

```
pool queue: [ unique_ptr(C0) ]
T4 wakes inside wait(), predicate true, takes C0:
T4 holds:   shared_ptr(C0)      use_count = 1
pool queue: [ ]
```

### Step 7 — pool destruction

When `pool` itself is finally destroyed, the `queue<unique_ptr<Connection>>`
runs each `unique_ptr`'s destructor, which calls `delete` on the
`Connection` — only **now** is the underlying object actually freed.

```
~ConnectionPool runs
~queue runs
~unique_ptr(C0) runs → delete C0  → "Connection 0 destroyed."
~unique_ptr(C1) runs → delete C1  → "Connection 1 destroyed."
```

### Three invariants this trace illustrates

1. **Pool capacity is fixed.** No `new Connection(...)` ever runs outside
   the constructor; the deleter only **recycles** existing objects.
2. **Sharing delays return.** A borrowed connection is returned only when
   every `shared_ptr` to it has been destroyed.
3. **Borrowing blocks when empty.** The condition variable ensures the
   waiter sleeps efficiently (no spinning) and is woken the instant a
   connection is returned.
