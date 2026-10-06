#pragma once

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

// Loads and parses a JSON file, throwing std::runtime_error with a clear
// message when the file is missing or malformed, instead of letting an
// unhandled parse exception escape from a constructor.
inline nlohmann::json loadJsonFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Could not open config file: " + path);

    try
    {
        return nlohmann::json::parse(file);
    }
    catch (const nlohmann::json::exception& e)
    {
        throw std::runtime_error("Failed to parse config file '" + path + "': " + e.what());
    }
}
