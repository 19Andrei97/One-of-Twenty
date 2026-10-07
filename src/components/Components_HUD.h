#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// HUD COMPONENTS
//
// Each widget owns its shapes and the behaviour that used to live in Hud:
// placement relative to the view, hit testing, drawing and input handling.
// A widget stores its position as an offset from the top-left of the HUD view,
// so the same widget can be laid out at different view sizes.

namespace hud_detail
{
// Centre a line of text inside a shape's bounds.
inline sf::Vector2f centeredTextPosition(const sf::FloatRect& box, const sf::FloatRect& text)
{
    return {
        box.position.x + box.size.x / 2.f - text.size.x / 2.f - text.position.x,
        box.position.y + box.size.y / 2.f - text.size.y / 2.f - text.position.y
    };
}
} // namespace hud_detail

class CButton
{
    sf::RectangleShape        m_rect;
    sf::Text m_text;
    sf::Vector2f              m_offset;
    std::function<void()>     m_onClick;

public:
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
        : m_rect({ width, height })
        , m_offset(pos)
        , m_text(font)
    {
        m_rect.setPosition(pos);
        m_rect.setFillColor(fill);
        m_rect.setOutlineColor(outline);
        m_rect.setOutlineThickness(thickness);

        m_text.setString(label);
        m_text.setCharacterSize(charSize);
        m_text.setFillColor(sf::Color::Black);
    }

    void setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }

    void activate() { if (m_onClick) m_onClick(); }

    bool contains(const sf::Vector2f& point, const sf::Vector2f& viewOrigin) const
    {
        return sf::FloatRect(m_offset, m_rect.getSize()).contains(point - viewOrigin);
    }

    void draw(sf::RenderTarget& target, const sf::Vector2f& viewOrigin)
    {
        m_rect.setPosition(viewOrigin + m_offset);
        m_text.setPosition(hud_detail::centeredTextPosition(m_rect.getGlobalBounds(), m_text.getLocalBounds()));

        target.draw(m_rect);
        target.draw(m_text);
    }
};

class CInputBox
{
    sf::RectangleShape         m_rect;
    sf::Text  m_text;
    std::string                m_placeholder;
    sf::Vector2f               m_offset;
    std::function<void(float)> m_onEnter;

    std::string m_inputString;
    bool        m_active = false;

public:
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
        : m_rect({ width, height }), m_offset(pos_v), m_placeholder(placeholder_v), m_text(font)
    {
        m_rect.setPosition(pos_v);
        m_rect.setFillColor(fill);
        m_rect.setOutlineColor(outline);
        m_rect.setOutlineThickness(thickness);

        m_text.setString(m_placeholder);
        m_text.setCharacterSize(charSize);
        m_text.setFillColor(sf::Color::Black);
    }

    void setOnEnter(std::function<void(float)> callback) { m_onEnter = std::move(callback); }

    bool isActive() const { return m_active; }

    // Focus the box and clear any previous entry.
    void activate()
    {
        m_active = true;
        m_inputString.clear();
        m_text.setString("");
    }

    bool contains(const sf::Vector2f& point, const sf::Vector2f& viewOrigin) const
    {
        return sf::FloatRect(m_offset, m_rect.getSize()).contains(point - viewOrigin);
    }

    void handleText(const sf::Event::TextEntered& event)
    {
        if (!m_active) return;

        if (event.unicode == 8) // Backspace
        {
            if (!m_inputString.empty())
            {
                m_inputString.pop_back();
                m_text.setString(m_inputString.empty() ? m_placeholder : m_inputString);
            }
        }
        else if (event.unicode == 13) // Enter
        {
            if (!m_inputString.empty())
            {
                if (m_onEnter) m_onEnter(std::stof(m_inputString));
                m_inputString.clear();
                m_text.setString(m_placeholder);
                m_active = false; // unfocus after enter
            }
        }
        else if ((event.unicode >= '0' && event.unicode <= '9') || event.unicode == '.') // Numbers and dot
        {
            m_inputString += static_cast<char>(event.unicode);
            m_text.setString(m_inputString);
        }
    }

    void draw(sf::RenderTarget& target, const sf::Vector2f& viewOrigin)
    {
        m_rect.setPosition(viewOrigin + m_offset);
        m_text.setPosition(hud_detail::centeredTextPosition(m_rect.getGlobalBounds(), m_text.getLocalBounds()));

        target.draw(m_rect);
        target.draw(m_text);
    }
};

class CSlider
{
    sf::RectangleShape        m_bar;
    sf::CircleShape           m_handle;
    sf::Text m_text;
    sf::Vector2f              m_offset;

    float m_minValue;
    float m_maxValue;
    float m_value;
    bool  m_active = false;

    std::function<void(float)> m_onChange;

public:
    CSlider(
        float width,
        float height,
        const sf::Vector2f& pos_v,
        float minVal,
        float maxVal,
        const sf::Font& font,
        const std::string& text_p = "Default",
        const sf::Color& barColor = sf::Color::Black,
        const sf::Color& handleColor = sf::Color::White
    )
        : m_offset(pos_v), m_minValue(minVal), m_maxValue(maxVal), m_value(maxVal / 2), m_text(font)
    {
        m_text.setString(text_p);
        m_text.setCharacterSize(16u);
        m_text.setFillColor(sf::Color::Black);

        m_bar.setSize({ width, height });
        m_bar.setFillColor(barColor);
        m_bar.setPosition(pos_v);

        m_handle.setRadius(height);
        m_handle.setFillColor(handleColor);
        m_handle.setOrigin({ height, height });
        m_handle.setPosition(pos_v);
    }

    void setOnChange(std::function<void(float)> callback) { m_onChange = std::move(callback); }

    float getValue() const { return m_value; }
    bool  isActive() const { return m_active; }

    void endDrag() { m_active = false; }

    bool contains(const sf::Vector2f& point, const sf::Vector2f& viewOrigin) const
    {
        const sf::Vector2f local = point - viewOrigin;
        const float r = m_handle.getRadius();
        const sf::FloatRect hitBox({ m_offset.x - r, m_offset.y - r }, { m_bar.getSize().x + 2 * r, m_bar.getSize().y + 2 * r });
        return hitBox.contains(local);
    }

    void beginDrag() { m_active = true; }

    // Update the value from an absolute mouse position while dragging.
    void dragTo(const sf::Vector2f& point)
    {
        if (!m_active) return;

        const float left = m_bar.getPosition().x;
        const float right = left + m_bar.getSize().x;
        const float clampedX = std::max(left, std::min(point.x, right));

        const float ratio = (clampedX - left) / m_bar.getSize().x;
        m_value = m_minValue + ratio * (m_maxValue - m_minValue);

        if (m_onChange) m_onChange(m_value);
    }

    void draw(sf::RenderTarget& target, const sf::Vector2f& viewOrigin)
    {
        const sf::Vector2f screenPos = viewOrigin + m_offset;
        m_bar.setPosition(screenPos);
        m_text.setPosition({ screenPos.x, screenPos.y - m_offset.x });

        const float ratio = (m_value - m_minValue) / (m_maxValue - m_minValue);
        const float x = m_bar.getPosition().x + ratio * m_bar.getSize().x;
        const float y = m_bar.getPosition().y + m_bar.getSize().y / 2.f;
        m_handle.setPosition({ x, y });

        target.draw(m_bar);
        target.draw(m_handle);
        target.draw(m_text);
    }
};

class CInfoBox
{
    sf::RectangleShape                     m_rect;
    std::vector<sf::Text> m_text;
    sf::Vector2f                           m_pos;

public:
    CInfoBox(
        float width,
        float height,
        const sf::Font& font,
        const std::vector<std::string>& text,
        sf::View& view,
        const sf::Color& fill = { 0, 0, 0, 128 }, // black 50% transparent
        const sf::Color& text_color = sf::Color::White,
        unsigned int charSize = 16U
    )
        : m_rect({ width, height })
        , m_pos({ 0, view.getSize().y - height })
    {
        m_rect.setPosition(m_pos);
        m_rect.setFillColor(fill);

        for (auto& el : text)
        {
            m_text.emplace_back(font);
            m_text.back().setString(el);
            m_text.back().setCharacterSize(charSize);
            m_text.back().setFillColor(text_color);
        }
    }

    bool empty() const { return m_text.empty(); }

    void draw(sf::RenderTarget& target)
    {
        if (m_text.empty())
            return;

        target.draw(m_rect);

        int text_space{ 0 };
        for (auto& text : m_text)
        {
            text.setPosition({ m_pos.x + 20, m_pos.y + 20 + text_space });
            target.draw(text);

            text_space += 20;
        }
    }
};
