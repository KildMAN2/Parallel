#ifndef TREE_BARRIER_IMPL_H
#define TREE_BARRIER_IMPL_H

#include "tree_barrier.h"
#include <atomic>
#include <vector>

/*
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
        std::atomic<int>  count;   // remaining arrivals for this round
        std::atomic<bool> sense;   // current release sense at this node
        int               size;    // number of arrivals expected (== RADIX)
        int               parent;  // index of parent node, -1 for the root

        Node() : count(0), sense(false), size(0), parent(-1) {}
    };

    // Per-thread sense, padded to a cache line to avoid false sharing.
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

    // Recursively combine up the tree. The last thread to arrive at a node
    // propagates to its parent, then resets the node and flips its sense to
    // release the threads spinning on it.
    void await(int nodeIdx, bool mySense)
    {
        Node& node = m_nodes[nodeIdx];
        int position = node.count.fetch_sub(1);
        if (position == 1) {
            // Last arrival at this node.
            if (node.parent != -1) {
                await(node.parent, mySense);
            }
            node.count.store(node.size);   // reset for the next round
            node.sense.store(mySense);      // release the waiting threads
        } else {
            // Spin until the last arrival flips this node's sense.
            while (node.sense.load() != mySense) {
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
            return; // a single thread (or none) needs no real tree
        }

        m_numNodes  = numThreads - 1;          // complete binary tree
        m_firstLeaf = numThreads / RADIX - 1;  // first leaf index in heap layout

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

        // Each thread enters the tree at its leaf node.
        int leaf = m_firstLeaf + t / RADIX;
        await(leaf, mySense);

        // Flip this thread's sense for the next barrier round.
        m_threadSense[t].sense = !mySense;
    }
};

#endif // TREE_BARRIER_IMPL_H
