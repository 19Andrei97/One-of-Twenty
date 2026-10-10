#pragma once

#include "Components_Entities.h"
#include "../helpers/Jobs.h"

#include <algorithm>
#include <array>
#include <optional>

// Weighted decision-making for entities.
//
// This header is deliberately free of SFML, EnTT, clocks and threads: given an
// entity's raw need counters and its personality, it answers "which action is
// most worth doing right now?" as a pure function. `EntityManager` owns the
// world state and turns that answer into concrete movement targets, so the
// policy itself can be unit-tested without constructing a map or a registry.
namespace EntityDecision
{

// Settings for the decision policy. Loaded from config/entity_data.json so the
// tuning is data-driven rather than baked into EntityManager.
struct Config
{
    struct Need
    {
        float threshold{ 0.2f }; // urgency at which the need starts driving actions
        float bias{ 1.f };       // >1 pursues sooner/longer, <1 tolerates it longer
    };

    // Sleep is the only survival need now: food is a settlement-level stock that
    // is consumed once a day, so hunger is not an entity decision.
    Need sleep{};

    // Society work is only considered once every survival need is satisfied, so
    // its threshold is compared against the *least* urgent survival need. A
    // higher threshold means an entity must be more comfortable before it works.
    Need work{ 0.5f, 1.f };

    // Consecutive idle decisions allowed before the entity wanders instead of
    // standing still, so a contented entity still explores.
    int idle_tolerance{ 3 };
};

// The need a decision was made for. `Work` means "gather a nearby resource for
// the settlement". `Explore` means "push back the frontier so the settlement
// learns more of the map" (the Explorer job). `Wander` means "nothing urgent":
// move to a random reachable target. `None` means "stay put" (idle tolerance not
// yet spent).
enum class Need
{
    None,
    Sleep,
    Work,
    Explore,
    Wander
};

namespace detail
{

[[nodiscard]] inline float clampf(const float value, const float lo, const float hi)
{
    return std::max(lo, std::min(value, hi));
}

} // namespace detail

// Normalised (0..1) urgency of a need given its raw counter. `sleep` is a
// fatigue counter (0 rested, 100 exhausted) and maps straight through. Urgency
// is 0 while the need is comfortable and grows to 1 at the extreme.
[[nodiscard]] inline float urgency(const int value, const bool inverted) noexcept
{
    const float centered = inverted ? (100.f - static_cast<float>(value))
                                    : static_cast<float>(value);
    return detail::clampf((centered - 50.f) / 50.f, 0.f, 1.f);
}

// How ready an entity is to act on a need, before personality: urgency scaled
// by the need's configured bias.
[[nodiscard]] inline float biasedUrgency(const float urgency, const float bias) noexcept
{
    return detail::clampf(urgency * bias, 0.f, 1.f);
}

// Personality maps 0..100 onto a 0.5x..1.5x multiplier: higher traits act
// sooner (sleep earlier), lower ones wait longer.
[[nodiscard]] inline float personalityFactor(const int trait) noexcept
{
    return detail::clampf(0.5f + static_cast<float>(trait) / 100.f, 0.5f, 1.5f);
}

[[nodiscard]] inline int traitValue(const CPersonality& personality, const PersonalityTrait trait) noexcept
{
    const auto it = personality.traits.find(static_cast<int>(trait));
    return (it != personality.traits.end()) ? it->second : 50;
}

// The personality-adjusted urgency of each need. `sleep` is the survival need;
// `work` is the society drive, derived from how rested the entity is.
struct Urgencies
{
    float sleep{ 0.f };
    float work{ 0.f };

    // How comfortable the entity is overall: 1 when rested, 0 when exhausted.
    // Work is driven by this, so an entity only turns to society work once it is
    // not struggling to stay awake.
    [[nodiscard]] float comfort() const noexcept
    {
        return std::clamp(1.f - sleep, 0.f, 1.f);
    }
};

[[nodiscard]] inline Urgencies computeUrgencies(const CBasicNeeds& needs,
                                                const CPersonality& personality,
                                                const Config& cfg) noexcept
{
    const auto weighted = [](const float urgency, const float bias, const int trait)
    {
        return biasedUrgency(urgency, bias) * personalityFactor(trait);
    };

    Urgencies u{};
    u.sleep = weighted(EntityDecision::urgency(needs.sleep, /* inverted */ false),
                       cfg.sleep.bias, traitValue(personality, PersonalityTrait::Calm));

    // Work urgency leans on how comfortable the entity is overall (comfort) and
    // on Loyalty as the society trait, so a tired entity keeps to survival.
    u.work = weighted(u.comfort(), cfg.work.bias,
                      traitValue(personality, PersonalityTrait::Loyal));
    return u;
}

// Map a need onto the action that satisfies it.
[[nodiscard]] constexpr ActionTypes actionFor(const Need need) noexcept
{
    switch (need)
    {
        case Need::Sleep:   return ActionTypes::Sleeping;
        case Need::Work:    return ActionTypes::Gathering;
        // Exploration and wandering are both "walk somewhere": they differ only in
        // how the destination is chosen.
        default:            return ActionTypes::Moving; // Explore / Wander / None
    }
}

// Pick the survival need at or above its threshold, or `None`.
[[nodiscard]] inline Need strongestNeed(const Urgencies& u, const Config& cfg) noexcept
{
    if (u.sleep >= cfg.sleep.threshold)
        return Need::Sleep;
    return Need::None;
}

// Decide what an entity should do. Survival comes first; if sleep is not
// pressing and the entity is rested enough to work, an explorer sets off to
// extend the settlement's knowledge while everyone else gathers. Otherwise it
// idles, and once `idleFrames` reaches the tolerance it wanders instead of
// standing still.
[[nodiscard]] inline Need decide(const CBasicNeeds& needs,
                                 const CPersonality& personality,
                                 const Config& cfg,
                                 const int idleFrames,
                                 const Jobs::Job job = Jobs::Job::Idle) noexcept
{
    const Urgencies u = computeUrgencies(needs, personality, cfg);

    const Need strongest = strongestNeed(u, cfg);
    if (strongest != Need::None)
        return strongest;

    if (u.work >= cfg.work.threshold)
        return (job == Jobs::Job::Explorer) ? Need::Explore : Need::Work;

    return (idleFrames >= cfg.idle_tolerance) ? Need::Wander : Need::None;
}

// The need an action is currently working to satisfy. `Moving` is unknown (it is
// shared by every errand, from a work trip to exploration), so it maps to
// `None`: a plain move carries no committed need.
[[nodiscard]] constexpr Need servedNeed(const ActionTypes action) noexcept
{
    switch (action)
    {
        case ActionTypes::Sleeping:  return Need::Sleep;
        case ActionTypes::Gathering: return Need::Work;
        case ActionTypes::Building:  return Need::Work;
        default:                     return Need::None; // Moving / Idle
    }
}

// Whether a busy entity should abandon what it is doing for a more critical
// need. Returns the need to switch to, or `nullopt` to keep the current plan.
//
// The rule is "survive first": if a survival need is at or above its threshold
// and is not the one the current action already serves, the entity should drop
// its plan and see to that need. This is what lets a long gather trip be
// interrupted before a need drains health, and it never thrashes on the need it
// is already serving. The caller must still avoid re-planning a need whose
// action is already queued.
[[nodiscard]] inline std::optional<Need> interruptFor(const CBasicNeeds& needs,
                                                      const CPersonality& personality,
                                                      const Config& cfg,
                                                      const ActionTypes current) noexcept
{
    const Urgencies u = computeUrgencies(needs, personality, cfg);
    const Need strongest = strongestNeed(u, cfg);
    if (strongest == Need::None)
        return std::nullopt;             // nothing urgent enough to preempt

    // Never abandon a need for the very need being satisfied.
    if (strongest == servedNeed(current))
        return std::nullopt;

    return strongest;
}

} // namespace EntityDecision
