#include "../command/ipc_request.h"
#include <future>
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    using namespace std::chrono_literals;
    std::atomic<bool> running{true};
    auto completed = std::make_shared<ipc_bridge::Request>("query");
    auto response = std::async(std::launch::async, [&] {
        return ipc_bridge::waitForRequest(*completed, running, 2s);
    });
    ipc_bridge::completeRequest(completed.get(), {true, "result"});
    check(response.get() == ipc_bridge::WaitStatus::Completed, "completion did not wake worker");
    check(completed->result.text == "result", "result was not preserved");

    auto stopped = std::make_shared<ipc_bridge::Request>("open");
    std::promise<void> waiting;
    auto worker = std::async(std::launch::async, [&] {
        waiting.set_value();
        return ipc_bridge::waitForRequest(*stopped, running, 180s);
    });
    waiting.get_future().wait();
    running = false;
    check(worker.wait_for(1s) == std::future_status::ready, "shutdown waited for UI-thread message delivery");
    check(worker.get() == ipc_bridge::WaitStatus::Stopped, "shutdown was not cancelled");
    check(stopped->cancelled, "queued operation remained executable after shutdown");

    running = true;
    auto timedOut = std::make_shared<ipc_bridge::Request>("slow query");
    std::weak_ptr<ipc_bridge::Request> lifetime = timedOut;
    auto queuedMessage = std::make_unique<ipc_bridge::RequestPtr>(timedOut);
    check(ipc_bridge::waitForRequest(*timedOut, running, 1ms) == ipc_bridge::WaitStatus::TimedOut, "timeout did not return");
    check(timedOut->cancelled, "timeout did not cancel queued work");
    timedOut.reset();
    check(!lifetime.expired(), "queued request storage expired after client timeout");
    // A late main-thread response still has valid storage, even after timeout.
    ipc_bridge::completeRequest(queuedMessage->get(), {false, "cancelled"});
    queuedMessage.reset();
    check(lifetime.expired(), "queued request storage leaked");
    std::cout << "IPC completion, shutdown cancellation, timeout, and queued lifetime checks passed\n";
}
