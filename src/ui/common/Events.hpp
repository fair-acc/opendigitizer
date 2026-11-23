#ifndef OPENDIGITIZER_UI_EVENTS_HPP_
#define OPENDIGITIZER_UI_EVENTS_HPP_

#include <functional>
#include <mutex>
#include <vector>

namespace gr::od {
struct EventLoop {
    std::vector<std::function<void()>> _activeCallbacks;
    std::mutex                         _callbacksMutex;

    static EventLoop& instance() {
        static EventLoop loop;
        return loop;
    }

    // schedule a function to be called at the next opportunity on the main thread
    void executeLater(std::function<void()>&& callback) {
        std::lock_guard lock(_callbacksMutex);
        _activeCallbacks.push_back(std::move(callback));
    }

    void fireCallbacks() {
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lock(_callbacksMutex);
            std::swap(callbacks, _activeCallbacks);
        }
        for (auto& cb : callbacks) {
            cb();
        }
    }
};
} // namespace gr::od

#endif
