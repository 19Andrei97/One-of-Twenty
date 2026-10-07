#include <doctest/doctest.h>

#include "Logger.h"

#include <stdexcept>
#include <string>

TEST_CASE("logger level strings map to spdlog levels")
{
    CHECK(Logger::levelFromString("trace") == spdlog::level::trace);
    CHECK(Logger::levelFromString("debug") == spdlog::level::debug);
    CHECK(Logger::levelFromString("info") == spdlog::level::info);
    CHECK(Logger::levelFromString("warn") == spdlog::level::warn);
    CHECK(Logger::levelFromString("warning") == spdlog::level::warn);
    CHECK(Logger::levelFromString("error") == spdlog::level::err);
    CHECK(Logger::levelFromString("err") == spdlog::level::err);
    CHECK(Logger::levelFromString("critical") == spdlog::level::critical);
    CHECK(Logger::levelFromString("off") == spdlog::level::off);
}

TEST_CASE("logger level parsing is case-insensitive")
{
    CHECK(Logger::levelFromString("DEBUG") == spdlog::level::debug);
    CHECK(Logger::levelFromString("Info") == spdlog::level::info);
}

TEST_CASE("unknown logger level throws instead of silently defaulting")
{
    CHECK_THROWS_AS(Logger::levelFromString("verbose"), std::runtime_error);

    try
    {
        Logger::levelFromString("nonsense");
    }
    catch (const std::runtime_error& e)
    {
        CHECK(std::string(e.what()).find("nonsense") != std::string::npos);
    }
}
