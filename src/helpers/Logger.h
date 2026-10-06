#pragma once

#include <filesystem>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

class Logger {
public:
    static void init(const std::string& file = "logs/game.log") {
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

        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        // Flush at debug level too: a game may terminate without an info-level
        // line ever being logged, which would otherwise drop buffered debug output.
        spdlog::flush_on(spdlog::level::debug);
    }
};

// Macros with source location support
#define LOG_INFO(...)  spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...)  spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::err, __VA_ARGS__)
#define LOG_DEBUG(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::debug, __VA_ARGS__)
#define LOG_TRACE(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::trace, __VA_ARGS__)
