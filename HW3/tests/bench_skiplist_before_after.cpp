// Standalone benchmark: original hand-over-hand skiplist vs the optimized one.
// Not part of the graded submission - just to quantify the speedup claim.
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include <iostream>
#include <mutex>
#include <climits>
#include <cstdint>
#include <algorithm>

// ---------------- ORIGINAL (pre-optimization) implementation ----------------
namespace orig {
struct SkipList {
    SkipList()=delete;
    SkipList(int maxLevel):m_maxLevel(maxLevel){}
    virtual void insert(int value)=0;
    virtual bool search(int value)=0;
    virtual void remove(int value)=0;
    virtual ~SkipList()=default;
protected:
    int m_maxLevel;
};

class SkipListImpl : public SkipList {
private:
    struct Node {
        int key;
        int height;
        std::vector<Node*> next;
        std::recursive_mutex m;
        Node(int k, int h) : key(k), height(h), next(h, nullptr) {}
    };

    Node* m_head;
    int m_levels;

    int randomLevel() {
        static thread_local std::mt19937 gen(
            (unsigned)(std::random_device{}() ^ (uintptr_t)&gen));
        int lvl = 1;
        while (lvl < m_levels && (gen() & 1u)) lvl++;
        return lvl;
    }

    void findPreds(int value, std::vector<Node*>& preds) {
        Node* pred = m_head;
        pred->m.lock();
        for (int level = m_levels - 1; level >= 0; level--) {
            Node* curr = pred->next[level];
            if (curr != nullptr) curr->m.lock();
            while (curr != nullptr && curr->key < value) {
                pred->m.unlock();
                pred = curr;
                curr = pred->next[level];
                if (curr != nullptr) curr->m.lock();
            }
            if (curr != nullptr) curr->m.unlock();
            pred->m.lock();
            preds[level] = pred;
        }
        pred->m.unlock();
    }

    void unlockPreds(std::vector<Node*>& preds) {
        for (int level = 0; level < m_levels; level++) preds[level]->m.unlock();
    }

public:
    SkipListImpl(int maxLevel) : SkipList(maxLevel), m_levels(std::max(1, maxLevel)) {
        m_head = new Node(INT_MIN, m_levels);
    }
    ~SkipListImpl() {
        Node* curr = m_head;
        while (curr != nullptr) { Node* nxt = curr->next[0]; delete curr; curr = nxt; }
    }

    void insert(int value) override {
        const int lvl = randomLevel();
        std::vector<Node*> preds(m_levels);
        findPreds(value, preds);
        Node* nxt = preds[0]->next[0];
        if (nxt != nullptr && nxt->key == value) { unlockPreds(preds); return; }
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
            if (curr != nullptr) curr->m.lock();
            while (curr != nullptr && curr->key < value) {
                pred->m.unlock();
                pred = curr;
                curr = pred->next[level];
                if (curr != nullptr) curr->m.lock();
            }
            bool match = (curr != nullptr && curr->key == value);
            if (curr != nullptr) curr->m.unlock();
            if (match) { found = true; break; }
        }
        pred->m.unlock();
        return found;
    }

    void remove(int value) override {
        std::vector<Node*> preds(m_levels);
        findPreds(value, preds);
        Node* victim = preds[0]->next[0];
        if (victim == nullptr || victim->key != value) { unlockPreds(preds); return; }
        victim->m.lock();
        for (int i = 0; i < victim->height; i++)
            if (preds[i]->next[i] == victim) preds[i]->next[i] = victim->next[i];
        victim->m.unlock();
        delete victim;
        unlockPreds(preds);
    }
};
} // namespace orig

// ---------------- OPTIMIZED implementation (current file) ----------------
#include "../skip_list_par_impl.h"

template <typename SL>
double runBench(SL& sl, int T, int OPS, int KEYS) {
    std::vector<std::thread> th;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < T; t++) {
        th.emplace_back([&, t]() {
            std::mt19937 rng(1000 + t);
            std::uniform_int_distribution<int> k(0, KEYS - 1);
            std::uniform_int_distribution<int> op(0, 9); // 70% search, 15% insert, 15% remove
            for (int i = 0; i < OPS; i++) {
                int key = k(rng);
                int r = op(rng);
                if (r < 7) sl.search(key);
                else if (r < 9) sl.insert(key);
                else sl.remove(key);
            }
        });
    }
    for (auto& x : th) x.join();
    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

int main() {
    const int T = 8;
    const int OPS = 200000;
    const int KEYS = 20000;

    orig::SkipListImpl slOld(20);
    for (int i = 0; i < KEYS; i += 2) slOld.insert(i); // pre-populate half
    double tOld = runBench(slOld, T, OPS, KEYS);

    SkipListImpl slNew(20);
    for (int i = 0; i < KEYS; i += 2) slNew.insert(i);
    double tNew = runBench(slNew, T, OPS, KEYS);

    double totalOps = (double)T * OPS;
    std::cout << "ORIGINAL : " << tOld << " s  (" << (totalOps / tOld / 1e6) << " M ops/s)\n";
    std::cout << "OPTIMIZED: " << tNew << " s  (" << (totalOps / tNew / 1e6) << " M ops/s)\n";
    std::cout << "Speedup  : " << (tOld / tNew) << "x\n";
    return 0;
}
