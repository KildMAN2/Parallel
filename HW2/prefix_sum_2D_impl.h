#ifndef PREFIX_SUM_2D_IMPL
#define PREFIX_SUM_2D_IMPL

#include "prefix_sum_2D.h"
#include <vector>
#include <thread>
#include <barrier>
#include <algorithm>
#include <utility>
#include <functional>
#include <tbb/tbb.h>

/*
 * Sari Mansour 
 * The 2D table A (NxN) is stored in row-major order, so logical element
 * A[i][j] lives at the flat index i*N + j (every access goes through
 * TableAbs::operator[], which is what the counters measure).
 *
 * A 2D prefix sum can be computed in two independent passes:
 *   Phase 1 (rows):    for each row i, accumulate left-to-right over j.
 *   Phase 2 (columns): for each column j, accumulate top-to-bottom over i.
 *
 * After phase 1:  B[i][j] = sum_{n<=j} A[i][n]
 * After phase 2:  C[i][j] = sum_{m<=i} B[m][j] = sum_{m<=i} sum_{n<=j} A[m][n]
 * which is exactly the required 2D prefix sum.
 *
 * Rows are independent of each other in phase 1, and columns are independent
 * of each other in phase 2, so each phase parallelizes cleanly. With N workers,
 * every worker handles one row (then one column), giving O(N) work per worker.
 */

// Partition the range [0, totalItems) into numThreads contiguous, near-equal
// blocks and return the [rangeStart, rangeEnd) block owned by thread threadId.
//
// Items rarely divide evenly among threads, so we give every thread a base
// share of (totalItems / numThreads) items, then hand out the leftover
// (totalItems % numThreads) items one-by-one to the first few threads. This
// keeps block sizes balanced (they differ by at most 1) and contiguous.
//
// Example: totalItems = 8, numThreads = 3
//   baseShare = 2, remainder = 2
//   thread 0 -> [0, 3)  (2 base + 1 extra)
//   thread 1 -> [3, 6)  (2 base + 1 extra)
//   thread 2 -> [6, 8)  (2 base, no extra)
static inline std::pair<unsigned long long, unsigned long long>
partitionRange(int threadId, int numThreads, unsigned long long totalItems)
{
    unsigned long long id        = static_cast<unsigned long long>(threadId);
    unsigned long long baseShare = totalItems / static_cast<unsigned long long>(numThreads);
    unsigned long long remainder = totalItems % static_cast<unsigned long long>(numThreads);

    unsigned long long rangeStart = id * baseShare + std::min<unsigned long long>(id, remainder);

    unsigned long long rangeEnd   = rangeStart + baseShare + (id < remainder ? 1ULL : 0ULL);

    return std::make_pair(rangeStart, rangeEnd);
}

inline void prefixSum_serial(TableAbs& table, unsigned long long N)
{
    for (unsigned long long i = 0; i < N; ++i) {
        for (unsigned long long j = 1; j < N; ++j) {
            table[i * N + j] += table[i * N + j - 1];
        }
    }
    for (unsigned long long j = 0; j < N; ++j) {
        for (unsigned long long i = 1; i < N; ++i) {
            table[i * N + j] += table[(i - 1) * N + j];
        }
    }
}

inline void prefixSum_threads_worker(TableAbs& table, unsigned long long N,
                                     int numThreads, int t, std::barrier<>& sync)
{
    std::pair<unsigned long long, unsigned long long> rows =
        partitionRange(t, numThreads, N);
    for (unsigned long long i = rows.first; i < rows.second; ++i) {
        for (unsigned long long j = 1; j < N; ++j) {
            table[i * N + j] += table[i * N + j - 1];
        }
    }

    sync.arrive_and_wait();

    std::pair<unsigned long long, unsigned long long> cols =
        partitionRange(t, numThreads, N);
    for (unsigned long long j = cols.first; j < cols.second; ++j) {
        for (unsigned long long i = 1; i < N; ++i) {
            table[i * N + j] += table[(i - 1) * N + j];
        }
    }
}

inline void prefixSum_threads(TableAbs& table, unsigned long long N, int numThreads)
{
    if(N == 0 || numThreads <= 1) {
        return;
    }
    if (numThreads < 1) {
        prefixSum_serial(table, N);
        return;
    }

    std::barrier<> sync(numThreads);

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back(prefixSum_threads_worker,
                             std::ref(table), N, numThreads, t, std::ref(sync));
    }
    for (std::thread& th : threads) {
        th.join();
    }
}

inline void prefixSum_tbb(TableAbs& table, unsigned long long N)
{
    if(N == 0) {
        return;
    }
    tbb::parallel_for(
        tbb::blocked_range<unsigned long long>(0, N),
        [&](const tbb::blocked_range<unsigned long long>& r) {
            for (unsigned long long i = r.begin(); i < r.end(); ++i) {
                for (unsigned long long j = 1; j < N; ++j) {
                    table[i * N + j] += table[i * N + j - 1];
                }
            }
        });

    tbb::parallel_for(
        tbb::blocked_range<unsigned long long>(0, N),
        [&](const tbb::blocked_range<unsigned long long>& r) {
            for (unsigned long long j = r.begin(); j < r.end(); ++j) {
                for (unsigned long long i = 1; i < N; ++i) {
                    table[i * N + j] += table[(i - 1) * N + j];
                }
            }
        });
}

#endif 
