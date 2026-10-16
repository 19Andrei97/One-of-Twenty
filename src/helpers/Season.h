#pragma once

#include "GameClock.h"

#include <algorithm>
#include <cmath>

// The seasonal swing in temperature.
//
// A world's climate is not fixed: the same tile is warmer in summer and colder
// in winter, and the swing is strongest toward the poles while the equator stays
// warm year-round. This is a pure function of the in-game day and the biome's
// base (latitude-driven) temperature, so it can be unit-tested without a map or
// a clock and so the game and the tile readout agree on what "the temperature
// here" means.
namespace Season
{
// Peak-to-peak swing at the pole, in the same [0,1] climate space the terrain
// uses. The equator sees none of it; a mid-latitude tile sees roughly half.
inline constexpr float kPoleSwing{ 0.22f };

// The seasonal offset in [-1, 1] for a given day: +1 at the height of summer,
// -1 at the depth of winter. One full cycle per year. The phase is aligned to
// the calendar's seasons, so the peak falls at mid-summer (day 135, the middle
// of months 4-6) and the trough at mid-winter (day 315, the middle of months
// 10-12); the start of spring (day 0) is then on the cold side of the curve,
// warming toward summer.
[[nodiscard]] inline float offsetForDay(const std::int64_t day) noexcept
{
        constexpr float kSummerPeakDay{ 135.0f }; // mid-summer
        const float phase = static_cast<float>(day % GameTime::kDaysPerYear)
                          / static_cast<float>(GameTime::kDaysPerYear);
        const float peak = kSummerPeakDay / static_cast<float>(GameTime::kDaysPerYear);
        return std::cos((phase - peak) * 2.0f * 3.14159265358979323846f);
}

// Apply the seasonal swing to a base temperature, scaled by latitude: the poles
// (cold base) swing fully, the equator (warm base) barely at all. The result is
// clamped to the same [0,1] range the climate uses.
[[nodiscard]] inline float apply(const float base_temperature, const std::int64_t day) noexcept
{
        // Warm ground (the equator) barely swings; cold ground (the poles) swings
        // fully. 1 - base is the latitude proxy: it is near 0 at the equator.
        const float latitude = std::clamp(1.0f - base_temperature, 0.0f, 1.0f);
        const float swing = kPoleSwing * latitude * offsetForDay(day);
        return std::clamp(base_temperature + swing, 0.0f, 1.0f);
}

// The [0,1] climate value as a display temperature in degrees Celsius. The map
// is calibrated so 0.5 is a pleasant 15 C and the full range spans about -15 to
// 45 C, which reads plausibly next to the biome palette.
[[nodiscard]] inline int celsius(const float climate) noexcept
{
        return static_cast<int>(std::lround(-15.0f + climate * 60.0f));
}
} // namespace Season
