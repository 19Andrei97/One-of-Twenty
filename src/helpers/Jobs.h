#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>

// Settlement roles. A job decides *which* resource an entity prefers to gather:
// a farmer values forage, a lumberjack wood, a miner ore, and a builder wood and
// stone. The enum plus its name<->string helpers live here (rather than in a
// component) so both the config loader and the entity systems can share one
// definition without pulling in SFML or EnTT.
namespace Jobs
{
enum class Job
{
    Idle = 0,
    Farmer,
    Lumberjack,
    Miner,
    Builder,

    Count
};

inline constexpr std::size_t kJobCount = static_cast<std::size_t>(Job::Count);

[[nodiscard]] inline constexpr std::size_t index(const Job job) noexcept
{
    return static_cast<std::size_t>(job);
}

[[nodiscard]] inline std::string name(const Job job)
{
    switch (job)
    {
        case Job::Idle:       return "Idle";
        case Job::Farmer:     return "Farmer";
        case Job::Lumberjack: return "Lumberjack";
        case Job::Miner:      return "Miner";
        case Job::Builder:    return "Builder";
        default:              return "?";
    }
}

// Parse a job name (case-insensitive) as it appears in a config file. Returns
// nullopt for an unknown name so the loader can ignore it rather than throw.
[[nodiscard]] inline std::optional<Job> fromString(const std::string& value)
{
    std::string lower;
    lower.reserve(value.size());
    for (const char c : value)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower == "farmer")     return Job::Farmer;
    if (lower == "lumberjack") return Job::Lumberjack;
    if (lower == "miner")      return Job::Miner;
    if (lower == "builder")    return Job::Builder;
    if (lower == "idle")       return Job::Idle;
    return std::nullopt;
}

// How many of each job the settlement wants to staff. Indexed by Job; entry 0
// (Idle) is always unused, kept so the array can be indexed directly.
using JobTargets = std::array<int, kJobCount>;

} // namespace Jobs
