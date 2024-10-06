#pragma once

#include <chrono>
#include <string>

namespace DigitizerUi::components {

struct Notification {
    std::string               text;
    std::chrono::milliseconds dismissTime{5000};

    static void success(Notification&& notification);
    static void success(const std::string& text, std::chrono::milliseconds dismissTime = std::chrono::seconds{5});

    static void warning(Notification&& notification);
    static void warning(const std::string& text, std::chrono::milliseconds dismissTime = std::chrono::seconds{5});

    static void error(Notification&& notification);
    static void error(const std::string& text, std::chrono::milliseconds dismissTime = std::chrono::seconds{10});

    static void info(Notification&& notification);
    static void info(const std::string& text, std::chrono::milliseconds dismissTime = std::chrono::seconds{5});

    static void render();
};

} // namespace DigitizerUi::components
