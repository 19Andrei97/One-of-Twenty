#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace Logger {

// Map a config string ("trace", "debug", "info", "warn", "error", "critical",
// "off") to an spdlog level, throwing on an unknown value so a typo in
// config.json fails loudly instead of silently falling back.
inline spdlog::level::level_enum levelFromString(const std::string& name)
{
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowered == "trace")    return spdlog::level::trace;
    if (lowered == "debug")    return spdlog::level::debug;
    if (lowered == "info")     return spdlog::level::info;
    if (lowered == "warn" || lowered == "warning") return spdlog::level::warn;
    if (lowered == "error" || lowered == "err")    return spdlog::level::err;
    if (lowered == "critical") return spdlog::level::critical;
    if (lowered == "off")      return spdlog::level::off;

    throw std::runtime_error("Unknown logger level: '" + name + "'");
}

inline void init(const std::string& file = "logs/game.log",
                 const std::string& level = "debug")
{
    // The file sink does not create parent directories, so a fresh clone
    // (where logs/ is gitignored) would throw on startup without this.
    if (const std::filesystem::path path(file); path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%T] [%^%l%$] [%s:%#] %v");

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
    file_sink->set_pattern("[%T] [%l] [%s:%#] %v");

    std::vector<spdlog::sink_ptr> sinks{ console_sink, file_sink };
    auto logger = std::make_shared<spdlog::logger>("multi_sink", sinks.begin(), sinks.end());

    const auto parsed = levelFromString(level);
    logger->set_level(parsed);

    spdlog::set_default_logger(logger);
    spdlog::set_level(parsed);
    // Flush at the configured level: a game may terminate without a higher-level
    // line ever being logged, which would otherwise drop buffered output.
    spdlog::flush_on(parsed);
}

} // namespace Logger

// Macros with source location support
#define LOG_INFO(...)  spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...)  spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::err, __VA_ARGS__)
#define LOG_DEBUG(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::debug, __VA_ARGS__)
#define LOG_TRACE(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::trace, __VA_ARGS__)
