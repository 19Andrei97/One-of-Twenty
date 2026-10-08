#pragma once

#include "../map_generator/MapGenerator.h"
#include "../helpers/Resources.h"
#include "../helpers/Jobs.h"
#include "../helpers/Economy.h"

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
    Human_Explorer,
    Animal_Dog,
    Animal_Cat,

    Count   // number of entity types; not a real type
};

inline constexpr std::size_t kEntityTypeCount = static_cast<std::size_t>(EntityType::Count);

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

// The settlement role an entity works. Set at spawn from the entity type's
// configured job default, and reassigned by the shortage rule so the settlement
// keeps the roles it needs staffed.
struct CJob
{
    Jobs::Job job{ Jobs::Job::Idle };

    CJob() = default;
    explicit CJob(const Jobs::Job j) : job(j) {}
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

    std::int64_t last_update{ 0 };

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

    // Whether any need is still comfortable. Survival needs decay at a fixed rate
    // per in-game hour; when a single frame advances several hours at once (a slow
    // frame, a high time scale) an entity can run from comfortable to empty before
    // it gets a chance to act. Death is only allowed once a need has been seen
    // low, so such a jump cannot kill an otherwise healthy entity.
    [[nodiscard]] bool healthy() const noexcept
    {
        return thirst > kMax / 4 && hunger > kMax / 4 && sleep < kMax * 3 / 4;
    }
};

// A slow resource separate from the needs: starvation and dehydration drain it,
// a comfortable entity slowly regains it, and the entity dies when it reaches
// zero. Keeping death on health rather than on a need crossing zero means an
// entity weakens and can recover instead of dropping dead the instant it runs
// out of water, which also makes coarse clock steps survivable.
struct CHealth
{
    static constexpr int kMax{ 100 };

    int value{ kMax };

    [[nodiscard]] bool isAlive() const noexcept { return value > 0; }

    void damage(const int amount) { value = std::clamp(value - amount, 0, kMax); }
    void heal(const int amount) { value = std::clamp(value + amount, 0, kMax); }
};

// Per-entity reproduction timer. Decrementing this every in-game hour and
// resetting it on a birth is what keeps growth proportional to the number of
// comfortable adults rather than to one settlement-wide cooldown, and it stops a
// newborn from immediately reproducing.
struct CReproduction
{
    int cooldown_hours{ 0 };
};

// What a remembered location is good for. The settlement's knowledge lives in
// CivKnowledge (helpers/Knowledge.h); an entity only tracks the last tile it
// scanned so a stationary entity does not rescan every frame.
struct CKnowledgeScan
{
    // The tile the entity last observed. Its vision is the expensive part of the
    // update, so refresh on entering a new tile rather than every frame.
    std::optional<sf::Vector2i> last_scan_tile;
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

	CInput() {}
};


// ACTIONS

struct CAction
{
    ActionTypes action_name;

    CAction(ActionTypes action) : action_name(action) {}
    virtual ~CAction() = default;
};

inline bool operator==(const CAction& action, const ActionTypes type) noexcept
{
    return action.action_name == type;
}

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

// The route an entity is following, in world (pixel) positions: the corners of
// the tiles returned by the pathfinder, minus the tile it already stands on.
// The front element is the current waypoint. Empty means "no route": movement
// then falls back to a straight line to the action's target.
struct CPath
{
    std::vector<sf::Vector2i> waypoints;

    CPath() = default;

    bool empty() const noexcept { return waypoints.empty(); }
    sf::Vector2i current() const { return waypoints.front(); }
    void advance() { waypoints.erase(waypoints.begin()); }
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