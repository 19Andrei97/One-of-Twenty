#pragma once

#include "Jobs.h"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Angle.hpp>

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>

// Data-driven entity appearance: how each entity type and each profession (job)
// is drawn. Kept separate from the simulation tuning and free of EnTT, clocks
// and threads, so a look can be resolved and unit-tested on its own and a modder
// can recolor or reshape a profession from JSON without a rebuild.
//
// Everything is expressed with one sf::CircleShape: a "shape" is just a point
// count and a rotation (a square is a 4-gon turned 45 degrees, a triangle a
// 3-gon), so no per-shape geometry code is needed and MapRenderer-style call
// sites stay unchanged.
namespace Appearance
{
enum class Shape
{
    Circle = 0, // a smooth blob; `points` controls how round it is
    Square,
    Diamond,
    Triangle,

    Count
};

inline constexpr std::size_t kShapeCount = static_cast<std::size_t>(Shape::Count);

[[nodiscard]] inline std::string shapeName(const Shape shape)
{
    switch (shape)
    {
        case Shape::Circle:   return "circle";
        case Shape::Square:   return "square";
        case Shape::Diamond:  return "diamond";
        case Shape::Triangle: return "triangle";
        default:              return "?";
    }
}

// Parse a shape name (case-insensitive) as it appears in a config file. Returns
// nullopt for an unknown name so the loader can ignore it rather than throw.
[[nodiscard]] inline std::optional<Shape> shapeFromString(const std::string& value)
{
    std::string lower;
    lower.reserve(value.size());
    for (const char c : value)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower == "circle")   return Shape::Circle;
    if (lower == "square")   return Shape::Square;
    if (lower == "diamond")  return Shape::Diamond;
    if (lower == "triangle") return Shape::Triangle;
    return std::nullopt;
}

// A resolved look: shape, fill, outline and size, all in world units. The
// defaults reproduce the pre-JSON appearance (a white 10-unit circle), so an
// entity type or job with no configured entry keeps looking as it did.
struct Look
{
    Shape     shape{ Shape::Circle };
    sf::Color fill{ sf::Color::White };
    sf::Color outline{ 0, 0, 0, 0 }; // fully transparent by default
    float     radius{ 10.f };        // half-size; also the origin offset
    float     outline_thickness{ 0.f };
    unsigned  points{ 4 };           // circle segments (4 = the old polygon look)

    // Build the renderable shape for this look. The origin is the centre so the
    // circle can be positioned straight at an entity's world position.
    [[nodiscard]] sf::CircleShape makeShape() const
    {
        sf::CircleShape circle(radius, points);
        switch (shape)
        {
            case Shape::Circle:   circle.setPointCount(points); break;
            case Shape::Square:   circle.setPointCount(4); circle.setRotation(sf::degrees(45.f)); break;
            case Shape::Diamond:  circle.setPointCount(4); break;
            case Shape::Triangle: circle.setPointCount(3); break;
            default:              break;
        }
        circle.setFillColor(fill);
        circle.setOutlineColor(outline);
        circle.setOutlineThickness(outline_thickness);
        circle.setOrigin({ radius, radius });
        return circle;
    }
};

} // namespace Appearance
