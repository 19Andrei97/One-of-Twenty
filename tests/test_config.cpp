#include <doctest/doctest.h>

#include "Config.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace
{
    std::string writeTemp(const std::string& name, const std::string& content)
    {
        const std::string path = std::string("config_test_") + name;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << content;
        return path;
    }
}

TEST_CASE("valid JSON is parsed")
{
    const std::string path = writeTemp("valid.json", R"({"window":{"width":1280}})");
    const auto data = loadJsonFile(path);

    CHECK(data["window"]["width"].get<int>() == 1280);
    std::remove(path.c_str());
}

TEST_CASE("missing file throws with a helpful message")
{
    CHECK_THROWS_AS(loadJsonFile("definitely_not_here_12345.json"), std::runtime_error);

    try
    {
        loadJsonFile("definitely_not_here_12345.json");
    }
    catch (const std::runtime_error& e)
    {
        CHECK(std::string(e.what()).find("definitely_not_here_12345.json") != std::string::npos);
    }
}

TEST_CASE("malformed JSON throws instead of aborting")
{
    const std::string path = writeTemp("bad.json", R"({"window": )");
    CHECK_THROWS_AS(loadJsonFile(path), std::runtime_error);
    std::remove(path.c_str());
}
