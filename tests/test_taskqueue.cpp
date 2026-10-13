// TaskQueue: ordering, producer/consumer correctness, bounded size, close, cancel.
#include "TaskQueue.hpp"
#include "test_common.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace spectra;
using namespace std::chrono_literals;

static FrameTask make(std::uint64_t id)
{
    FrameTask t;
    t.frameId = id;
    return t;
}

int main()
{
    // --- FIFO order ---------------------------------------------------------
    {
        TaskQueue q(8);
        for (std::uint64_t i = 0; i < 5; ++i) CHECK(q.push(make(i)));
        FrameTask t;
        for (std::uint64_t i = 0; i < 5; ++i) {
            CHECK(q.pop(t));
            CHECK(t.frameId == i);
        }
        CHECK(q.size() == 0);
    }

    // --- 1 producer, 4 consumers: every task is delivered exactly once --------
    {
        const int N = 5000;
        TaskQueue q(16);
        std::vector<std::atomic<int>> seen(N);
        for (auto& s : seen) s = 0;
        std::atomic<int> total{0};

        std::vector<std::thread> consumers;
        for (int c = 0; c < 4; ++c) {
            consumers.emplace_back([&] {
                FrameTask t;
                while (q.pop(t)) {
                    seen[t.frameId]++;
                    total++;
                }
            });
        }
        std::thread producer([&] {
            for (int i = 0; i < N; ++i) q.push(make(i));
            q.close();
        });
        producer.join();
        for (auto& c : consumers) c.join();

        CHECK(total == N);
        bool exactlyOnce = true;
        for (auto& s : seen) exactlyOnce = exactlyOnce && (s == 1);
        CHECK(exactlyOnce);
    }

    // --- bounded: producer must wait when the queue is full -------------------
    {
        TaskQueue q(2);
        std::atomic<int> pushed{0};
        std::thread producer([&] {
            for (int i = 0; i < 5; ++i) {
                q.push(make(i));
                pushed++;
            }
        });
        std::this_thread::sleep_for(200ms);
        CHECK(q.size() <= 2);
        CHECK(pushed <= 2);          // the producer is blocked (sleeping), not spinning
        FrameTask t;
        for (int i = 0; i < 5; ++i) CHECK(q.pop(t));
        producer.join();
        CHECK(pushed == 5);
    }

    // --- close: remaining items are still delivered, then pop() returns false ---
    {
        TaskQueue q(8);
        q.push(make(1));
        q.push(make(2));
        q.close();
        CHECK(!q.push(make(3)));      // no pushes after close
        FrameTask t;
        CHECK(q.pop(t));
        CHECK(q.pop(t));
        CHECK(!q.pop(t));
    }

    // --- cancel wakes a sleeping consumer and a blocked producer -----------------
    {
        TaskQueue q(1);
        std::atomic<bool> consumerReturned{false};
        std::thread consumer([&] {
            FrameTask t;
            bool got = q.pop(t);       // queue is empty: sleeps here
            consumerReturned = !got;
        });
        std::this_thread::sleep_for(100ms);
        q.cancel();
        consumer.join();
        CHECK(consumerReturned);

        TaskQueue q2(1);
        q2.push(make(0));
        std::atomic<bool> producerReturned{false};
        std::thread producer([&] {
            bool ok = q2.push(make(1));   // full: sleeps here
            producerReturned = !ok;
        });
        std::this_thread::sleep_for(100ms);
        q2.cancel();
        producer.join();
        CHECK(producerReturned);
        CHECK(q2.size() == 0);            // pending work was dropped
    }

    return finishTest("test_taskqueue");
}
