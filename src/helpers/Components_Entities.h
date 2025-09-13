#pragma once

#include "../mapgen/MapGenerator.h"

#include <SFML/Graphics.hpp>
#include <iostream>
#include <unordered_map>

enum class PersonalityTrait {
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

enum class EntityType {
    Human_Generic,
    Human_Farmer,
    Human_Lumberjack,
    Animal_Dog,
    Animal_Cat
};

struct CType {
    EntityType type;

    CType(const EntityType& t)
        : type(t)
    {}
};

struct CPersonality {
    std::map<int, int> traits;

    CPersonality() {
        for (int i = 0; i < static_cast<int>(PersonalityTrait::End); i++)
        {
            traits[i] = Random::get(0, 100);
        }
    }
};

struct CMemory {
    std::unordered_map<Elements, sf::Vector2f> locations;

    CMemory(){}

    void rememberLocation(const sf::Vector2f& pos, const Elements& type)
    {
        locations[type] = pos;
    }

    void rememberLocation(const std::unordered_map<Elements, sf::Vector2f>& map)
    {
        for(auto& [key, val] : map)
            locations[key] = val;
    }

    sf::Vector2f getLocation(const Elements& type)
    {
        const auto& it = locations.find(type);

        if (it != locations.end())
            return it->second;
        else
            return sf::Vector2f{ 0.f, 0.f };
    }
};

struct CTransform
{
    bool            has_target{ false };
    float           speed{ 0.f };
	sf::Vector2f    pos{ 0.f, 0.f };
    sf::Vector2f    target{ 0.f, 0.f };

	CTransform(const sf::Vector2f& p, const float v)
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
