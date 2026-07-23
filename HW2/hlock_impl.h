#ifndef HLOCK_IMPL_H
#define HLOCK_IMPL_H

#include "hlock.h"
#include <mutex>
#include <limits>

/* Sari Mansour */
class HierarchicalMutex_impl : public HierarchicalMutex {
private:
    std::mutex m_internal;
    long long  m_level;
    long long  m_previousLevel;


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

#endif 
