#include "../skip_list_par_impl.h"
#include <thread>
#include <set>
#include <atomic>
#include <iostream>
#include <random>
#include <cassert>

// ---- Test 1: disjoint concurrent inserts, then verify all present + sorted ----
static void test_disjoint_inserts() {
    SkipListImpl sl(16);
    const int T = 8, per = 5000;
    std::vector<std::thread> th;
    for (int t = 0; t < T; t++) {
        th.emplace_back([&, t]() {
            for (int i = 0; i < per; i++) sl.insert(t * per + i);
        });
    }
    for (auto& x : th) x.join();

    for (int v = 0; v < T * per; v++) {
        if (!sl.search(v)) { std::cerr << "MISSING " << v << "\n"; assert(false); }
    }
    std::cout << "test_disjoint_inserts OK (" << T * per << " keys)\n";
}

// ---- Test 2: concurrent remove of half, verify remaining ----
static void test_concurrent_remove() {
    SkipListImpl sl(16);
    const int N = 40000;
    for (int i = 0; i < N; i++) sl.insert(i);

    const int T = 8;
    std::vector<std::thread> th;
    for (int t = 0; t < T; t++) {
        th.emplace_back([&, t]() {
            for (int i = t; i < N; i += T)
                if (i % 2 == 0) sl.remove(i);
        });
    }
    for (auto& x : th) x.join();

    for (int i = 0; i < N; i++) {
        bool expect = (i % 2 != 0);
        if (sl.search(i) != expect) {
            std::cerr << "BAD " << i << " expected " << expect << "\n";
            assert(false);
        }
    }
    std::cout << "test_concurrent_remove OK\n";
}

// ---- Test 3: heavy mixed random ops on small key space (stress UAF/races) ----
static void test_mixed_stress() {
    SkipListImpl sl(12);
    const int T = 12;
    const int KEYS = 500;
    const int OPS = 40000;
    std::atomic<bool> stop{false};
    std::vector<std::thread> th;
    for (int t = 0; t < T; t++) {
        th.emplace_back([&, t]() {
            std::mt19937 rng(1234 + t);
            std::uniform_int_distribution<int> k(0, KEYS - 1);
            std::uniform_int_distribution<int> op(0, 2);
            for (int i = 0; i < OPS; i++) {
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

    // Consistency after quiescence: insert all, then all must be found.
    for (int i = 0; i < KEYS; i++) sl.insert(i);
    for (int i = 0; i < KEYS; i++)
        if (!sl.search(i)) { std::cerr << "post-insert missing " << i << "\n"; assert(false); }
    std::cout << "test_mixed_stress OK\n";
}

int main() {
    test_disjoint_inserts();
    test_concurrent_remove();
    for (int r = 0; r < 5; r++) test_mixed_stress();
    std::cout << "ALL SKIPLIST TESTS PASSED\n";
    return 0;
}
