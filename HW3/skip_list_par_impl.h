/**************************************************/
/* Sari Mansour */
/**************************************************/
#ifndef SKIPLIST_PAR_IMPL_H
#define SKIPLIST_PAR_IMPL_H

#include "skip_list_seq.h"

#include <mutex>
#include <random>
#include <climits>
#include <cstdint>
#include <algorithm>

class SkipListImpl : public SkipList {
private:
    // Fixed cap on tower height, way beyond any practical log2(n) requirement.
    // Lets preds/next live inline (stack array / in-Node array) instead of
    // heap-allocating a vector on every single insert/remove/Node ctor call.
    static constexpr int MAX_LEVELS = 64;

    struct Node {
        int key;
        int height;
        Node* next[MAX_LEVELS];
        std::recursive_mutex m;

        Node(int k, int h) : key(k), height(h) {
            for (int i = 0; i < h; i++) next[i] = nullptr;
        }
    };

    Node* m_head;    
    int m_levels;    

    int randomLevel() {
        static thread_local std::mt19937 gen(
            (unsigned)(std::random_device{}() ^
                       (uintptr_t)&gen));
        int lvl = 1;
        while (lvl < m_levels && (gen() & 1u)) {
            lvl++;
        }
        return lvl;
    }

    // NOTE on locking: while `pred` is held locked, `pred->next[level]` (== curr)
    // cannot be deleted by any other thread, because remove() must lock every
    // one of a victim's predecessors (including this exact `pred`, at this exact
    // level) before unlinking/deleting it. So curr->key can be read safely
    // without locking curr first -- we only need to lock curr right before it
    // is *promoted* to `pred`, to avoid a zero-locks window during the handoff
    // (which is the only place a UAF could occur).
    void findPreds(int value, Node** preds) {
        Node* pred = m_head;
        pred->m.lock();
        for (int level = m_levels - 1; level >= 0; level--) {
            Node* curr = pred->next[level];
            while (curr != nullptr && curr->key < value) {
                curr->m.lock();
                pred->m.unlock();
                pred = curr;
                curr = pred->next[level];
            }
            pred->m.lock();          // pin for this level (recursive: +1 on pred)
            preds[level] = pred;
        }
        pred->m.unlock();
    }

    // Release the per-level pins taken by findPreds().
    void unlockPreds(Node** preds) {
        for (int level = 0; level < m_levels; level++) {
            preds[level]->m.unlock();
        }
    }

public:
    SkipListImpl(int maxLevel) : SkipList(maxLevel), m_levels(std::min(std::max(1, maxLevel), MAX_LEVELS)) {
        m_head = new Node(INT_MIN, m_levels);
    }

    ~SkipListImpl() {
        Node* curr = m_head;
        while (curr != nullptr) {
            Node* nxt = curr->next[0];
            delete curr;
            curr = nxt;
        }
    }

    void insert(int value) override {
        const int lvl = randomLevel();
        Node* preds[MAX_LEVELS];
        findPreds(value, preds);

        Node* nxt = preds[0]->next[0];
        if (nxt != nullptr && nxt->key == value) {
            unlockPreds(preds);
            return;
        }

        Node* node = new Node(value, lvl);
        for (int i = 0; i < lvl; i++) {
            node->next[i] = preds[i]->next[i];
            preds[i]->next[i] = node;
        }
        unlockPreds(preds);
    }

    bool search(int value) override {
        Node* pred = m_head;
        pred->m.lock();
        bool found = false;
        for (int level = m_levels - 1; level >= 0; level--) {
            Node* curr = pred->next[level];
            while (curr != nullptr && curr->key < value) {
                curr->m.lock();
                pred->m.unlock();
                pred = curr;
                curr = pred->next[level];
            }
            if (curr != nullptr && curr->key == value) {
                found = true;
                break;
            }
        }
        pred->m.unlock();
        return found;
    }

    void remove(int value) override {
        Node* preds[MAX_LEVELS];
        findPreds(value, preds);

        Node* victim = preds[0]->next[0];
        if (victim == nullptr || victim->key != value) {
            unlockPreds(preds);
            return;
        }

        victim->m.lock();
        for (int i = 0; i < victim->height; i++) {
            if (preds[i]->next[i] == victim) {
                preds[i]->next[i] = victim->next[i];
            }
        }
        victim->m.unlock();

        delete victim;

        unlockPreds(preds);
    }
};

#endif 
