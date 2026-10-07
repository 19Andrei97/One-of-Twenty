#pragma once

#include "../map_generator/MapGenerator.h"
#include "../helpers/Resources.h"

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <iostream>
#include <optional>
#include <unordered_map>


enum class PersonalityTrait
{
    Brave,
    Curious,
    Greedy,
    Loyal,
    Aggressive,
    Calm,
    Honest,
    Cunning,
    End
};

enum class EntityType 
{
    Human_Generic,
    Human_Farmer,
    Human_Lumberjack,
    Animal_Dog,
    Animal_Cat
};

enum class ActionTypes 
{
    Moving,
    Eating,
    Drinking,
    Sleeping,
    Gathering,
    Idle
};

struct CType 
{
    EntityType type;

    CType(const EntityType& t)
        : type(t)
    {}
};

struct CPersonality 
{
    std::map<int, int> traits;

    CPersonality() {
        for (int i = 0; i < static_cast<int>(PersonalityTrait::End); i++)
        {
            traits[i] = Random::get(0, 100);
        }
    }
};

struct CBasicNeeds
{
    static constexpr int kMax{ 100 };

    int thirst{ kMax };
    int hunger{ kMax };
    int sleep{ 0 };

    int last_update{ 0 };

    CBasicNeeds() {}

    // Advance the counters for a one-hour step: fullness falls, fatigue rises.
    // Values are clamped to [0, 100] so callers never have to re-clamp.
    void applyHourlyDecay(const int thirst_delta, const int hunger_delta, const int sleep_delta)
    {
        thirst = std::clamp(thirst - thirst_delta, 0, kMax);
        hunger = std::clamp(hunger - hunger_delta, 0, kMax);
        sleep  = std::clamp(sleep + sleep_delta, 0, kMax);
    }

    // Restore a need to full when its action finishes.
    void satisfy(const int which)  // 0 = thirst, 1 = hunger, 2 = sleep
    {
        if (which == 0) thirst = kMax;
        else if (which == 1) hunger = kMax;
        else sleep = 0;
    }
};

// What a remembered location is good for. Keeping the set small (rather than
// remembering every biome) is deliberate: memory exists to answer "where do I
// drink?" and "where do I work?".
enum class MemoryKind
{
    Water,
    Food
};

struct CMemory
{
    std::unordered_map<Elements, sf::Vector2i> locations;

    CMemory(){}

    void rememberLocation(const sf::Vector2i& pos, const Elements& type)
    {
        locations[type] = pos;
    }

    void rememberLocation(const std::unordered_map<Elements, sf::Vector2i>& map)
    {
        for(auto& [key, val] : map)
            locations[key] = val;
    }

    std::optional<sf::Vector2i> getLocation(const Elements& type) const
    {
        const auto it = locations.find(type);

        if (it != locations.end())
            return it->second;
        return std::nullopt;
    }

    // The elements that count as drinkable / workable, from the shared
    // classification so memory and gathering agree.
    static bool isWater(const Elements element) noexcept
    {
        return Resources::isWater(element);
    }

    static bool isFood(const Elements element) noexcept
    {
        return Resources::isGatherable(element);
    }

    // Closest remembered location of the given kind, if any. Iterates the
    // remembered set, so it is O(remembered).
    std::optional<sf::Vector2i> findNearest(const sf::Vector2i& from, const MemoryKind kind) const
    {
        std::optional<sf::Vector2i> best;
        float bestDist = 0.f;

        for (const auto& [element, pos] : locations)
        {
            const bool usable = (kind == MemoryKind::Water) ? isWater(element) : isFood(element);
            if (!usable)
                continue;

            const float dx = static_cast<float>(pos.x - from.x);
            const float dy = static_cast<float>(pos.y - from.y);
            const float dist = dx * dx + dy * dy;

            if (!best || dist < bestDist)
            {
                best = pos;
                bestDist = dist;
            }
        }
        return best;
    }
};

// What an entity gathers by working a tile. One unit per completed gather, so
// the settlement totals are a simple count of productive trips.
struct CInventory
{
    int carried{ 0 };

    CInventory() {}

    void gather(const int amount = 1) { carried += amount; }

    // Hand over everything carried, e.g. on depositing at the settlement.
    int deposit()
    {
        const int amount = carried;
        carried = 0;
        return amount;
    }
};


struct CTransform
{
    float           speed{ 0.f };
	sf::Vector2i    pos{ 0, 0 };
    sf::Vector2i    target{ 0, 0 };

	CTransform(const sf::Vector2i& p, const float v)
		: pos(p), speed(v)
    {}
};

struct CShape
{
	sf::CircleShape circle;

	CShape(float radius, int points, const sf::Color& fill)
		: circle(radius, points)
	{
		circle.setFillColor(fill);
		//circle.setOutlineColor(outline);
		//circle.setOutlineThickness(thickness);
		circle.setOrigin({ radius, radius });
	}
};

struct CCollision
{
	float radius;

	CCollision(float r)
		: radius(r) 
    {}
};

struct CVision
{
    float radius;

    CVision(float r = 250.f)
        : radius(r)
    {}
};

struct CLifespan
{
	int remaining;
	int total;

	CLifespan(int total)
		: remaining(total), total(total) 
    {}
};

struct CInput
{
    bool up{ false };
	bool down{ false };
	bool left{ false };
	bool right{ false };
	bool shoot{ false };

	CInput() {}
};


// ACTIONS

struct CAction
{
    ActionTypes action_name;

    CAction(ActionTypes action) : action_name(action) {}
    virtual ~CAction() = default;
};

struct CMoving : public CAction
{
    sf::Vector2i target;

    CMoving(ActionTypes type, const sf::Vector2i& tgt = { 0, 0 })
        : CAction(type), target(tgt) {
    }
};

struct CEating : public CAction
{
    std::int64_t timestamp_min{ 0 };
    int duration_min{ 45 };

    CEating(ActionTypes type, std::int64_t stamp)
        : CAction(type), timestamp_min(stamp) {
    }
};

struct CDrinking : public CAction
{
    std::int64_t timestamp_min{ 0 };
    int duration_min{ 45 };

    CDrinking(ActionTypes type, std::int64_t stamp)
        : CAction(type), timestamp_min(stamp) {
    }
};

struct CSleeping : public CAction
{
    std::int64_t timestamp_min{ 0 };
    int duration_min{ 500 };

    CSleeping(ActionTypes type, std::int64_t stamp)
        : CAction(type), timestamp_min(stamp) {
    }
};

struct CGather : public CAction
{
    sf::Vector2i tile{ 0, 0 };      // the tile being worked
    Elements element{ Elements::test }; // what it yields
    std::int64_t timestamp_min{ 0 };
    int duration_min{ 60 };

    CGather(ActionTypes type, const sf::Vector2i& t, const Elements e, std::int64_t stamp)
        : CAction(type), tile(t), element(e), timestamp_min(stamp) {
    }
};

struct CActionsQueue
{
    std::list<std::shared_ptr<CAction>> actions;
};

// HUD 
class CEntityInfo
{
public:
    sf::RectangleShape     shape;
    std::vector<sf::Text>  text;
    sf::Color              text_color;
    int                    size;

    CEntityInfo
    (
        float width,
        float height,
        const sf::Color& fill = { 0, 0, 0, 128 }, // black 50% transparent
        const sf::Color& p_text_color = sf::Color::White,
        unsigned int charSize = 12U
    )
        : shape({ width, height })
        , text_color(p_text_color)
        , size(charSize)
    {
        shape.setFillColor(fill);
    }
};