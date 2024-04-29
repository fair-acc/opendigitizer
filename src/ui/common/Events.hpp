#ifndef OPENDIGITIZER_UI_EVENTS_HPP_
#define OPENDIGITIZER_UI_EVENTS_HPP_

namespace DigitizerUi {
struct EventLoop {
    static EventLoop &instance() {
        static EventLoop s_instance;
        return s_instance;
    }

    std::vector<std::function<void()>> _activeCallbacks;
    std::vector<std::function<void()>> _garbageCallbacks; // TODO: Cleaning up callbacks
    std::mutex                         _callbacksMutex;

    // schedule a function to be called at the next opportunity on the main thread
    void executeLater(std::function<void()> &&callback) {
        std::lock_guard lock(_callbacksMutex);
        _activeCallbacks.push_back(std::move(callback));
    }

    void fireCallbacks() {
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lock(_callbacksMutex);
            std::swap(callbacks, _activeCallbacks);
        }
        for (auto &cb : callbacks) {
            cb();
            _garbageCallbacks.push_back(std::move(cb));
        }
    }
};
} // namespace DigitizerUi

#endif
