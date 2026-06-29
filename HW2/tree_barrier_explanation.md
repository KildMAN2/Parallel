# Combining Binary Tree Barrier — Full Explanation

This document explains both files for Exercise 2:

- `tree_barrier.h` — the provided header (abstract interface + thread id)
- `tree_barrier_impl.h` — your `BinaryTreeBarrier` implementation

---

## 1. What is a Barrier?

A **barrier** is a synchronization point: every thread that reaches `barrier()`
must wait until **all** threads have reached it. Only then are they all released
to continue. It is used between phases of a parallel algorithm so that no thread
runs ahead with stale data.

### The contention problem
The simple "sense barrier" uses **one shared counter** that every thread
decrements. With many threads, they all hammer the same cache line at once —
this is **memory contention** and it does not scale.

### The combining-tree fix
A **combining tree barrier** spreads the load. Threads are grouped in pairs at
the leaves of a binary tree. Each pair synchronizes locally; the "last" thread
of the pair climbs one level up and synchronizes with the neighboring pair, and
so on up to the root. Only `log2(N)` levels of small (2-way) contention happen
instead of one huge N-way pile-up.

```
        root            <- last 2 children meet here, then release cascades down
       /    \
     n1      n2          <- internal nodes (combine pairs of leaves)
    /  \    /  \
  L0   L1  L2   L3       <- leaves (each waits for 2 threads)
  /\   /\  /\   /\
 t0 t1 ...        t7     <- 8 threads
```

---

## 2. The Header — `tree_barrier.h`

```cpp
class BinaryTreeBarrierAbstract {
    public:
        virtual void barrier() = 0;   // pure virtual -> abstract class
};

// every thread sets this to its own id in [0..N-1] before using the barrier
thread_local int thread_id;
```

**Syntax notes**

- `virtual void barrier() = 0;` — the `= 0` makes it a **pure virtual**
  function, so `BinaryTreeBarrierAbstract` is **abstract** (cannot be
  instantiated). Your class must override `barrier()`.
- `thread_local int thread_id;` — `thread_local` gives **each thread its own
  private copy** of this variable. The test harness assigns `thread_id = i`
  inside each thread, so the barrier can tell threads apart without any map.

**Example — `thread_local` in action:**
```cpp
thread_local int thread_id;   // one private copy PER thread

void run(int id, BinaryTreeBarrier& bar) {
    thread_id = id;           // thread 3 stores 3 in ITS OWN copy
    bar.barrier();            // inside, reading thread_id gives 3 for this thread,
                              // 5 for the thread that set it to 5, etc.
}
// Without thread_local, all threads would share one variable and overwrite
// each other's id -> chaos.
```

---

## 3. The Implementation — `tree_barrier_impl.h`

```cpp
class BinaryTreeBarrier : public BinaryTreeBarrierAbstract {
```
`: public BinaryTreeBarrierAbstract` means **inherit** from the abstract class
and implement its interface.

### 3.1 `RADIX`
```cpp
static const int RADIX = 2;
```
A **binary** tree, so every node combines exactly 2 arrivals. `static const`
means it is a single shared compile-time constant for the class.

### 3.2 The `Node` struct — one tree node
```cpp
struct Node {
    std::atomic<int>  count;   // remaining arrivals for this round
    std::atomic<bool> sense;   // current release flag at this node
    int               size;    // arrivals expected (== RADIX == 2)
    int               parent;  // index of parent node, -1 for the root
    Node() : count(0), sense(false), size(0), parent(-1) {}
};
```

**Syntax notes**

- `std::atomic<int>` / `std::atomic<bool>` — values that multiple threads can
  read/write **safely without a mutex**. Operations are indivisible.
- `Node() : count(0), ... {}` — a **member-initializer list**; it initializes
  each field. Atomics *must* be initialized this way (their default constructor
  leaves them with an undefined value).

**Field meanings**

| Field | Meaning |
|-------|---------|
| `count` | how many more arrivals this node still needs this round (starts at 2) |
| `sense` | the flag waiters spin on; flipping it releases them |
| `size`  | the value `count` is reset to each round (always 2) |
| `parent`| heap index of the parent node, `-1` if this is the root |

### 3.3 `PaddedSense` — per-thread sense flag
```cpp
struct PaddedSense {
    bool sense;
    char padding[64 - sizeof(bool)];   // fill the rest of a 64-byte cache line
    PaddedSense() : sense(true) {}     // start at true
};
```
Each thread keeps a `sense` flag it **flips every round** (true → false → true…).
The `padding` pushes each thread's flag onto its **own cache line** so two
threads never write to the same line — avoiding **false sharing** (a slowdown
where unrelated writes invalidate each other's cache). Starting at `true`
matches the nodes starting at `false`.

**Example — false sharing without padding:**
```
Cache line (64 bytes):  [ flag0 ][ flag1 ][ flag2 ] ...   <- all share ONE line
Thread 0 writes flag0  -> CPU invalidates the whole line on every other core
Thread 1 writes flag1  -> invalidates it again, even though flag1 != flag0
```
The writes don't actually conflict logically, but the hardware tracks whole
cache lines, so the cores keep stealing the line from each other ("ping-pong")
and everything slows down. With `PaddedSense` each flag sits alone on its own
64-byte line, so the cores never fight:
```
[ flag0 + padding (64B) ][ flag1 + padding (64B) ][ flag2 + padding (64B) ]
```

### 3.4 Data members
```cpp
Node*                    m_nodes;       // array of N-1 tree nodes
int                      m_numNodes;    // = N - 1
int                      m_numThreads;  // = N
int                      m_firstLeaf;   // heap index of the first leaf
std::vector<PaddedSense> m_threadSense; // one flag per thread
```

### 3.5 The heap layout (how the tree is stored)
The tree is stored in a flat array like a binary heap:

- root is index `0`, parent of `i` is `(i-1)/2`, children are `2i+1`, `2i+2`
- with `N` threads there are `N-1` nodes total
- the last `N/2` indices are the **leaves**; `m_firstLeaf = N/2 - 1`

**Example, N = 8** → 7 nodes:

```
index:   0   1   2   3   4   5   6
role:  root  in  in  L0  L1  L2  L3
leaves start at index N/2 - 1 = 3
thread t uses leaf  m_firstLeaf + t/2:
  t0,t1 -> leaf 3   t2,t3 -> leaf 4
  t4,t5 -> leaf 5   t6,t7 -> leaf 6
parents: node3.parent=(3-1)/2=1, node1.parent=0, node0.parent=-1
```

### 3.6 The constructor — building the tree
```cpp
explicit BinaryTreeBarrier(int numThreads)
    : m_nodes(nullptr), m_numNodes(0),
      m_numThreads(numThreads), m_firstLeaf(0)
{
    m_threadSense.resize(numThreads > 0 ? numThreads : 0);
    if (numThreads <= 1) return;          // 1 thread needs no tree

    m_numNodes  = numThreads - 1;
    m_firstLeaf = numThreads / RADIX - 1;

    m_nodes = new Node[m_numNodes];       // allocate the node array
    for (int i = 0; i < m_numNodes; ++i) {
        m_nodes[i].size = RADIX;
        m_nodes[i].count.store(RADIX);    // each node waits for 2 arrivals
        m_nodes[i].sense.store(false);
        m_nodes[i].parent = (i == 0) ? -1 : (i - 1) / RADIX;
    }
}
```

**Syntax notes**

- `explicit` — prevents an accidental implicit conversion from `int` to a
  `BinaryTreeBarrier`.
- the `: m_nodes(nullptr), ...` list initializes members before the body runs.
- `.store(x)` — the atomic way to write a value.
- `(i == 0) ? -1 : (i - 1) / RADIX` — **ternary operator**: root gets `-1`,
  everyone else gets their heap parent.

### 3.7 The destructor
```cpp
~BinaryTreeBarrier() { delete[] m_nodes; }
```
`new Node[...]` must be paired with `delete[]` to free the array (no leak).

### 3.8 `await` — the core combining logic
```cpp
void await(int nodeIdx, bool mySense)
{
    Node& node = m_nodes[nodeIdx];
    int position = node.count.fetch_sub(1);   // atomically count--, return OLD value
    if (position == 1) {
        // OLD value was 1 -> I am the LAST arrival at this node
        if (node.parent != -1) {
            await(node.parent, mySense);       // combine upward (recursion)
        }
        node.count.store(node.size);           // reset for next round
        node.sense.store(mySense);             // release everyone waiting here
    } else {
        // not last -> wait until the last arrival flips the sense
        while (node.sense.load() != mySense) {
            // busy-wait (spin)
        }
    }
}
```

**Syntax notes**

- `Node& node` — a **reference** (alias) to the real node, so writes affect the
  stored node, not a copy.
- `fetch_sub(1)` — atomically subtracts 1 and returns the value **before**
  subtracting. So the thread that reads `1` was the one that took the count to
  `0` — the last to arrive.
- `node.sense.load()` — atomically read the flag.
- `while (... ) {}` — a **spin loop**: the thread keeps checking until released.

**Example — why `fetch_sub` returning the OLD value identifies the last thread.**
A leaf starts with `count = 2`. Two threads call `fetch_sub(1)`:
```
count starts at 2
 thread A: fetch_sub(1) returns 2, count is now 1   -> position 2 != 1 -> NOT last -> spins
 thread B: fetch_sub(1) returns 1, count is now 0   -> position 1 == 1 -> LAST  -> goes up
```
Even if A and B run at the exact same instant, `fetch_sub` is **atomic**, so one
of them is guaranteed to get `2` and the other `1` — never both the same. That
is how exactly one thread is chosen as "last" without any lock.

**Counter-example (what a plain `int` would do):** `count--` is really *read,
subtract, write*. Two threads could both read `2`, both write `1`, and neither
sees `0` — the barrier would hang forever. `std::atomic` prevents this.

**The logic, in words**

1. Every arriving thread decrements the node's `count`.
2. Non-last arrivals **spin** on `sense`, waiting to be released.
3. The **last** arrival doesn't release immediately — it first recurses up to
   the parent (combining), repeating the same rule. Only the thread that is last
   all the way to the **root** turns around.
4. On the way back, each node's last arrival resets `count` (for the next
   barrier) and flips `sense`, which **wakes the spinners** at that node. The
   release **cascades down** the tree.

### 3.9 `barrier()` — what each thread calls
```cpp
void barrier() override
{
    if (m_numThreads <= 1) return;          // trivial case

    int t = thread_id;                       // this thread's id (thread_local)
    bool mySense = m_threadSense[t].sense;   // the sense for THIS round

    int leaf = m_firstLeaf + t / RADIX;      // which leaf this thread enters
    await(leaf, mySense);                    // walk/spin up the tree

    m_threadSense[t].sense = !mySense;       // flip sense for the NEXT round
}
```

**Syntax notes**

- `override` — tells the compiler this implements the base class's virtual
  `barrier()`; it errors out if the signature doesn't match.
- `t / RADIX` — integer division pairs threads: `t=0,1 → leaf 0`, `t=2,3 → leaf 1`…
- `!mySense` — flips the boolean. Because the per-thread flag and the node flags
  always alternate together, a thread is released exactly when *its* round's
  sense appears — this is the classic **sense-reversal** trick that lets the
  same barrier object be reused round after round without resetting flags.

**Example — sense-reversal over 3 rounds (one node, one thread's view):**
```
start:  node.sense = false        thread's mySense flag = true

Round 1: mySense=true.  Waiters spin while node.sense != true.
         Last arrival sets node.sense=true -> released.
         Thread flips its flag: true -> false.

Round 2: mySense=false. Waiters spin while node.sense != false.
         Last arrival sets node.sense=false -> released.
         Thread flips its flag: false -> true.

Round 3: mySense=true again... and so on.
```
The flag and the node flip *together* every round, so a leftover `sense` value
from the previous round can never accidentally release this round's waiters. No
reset needed — that is why the same barrier object works for unlimited rounds.

---

## 4. End-to-end Example (N = 4)

Nodes: index 0 = root, indices 1,2 = leaves (`m_firstLeaf = 1`).
Threads: t0,t1 → leaf 1; t2,t3 → leaf 2. All `mySense = true` first round.

```
1. t0 arrives at leaf1: count 2->1, not last -> spins
2. t2 arrives at leaf2: count 2->1, not last -> spins
3. t1 arrives at leaf1: count 1->0, LAST -> go up to root
       root: count 2->1, not last -> t1 spins at root
4. t3 arrives at leaf2: count 1->0, LAST -> go up to root
       root: count 1->0, LAST and parent=-1 ->
            reset root.count=2, root.sense=true   (releases t1 at root)
5. t1 wakes at root, returns to leaf1:
       reset leaf1.count=2, leaf1.sense=true      (releases t0)
6. t3 returns to leaf2:
       reset leaf2.count=2, leaf2.sense=true      (releases t2)
-> all four threads released; each flips its own sense to false for round 2
```

Contention was only ever **2-way** at any node — that is the whole point.

---

## 5. Quick Summary

| Piece | Role |
|-------|------|
| `RADIX = 2` | binary tree: 2 arrivals combine per node |
| `Node.count` | down-counter of remaining arrivals (atomic) |
| `Node.sense` | release flag waiters spin on (atomic) |
| `PaddedSense` | per-thread, cache-padded sense flag |
| heap layout | tree stored in `N-1` array slots, `parent=(i-1)/2` |
| `await` | decrement; last arrival recurses up then releases on the way down |
| `barrier()` | enter at your leaf, then flip your sense for next round |
