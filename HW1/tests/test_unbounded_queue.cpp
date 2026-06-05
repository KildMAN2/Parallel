// Test for Exercise 3: UnboundedQueue1p1c
#include "../UnboundedQueue1p1c_impl.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

int main() {
    // --- Single-threaded sanity ---
    {
        UnboundedQueue1p1c q;
        int v;
        assert(!q.pop(v));        // empty (only dummy)
        assert(q.size() == 0);
        q.push(1);
        q.push(2);
        q.push(3);
        assert(q.size() == 3);
        assert(q.pop(v) && v == 1);
        assert(q.pop(v) && v == 2);
        assert(q.pop(v) && v == 3);
        assert(!q.pop(v));
        assert(q.size() == 0);
        std::cout << "[Ex3] single-threaded sanity OK\n";
    }

    // --- 1P/1C stress ---
    {
        constexpr int N = 500000;
        UnboundedQueue1p1c q;

        std::thread prod([&]{
            for (int i = 1; i <= N; ++i) q.push(i);
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
        std::cout << "[Ex3] 1P/1C stress OK  (sum=" << sum << ")\n";
    }

    std::cout << "[Ex3] ALL TESTS PASSED\n";
    return 0;
}
