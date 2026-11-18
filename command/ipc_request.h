#pragma once

#include "../src/js_engine.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

namespace ipc_bridge
{
struct Request
{
    explicit Request(const std::string& source) : script(source) {}
    std::string script;
    js_engine::EvalResult result{false, ""};
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
    std::atomic<bool> cancelled{false};
};

using RequestPtr = std::shared_ptr<Request>;
enum class WaitStatus { Completed, Stopped, TimedOut };

inline void completeRequest(Request* request, js_engine::EvalResult result)
{
    {
        std::lock_guard<std::mutex> lock(request->mutex);
        request->result = std::move(result);
        request->done = true;
    }
    request->condition.notify_all();
}

inline WaitStatus waitForRequest(Request& request, const std::atomic<bool>& running,
    std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(request.mutex);
    while (!request.done && running)
    {
        const auto next = (std::min)(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
        request.condition.wait_until(lock, next);
        if (!request.done && std::chrono::steady_clock::now() >= deadline)
        {
            request.cancelled = true;
            return WaitStatus::TimedOut;
        }
    }
    if (request.done) return WaitStatus::Completed;
    request.cancelled = true;
    return WaitStatus::Stopped;
}
}
