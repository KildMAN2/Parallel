#ifndef PREFIX_SUM_2D
#define PREFIX_SUM_2D
#include <vector>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <tbb/tbb.h>

enum TableType{
    Serial=0,
    Thread=1,
    Tbb=2
};

struct alignas(64) PaddedAtomicULL {
    std::atomic<unsigned long long> value;
    char padding[64 - sizeof(std::atomic<unsigned long long>)]; // Ensure 64-byte alignment
};


class TableAbs
{
protected:
    int* m_table;   
    unsigned long long m_size;
public:
    TableAbs(unsigned long long size)
    {       
        m_size=size;
        m_table=new int[size];
    }
    ~TableAbs()
    {
        delete[] m_table;
    }
    virtual int& operator[](std::size_t index)
    {
        throw std::logic_error("operator[] not implemented for this class");
    }
    virtual void zeroCounter()
    {
        throw std::logic_error("zeroCounter not implemented for this class");
    }
    virtual unsigned long long getCounter()
    {
        throw std::logic_error("getCounter not implemented for this class");
    }
    virtual unsigned long long getMax()
    {
        throw std::logic_error("getMax not implemented for this class");
    }
    virtual TableType getTableType()
    {
        throw std::logic_error("getTableType not implemented for this class");
    }
};
class TableSeq:public TableAbs
{
private:
    unsigned long long m_counter;
public:
    TableSeq(unsigned long long size):TableAbs(size),m_counter(0) {  }
    ~TableSeq()=default;
    int& operator[](std::size_t index) override
    {
        if (index >= m_size||index<0) {
            throw std::out_of_range("Index out of range");
        }
        m_counter++;
        return m_table[index];
    }
    void zeroCounter() override
    {
        m_counter=0;
    }
    unsigned long long getCounter() override
    {
        return m_counter;
    }
    TableType getTableType() override
    {
        return TableType::Serial;
    }
};
class TableThreadParallel : public TableAbs {
private:
    alignas(64) std::vector<PaddedAtomicULL> m_counters;

    std::unordered_map<std::thread::id, int> m_threadsId;
    std::atomic<int> m_threadCounter;
    std::mutex m_mutex;

public:
    TableThreadParallel(unsigned long long size, int numThreads)
        : TableAbs(size),
          m_counters(numThreads),
          m_threadCounter(0) {}

    int& operator[](std::size_t index) override
    {
        if (index >= m_size) {
            throw std::out_of_range("Index out of range");
        }

        std::thread::id tid = std::this_thread::get_id();
        int idx;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_threadsId.find(tid);
            if (it == m_threadsId.end()) {
                idx = m_threadCounter++;
                m_threadsId[tid] = idx;
            } else {
                idx = it->second;
            }
        }

        m_counters[idx].value.fetch_add(1, std::memory_order_relaxed);

        return m_table[index];
    }


    unsigned long long getMax() override {
        unsigned long long max = 0;
        for (const auto& c : m_counters) {
            if(c.value.load()>max)
            {
                max=c.value.load();
            }           
        }
        return max;
    }

    unsigned long long getCounter() override {
        unsigned long long total = 0;
        for (const auto& c : m_counters) {
            total += c.value.load();
        }
        return total;
    }
    void zeroCounter() override
    {
        for(auto& c:m_counters)
        {
            c.value.store(0);
        }

    }
    TableType getTableType() override
    {
        return TableType::Thread;
    }
};
class TableTBBParallel:public TableAbs 
{
    private:        
    alignas(64) std::vector<PaddedAtomicULL> m_counters;
    std::unordered_map<std::thread::id, int> m_threadsId;
    std::atomic<int> m_threadCounter;
    std::mutex m_mutex;
        
    
    public:
    TableTBBParallel(unsigned long long size):TableAbs(size),      
        m_threadCounter(0), m_counters(tbb::this_task_arena::max_concurrency()) {}

    ~TableTBBParallel()=default;

    int& operator[](std::size_t index) override
    {
        if (index >= m_size) {
            throw std::out_of_range("Index out of range");
        }

        std::thread::id tid = std::this_thread::get_id();
        int idx;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_threadsId.find(tid);
            if (it == m_threadsId.end()) {
                idx = m_threadCounter.fetch_add(1,std::memory_order_seq_cst);
                m_threadsId[tid] = idx;
            } else {
                idx = it->second;
            }
        }

        m_counters[idx].value.fetch_add(1, std::memory_order_relaxed);

        return m_table[index];
    }

    unsigned long long getMax() override 
    {
        unsigned long long max = 0;
        for (const auto& c : m_counters) {
            if(c.value.load()>max)
            {
                max=c.value.load();
            }    
        }
        return max;
    }
    unsigned long long getCounter() override
    {
        unsigned long long total = 0;
        for (const auto& c : m_counters) {
            total += c.value.load();
        }
        return total;
    }
    
    void zeroCounter() override
    {
        for(auto& c:m_counters)
        {
            c.value.store(0);
        }    
    }
    TableType getTableType() override
    {
        return TableType::Tbb;
    }
};



// Compute the prefix sum sequentially in O(N^2), where NxN is the table size.
void prefixSum_serial(TableAbs& table,unsigned long long N);

// Compute the prefix sum in parallel by numOfThreads threads. This approach should reduce the overall time complexity to O(N), if enough cores are available. 
// Every thread should have a complexity of O(N).
void prefixSum_threads(TableAbs& table,unsigned long long N,int numThreads);

// Compute the prefix sum in parallel by TBB. This approach should reduce the overall time complexity to O(N). Every TBB worker has a complexity of O(N).
void prefixSum_tbb(TableAbs& table,unsigned long long N);

#endif  