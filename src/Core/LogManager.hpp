#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace opusoptima {

/// Centralised logging setup.
/// Call InitLogging() once at startup to create file sinks under `log/` and
/// attach a console sink
class LogManager {
    public:
    /// Initialise console + file logger
    /// @param logDir  Directory to store log files
    static void Init(const std::string& logDir = "log");

    /// Shorthand to get the project-wide logger.
    [[nodiscard]] static std::shared_ptr<spdlog::logger> Get();

    /// Get a sub-logger for a specific module.
    [[nodiscard]] static std::shared_ptr<spdlog::logger> Module(const std::string& name);

    private:
    static inline std::shared_ptr<spdlog::logger> mainLogger_;
};

/// Get a named logger
[[nodiscard]] inline std::shared_ptr<spdlog::logger> GetLoggerOrFallback(const std::string& name) {
    auto logger = spdlog::get(name);
    return logger ? logger : spdlog::default_logger();
}

} // namespace opusoptima
