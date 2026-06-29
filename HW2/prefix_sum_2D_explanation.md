# 2D Prefix Sum — Full Explanation

This document explains both files for Exercise 1:

- `prefix_sum_2D.h` — the provided header (interface, table classes, counters)
- `prefix_sum_2D_impl.h` — your implementation of the three algorithms

---

## 1. The Problem

Given an `N x N` table `A`, compute the **2D prefix sum** in-place. Every cell becomes the sum of the rectangle from the top-left corner down to itself:

$$A[i,j] = \sum_{m=0}^{i} \sum_{n=0}^{j} A[m,n]$$

The table is stored as a **1D row-major array** of `N*N` integers, so logical cell `A[i][j]` lives at flat index `i*N + j`.

**Example (N=3) — flat layout.** The logical table on the left is stored as the 1D array on the right:

```
logical 3x3                flat indices (i*N + j)
[ 1  2  3 ]                [0][1][2][3][4][5][6][7][8]
[ 4  5  6 ]   ->  array =   1  2  3  4  5  6  7  8  9
[ 7  8  9 ]                so A[1][2] = array[1*3 + 2] = array[5] = 6
```

**Example — final prefix sum.** After running the algorithm, every cell holds the sum of the rectangle above-and-left of it:

```
input          ->     prefix sum
[ 1  2  3 ]           [ 1  3  6 ]
[ 4  5  6 ]           [ 5 12 21 ]
[ 7  8  9 ]           [12 27 45 ]

e.g. A[2][2]=45 = sum of all 9 cells; A[1][1]=12 = 1+2+4+5.
```

---

## 2. The Header — `prefix_sum_2D.h`

### 2.1 `TableType`
```cpp
enum TableType { Serial=0, Thread=1, Tbb=2 };
```
A tag returned by each table so the grader knows which variant is in use.

### 2.2 `PaddedAtomicULL`
```cpp
struct alignas(64) PaddedAtomicULL {
    std::atomic<unsigned long long> value;
    char padding[64 - sizeof(...)];
};
```
A counter padded to a full 64-byte cache line. Multiple threads each get their own counter, so padding prevents **false sharing** (two threads writing the same cache line and slowing each other down).

### 2.3 `TableAbs` (abstract base)
Holds the data and defines the interface:

| Member | Purpose |
|--------|---------|
| `m_table` | the flat `int[N*N]` array |
| `m_size` | total cells (`N*N`) |
| `operator[](index)` | access a cell **and** increment a counter |
| `getCounter()` | total number of accesses |
| `getMax()` | busiest single thread's accesses |
| `zeroCounter()` | reset counters |
| `getTableType()` | which variant |

Every cell access is counted — that is how the grader measures complexity.

### 2.4 The three concrete tables

| Class | How it counts | What it verifies |
|-------|---------------|------------------|
| `TableSeq` | single `m_counter++` | total ≈ O(N²) |
| `TableThreadParallel` | one atomic **per thread** (registered by `thread::id`) | `getMax()` ≈ O(N) per thread |
| `TableTBBParallel` | one atomic **per TBB worker** | `getMax()` ≈ O(N) per worker |

The two parallel tables map each thread to a private counter slot on first access (under a mutex), so `getMax()` reveals the most loaded worker — proving the per-worker O(N) bound.

### 2.5 The functions you must implement
```cpp
// Serial: O(N^2)
void prefixSum_serial(TableAbs& table, unsigned long long N);

// Threads: overall O(N) with enough cores, O(N) per thread
void prefixSum_threads(TableAbs& table, unsigned long long N, int numThreads);

// TBB: overall O(N), O(N) per worker
void prefixSum_tbb(TableAbs& table, unsigned long long N);
```

---

## 3. The Implementation — `prefix_sum_2D_impl.h`

### 3.1 Key insight: two independent passes
A 2D prefix sum = a row pass followed by a column pass:

1. **Row pass:** accumulate left→right within each row
   `B[i][j] = Σ_{n≤j} A[i][n]`
2. **Column pass:** accumulate top→bottom within each column
   `C[i][j] = Σ_{m≤i} B[m][j]`

Composing them yields `Σ_{m≤i} Σ_{n≤j} A[m][n]` — the required result.
Crucially, rows are independent in pass 1 and columns are independent in pass 2, so each pass parallelizes with no data races. Each `table[a] += table[b]` is 2 counter accesses.

**Worked example (N=3):**

```
start            after ROW pass        after COLUMN pass
[ 1  2  3 ]      [ 1  3  6 ]           [ 1  3  6 ]
[ 4  5  6 ]  ->  [ 4  9 15 ]    ->     [ 5 12 21 ]
[ 7  8  9 ]      [ 7 15 24 ]           [12 27 45 ]

Row pass: each row summed left->right (3=1+2, 6=3+3, ...).
Col pass: each col summed top->bottom (5=1+4, 12=3+9, ...).
```

Row 0 and row 1 are summed by different threads at the same time (independent); then column 0 and column 1 are summed in parallel.

### 3.2 `partitionRange` helper
Splits `[0, total)` into `numThreads` near-equal contiguous blocks and returns thread `t`'s `[start, end)`. Used to hand each thread its own rows (pass 1) and columns (pass 2).

**Example:** `N=8` rows, `numThreads=3` ⇒ blocks of sizes 3,3,2:

```
thread 0 -> rows [0,3)  = 0,1,2
thread 1 -> rows [3,6)  = 3,4,5
thread 2 -> rows [6,8)  = 6,7
```

With `numThreads = N = 8`, each thread gets exactly one row, so each does O(N) work — the ideal case.

### 3.3 `prefixSum_serial` — O(N²)
Two nested loops for rows, then two for columns. Simplest correct version, within the cap.

### 3.4 `prefixSum_threads` — O(N) per thread
```mermaid
flowchart LR
  S[spawn numThreads] --> R[Phase 1: rows] --> B((barrier)) --> C[Phase 2: cols] --> J[join]
```
- The **same** threads run both phases, separated by `std::barrier`.
- Why reuse threads? `TableThreadParallel`'s counter array is sized to `numThreads`. Spawning new threads for phase 2 would register new IDs and overflow that array (out-of-bounds). One barrier keeps each thread registered exactly once.
- With ~N threads: 1 row + 1 column each ⇒ O(N) per thread.

**Example (N=8, 8 threads):** thread 5 owns row 5 in phase 1 and column 5 in phase 2, touching ~`2*(8-1)` cells per phase ≈ 28 accesses. So `getMax()` ≈ 28 ≈ O(N) — that is exactly the proof the grader checks (not O(N²)). Without the barrier, the column pass could read cells the row pass hasn't finished writing.

### 3.5 `prefixSum_tbb` — O(N) per worker
Two `tbb::parallel_for` calls over `blocked_range`: rows, then columns. TBB reuses its worker pool, so counters stay within `max_concurrency()`.

### 3.6 Notes
- All cells use index `i*N + j`; the result is layout-independent as long as indexing is consistent.
- Functions are `inline` so the header includes cleanly with no link conflicts.
- Built with C++20 (`std::barrier`), which the assignment allows for Exercise 1.

---

## 4. Quick Summary

| Function | Strategy | Complexity |
|----------|----------|-----------|
| `prefixSum_serial` | rows then columns, single thread | O(N²) |
| `prefixSum_threads` | rows then columns, persistent threads + barrier | O(N)/thread |
| `prefixSum_tbb` | two `parallel_for` passes | O(N)/worker |
