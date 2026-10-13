#include "TaskQueue.hpp"

#include <utility>

namespace spectra {

TaskQueue::TaskQueue(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity)
{
}

bool TaskQueue::push(FrameTask&& task)
{
    std::unique_lock<std::mutex> lock(mutex_);
    // Sleep until there is room (or until we are told to stop).
    notFull_.wait(lock, [this] { return queue_.size() < capacity_ || closed_ || cancelled_; });
    if (closed_ || cancelled_) {
        return false;
    }
    queue_.push(std::move(task));
    lock.unlock();
    notEmpty_.notify_one();   // wake exactly one sleeping worker
    return true;
}

bool TaskQueue::pop(FrameTask& out)
{
    std::unique_lock<std::mutex> lock(mutex_);
    // Sleep until there is work, or the producer is finished, or we are cancelled.
    notEmpty_.wait(lock, [this] { return !queue_.empty() || closed_ || cancelled_; });
    if (cancelled_ || queue_.empty()) {
        return false;   // cancelled, or closed and fully drained
    }
    out = std::move(queue_.front());
    queue_.pop();
    lock.unlock();
    notFull_.notify_one();   // a slot just became free for the producer
    return true;
}

void TaskQueue::close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
}

void TaskQueue::cancel()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = true;
        std::queue<FrameTask> empty;
        queue_.swap(empty);   // drop pending frames immediately (frees memory)
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
}

std::size_t TaskQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

} // namespace spectra
