// Targeted repro for the Gradescope SIGSEGV on the "single-level skiplist"
// (maxLevel = 1) parallel test. maxLevel=1 forces EVERY node to height 1,
// removing all "express lane" shortcuts, so many threads land on the exact
// same predecessor locks far more often -> any rare race gets hit hard.
#include "../skip_list_par_impl.h"
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <iostream>
#include <cstdlib>

static void run_once(int nThreads, int keyRange, int opsPerThread, unsigned seed) {
    SkipListImpl sl(1);   // <-- single level, maximum contention
    std::vector<std::thread> th;
    for (int t = 0; t < nThreads; t++) {
        th.emplace_back([&, t]() {
            std::mt19937 rng(seed + t * 7919u);
            std::uniform_int_distribution<int> k(0, keyRange - 1);
            std::uniform_int_distribution<int> op(0, 2);
            for (int i = 0; i < opsPerThread; i++) {
                int key = k(rng);
                switch (op(rng)) {
                    case 0: sl.insert(key); break;
                    case 1: sl.remove(key); break;
                    default: sl.search(key); break;
                }
            }
        });
    }
    for (auto& x : th) x.join();
}

int main(int argc, char** argv) {
    int nThreads = 7;
    int keyRange = 64;       // small range -> heavy collisions
    int opsPerThread = 20000;
    int iterations = 200;    // repeat many times to catch a rare race

    if (argc > 1) iterations = std::atoi(argv[1]);
    if (argc > 2) nThreads = std::atoi(argv[2]);
    if (argc > 3) keyRange = std::atoi(argv[3]);
    if (argc > 4) opsPerThread = std::atoi(argv[4]);

    std::cout << "iterations=" << iterations << " threads=" << nThreads
              << " keyRange=" << keyRange << " opsPerThread=" << opsPerThread << "\n";

    for (int it = 0; it < iterations; it++) {
        run_once(nThreads, keyRange, opsPerThread, (unsigned)(it * 104729 + 1));
        std::cout << "iter " << it << " OK" << std::flush << "\n";
    }
    std::cout << "ALL ITERATIONS SURVIVED\n";
    return 0;
}
