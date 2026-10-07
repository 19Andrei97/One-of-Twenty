#pragma once

#include "Components_Entities.h"

#include <algorithm>
#include <array>

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

    Need thirst{};
    Need hunger{};
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
// the settlement". `Wander` means "nothing urgent": move to a random reachable
// target. `None` means "stay put" (idle tolerance not yet spent).
enum class Need
{
    None,
    Thirst,
    Hunger,
    Sleep,
    Work,
    Wander
};

namespace detail
{

[[nodiscard]] inline float clampf(const float value, const float lo, const float hi)
{
    return std::max(lo, std::min(value, hi));
}

} // namespace detail

// Normalised (0..1) urgency of a need given its raw counter. `hunger`/`thirst`
// are fullness counters (100 comfortable, 0 dire) so they invert; `sleep` is a
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
// sooner (drink/eat/sleep earlier), lower ones wait longer.
[[nodiscard]] inline float personalityFactor(const int trait) noexcept
{
    return detail::clampf(0.5f + static_cast<float>(trait) / 100.f, 0.5f, 1.5f);
}

[[nodiscard]] inline int traitValue(const CPersonality& personality, const PersonalityTrait trait) noexcept
{
    const auto it = personality.traits.find(static_cast<int>(trait));
    return (it != personality.traits.end()) ? it->second : 50;
}

// The personality-adjusted urgency of each need. `thirst`/`hunger`/`sleep` are
// the survival needs; `work` is the society drive, derived from how comfortable
// the entity is overall. Exposed for tests and the HUD.
struct Urgencies
{
    float thirst{ 0.f };
    float hunger{ 0.f };
    float sleep{ 0.f };
    float work{ 0.f };

    [[nodiscard]] float byIndex(const std::size_t index) const noexcept
    {
        switch (index)
        {
            case 0: return thirst;
            case 1: return hunger;
            default: return sleep;
        }
    }

    // How comfortable the entity is overall: 1 when every need is satisfied, 0
    // when any need is critical. Work is driven by this, so an entity only turns
    // to society work once it is not struggling to survive.
    [[nodiscard]] float comfort() const noexcept
    {
        const float worst = std::max({ thirst, hunger, sleep });
        return std::clamp(1.f - worst, 0.f, 1.f);
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

    Urgencies u{
        weighted(EntityDecision::urgency(needs.thirst, /* inverted */ true),
                 cfg.thirst.bias, traitValue(personality, PersonalityTrait::Brave)),
        weighted(EntityDecision::urgency(needs.hunger, /* inverted */ true),
                 cfg.hunger.bias, traitValue(personality, PersonalityTrait::Greedy)),
        weighted(EntityDecision::urgency(needs.sleep, /* inverted */ false),
                 cfg.sleep.bias, traitValue(personality, PersonalityTrait::Calm)),
        0.f,
    };

    // Work urgency leans on how comfortable the entity is overall (comfort) and
    // on Loyalty as the society trait, so a stressed entity keeps to survival.
    u.work = weighted(u.comfort(), cfg.work.bias,
                      traitValue(personality, PersonalityTrait::Loyal));
    return u;
}

// Map a need onto the action that satisfies it.
[[nodiscard]] constexpr ActionTypes actionFor(const Need need) noexcept
{
    switch (need)
    {
        case Need::Thirst: return ActionTypes::Drinking;
        case Need::Hunger: return ActionTypes::Eating;
        case Need::Sleep:  return ActionTypes::Sleeping;
        case Need::Work:   return ActionTypes::Gathering;
        default:           return ActionTypes::Moving; // Wander / None
    }
}

// Pick the most urgent survival need at or above its threshold, or `None`. Ties
// resolve in the fixed thirst > hunger > sleep order so the result is
// deterministic. Work is handled separately in `decide` because it is only
// eligible once no survival need is pressing.
[[nodiscard]] inline Need strongestNeed(const Urgencies& u, const Config& cfg) noexcept
{
    const std::array<float, 3> values{ u.thirst, u.hunger, u.sleep };
    const std::array<float, 3> thresholds{ cfg.thirst.threshold, cfg.hunger.threshold, cfg.sleep.threshold };
    const std::array<Need, 3> needs{ Need::Thirst, Need::Hunger, Need::Sleep };

    Need best = Need::None;
    float bestValue = 0.f;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (values[i] >= thresholds[i] && values[i] > bestValue)
        {
            bestValue = values[i];
            best = needs[i];
        }
    }
    return best;
}

// Decide what an entity should do. Survival needs come first; if none is
// pressing and the entity is comfortable enough to work, it gathers for the
// settlement. Otherwise it idles, and once `idleFrames` reaches the tolerance it
// wanders instead of standing still.
[[nodiscard]] inline Need decide(const CBasicNeeds& needs,
                                 const CPersonality& personality,
                                 const Config& cfg,
                                 const int idleFrames) noexcept
{
    const Urgencies u = computeUrgencies(needs, personality, cfg);

    const Need strongest = strongestNeed(u, cfg);
    if (strongest != Need::None)
        return strongest;

    if (u.work >= cfg.work.threshold)
        return Need::Work;

    return (idleFrames >= cfg.idle_tolerance) ? Need::Wander : Need::None;
}

} // namespace EntityDecision
