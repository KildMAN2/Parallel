// Test for Exercise 1: BoundedQueue
#include "../BoundedQueue_impl.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    using namespace std::chrono_literals;

    // --- Single-threaded sanity ---
    {
        BoundedQueue q(4);
        assert(q.size() == 0);
        q.push(10);
        q.push(20);
        q.push(30);
        assert(q.size() == 3);
        assert(q.pop() == 10);
        assert(q.pop() == 20);
        assert(q.size() == 1);
        q.push(40);
        q.push(50);
        q.push(60); // fills it back up (3 elements + 30 already there)
        assert(q.size() == 4);
        assert(q.pop() == 30);
        assert(q.pop() == 40);
        assert(q.pop() == 50);
        assert(q.pop() == 60);
        assert(q.size() == 0);
        std::cout << "[Ex1] single-threaded sanity OK\n";
    }

    // --- Blocking pop wakes on push ---
    {
        BoundedQueue q(2);
        std::atomic<bool> popped{false};
        std::thread consumer([&]{
            int v = q.pop();
            assert(v == 42);
            popped.store(true);
        });
        std::this_thread::sleep_for(50ms);
        assert(!popped.load()); // consumer must be blocked
        q.push(42);
        consumer.join();
        assert(popped.load());
        std::cout << "[Ex1] blocking pop wakes on push OK\n";
    }

    // --- Blocking push wakes on pop ---
    {
        BoundedQueue q(2);
        q.push(1);
        q.push(2);
        std::atomic<bool> pushed{false};
        std::thread producer([&]{
            q.push(3); // must block until a pop frees a slot
            pushed.store(true);
        });
        std::this_thread::sleep_for(50ms);
        assert(!pushed.load());
        assert(q.pop() == 1);
        producer.join();
        assert(pushed.load());
        assert(q.pop() == 2);
        assert(q.pop() == 3);
        std::cout << "[Ex1] blocking push wakes on pop OK\n";
    }

    // --- Multi-producer / multi-consumer stress ---
    {
        constexpr int CAP = 8;
        constexpr int PRODUCERS = 4;
        constexpr int CONSUMERS = 4;
        constexpr int PER_PRODUCER = 5000;
        constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

        BoundedQueue q(CAP);
        std::atomic<long long> sum_pushed{0};
        std::atomic<long long> sum_popped{0};
        std::atomic<int> popped_count{0};

        std::vector<std::thread> threads;
        for (int p = 0; p < PRODUCERS; ++p) {
            threads.emplace_back([&, p]{
                long long local = 0;
                for (int i = 0; i < PER_PRODUCER; ++i) {
                    int v = p * PER_PRODUCER + i + 1;
                    q.push(v);
                    local += v;
                }
                sum_pushed.fetch_add(local);
            });
        }
        for (int c = 0; c < CONSUMERS; ++c) {
            threads.emplace_back([&]{
                while (popped_count.load() < TOTAL) {
                    if (popped_count.fetch_add(1) >= TOTAL) {
                        popped_count.fetch_sub(1);
                        break;
                    }
                    int v = q.pop();
                    sum_popped.fetch_add(v);
                }
            });
        }
        for (auto &t : threads) t.join();
        assert(sum_pushed.load() == sum_popped.load());
        std::cout << "[Ex1] MPMC stress OK  (sum=" << sum_pushed.load() << ")\n";
    }

    std::cout << "[Ex1] ALL TESTS PASSED\n";
    return 0;
}
