#include "ThreadPool.hpp"

namespace spectra {

ThreadPool::ThreadPool(std::size_t workerCount, TaskQueue& queue, Handler handler)
    : queue_(queue)
    , handler_(std::move(handler))
{
    workers_.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool()
{
    // Safety net: if someone forgot to close the queue, make sure we never block forever.
    queue_.cancel();
    join();
}

void ThreadPool::join()
{
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void ThreadPool::workerLoop()
{
    FrameTask task;
    while (queue_.pop(task)) {
        try {
            handler_(task);
        } catch (...) {
            // A bad frame must never kill a worker thread (or the whole program).
        }
        task = FrameTask();   // release the frame memory before sleeping again
    }
}

} // namespace spectra
