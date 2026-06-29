#ifndef HLOCK_IMPL_H
#define HLOCK_IMPL_H

#include "hlock.h"
#include <mutex>
#include <limits>

/*
 * Hierarchical mutex.
 *
 * Every lock has a unique level. To prevent deadlocks, a thread must acquire
 * locks in strictly increasing level order: a lock() fails (throws
 * HierarchicalMutexException) if the calling thread already holds another
 * hierarchical lock whose level is higher than or equal to this lock's level.
 *
 * Each thread keeps a single "current level" (the level of the most recently
 * acquired lock, which is also the highest one it currently holds, since locks
 * are taken in increasing order). The check is therefore O(1).
 *
 * When a lock is taken, it saves the thread's previous current level in a
 * per-mutex field and installs its own level. On unlock the previous level is
 * restored. Storing the saved value in the mutex (rather than per thread) is
 * safe because the underlying std::mutex guarantees only one thread holds the
 * lock at a time, and unlocks happen in reverse order of locks.
 */
class HierarchicalMutex_impl : public HierarchicalMutex {
private:
    std::mutex m_internal;
    long long  m_level;
    long long  m_previousLevel;

    // Thread-local "current level"; a function-local static keeps this header
    // safe to include from multiple translation units. Initialized to the
    // minimum so the very first lock on any thread always succeeds.
    static long long& thisThreadLevel()
    {
        static thread_local long long level =
            (std::numeric_limits<long long>::min)();
        return level;
    }

    void checkHierarchy() const
    {
        if (thisThreadLevel() >= m_level) {
            throw HierarchicalMutexException();
        }
    }

public:
    explicit HierarchicalMutex_impl(int lvl)
        : HierarchicalMutex(lvl),
          m_level(static_cast<long long>(lvl)),
          m_previousLevel(0)
    {
    }

    void lock() override
    {
        checkHierarchy();
        m_internal.lock();
        m_previousLevel = thisThreadLevel();
        thisThreadLevel() = m_level;
    }

    void unlock() override
    {
        thisThreadLevel() = m_previousLevel;
        m_internal.unlock();
    }

    bool try_lock() override
    {
        checkHierarchy();
        if (!m_internal.try_lock()) {
            return false;
        }
        m_previousLevel = thisThreadLevel();
        thisThreadLevel() = m_level;
        return true;
    }
};

#endif // HLOCK_IMPL_H
