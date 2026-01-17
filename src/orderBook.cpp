#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <numeric>
#include <vector>
#include <memory>
#include <set>
#include <list>
#include <ctime>
#include <deque>
#include <stack>
#include <limits>
#include <string>
#include <variant>
#include <optional>
#include <tuple>
#include <stdexcept>

// WINDOWS SPECIFIC HEADERS FOR CPU PINNING
#ifdef _WIN32
#include <windows.h>
#endif

enum class OrderType
{
    GoodTillCancel,
    FillAndKill
};

enum class Side
{
    Buy,
    Sell
};

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

struct LevelInfo
{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos
{
public:
    OrderBookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
        : bids_{ bids }
        , asks_{ asks }
    {}

    const LevelInfos& GetBids() const { return bids_; }
    const LevelInfos& GetAsks() const { return asks_; }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};

class Order
{
public:
    // REQUIRED FOR OBJECT POOL (Vector resizing needs a default state)
    Order() = default; 

    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
        : orderType_{ orderType }
        , orderId_{ orderId }
        , side_{ side }
        , price_{ price }
        , initialQuantity_{ quantity }
        , remainingQuantity_{ quantity }
    {}

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    OrderType GetOrderType() const { return orderType_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; }
    Quantity GetRemainingQuantity() const { return remainingQuantity_; }
    Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
    bool IsFilled() const { return GetRemainingQuantity() == 0; }
    void Fill(Quantity quantity)
    {
        if (quantity > GetRemainingQuantity())
        {
            throw std::logic_error("Order cannot be filled for more than its remaining quantity.");
        }

        remainingQuantity_ -= quantity;
    }

private:
    OrderType orderType_ = OrderType::GoodTillCancel;
    OrderId orderId_ = 0;
    Side side_ = Side::Buy;
    Price price_ = 0;
    Quantity initialQuantity_ = 0;
    Quantity remainingQuantity_ = 0;
};

// --- NEW COMPONENT: OBJECT POOL ---
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

        // Re-construct object in place (simple assignment for POD-like types)
        pool_[index] = T(std::forward<Args>(args)...);
        return &pool_[index];
    }

    void release(T* ptr) {
        size_t index = ptr - &pool_[0];
        if (index >= pool_.size()) throw std::logic_error("Pointer not from this pool");
        free_indices_.push_back(index);
    }
};

// CHANGED: Use raw pointer, managed by pool
using OrderPointer = Order*;
using OrderPointers = std::list<OrderPointer>;

class OrderModify
{
public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
        : orderId_{ orderId }
        , price_{ price }
        , side_{ side }
        , quantity_{ quantity }
    {}

    OrderId GetOrderId() const { return orderId_; }
    Price GetPrice() const { return price_; }
    Side GetSide() const { return side_; }
    Quantity GetQuantity() const { return quantity_; }

    // Logic moved inside OrderBook to keep pool logic encapsulated
    // OrderPointer ToOrderPointer(OrderType type) const ... REMOVED
private:
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity quantity_;
};

struct TradeInfo
{
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade
{
public:
    Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
        : bidTrade_{ bidTrade }
        , askTrade_{ askTrade }
    {}

    const TradeInfo& GetBidTrade() const { return bidTrade_; }
    const TradeInfo& GetAskTrade() const { return askTrade_; }

private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;

class OrderBook
{
private:
    struct OrderEntry
    {
        OrderPointer order_{ nullptr };
        OrderPointers::iterator location_;
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;
    
    // THE MAGIC SAUCE: Memory Pool
    ObjectPool<Order> orderPool_{ 100000 }; 

    bool CanMatch(Side side, Price price) const
    {
        if (side == Side::Buy)
        {
            if (asks_.empty()) return false;
            const auto& [bestAsk, _] = *asks_.begin();
            return price >= bestAsk;
        }
        else
        {
            if (bids_.empty()) return false;
            const auto& [bestBid, _] = *bids_.begin();
            return price <= bestBid;
        }
    }

    Trades MatchOrders()
    {
        Trades trades;
        trades.reserve(orders_.size());

        while (true)
        {
            if (bids_.empty() || asks_.empty()) break;

            auto& [bidPrice, bids] = *bids_.begin();
            auto& [askPrice, asks] = *asks_.begin();

            if (bidPrice < askPrice) break;

            while (bids.size() && asks.size())
            {
                auto bid = bids.front();
                auto ask = asks.front();

                Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

                bid->Fill(quantity);
                ask->Fill(quantity);

                if (bid->IsFilled())
                {
                    bids.pop_front();
                    orders_.erase(bid->GetOrderId());
                    // RETURN TO POOL
                    orderPool_.release(bid); 
                }

                if (ask->IsFilled())
                {
                    asks.pop_front();
                    orders_.erase(ask->GetOrderId());
                    // RETURN TO POOL
                    orderPool_.release(ask); 
                }

                if (bids.empty()) bids_.erase(bidPrice);
                if (asks.empty()) asks_.erase(askPrice);

                trades.push_back(Trade{
                    TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
                    TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity }
                    });
            }
        }

        if (!bids_.empty())
        {
            auto& [_, bids] = *bids_.begin();
            auto& order = bids.front();
            if (order->GetOrderType() == OrderType::FillAndKill) CancelOrder(order->GetOrderId());
        }
        if (!asks_.empty())
        {
            auto& [_, asks] = *asks_.begin();
            auto& order = asks.front();
            if (order->GetOrderType() == OrderType::FillAndKill) CancelOrder(order->GetOrderId());
        }

        return trades;
    }

public:
    // CHANGED: Factory method pattern - takes data, not pointers
    Trades AddOrder(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
    {
        if (orders_.contains(orderId)) return {};

        if (orderType == OrderType::FillAndKill && !CanMatch(side, price))
        {
            return {};
        }

        // ACQUIRE MEMORY FROM POOL
        Order* order = orderPool_.acquire(orderType, orderId, side, price, quantity);

        OrderPointers::iterator iterator;

        if (order->GetSide() == Side::Buy)
        {
            auto& orders = bids_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }
        else
        {
            auto& orders = asks_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }

        orders_.insert({ order->GetOrderId(), OrderEntry{ order, iterator} });
        return MatchOrders();
    }

    void CancelOrder(OrderId orderId)
    {
        if (!orders_.contains(orderId)) return;

        const auto [order, iterator] = orders_.at(orderId);
        orders_.erase(orderId);

        if (order->GetSide() == Side::Sell)
        {
            auto price = order->GetPrice();
            auto& orders = asks_.at(price);
            orders.erase(iterator);
            if (orders.empty()) asks_.erase(price);
        }
        else
        {
            auto price = order->GetPrice();
            auto& orders = bids_.at(price);
            orders.erase(iterator);
            if (orders.empty()) bids_.erase(price);
        }

        // RELEASE MEMORY BACK TO POOL
        orderPool_.release(order); 
    }

    Trades MatchOrder(OrderModify order)
    {
        if (!orders_.contains(order.GetOrderId())) return {};

        const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
        OrderType type = existingOrder->GetOrderType(); // copy type before deleting
        
        CancelOrder(order.GetOrderId());
        
        // Pass arguments directly
        return AddOrder(type, order.GetOrderId(), order.GetSide(), order.GetPrice(), order.GetQuantity());
    }

    std::size_t Size() const { return orders_.size(); }

    OrderBookLevelInfos GetOrderInfos() const
    {
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(orders_.size());
        askInfos.reserve(orders_.size());

        auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
        {
            return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
                [](Quantity runningSum, const OrderPointer& order)
            { return runningSum + order->GetRemainingQuantity(); }) };
        };

        for (const auto& [price, orders] : bids_)
            bidInfos.push_back(CreateLevelInfos(price, orders));

        for (const auto& [price, orders] : asks_)
            askInfos.push_back(CreateLevelInfos(price, orders));

        return OrderBookLevelInfos{ bidInfos, askInfos };
    }
};

// UTILITY: Pin to Core (Windows Version)
void PinThreadToCore(int core_id) {
#ifdef _WIN32
    DWORD_PTR mask = (static_cast<DWORD_PTR>(1) << core_id);
    HANDLE thread = GetCurrentThread();
    DWORD_PTR result = SetThreadAffinityMask(thread, mask);
    if (result == 0) std::cerr << "Failed to pin thread." << std::endl;
    else std::cout << "Thread pinned to core " << core_id << std::endl;
#else
    // Linux/Mac implementation (stub for portability)
    std::cout << "Pinning not implemented for this OS." << std::endl;
#endif
}

int main()
{
    // 1. PIN THE THREAD
    PinThreadToCore(1); 

    OrderBook orderbook;
    const OrderId orderId = 1;

    // 2. ADD ORDER USING NEW SYNTAX (No shared_ptr, no 'new')
    orderbook.AddOrder(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10);
    
    std::cout << "Order Count: " << orderbook.Size() << std::endl;
    
    orderbook.CancelOrder(orderId);
    
    std::cout << "Order Count: " << orderbook.Size() << std::endl;

    return 0;
}