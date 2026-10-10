// Headless simulation CLI.
//
// Runs One Of Twenty with no window and no GL, so it can run on a server or in
// CI. It is a front end over HeadlessSimulation (the same clock/map/entity code
// the game runs); use it to watch a settlement unfold, benchmark a config, or
// reproduce a run headlessly.
//
// Examples:
//   OneOfTwentySim                              # config.json, 1 in-game year
//   OneOfTwentySim --days 360 --population 8 --seed 42
//   OneOfTwentySim --days 720 --speed 360       # 6 in-game hours per real second
//
// Clock speed changes outcomes: at very high speeds a frame advances many
// in-game minutes, so entity decisions and construction cadence are sampled more
// coarsely and an otherwise-healthy settlement can starve. The default (6 h/s,
// a shipped preset) keeps a settlement thriving over long runs.
//
// Exit code is 0 on a completed run.
#include "HeadlessSimulation.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

[[noreturn]] void usage(const char* argv0, int code = 0)
{
    std::cout <<
        "Usage: " << argv0 << " [options]\n"
        "  --config <path>     game config.json (default config/config.json)\n"
        "  --days <n>          in-game days to simulate (default 360)\n"
        "  --population <n>    override the founding population\n"
        "  --rng-seed <n>      seed the RNG for a repeatable run\n"
        "  --seed <n>          terrain seed (default from the map config)\n"
        "  --speed <min/sec>   in-game minutes per real second (default 360 = 6 h/s)\n"
        "  --start-hour <h>    time of day to start at (default from config)\n"
        "  --stream-px <n>     half-extent of the streamed area in px (default 1024)\n"
        "  --report-days <n>   print a summary every n in-game days (default 30, 0=off)\n"
        "  --help              show this help\n";
    std::exit(code);
}

// Parse an integer, exiting with a clear message on a malformed value.
long parseInt(const char* value, const char* flag)
{
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0')
    {
        std::cerr << "Invalid value for " << flag << ": " << value << "\n";
        std::exit(2);
    }
    return parsed;
}

} // namespace

int main(int argc, char** argv)
{
    std::string config_path = "config/config.json";
    double days = 360.0;
    long population = -1;
    long seed = -1;
    double speed = 360.0;   // 6 in-game hours per real second: fast and stable
    long start_hour = -1;
    long stream_px = -1;
    long report_days = -1;
    long rng_seed = -1;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { std::cerr << "Missing value for " << flag << "\n"; std::exit(2); }
            return argv[++i];
        };

        if      (arg == "--help" || arg == "-h")  usage(argv[0]);
        else if (arg == "--config")       config_path = next("--config");
        else if (arg == "--days")         days = std::atof(next("--days"));
        else if (arg == "--population")   population = parseInt(next("--population"), "--population");
        else if (arg == "--seed")         seed = parseInt(next("--seed"), "--seed");
        else if (arg == "--speed")        speed = std::atof(next("--speed"));
        else if (arg == "--start-hour")   start_hour = parseInt(next("--start-hour"), "--start-hour");
        else if (arg == "--stream-px")    stream_px = parseInt(next("--stream-px"), "--stream-px");
        else if (arg == "--report-days")  report_days = parseInt(next("--report-days"), "--report-days");
        else if (arg == "--rng-seed")     rng_seed = parseInt(next("--rng-seed"), "--rng-seed");
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            usage(argv[0], 2);
        }
    }

    try
    {
        HeadlessSimulation sim(config_path);

        if (seed >= 0)
            sim.setSeed(static_cast<int>(seed));
        sim.setTimeScale(static_cast<float>(speed));
        if (start_hour >= 0)
            sim.setStartHour(static_cast<int>(start_hour));
        if (stream_px > 0)
            sim.setStreamHalfExtent(static_cast<int>(stream_px));
        if (report_days >= 0)
            sim.setReportIntervalDays(static_cast<int>(report_days));
        // Seed the RNG before the population, because seeding the founders draws
        // positions from it; otherwise the spawn varies run to run.
        if (rng_seed >= 0)
            sim.setRandomSeed(static_cast<unsigned>(rng_seed));
        if (population >= 0)
            sim.setPopulation(static_cast<int>(population));

        std::cout << "One Of Twenty - headless simulation (seed " << sim.map().getSeed()
                  << ", " << sim.clock().getTimeScale() << " min/s)\n";

        sim.runDays(days);

        std::cout << "Final: " << sim.summaryLine() << "\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Simulation failed: " << e.what() << "\n";
        return 1;
    }
}
