/*
Written Sari Mansour, 2026
*/

#pragma once

#include "BoundedQueue1p1c.h"
#include <atomic>
#include <memory>

class BoundedQueue1p1c : public BoundedQueueAbstract_1p1c {
public:
    explicit BoundedQueue1p1c(int capacity)
        : _capacity(capacity + 1),
          _buf(new int[capacity + 1]),
          _head(0),
          _tail(0)
    {}

    int size() override {
        int head = _head.load(std::memory_order_acquire);
        int tail = _tail.load(std::memory_order_acquire);
        return (tail - head + _capacity) % _capacity;
    }

    // Called only by the single consumer thread.
    // Returns false (and leaves val unchanged) if the queue is empty.
    bool pop(int &val) override {
        int head = _head.load(std::memory_order_relaxed);
        int tail = _tail.load(std::memory_order_acquire); 

        if (head == tail) return false; 

        val = _buf[head];
        // Release: make the consumed slot visible to the producer
        _head.store((head + 1) % _capacity, std::memory_order_release);
        return true;
    }

    // Called only by the single producer thread.
    // Returns false if the queue is full.
    bool push(int v) override {
        int tail = _tail.load(std::memory_order_relaxed);
        int next_tail = (tail + 1) % _capacity;

        if (next_tail == _head.load(std::memory_order_acquire)) return false; 

        _buf[tail] = v;
        _tail.store(next_tail, std::memory_order_release);
        return true;
    }

private:
    const int              _capacity; 
    std::unique_ptr<int[]> _buf;
    std::atomic<int>       _head;     
    std::atomic<int>       _tail;     
};
