#ifndef OPENDIGITIZER_UTILS_SCOPE_EXIT_H
#define OPENDIGITIZER_UTILS_SCOPE_EXIT_H

#include <utility>

namespace Digitizer::utils {

template<typename OnExitFn>
struct scope_exit {
    bool     disable = false;
    OnExitFn onExit;

    scope_exit(OnExitFn&& _onExit) : onExit(std::move(_onExit)) {}
    ~scope_exit() {
        if (!disable) {
            onExit();
        }
    }
};

} // namespace Digitizer::utils

#endif
