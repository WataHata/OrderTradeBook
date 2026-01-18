#pragma once
#include <vector>
#include <stdexcept>
#include <utility>

template<typename T>
class ObjectPool {
private:
    std::vector<T> pool_;
    std::vector<size_t> free_indices_;

public:
    ObjectPool(size_t size) {
        pool_.resize(size);
        free_indices_.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            free_indices_.push_back(i);
        }
    }

    template<typename... Args>
    T* acquire(Args&&... args) {
        if (free_indices_.empty()) {
            throw std::runtime_error("Pool exhausted! Increase pool size.");
        }

        size_t index = free_indices_.back();
        free_indices_.pop_back();

        // Re-construct object in place
        pool_[index] = T(std::forward<Args>(args)...);
        return &pool_[index];
    }

    void release(T* ptr) {
        size_t index = ptr - &pool_[0];
        if (index >= pool_.size()) throw std::logic_error("Pointer not from this pool");
        free_indices_.push_back(index);
    }
};