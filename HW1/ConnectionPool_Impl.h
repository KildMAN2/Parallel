/*
Written by Yariv Aridor, 2022
*/

#pragma once

#include "Connection.h"
#include <memory>
#include <mutex>
#include <condition_variable>

class ConnectionPool : public ConnectionPoolAbstract {
public:
    // Construct the pool and pre-create poolSize connections
    explicit ConnectionPool(size_t poolSize) : ConnectionPoolAbstract(poolSize) {
        for (size_t i = 0; i < poolSize; ++i) {
            pool.push(std::unique_ptr<Connection>(new Connection(static_cast<int>(i))));
        }
    }

    // Borrow a connection from the pool.
    // Blocks if the pool is empty until a connection is returned.
    // The returned shared_ptr uses a custom deleter that automatically
    // returns the connection to the pool when the last shared_ptr is destroyed.
    std::shared_ptr<Connection> borrowConnection() override {
        std::unique_lock<std::mutex> lock(_mutex);
        _available.wait(lock, [this]{ return !pool.empty(); });

        // Take ownership of the raw pointer out of the unique_ptr
        std::unique_ptr<Connection> conn = std::move(pool.front());
        pool.pop();

        Connection* raw = conn.release();

        // Custom deleter: returns the connection back to the pool
        auto deleter = [this](Connection* c) {
            std::unique_lock<std::mutex> lk(_mutex);
            pool.push(std::unique_ptr<Connection>(c));
            _available.notify_one();
        };

        return std::shared_ptr<Connection>(raw, deleter);
    }

private:
    std::mutex              _mutex;
    std::condition_variable _available;
};
