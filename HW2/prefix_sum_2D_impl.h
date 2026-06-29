#ifndef PREFIX_SUM_2D_IMPL
#define PREFIX_SUM_2D_IMPL

#include "prefix_sum_2D.h"
#include <vector>
#include <thread>
#include <barrier>
#include <algorithm>
#include <utility>
#include <tbb/tbb.h>

/*
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

// Partition [0, total) into numThreads contiguous, near-equal blocks and return
// the [start, end) range owned by thread t.
static inline std::pair<unsigned long long, unsigned long long>
partitionRange(int t, int numThreads, unsigned long long total)
{
    unsigned long long chunk = total / static_cast<unsigned long long>(numThreads);
    unsigned long long rem   = total % static_cast<unsigned long long>(numThreads);
    unsigned long long ut    = static_cast<unsigned long long>(t);
    unsigned long long start = ut * chunk + std::min<unsigned long long>(ut, rem);
    unsigned long long end   = start + chunk + (ut < rem ? 1ULL : 0ULL);
    return std::make_pair(start, end);
}

// Compute the prefix sum sequentially in O(N^2), where NxN is the table size.
inline void prefixSum_serial(TableAbs& table, unsigned long long N)
{
    // Phase 1: prefix sum along each row.
    for (unsigned long long i = 0; i < N; ++i) {
        for (unsigned long long j = 1; j < N; ++j) {
            table[i * N + j] += table[i * N + j - 1];
        }
    }
    // Phase 2: prefix sum along each column.
    for (unsigned long long j = 0; j < N; ++j) {
        for (unsigned long long i = 1; i < N; ++i) {
            table[i * N + j] += table[(i - 1) * N + j];
        }
    }
}

// Compute the prefix sum in parallel by numThreads threads. With enough cores
// the overall complexity is O(N); every thread has a complexity of O(N).
inline void prefixSum_threads(TableAbs& table, unsigned long long N, int numThreads)
{
    if (numThreads < 1) {
        numThreads = 1;
    }

    // The same threads are reused across both phases (separated by a barrier)
    // so that each thread registers exactly once with the table's counters.
    std::barrier<> sync(numThreads);

    auto work = [&](int t) {
        // Phase 1: rows owned by this thread.
        std::pair<unsigned long long, unsigned long long> rows =
            partitionRange(t, numThreads, N);
        for (unsigned long long i = rows.first; i < rows.second; ++i) {
            for (unsigned long long j = 1; j < N; ++j) {
                table[i * N + j] += table[i * N + j - 1];
            }
        }

        sync.arrive_and_wait();

        // Phase 2: columns owned by this thread.
        std::pair<unsigned long long, unsigned long long> cols =
            partitionRange(t, numThreads, N);
        for (unsigned long long j = cols.first; j < cols.second; ++j) {
            for (unsigned long long i = 1; i < N; ++i) {
                table[i * N + j] += table[(i - 1) * N + j];
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back(work, t);
    }
    for (std::thread& th : threads) {
        th.join();
    }
}

// Compute the prefix sum in parallel by TBB. The overall complexity is O(N);
// every TBB worker has a complexity of O(N).
inline void prefixSum_tbb(TableAbs& table, unsigned long long N)
{
    // Phase 1: prefix sum along each row (rows are independent).
    tbb::parallel_for(
        tbb::blocked_range<unsigned long long>(0, N),
        [&](const tbb::blocked_range<unsigned long long>& r) {
            for (unsigned long long i = r.begin(); i < r.end(); ++i) {
                for (unsigned long long j = 1; j < N; ++j) {
                    table[i * N + j] += table[i * N + j - 1];
                }
            }
        });

    // Phase 2: prefix sum along each column (columns are independent).
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

#endif // PREFIX_SUM_2D_IMPL
