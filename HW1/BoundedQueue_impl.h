/*
Written by Sari Mansour, 2026
*/

#pragma once

#include "BoundedQueue.h"
#include <mutex>
#include <condition_variable>
#include <memory>

class BoundedQueue : public BoundedQueueAbstract {
private:
    const int              _capacity;
    std::unique_ptr<int[]> _buf;
    int                    _head;
    int                    _tail;
    int                    _count;
    std::mutex             _mutex;
    std::condition_variable _not_empty;
    std::condition_variable _not_full;
public:
    explicit BoundedQueue(int capacity)
        : _capacity(capacity),
          _buf(new int[capacity]),
          _head(0),
          _tail(0),
          _count(0)
    {}

    int size() override {
        std::unique_lock<std::mutex> lock(_mutex);
        return _count;
    }

    int pop() override {
        std::unique_lock<std::mutex> lock(_mutex);
        _not_empty.wait(lock, [this]{ return _count > 0; });

        int val = _buf[_head];
        _head = (_head + 1) % _capacity;
        --_count;

        _not_full.notify_one();
        return val;
    }

    void push(int v) override {
        std::unique_lock<std::mutex> lock(_mutex);
        _not_full.wait(lock, [this]{ return _count < _capacity; });

        _buf[_tail] = v;
        _tail = (_tail + 1) % _capacity;
        ++_count;

        _not_empty.notify_one();
    }

};
