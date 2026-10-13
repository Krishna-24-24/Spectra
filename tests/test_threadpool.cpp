// ThreadPool: workers are reused, work really runs in parallel, errors do not kill workers.
#include "TaskQueue.hpp"
#include "ThreadPool.hpp"
#include "test_common.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>

using namespace spectra;
using namespace std::chrono_literals;

int main()
{
    // --- 1000 tasks, 4 workers: at most 4 distinct threads are ever used ---------
    {
        TaskQueue q(8);
        std::mutex m;
        std::set<std::thread::id> ids;
        std::atomic<int> processed{0}, running{0}, maxRunning{0};

        ThreadPool pool(4, q, [&](FrameTask&) {
            int now = ++running;
            int prev = maxRunning.load();
            while (now > prev && !maxRunning.compare_exchange_weak(prev, now)) {}
            std::this_thread::sleep_for(1ms);
            {
                std::lock_guard<std::mutex> lock(m);
                ids.insert(std::this_thread::get_id());
            }
            --running;
            ++processed;
        });
        CHECK(pool.workerCount() == 4);

        for (int i = 0; i < 1000; ++i) {
            FrameTask t;
            t.frameId = i;
            q.push(std::move(t));
        }
        q.close();
        pool.join();

        CHECK(processed == 1000);
        CHECK(ids.size() <= 4);            // threads were REUSED (not one thread per task)
        CHECK(ids.size() >= 2);
        CHECK(maxRunning >= 2);            // tasks really ran at the same time
    }

    // --- an exception inside a task does not stop the worker ----------------------
    {
        TaskQueue q(4);
        std::atomic<int> attempted{0};
        ThreadPool pool(2, q, [&](FrameTask& t) {
            ++attempted;
            if (t.frameId % 10 == 0) throw 42;
        });
        for (int i = 0; i < 100; ++i) {
            FrameTask t;
            t.frameId = i;
            q.push(std::move(t));
        }
        q.close();
        pool.join();
        CHECK(attempted == 100);
    }

    // --- destroying an idle pool never hangs ------------------------------------------
    {
        TaskQueue q(4);
        {
            ThreadPool pool(3, q, [](FrameTask&) {});
            std::this_thread::sleep_for(50ms);
        }   // destructor must cancel + join here
        CHECK(true);
    }

    return finishTest("test_threadpool");
}
