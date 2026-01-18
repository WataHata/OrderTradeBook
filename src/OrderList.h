#pragma once
#include "Order.h"

class OrderList
{
public:
    OrderList() = default;

    void push_back(Order* order)
    {
        if (!head_)
        {
            head_ = order;
            tail_ = order;
            order->prev_ = nullptr;
            order->next_ = nullptr;
        }
        else
        {
            tail_->next_ = order;
            order->prev_ = tail_;
            order->next_ = nullptr;
            tail_ = order;
        }
        size_++; 
    }

    void remove(Order* order)
    {
        if (order->prev_) order->prev_->next_ = order->next_;
        else head_ = order->next_;

        if (order->next_) order->next_->prev_ = order->prev_;
        else tail_ = order->prev_;

        order->prev_ = nullptr;
        order->next_ = nullptr;
        size_--;
    }

    Order* front() const { return head_; }
    void pop_front() { if (head_) remove(head_); }
    bool empty() const { return head_ == nullptr; }
    size_t size() const { return size_; }

    class Iterator {
    public:
        Iterator(Order* ptr) : ptr_(ptr) {}
        Order* operator*() { return ptr_; }
        Iterator& operator++() { if (ptr_) ptr_ = ptr_->next_; return *this; }
        bool operator!=(const Iterator& other) const { return ptr_ != other.ptr_; }
        bool operator==(const Iterator& other) const { return ptr_ == other.ptr_; }
    private:
        Order* ptr_;
    };

    class ConstIterator {
    public:
        ConstIterator(const Order* ptr) : ptr_(ptr) {}
        const Order* operator*() const { return ptr_; }
        ConstIterator& operator++() { if (ptr_) ptr_ = ptr_->next_; return *this; }
        bool operator!=(const ConstIterator& other) const { return ptr_ != other.ptr_; }
        bool operator==(const ConstIterator& other) const { return ptr_ == other.ptr_; }
    private:
        const Order* ptr_;
    };

    Iterator begin() { return Iterator(head_); }
    Iterator end() { return Iterator(nullptr); }
    ConstIterator begin() const { return ConstIterator(head_); }
    ConstIterator end() const { return ConstIterator(nullptr); }

private:
    Order* head_ = nullptr;
    Order* tail_ = nullptr;
    size_t size_ = 0;
};