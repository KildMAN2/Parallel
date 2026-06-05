// Test for Exercise 2: BoundedQueue1p1c
#include "../BoundedQueue1p1c_impl.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

int main() {
    // --- Single-threaded sanity ---
    {
        BoundedQueue1p1c q(3);
        int v;
        assert(q.size() == 0);
        assert(!q.pop(v));            // empty
        assert(q.push(1));
        assert(q.push(2));
        assert(q.push(3));
        assert(!q.push(4));           // full
        assert(q.size() == 3);
        assert(q.pop(v) && v == 1);
        assert(q.pop(v) && v == 2);
        assert(q.push(4));
        assert(q.pop(v) && v == 3);
        assert(q.pop(v) && v == 4);
        assert(!q.pop(v));
        std::cout << "[Ex2] single-threaded sanity OK\n";
    }

    // --- 1P/1C stress ---
    {
        constexpr int CAP = 16;
        constexpr int N   = 200000;
        BoundedQueue1p1c q(CAP);
        std::atomic<bool> done{false};

        std::thread prod([&]{
            for (int i = 1; i <= N; ++i) {
                while (!q.push(i)) { /* spin: full */ }
            }
            done.store(true, std::memory_order_release);
        });

        long long sum = 0;
        int count = 0;
        while (count < N) {
            int v;
            if (q.pop(v)) {
                sum += v;
                ++count;
            }
        }
        prod.join();
        long long expected = static_cast<long long>(N) * (N + 1) / 2;
        assert(sum == expected);
        std::cout << "[Ex2] 1P/1C stress OK  (sum=" << sum << ")\n";
    }

    std::cout << "[Ex2] ALL TESTS PASSED\n";
    return 0;
}
