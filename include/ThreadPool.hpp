#pragma once
//
// ThreadPool
// ----------
// A fixed group of worker threads that are created ONCE and then reused for
// every frame. (Creating a new thread per frame would waste time, because
// starting and stopping an OS thread is expensive.)
//
// Each worker runs this loop:
//      while (queue.pop(task))  handler(task);
// When the queue has no work the worker sleeps inside pop().

#include "TaskQueue.hpp"

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace spectra {

class ThreadPool {
public:
    using Handler = std::function<void(FrameTask&)>;

    ThreadPool(std::size_t workerCount, TaskQueue& queue, Handler handler);
    ~ThreadPool();   // always joins, so no thread is ever left running

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Wait for every worker to finish (call after queue.close() or queue.cancel()).
    void join();

    std::size_t workerCount() const { return workers_.size(); }

private:
    void workerLoop();

    TaskQueue& queue_;
    Handler handler_;
    std::vector<std::thread> workers_;
};

} // namespace spectra
