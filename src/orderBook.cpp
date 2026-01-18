#include <iostream>
#include <vector>
#include <chrono>

#include "OrderBook.h"

// Windows Header for CPU Pinning
#ifdef _WIN32
#include <windows.h>
#endif

void PinThreadToCore(int core_id) {
#ifdef _WIN32
    DWORD_PTR mask = (static_cast<DWORD_PTR>(1) << core_id);
    HANDLE thread = GetCurrentThread();
    DWORD_PTR result = SetThreadAffinityMask(thread, mask);
    if (result == 0) std::cerr << "Failed to pin thread." << std::endl;
    else std::cout << "Thread pinned to core " << core_id << std::endl;
#else
    std::cout << "Pinning not implemented for this OS." << std::endl;
#endif
}

int main()
{
    // 1. PIN THE THREAD
    PinThreadToCore(1);

    OrderBook orderbook;
    
    // CONFIGURATION
    const int NUM_ORDERS = 1000000;
    
    // 2. PRE-GENERATE TRAFFIC
    struct OrderEvent {
        OrderType type;
        OrderId id;
        Side side;
        Price price;
        Quantity qty;
    };
    
    std::vector<OrderEvent> events;
    events.reserve(NUM_ORDERS);

    for(int i = 0; i < NUM_ORDERS; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = (i % 2 == 0) ? 100 : 100; 
        events.push_back({OrderType::GoodTillCancel, (OrderId)i+1, side, price, 10});
    }

    std::cout << "Warming up caches..." << std::endl;
    for(int i = 0; i < 100; ++i) {
        orderbook.AddOrder(OrderType::GoodTillCancel, 999999+i, Side::Buy, 99, 1);
        orderbook.CancelOrder(999999+i);
    }

    std::cout << "Starting Event Loop for " << NUM_ORDERS << " orders..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (const auto& event : events) {
        orderbook.AddOrder(event.type, event.id, event.side, event.price, event.qty);
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    double avg_latency = duration.count() / (double)NUM_ORDERS;
    
    std::cout << "------------------------------------------------" << std::endl;
    std::cout << "Total Time: " << duration.count() << " ns" << std::endl;
    std::cout << "Average Latency per Order: " << avg_latency << " ns" << std::endl;
    std::cout << "Throughput: " << (int)(1e9 / avg_latency) << " orders/sec" << std::endl;
    std::cout << "Resulting Orderbook Size: " << orderbook.Size() << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    return 0;
}