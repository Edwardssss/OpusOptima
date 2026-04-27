#include "LogManager.hpp"

#include <filesystem>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace opusoptima {

void LogManager::Init(const std::string& logDir) {
    std::filesystem::create_directories(logDir);

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::info);

    const auto logFilePath = std::filesystem::path(logDir) / "opusoptima.log";
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    fileSink->set_level(spdlog::level::trace);

    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
    mainLogger_ = std::make_shared<spdlog::logger>("opusoptima", sinks.begin(), sinks.end());
    mainLogger_->set_level(spdlog::level::trace);
    mainLogger_->flush_on(spdlog::level::err);

    spdlog::register_logger(mainLogger_);
    spdlog::set_default_logger(mainLogger_);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

    mainLogger_->info("Logging initialised — logs will be written to {}", logFilePath.string());
}

std::shared_ptr<spdlog::logger> LogManager::Get() {
    return mainLogger_ ? mainLogger_ : spdlog::default_logger();
}

std::shared_ptr<spdlog::logger> LogManager::Module(const std::string& name) {
    // spdlog will auto-register and cache clones
    return spdlog::get(name) ? spdlog::get(name) : mainLogger_->clone(name);
}

} // namespace opusoptima
