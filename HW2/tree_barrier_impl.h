#ifndef TREE_BARRIER_IMPL_H
#define TREE_BARRIER_IMPL_H

#include "tree_barrier.h"
#include <atomic>
#include <vector>

/*
 * Sari Mansour
 * Combining binary tree barrier.
 *
 * A single shared sense-counter suffers from heavy memory contention because
 * every thread hits the same counter. A combining tree spreads that contention
 * across many small nodes: threads only contend on the leaf they are mapped to,
 * and contention is "combined" level by level up to the root.
 *
 * The tree is a complete binary tree stored in a heap array of (N-1) nodes:
 *   - node 0 is the root (parent == -1)
 *   - the parent of node i (i > 0) is (i-1)/2
 *   - the children of node i are 2*i+1 and 2*i+2
 *   - the last N/2 nodes are the leaves
 *
 * Each node behaves like a 2-way sense-reversing barrier (radix == 2):
 *   - leaves wait for their 2 threads
 *   - internal nodes wait for their 2 children to "report up"
 * The last arrival at a node climbs to its parent; only after the root is
 * reached does the release cascade back down via per-node sense reversal.
 *
 * N (the number of threads) is assumed to be a power of 2.
 */
class BinaryTreeBarrier : public BinaryTreeBarrierAbstract {
private:
    static const int RADIX = 2;

    struct Node {
        std::atomic<int>  count;   
        std::atomic<bool> sense;   
        int               size;   
        int               parent;  

        Node() : count(0), sense(false), size(0), parent(-1) {}
    };

    struct PaddedSense {
        bool sense;
        char padding[64 - sizeof(bool)];
        PaddedSense() : sense(true) {}
    };

    Node*                    m_nodes;
    int                      m_numNodes;
    int                      m_numThreads;
    int                      m_firstLeaf;
    std::vector<PaddedSense> m_threadSense;

    void await(int nodeIdx, bool mySense)
    {
        Node& node = m_nodes[nodeIdx];
        int position = node.count.fetch_sub(1, std::memory_order_acq_rel);
        if (position == 1) {
            if (node.parent != -1) {
                await(node.parent, mySense);
            }
            node.count.store(node.size, std::memory_order_release);   
            node.sense.store(mySense, std::memory_order_release);      
        } else {
            while (node.sense.load(std::memory_order_acquire) != mySense) {
                // busy-wait
            }
        }
    }

public:
    explicit BinaryTreeBarrier(int numThreads)
        : m_nodes(nullptr),
          m_numNodes(0),
          m_numThreads(numThreads),
          m_firstLeaf(0)
    {
        m_threadSense.resize(numThreads > 0 ? numThreads : 0);

        if (numThreads <= 1) {
            return; 
        }

        m_numNodes  = numThreads - 1;         
        m_firstLeaf = numThreads / RADIX - 1; 

        m_nodes = new Node[m_numNodes];
        for (int i = 0; i < m_numNodes; ++i) {
            m_nodes[i].size = RADIX;
            m_nodes[i].count.store(RADIX);
            m_nodes[i].sense.store(false);
            m_nodes[i].parent = (i == 0) ? -1 : (i - 1) / RADIX;
        }
    }

    ~BinaryTreeBarrier()
    {
        delete[] m_nodes;
    }

    void barrier() override
    {
        if (m_numThreads <= 1) {
            return;
        }

        int t = thread_id;
        bool mySense = m_threadSense[t].sense;

        int leaf = m_firstLeaf + t / RADIX;
        await(leaf, mySense);

        m_threadSense[t].sense = !mySense;
    }
};

#endif 
