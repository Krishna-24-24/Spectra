#pragma once
//
// TaskQueue
// ---------
// The "waiting line" between the producer (the video reader) and the
// consumers (the worker threads).
//
//  * std::mutex               - only one thread touches the queue at a time
//  * std::condition_variable  - threads SLEEP while they wait (no busy polling)
//  * bounded capacity         - if workers are slower than the reader, the reader
//                               waits instead of filling the RAM with frames

#include "Types.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

namespace spectra {

class TaskQueue {
public:
    explicit TaskQueue(std::size_t capacity = 16);

    // Producer side. Blocks while the queue is full.
    // Returns false if the queue was closed or cancelled (the task is dropped).
    bool push(FrameTask&& task);

    // Consumer side. Blocks while the queue is empty.
    // Returns false when there is nothing more to do (closed and drained, or cancelled).
    bool pop(FrameTask& out);

    // "No more frames are coming." Consumers finish what is already queued.
    void close();

    // "Stop now." Pending tasks are thrown away and every waiting thread wakes up.
    void cancel();

    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::queue<FrameTask> queue_;
    std::size_t capacity_;
    bool closed_ = false;
    bool cancelled_ = false;
};

} // namespace spectra
