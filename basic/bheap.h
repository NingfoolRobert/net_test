/**
 * @file bheap.h
 * @brief binary heap  support, ie erase, insert, extract
 * @author bfning
 * @version 0.1
 * @date 2025-01-04
 */
#pragma once
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace detail {
template <typename T, typename C = std::less<T>>
class bheap {
public:
    struct bheap_context {
        size_t idx;
        T val;
    };

    bheap() = default;
    //
    ~bheap() {
        while (!empty()) {
            pop();
        }
    }
    //
    bool insert(T &&t, bheap_context *&context) {

        context = new bheap_context();
        if (nullptr == context) {
            return false;
        }
        //
        context->idx = size();
        context->val = t;
        heap_.push_back(context);
        sift_up(size() - 1);
        return true;
    }
    //
    void erase(size_t idx) {
        if (idx >= size()) {
            throw std::runtime_error("erase idx invalid(exceed size)");
        }
        //
        std::swap(heap_[idx], heap_[size() - 1]);
        std::swap(heap_[idx]->idx, heap_[size() - 1]->idx);
        delete heap_[size() - 1];
        heap_.pop_back();

        if (idx < heap_.size()) {
            if (comp_(heap_[idx]->val, heap_[size() - 1]->val)) {
                sift_up(idx);
            }
            else {
                sift_down(idx);
            }
        }
    }
    //
    T &top() {
        if (empty()) {
            throw std::runtime_error("bheap is empty.");
        }
        return heap_[0]->val;
    }
    //
    T pop() {
        if (empty()) {
            throw std::runtime_error("pop empty binary heap");
        }
        T t = heap_[0]->val;
        erase(0);
        return t;
    }
    //
    size_t size() {
        return heap_.size();
    }

    bool empty() {
        return size() == 0;
    }

private:
    //
    void sift_up(size_t idx) {
        size_t parent = (idx - 1) / 2;
        while (idx > 0 && comp_(heap_[parent]->val, heap_[idx]->val)) {
            std::swap(heap_[parent], heap_[idx]);
            std::swap(heap_[parent]->idx, heap_[idx]->idx);
            idx = parent;
            parent = (idx - 1) / 2;
        }
    }
    //
    void sift_down(size_t idx) {

        size_t size = heap_.size();
        size_t left, right, largest;
        while (true) {

            left = 2 * idx + 1;
            right = 2 * idx + 2;
            largest = idx;
            if (left < size && comp_(heap_[largest]->val, heap_[left]->val)) {
                largest = left;
            }
            //
            if (right < size && comp_(heap_[largest]->val, heap_[right]->val)) {
                largest = right;
            }
            //
            if (largest == idx) {
                break;
            }
            //
            std::swap(heap_[largest], heap_[idx]);
            std::swap(heap_[largest]->idx, heap_[idx]->idx);
            sift_down(largest);
            idx = largest;
        }
    }

private:
    std::vector<bheap_context *> heap_;
    C comp_;
};
}  // namespace detail
