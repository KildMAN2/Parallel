#include <exception>

class HierarchicalMutexException : public std::exception {
public:
    const char * what () {
        return "Higher level mutex already locked";
    }
};

class HierarchicalMutex {

public:

    // lvl is the ordering number of the mutex. This class checks the order of mutexes to prevent deadlocks
    HierarchicalMutex(int lvl) {};
    
    virtual void lock() {};
    virtual void unlock() {};
    virtual bool try_lock() {return false;};
};