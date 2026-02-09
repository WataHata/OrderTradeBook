#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>

#include "OrderBook.h"

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
    PinThreadToCore(5);

    const int NUM_ORDERS = 2000000;
    const int REPEATS = 50;

    struct OrderEvent {
        OrderType type;
        OrderId id;
        Side side;
        Price price;
        Quantity qty;
    };
    
    std::vector<OrderEvent> events;
    events.reserve(NUM_ORDERS);

    std::mt19937 rng(126456u);
    std::uniform_int_distribution<int> qty_dist(1, 50);
    std::bernoulli_distribution fak_dist(0.05); // ~5% FillAndKill
    std::uniform_int_distribution<int> offset_dist(0, 5);
    std::normal_distribution<double> mid_step_dist(0.0, 0.25);

    Price mid = 100;
    for (int i = 0; i < NUM_ORDERS; ++i) {
        mid = static_cast<Price>(std::max<Price>(1, mid + static_cast<Price>(std::lround(mid_step_dist(rng)))));

        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;

        const int offset = offset_dist(rng);
        Price price = (side == Side::Buy)
            ? static_cast<Price>(std::max<Price>(1, mid - offset))
            : static_cast<Price>(std::max<Price>(1, mid + offset));

        Quantity qty = static_cast<Quantity>(qty_dist(rng));
        OrderType type = fak_dist(rng) ? OrderType::FillAndKill : OrderType::GoodTillCancel;

        events.push_back({ type, static_cast<OrderId>(i) + 1, side, price, qty });
    }

    auto run_once_ns = [&events]() -> std::pair<long long, std::size_t> {
        OrderBook orderbook;

        for (int i = 0; i < 100; ++i) {
            orderbook.AddOrder(OrderType::GoodTillCancel, 999999 + i, Side::Buy, 99, 1);
            orderbook.CancelOrder(999999 + i);
        }

        const auto start = std::chrono::steady_clock::now();
        for (const auto& event : events) {
            orderbook.AddOrder(event.type, event.id, event.side, event.price, event.qty);
        }
        const auto end = std::chrono::steady_clock::now();

        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        return { static_cast<long long>(duration.count()), orderbook.Size() };
    };

    std::cout << "Warming up (not timed)..." << std::endl;
    (void)run_once_ns();

    std::cout << "Starting Consistency Mode: " << REPEATS << " runs of " << NUM_ORDERS << " orders..." << std::endl;

    std::vector<long long> durations_ns;
    durations_ns.reserve(static_cast<std::size_t>(REPEATS));

    std::size_t last_book_size = 0;
    for (int r = 0; r < REPEATS; ++r) {
        const auto [ns, book_size] = run_once_ns();
        durations_ns.push_back(ns);
        last_book_size = book_size;
    }

    const auto min_ns = *std::min_element(durations_ns.begin(), durations_ns.end());
    const auto max_ns = *std::max_element(durations_ns.begin(), durations_ns.end());
    const double avg_ns = std::accumulate(durations_ns.begin(), durations_ns.end(), 0.0) / durations_ns.size();

    std::vector<long long> sorted = durations_ns;
    std::sort(sorted.begin(), sorted.end());
    const auto median_ns = sorted[sorted.size() / 2];

    const double avg_latency_ns = avg_ns / static_cast<double>(NUM_ORDERS);
    const double median_latency_ns = static_cast<double>(median_ns) / static_cast<double>(NUM_ORDERS);

    std::cout << "------------------------------------------------" << std::endl;
    std::cout << "Total Time (min): " << min_ns << " ns" << std::endl;
    std::cout << "Total Time (median): " << median_ns << " ns" << std::endl;
    std::cout << "Total Time (max): " << max_ns << " ns" << std::endl;
    std::cout << "Average Latency per Order (avg): " << avg_latency_ns << " ns" << std::endl;
    std::cout << "Average Latency per Order (median): " << median_latency_ns << " ns" << std::endl;
    std::cout << "Throughput (from median): " << static_cast<long long>(1e9 / median_latency_ns) << " orders/sec" << std::endl;
    std::cout << "Resulting Orderbook Size (last run): " << last_book_size << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    return 0;
}