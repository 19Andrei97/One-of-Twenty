#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

class Logger {
public:
    static void init(const std::string& file = "logs/game.log") {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%T] [%^%l%$] [%s:%#] %v");

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
        file_sink->set_pattern("[%T] [%l] [%s:%#] %v");

        std::vector<spdlog::sink_ptr> sinks{ console_sink, file_sink };
        auto logger = std::make_shared<spdlog::logger>("multi_sink", sinks.begin(), sinks.end());

        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::info);
    }
};

// Macros with source location support
#define LOG_INFO(...)  spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...)  spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::err, __VA_ARGS__)
#define LOG_DEBUG(...) spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__}, spdlog::level::debug, __VA_ARGS__)
