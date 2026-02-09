#pragma once
#include <map>
#include <unordered_map>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iostream>

#include "Types.h"
#include "Order.h"
#include "OrderList.h"
#include "ObjectPool.h"

// --- Helper Structs ---
struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos {
public:
    OrderBookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
        : bids_{ bids }, asks_{ asks } {}
    const LevelInfos& GetBids() const { return bids_; }
    const LevelInfos& GetAsks() const { return asks_; }
private:
    LevelInfos bids_;
    LevelInfos asks_;
};

class OrderModify {
public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
        : orderId_{ orderId }, price_{ price }, side_{ side }, quantity_{ quantity } {}
    OrderId GetOrderId() const { return orderId_; }
    Price GetPrice() const { return price_; }
    Side GetSide() const { return side_; }
    Quantity GetQuantity() const { return quantity_; }
private:
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity quantity_;
};

struct TradeInfo {
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade {
public:
    Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
        : bidTrade_{ bidTrade }, askTrade_{ askTrade } {}
    const TradeInfo& GetBidTrade() const { return bidTrade_; }
    const TradeInfo& GetAskTrade() const { return askTrade_; }
private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;
using OrderPointer = Order*;
using OrderPointers = OrderList;

// --- Main Class ---
class OrderBook
{
private:
    struct OrderEntry
    {
        OrderPointer order_{ nullptr };
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;
    Trades trades_;
    
    ObjectPool<Order> orderPool_{ 1000000 };

    bool CanMatch(Side side, Price price) const
    {
        if (side == Side::Buy) {
            if (asks_.empty()) return false;
            const auto& [bestAsk, _] = *asks_.begin();
            return price >= bestAsk;
        } else {
            if (bids_.empty()) return false;
            const auto& [bestBid, _] = *bids_.begin();
            return price <= bestBid;
        }
    }

    const Trades MatchOrders()
    {
        trades_.clear();

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

                if (bid->IsFilled()) {
                    bids.pop_front();
                    orders_.erase(bid->GetOrderId());
                    orderPool_.release(bid);
                }

                if (ask->IsFilled()) {
                    asks.pop_front();
                    orders_.erase(ask->GetOrderId());
                    orderPool_.release(ask);
                }

                trades_.push_back(Trade{
                    TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
                    TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity }
                });
                
                bool bidsEmpty = bids.empty();
                bool asksEmpty = asks.empty();

                if (bidsEmpty) bids_.erase(bidPrice);
                if (asksEmpty) asks_.erase(askPrice);

                if (bidsEmpty || asksEmpty) break;
            }
        }

        if (!bids_.empty()) {
            auto& [_, bids] = *bids_.begin();
            if(!bids.empty()){
                auto order = bids.front();
                if (order->GetOrderType() == OrderType::FillAndKill) CancelOrder(order->GetOrderId());
            }
        }
        if (!asks_.empty()) {
            auto& [_, asks] = *asks_.begin();
            if(!asks.empty()){
                auto order = asks.front();
                if (order->GetOrderType() == OrderType::FillAndKill) CancelOrder(order->GetOrderId());
            }
        }

        return trades_;
    }

public:
    OrderBook() {
        trades_.reserve(10000); 
        orders_.reserve(1200000);
        orders_.max_load_factor(0.7f);
    }


    const Trades& AddOrder(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
    {
        if (orders_.contains(orderId)) {
            trades_.clear();
            return trades_;
        }
        
        if (orderType == OrderType::FillAndKill && !CanMatch(side, price)) return {};

        Order* order = orderPool_.acquire(orderType, orderId, side, price, quantity);

        if (order->GetSide() == Side::Buy) {
            auto& orders = bids_[order->GetPrice()];
            orders.push_back(order);
        } else {
            auto& orders = asks_[order->GetPrice()];
            orders.push_back(order);
        }

        orders_.insert({ order->GetOrderId(), OrderEntry{ order } });
        return MatchOrders();
    }

    void CancelOrder(OrderId orderId)
    {
        if (!orders_.contains(orderId)) return;

        const auto [order] = orders_.at(orderId);
        orders_.erase(orderId);

        if (order->GetSide() == Side::Sell) {
            auto price = order->GetPrice();
            auto& orders = asks_.at(price);
            orders.remove(order);
            if (orders.empty()) asks_.erase(price);
        } else {
            auto price = order->GetPrice();
            auto& orders = bids_.at(price);
            orders.remove(order);
            if (orders.empty()) bids_.erase(price);
        }

        orderPool_.release(order); 
    }

    Trades MatchOrder(OrderModify order)
    {
        if (!orders_.contains(order.GetOrderId())) return {};
        const auto& [existingOrder] = orders_.at(order.GetOrderId());
        OrderType type = existingOrder->GetOrderType(); 
        CancelOrder(order.GetOrderId());
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
                [](Quantity runningSum, const Order* order)
            { return runningSum + order->GetRemainingQuantity(); }) };
        };

        for (const auto& [price, orders] : bids_)
            bidInfos.push_back(CreateLevelInfos(price, orders));
        for (const auto& [price, orders] : asks_)
            askInfos.push_back(CreateLevelInfos(price, orders));

        return OrderBookLevelInfos{ bidInfos, askInfos };
    }
};