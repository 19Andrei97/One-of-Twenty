#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
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
    std::string               m_label;
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
        , m_label(label)
        , m_text(font)
    {
        m_rect.setPosition(pos);
        m_rect.setFillColor(fill);
        m_rect.setOutlineColor(outline);
        m_rect.setOutlineThickness(thickness);

        m_text.setString(m_label);
        m_text.setCharacterSize(charSize);
        m_text.setFillColor(sf::Color::Black);
    }

    void setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }

    // Buttons that reflect state (e.g. Pause/Play) can relabel themselves.
    void setLabel(const std::string& label) { m_label = label; m_text.setString(m_label); }
    const std::string& getLabel() const { return m_label; }

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
    // Live numeric readout (e.g. "62%") drawn at the right end of the bar so the
    // exact value is visible while dragging, not just the handle position.
    sf::Text m_valueText;
    // One-line explanation drawn under the bar, so a player can tell what the
    // slider actually does without reading the config. Empty hides it.
    sf::Text m_description;
    sf::Vector2f              m_offset;

    float m_minValue;
    float m_maxValue;
    float m_value;
    bool  m_active = false;
    // Optional suffix appended to the live value readout ("%", "", " units").
    // Empty picks a sensible default: a percentage for a [0,1]-ish range, a plain
    // number otherwise. Set through setValueSuffix when a slider has real units.
    std::string m_valueSuffix;

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
        const sf::Color& handleColor = sf::Color::White,
        const std::string& description_p = "",
        float initialValue = std::numeric_limits<float>::quiet_NaN()
    )
        : m_offset(pos_v), m_minValue(minVal), m_maxValue(maxVal),
          m_value(std::isnan(initialValue) ? (minVal + maxVal) / 2.f : initialValue),
          m_text(font), m_valueText(font), m_description(font)
    {
        m_text.setString(text_p);
        m_text.setCharacterSize(16u);
        m_text.setFillColor(sf::Color::Black);

        m_valueText.setCharacterSize(14u);
        m_valueText.setFillColor(sf::Color(60, 60, 60));

        m_description.setString(description_p);
        m_description.setCharacterSize(12u);
        m_description.setFillColor(sf::Color(80, 80, 80));

        m_bar.setSize({ width, height });
        m_bar.setFillColor(barColor);
        m_bar.setPosition(pos_v);

        m_handle.setRadius(height);
        m_handle.setFillColor(handleColor);
        m_handle.setOrigin({ height, height });
        m_handle.setPosition(pos_v);
    }

    void setOnChange(std::function<void(float)> callback) { m_onChange = std::move(callback); }

    // Suffix appended to the live value readout. Leave empty to let formatValue
    // choose: a percent when the range spans roughly [0,1], a plain number else.
    void setValueSuffix(const std::string& suffix) { m_valueSuffix = suffix; }

    // The value the readout shows: a percentage for a unit-interval range so a
    // player reads "62%" instead of "0.62", a compact decimal otherwise.
    std::string formatValue() const
    {
        char buffer[32];
        // A true [0,1] control (a fraction the player thinks of as a level) reads
        // as a percentage; anything with real units stays a decimal.
        const bool unitRange = m_minValue <= 0.001f && m_maxValue >= 0.999f && m_maxValue <= 1.001f;
        if (unitRange)
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(m_value * 100.f));
        else if (m_maxValue <= 0.05f)
            std::snprintf(buffer, sizeof(buffer), "%.4f", static_cast<double>(m_value));
        else if (m_maxValue <= 2.f)
            std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(m_value));
        else
            std::snprintf(buffer, sizeof(buffer), "%.0f", static_cast<double>(m_value));
        return std::string(buffer) + m_valueSuffix;
    }

    float getValue() const { return m_value; }
    bool  isActive() const { return m_active; }

    // Drive the slider from outside (a preset button): move the handle and fire
    // the change callback as if the player had dragged it.
    void setValue(float value)
    {
        m_value = std::clamp(value, m_minValue, m_maxValue);
        if (m_onChange) m_onChange(m_value);
    }

    void endDrag() { m_active = false; }

    bool contains(const sf::Vector2f& point, const sf::Vector2f& viewOrigin) const
    {
        const sf::Vector2f local = point - viewOrigin;
        const float r = m_handle.getRadius();
        sf::FloatRect hitBox({ m_offset.x - r, m_offset.y - r }, { m_bar.getSize().x + 2 * r, m_bar.getSize().y + 2 * r });
        // Include the description line so hovering/clicking it still grabs the slider.
        hitBox.size.y += 16.f;
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

        m_description.setPosition({ screenPos.x, screenPos.y + 14.f });

        // Value readout at the right end of the bar, vertically centred on it, so
        // the exact number is visible without covering the label on the left.
        m_valueText.setString(formatValue());
        const sf::FloatRect vBounds = m_valueText.getLocalBounds();
        m_valueText.setPosition({ screenPos.x + m_bar.getSize().x - vBounds.size.x - vBounds.position.x,
                                  screenPos.y + (m_bar.getSize().y - vBounds.size.y) / 2.f - vBounds.position.y });

        target.draw(m_bar);
        target.draw(m_handle);
        target.draw(m_text);
        target.draw(m_valueText);
        if (!m_description.getString().isEmpty())
            target.draw(m_description);
    }
};

class CInfoBox
{
    sf::RectangleShape                     m_rect;
    std::vector<sf::Text>                  m_text;
    sf::Vector2f                           m_pos;
    const sf::Font*                        m_font{ nullptr };
    unsigned int                           m_charSize{ 16U };
    sf::Color                              m_textColor{ sf::Color::White };
    float                                  m_width{ 0.f };
    float                                  m_lineHeight{ 20.f };
    // Bottom anchored panels keep a fixed margin from the bottom edge and grow
    // upward, so the panel stays put as its line count changes.
    bool                                   m_anchoredBottom{ true };
    float                                  m_viewHeight{ 0.f };
    float                                  m_y{ 0.f };

    void resizeToFit(size_t lineCount)
    {
        const float height = static_cast<float>(lineCount) * m_lineHeight + 40.f;
        m_rect.setSize({ m_width, height });
        if (m_anchoredBottom)
            m_pos.y = m_viewHeight - height - m_y;
        m_rect.setPosition(m_pos);
    }

public:
    CInfoBox(
        float width,
        float height,
        const sf::Font& font,
        const std::vector<std::string>& text,
        sf::View& view,
        const sf::Color& fill = { 0, 0, 0, 128 }, // black 50% transparent
        const sf::Color& text_color = sf::Color::White,
        unsigned int charSize = 16U,
        bool anchored_bottom = true,
        float x = 0.f,
        float y = 0.f
    )
        : m_rect({ width, height })
        , m_font(&font)
        , m_charSize(charSize)
        , m_textColor(text_color)
        , m_width(width)
        , m_anchoredBottom(anchored_bottom)
        , m_viewHeight(view.getSize().y)
        , m_y(y)
    {
        m_pos = { x, anchored_bottom ? view.getSize().y - height - y : y };
        m_rect.setPosition(m_pos);
        m_rect.setFillColor(fill);

        setLines(text);
    }

    bool empty() const { return m_text.empty(); }

    sf::FloatRect getGlobalBounds() const { return m_rect.getGlobalBounds(); }

    // Refresh the lines in place, reusing the existing sf::Text objects and
    // growing the box only when the line count changes. Panels that update every
    // frame therefore allocate nothing in the steady state.
    void setLines(const std::vector<std::string>& lines)
    {
        const size_t previous = m_text.size();

        while (m_text.size() < lines.size())
        {
            m_text.emplace_back(*m_font);
            m_text.back().setCharacterSize(m_charSize);
            m_text.back().setFillColor(m_textColor);
        }

        for (size_t i = 0; i < lines.size(); ++i)
            m_text[i].setString(lines[i]);

        // Shrink without resize(): sf::Text is not default-constructible in SFML 3.
        while (m_text.size() > lines.size())
            m_text.pop_back();

        if (lines.size() != previous)
            resizeToFit(lines.size());
    }

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
