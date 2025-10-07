#pragma once

#include "../map_generator/MapGenerator.h"

#include <SFML/Graphics.hpp>
#include <iostream>
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
    int thirst{ 100 };
    int hunger{ 100 };
    int sleep{ 0 };

    int last_update{ 0 };

    CBasicNeeds() {}
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

    std::optional<sf::Vector2i> getLocation(const Elements& type)
    {
        const auto& it = locations.find(type);

        if (it != locations.end())
            return it->second;
        else
            return std::nullopt;
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