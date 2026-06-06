/*
Written by Sari Mansour, 2026
*/

#pragma once

#include "UnboundedQueue1p1c.h"
#include <atomic>
#include <cstddef>  // nullptr

// Plain C-style linked list node (no STL containers or arrays allowed)
struct Node {
    int   value;
    Node* next;
    explicit Node(int v = 0) : value(v), next(nullptr) {}
};

class UnboundedQueue1p1c : public UnboundedQueue1p1cAbstract {
public:
    // Initialize with one dummy node.
    // head and tail both point to it.
    // Queue is "empty" when head->next == nullptr (only the dummy exists).
    UnboundedQueue1p1c() {
        Node* dummy = new Node();
        _head.store(dummy, std::memory_order_relaxed);
        _tail.store(dummy, std::memory_order_relaxed);
        _size.store(0, std::memory_order_relaxed);
    }

    ~UnboundedQueue1p1c() {
        // Drain remaining nodes (including the dummy)
        Node* cur = _head.load(std::memory_order_relaxed);
        while (cur) {
            Node* next = cur->next;
            delete cur;
            cur = next;
        }
    }

    // Called only by the single producer thread.
    // Appends a new node after the current tail. Always succeeds.
    void push(int value) override {
        Node* node = new Node(value);

        Node* old_tail = _tail.load(std::memory_order_relaxed);
        old_tail->next = node;                                      
        _tail.store(node, std::memory_order_release);              

        _size.fetch_add(1, std::memory_order_relaxed);
    }

    // Called only by the single consumer thread.
    // Returns false if only the dummy node remains (logically empty).
    // Otherwise, pops head->next, makes it the new dummy, and returns true.
    bool pop(int &val) override {
        Node* head = _head.load(std::memory_order_relaxed);

        Node* next = head->next;   

        if (next == nullptr) return false; 

        val = next->value;

        _head.store(next, std::memory_order_release);

        delete head; 

        _size.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    int size() const override {
        return _size.load(std::memory_order_relaxed);
    }

private:
    std::atomic<Node*> _head;  // written only by consumer
    std::atomic<Node*> _tail;  // written only by producer
    std::atomic<int>   _size;
};
