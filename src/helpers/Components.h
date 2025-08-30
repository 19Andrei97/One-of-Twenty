#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <iostream>

class CTransform
{
public:
	sf::Vector2f pos = { 0.f, 0.f };
    sf::Vector2f velocity = { 0.f, 0.f };
	float angle = 0;

	CTransform(const sf::Vector2f& p, const sf::Vector2f& v, float a)
		: pos(p), velocity(v), angle(a) {
	}
};

class CShape
{
public:
	sf::CircleShape circle;

	CShape(float radius, int points, const sf::Color& fill, const sf::Color& outline, float thickness)
		: circle(radius, points)
	{
		circle.setFillColor(fill);
		circle.setOutlineColor(outline);
		circle.setOutlineThickness(thickness);
		circle.setOrigin({ radius, radius });
	}
};

class CCollision
{
public:
	float radius = 0.0f;
	CCollision(float r)
		: radius(r) {
	}
};

class CScore
{
public:
	int score = 0;
	CScore(int initialScore = 0)
		: score(initialScore) {
	}
};

class CLifespan
{
public:
	int remaining = 0;
	int total = 0;
	CLifespan(int total)
		: remaining(total), total(total) {
	}
};

class CInput
{
public:
	bool up = false;
	bool down = false;
	bool left = false;
	bool right = false;
	bool shoot = false;

	CInput() {}
};


// HUD COMPONENTS
class CButton
{
public:
    sf::RectangleShape              rect;
    std::unique_ptr<sf::Text>       text;
    sf::Vector2f                    m_pos;
    std::function<void()>           onClick;

    CButton(
        float width, 
        float height,
        const sf::Vector2f& pos,
        const sf::Font& font,
        const std::string& label = "Default",
        const sf::Color& fill = sf::Color::White,
        const sf::Color& outline = sf::Color::Black,
        float thickness = 2.f,
        unsigned int charSize = 16
    )
        : rect({ width, height })
        , m_pos(pos)
    {
        rect.setPosition(pos);
        rect.setFillColor(fill);
        rect.setOutlineColor(outline);
        rect.setOutlineThickness(thickness);

        text = std::make_unique<sf::Text>(font);
        text->setString(label);
        text->setCharacterSize(charSize);
        text->setFillColor(sf::Color::Black);
    }
};

class CInputBox
{
public:
    sf::RectangleShape              rect;
    std::unique_ptr<sf::Text>       text;
    std::string                     placeholder;
    sf::Vector2f                    pos;
    std::function<void(float)>      onEnter; 

    std::string inputString;
    bool active = false;

    CInputBox(
        float width, 
        float height,
        const sf::Vector2f& pos_v,
        const sf::Font& font,
        const std::string& placeholder_v = "Enter number...",
        const sf::Color& fill = sf::Color::White,
        const sf::Color& outline = sf::Color::Black,
        float thickness = 2.f,
        unsigned int charSize = 16
    )
        : rect({ width, height }), pos(pos_v), placeholder(placeholder_v)
    {
        rect.setPosition(pos);
        rect.setFillColor(fill);
        rect.setOutlineColor(outline);
        rect.setOutlineThickness(thickness);

        text = std::make_unique<sf::Text>(font);
        text->setString(placeholder);
        text->setCharacterSize(charSize);
        text->setFillColor(sf::Color::Black);
    }
};

class CSlider
{
public:
    sf::RectangleShape          bar;
    sf::CircleShape             handle;
    std::unique_ptr<sf::Text>   text;
    sf::Vector2f                pos;
        
    float   minValue;
    float   maxValue;
    float   value;
    bool    active = false;

    std::function<void(float)> onChange;

    CSlider(
        float width,
        float height,
        const sf::Vector2f& pos_v,
        float minVal,
        float maxVal,
        const sf::Font&     font,
        const std::string&  text_p = "Default",
        const sf::Color&    barColor = sf::Color::Black,
        const sf::Color&    handleColor = sf::Color::White
     )
        : pos(pos_v), minValue(minVal), maxValue(maxVal), value(maxVal / 2)
    {
        text = std::make_unique<sf::Text>(font);
        text->setString(text_p);
        text->setCharacterSize(16u);
        text->setFillColor(sf::Color::Black);

        bar.setSize({ width, height });
        bar.setPosition(pos);
        bar.setFillColor(barColor);

        handle.setRadius(height);
        handle.setFillColor(handleColor);
        handle.setOrigin({ height, height });
    }
};
