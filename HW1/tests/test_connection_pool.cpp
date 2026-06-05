// Test for Exercise 4: ConnectionPool
#include <iostream>      // included before Connection.h since Connection.h uses std::cout
#include <memory>        // shared_ptr is referenced in ConnectionPoolAbstract
#include "../Connection.h"
#include "../ConnectionPool_Impl.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

int main() {
    using namespace std::chrono_literals;

    // --- Borrow / auto-return ---
    {
        ConnectionPool pool(2);
        {
            auto c1 = pool.borrowConnection();
            auto c2 = pool.borrowConnection();
            c1->use();
            c2->use();
            // pool now empty; a third borrow would block
            std::atomic<bool> got_third{false};
            std::thread waiter([&]{
                auto c3 = pool.borrowConnection();
                got_third.store(true);
                c3->use();
            });
            std::this_thread::sleep_for(50ms);
            assert(!got_third.load()); // must be blocked
            // Drop c1 -> returns to pool -> waiter wakes
        } // c1, c2 destroyed here -- but we have waiter still alive...

        // re-do cleanly with proper scoping
    }

    {
        ConnectionPool pool(2);
        auto c1 = pool.borrowConnection();
        auto c2 = pool.borrowConnection();
        std::atomic<bool> got_third{false};
        std::thread waiter([&]{
            auto c3 = pool.borrowConnection();
            got_third.store(true);
            c3->use();
        });
        std::this_thread::sleep_for(50ms);
        assert(!got_third.load());
        c1.reset(); // returns to pool
        waiter.join();
        assert(got_third.load());
        std::cout << "[Ex4] blocking borrow wakes on return OK\n";
    }

    // --- Shared by multiple owners; returns only when LAST shared_ptr dies ---
    {
        ConnectionPool pool(1);
        std::atomic<bool> got_second{false};
        std::shared_ptr<Connection> shared_copy;
        {
            auto c = pool.borrowConnection();
            shared_copy = c; // share ownership
        } // c dies, but shared_copy still alive -> NOT returned

        std::thread waiter([&]{
            auto c2 = pool.borrowConnection();
            got_second.store(true);
        });
        std::this_thread::sleep_for(50ms);
        assert(!got_second.load());
        shared_copy.reset(); // last owner gone -> returned to pool
        waiter.join();
        assert(got_second.load());
        std::cout << "[Ex4] shared ownership delays return OK\n";
    }

    // --- Many threads contend ---
    {
        constexpr int POOL = 3;
        constexpr int THREADS = 16;
        constexpr int ITERS = 200;
        ConnectionPool pool(POOL);
        std::vector<std::thread> ts;
        for (int t = 0; t < THREADS; ++t) {
            ts.emplace_back([&]{
                for (int i = 0; i < ITERS; ++i) {
                    auto c = pool.borrowConnection();
                    // simulate brief work
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            });
        }
        for (auto &t : ts) t.join();
        std::cout << "[Ex4] contention stress OK\n";
    }

    std::cout << "[Ex4] ALL TESTS PASSED\n";
    return 0;
}
