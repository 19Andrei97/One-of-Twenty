#include <doctest/doctest.h>

#include "Hud.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{
std::string hudSourcePath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/hud_menu_data.json";
#else
    return "config/hud_menu_data.json";
#endif
}

std::string fontPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/fonts/arial.ttf";
#else
    return "fonts/arial.ttf";
#endif
}

std::string writeTempHudConfig(const std::string& name, const std::string& content)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("hud_test_" + name + ".json");
    std::ofstream out(path);
    out << content;
    return path.string();
}
} // namespace

TEST_CASE("Hud registers default callbacks and checks existence")
{
    sf::Font font;
    Hud hud(font, nullptr, "", 800.f, 600.f);
    hud.init();

    CHECK(hud.hasButtonCallback("random"));
    CHECK(hud.hasButtonCallback("cycle_hud"));

    CHECK(hud.hasSliderCallback("land_amount"));
    CHECK(hud.hasSliderCallback("continent_size"));
    CHECK(hud.hasSliderCallback("coast_roughness"));
    CHECK(hud.hasSliderCallback("mountain_height"));
    CHECK(hud.hasSliderCallback("mountain_scale"));
    CHECK(hud.hasSliderCallback("temperature"));
    CHECK(hud.hasSliderCallback("rainfall"));
    CHECK(hud.hasSliderCallback("snow_line"));
    CHECK(hud.hasSliderCallback("lake_level"));
    CHECK(hud.hasSliderCallback("lake_size"));
    CHECK(hud.hasSliderCallback("ore_richness"));

    CHECK_FALSE(hud.hasButtonCallback("non_existent_btn"));
    CHECK_FALSE(hud.hasSliderCallback("non_existent_sld"));
    CHECK_FALSE(hud.hasInputCallback("non_existent_inp"));
}

TEST_CASE("Hud binds custom callbacks by name from config")
{
    const std::string jsonContent = R"({
        "btn1": {
            "type": "button",
            "width": 100,
            "height": 30,
            "position": { "x": 10, "y": 10 },
            "label": "Click Me",
            "color_fill": [200, 200, 200],
            "outline": [0, 0, 0],
            "thickness": 1,
            "text_size": 14,
            "level": 0,
            "function": "custom_click"
        },
        "sld1": {
            "type": "slider",
            "width": 150,
            "height": 10,
            "position": { "x": 10, "y": 50 },
            "minimum_value": 0,
            "maximum_value": 100,
            "label": "Volume",
            "bar_color": [50, 50, 50],
            "handle_color": [255, 255, 255],
            "level": 0,
            "function": "set_volume"
        },
        "inp1": {
            "type": "input",
            "width": 100,
            "height": 30,
            "position": { "x": 10, "y": 80 },
            "placeholder": "Enter val",
            "color_fill": [255, 255, 255],
            "outline": [0, 0, 0],
            "level": 0,
            "function": "custom_input"
        }
    })";

    const std::string configFile = writeTempHudConfig("custom_cb", jsonContent);

    sf::Font font;
    Hud hud(font, nullptr, configFile, 800.f, 600.f);

    bool buttonClicked = false;
    hud.registerButtonCallback("custom_click", [&]() {
        buttonClicked = true;
    });

    float sliderVal = -1.f;
    hud.registerSliderCallback("set_volume", [&](float val) {
        sliderVal = val;
    });

    float inputVal = -1.f;
    hud.registerInputCallback("custom_input", [&](float val) {
        inputVal = val;
    });

    hud.init();

    CHECK(hud.getButtonCount() == 1);
    CHECK(hud.getSliderCount() == 1);
    CHECK(hud.getInputCount() == 1);

    // Simulate clicking the button at (20, 20) in view coords
    sf::Event::MouseButtonPressed pressEvent{
        sf::Mouse::Button::Left,
        { 20, 20 }
    };
    hud.input(pressEvent, sf::Vector2f(20.f, 20.f));
    CHECK(buttonClicked);

    // Register callback after init to verify re-binding
    bool secondClick = false;
    hud.registerButtonCallback("custom_click", [&]() {
        secondClick = true;
    });
    hud.input(pressEvent, sf::Vector2f(20.f, 20.f));
    CHECK(secondClick);

    std::filesystem::remove(configFile);
}

TEST_CASE("Hud multiple levels filter widgets during render and input")
{
    const std::string jsonContent = R"({
        "btn_lvl0": {
            "type": "button",
            "width": 100,
            "height": 30,
            "position": { "x": 10, "y": 10 },
            "label": "Lvl 0",
            "color_fill": [255, 255, 255],
            "outline": [0, 0, 0],
            "thickness": 1,
            "text_size": 14,
            "level": 0,
            "function": "action_lvl0"
        },
        "btn_lvl1": {
            "type": "button",
            "width": 100,
            "height": 30,
            "position": { "x": 10, "y": 50 },
            "label": "Lvl 1",
            "color_fill": [255, 255, 255],
            "outline": [0, 0, 0],
            "thickness": 1,
            "text_size": 14,
            "level": 1,
            "function": "action_lvl1"
        },
        "btn_lvl2": {
            "type": "button",
            "width": 100,
            "height": 30,
            "position": { "x": 10, "y": 90 },
            "label": "Lvl 2",
            "color_fill": [255, 255, 255],
            "outline": [0, 0, 0],
            "thickness": 1,
            "text_size": 14,
            "level": 2,
            "function": "action_lvl2"
        }
    })";

    const std::string configFile = writeTempHudConfig("levels", jsonContent);

    sf::Font font;
    Hud hud(font, nullptr, configFile, 800.f, 600.f);

    bool clicked0 = false;
    bool clicked1 = false;
    bool clicked2 = false;

    hud.registerButtonCallback("action_lvl0", [&]() { clicked0 = true; });
    hud.registerButtonCallback("action_lvl1", [&]() { clicked1 = true; });
    hud.registerButtonCallback("action_lvl2", [&]() { clicked2 = true; });

    hud.init();

    CHECK(hud.getMaxLevel() == 2);
    CHECK(hud.getLevel() == 0);
    CHECK(hud.getSliderCount() == 0);
    CHECK(hud.getInputCount() == 0);

    // At Level 0: Only level 0 is visible in Layered mode
    CHECK(hud.getVisibleButtonCount() == 1);
    CHECK(hud.isLevelVisible(0));
    CHECK_FALSE(hud.isLevelVisible(1));
    CHECK_FALSE(hud.isLevelVisible(2));

    // Clicking level 1 button at (20, 60) while at level 0 should NOT trigger action_lvl1
    sf::Event::MouseButtonPressed clickLvl1{ sf::Mouse::Button::Left, { 20, 60 } };
    hud.input(clickLvl1, sf::Vector2f(20.f, 60.f));
    CHECK_FALSE(clicked1);

    // Clicking level 0 button at (20, 20) while at level 0 DOES trigger
    sf::Event::MouseButtonPressed clickLvl0{ sf::Mouse::Button::Left, { 20, 20 } };
    hud.input(clickLvl0, sf::Vector2f(20.f, 20.f));
    CHECK(clicked0);

    // Switch to Level 1
    hud.nextLevel();
    CHECK(hud.getLevel() == 1);
    CHECK(hud.getVisibleButtonCount() == 2);
    CHECK(hud.isLevelVisible(0));
    CHECK(hud.isLevelVisible(1));
    CHECK_FALSE(hud.isLevelVisible(2));

    // Now clicking level 1 button triggers it
    hud.input(clickLvl1, sf::Vector2f(20.f, 60.f));
    CHECK(clicked1);

    // Switch to Level 2
    hud.nextLevel();
    CHECK(hud.getLevel() == 2);
    CHECK(hud.getVisibleButtonCount() == 3);

    // Next beyond max level stays at max
    hud.nextLevel();
    CHECK(hud.getLevel() == 2);

    // Cycle wraps back to 0
    hud.cycleLevel();
    CHECK(hud.getLevel() == 0);

    // Exact mode test
    hud.setLevelMode(HudLevelMode::Exact);
    hud.setLevel(1);
    CHECK(hud.getVisibleButtonCount() == 1); // Only lvl1 button
    CHECK_FALSE(hud.isLevelVisible(0));
    CHECK(hud.isLevelVisible(1));
    CHECK_FALSE(hud.isLevelVisible(2));

    std::filesystem::remove(configFile);
}

TEST_CASE("Main hud_menu_data.json loads successfully with named functions")
{
    sf::Font font;
    Hud hud(font, nullptr, hudSourcePath(), 1280.f, 720.f);
    hud.init();

    CHECK(hud.getButtonCount() > 0);
    CHECK(hud.getSliderCount() > 0);
    CHECK(hud.getVisibleButtonCount() == hud.getButtonCount());
    CHECK(hud.getVisibleInputCount() == hud.getInputCount());

    // The terrain sliders are tabbed across levels, so only the current tab's
    // sliders show. Raising the level to the maximum (Layered mode) reveals the
    // whole set, which is what proves every slider is reachable.
    CHECK(hud.getVisibleSliderCount() < hud.getSliderCount());
    hud.setLevel(hud.getMaxLevel());
    CHECK(hud.getVisibleSliderCount() == hud.getSliderCount());

    // Check that button_random has functionName "random" or valid name
    const auto& buttons = hud.getButtons();
    CHECK(buttons[0].functionName == "random");

    // Check slider function names
    const auto& sliders = hud.getSliders();
    for (const auto& sld : sliders)
    {
        CHECK_FALSE(sld.functionName.empty());
        CHECK(hud.hasSliderCallback(sld.functionName));
    }
}

TEST_CASE("Hud exposes the time-management controls and can relabel them")
{
    sf::Font font;
    Hud hud(font, nullptr, hudSourcePath(), 1280.f, 720.f);
    hud.init();

    CHECK(hud.hasButtonCallback("time_slower"));
    CHECK(hud.hasButtonCallback("time_pause"));
    CHECK(hud.hasButtonCallback("time_faster"));

    // The Pause button doubles as Play; the scene relabels it as state flips.
    hud.setButtonLabel("time_pause", "Play");
    for (const auto& b : hud.getButtons())
    {
        if (b.functionName == "time_pause" && b.widget)
            CHECK(b.widget->getLabel() == "Play");
    }

    // The readout panels are refreshed in place every frame and must not throw
    // even before any lines are supplied.
    CHECK_NOTHROW(hud.timeReadout({ "Year 1, Spring, Day 1  08:00", "Speed: 1 hour/s" }));
    CHECK_NOTHROW(hud.stats({ "Year 1, Spring, Day 1" }));
}


TEST_CASE("Terrain sliders expose a readable value and can be driven by code")
{
    sf::Font font;
    Hud hud(font, nullptr, hudSourcePath(), 1280.f, 720.f);
    hud.init();

    // A [0,1] slider reads as a percentage of its range; a small-unit slider
    // keeps decimals. This is what makes "Land Amount: 35%" clear at a glance.
    const auto findSlider = [&](const std::string& name) -> const CSlider* {
        for (const auto& s : hud.getSliders())
            if (s.functionName == name && s.widget)
                return s.widget.get();
        return nullptr;
    };

    const CSlider* land = findSlider("land_amount");
    REQUIRE(land != nullptr);
    CHECK(land->formatValue() == "35%");

    const CSlider* continent = findSlider("continent_size");
    REQUIRE(continent != nullptr);
    CHECK(continent->formatValue() == "0.0015");

    // Setting a value from code moves the handle (so a preset is reflected).
    CHECK(hud.setSliderValue("land_amount", 0.8f));
    CHECK(findSlider("land_amount")->getValue() == doctest::Approx(0.8f));
    CHECK(findSlider("land_amount")->formatValue() == "80%");
    // Out-of-range requests are clamped, never wrapped.
    CHECK(hud.setSliderValue("land_amount", 2.0f));
    CHECK(findSlider("land_amount")->getValue() == doctest::Approx(1.0f));

    // An unknown control name is reported rather than silently ignored.
    CHECK_FALSE(hud.setSliderValue("does_not_exist", 0.5f));
}

TEST_CASE("Terrain preset buttons drive every terrain slider at once")
{
    sf::Font font;
    Hud hud(font, nullptr, hudSourcePath(), 1280.f, 720.f);
    hud.init();

    const auto sliderValue = [&](const std::string& name) -> float {
        for (const auto& s : hud.getSliders())
            if (s.functionName == name && s.widget)
                return s.widget->getValue();
        return -1.f;
    };

    // Every preset is present and bound.
    CHECK(hud.hasButtonCallback("preset_earth"));
    CHECK(hud.hasButtonCallback("preset_archipelago"));
    CHECK(hud.hasButtonCallback("preset_pangaea"));

    const auto activate = [&](const std::string& name) {
        for (auto& b : hud.getButtons())
            if (b.functionName == name && b.widget)
                b.widget->activate();
    };

    // Start from a value that is not any preset's, then apply Pangaea and check a
    // few of its defining values landed on the sliders.
    hud.setSliderValue("continent_size", 0.002f);
    activate("preset_pangaea");
    CHECK(sliderValue("continent_size") == doctest::Approx(0.0007f));
    CHECK(sliderValue("land_amount") == doctest::Approx(0.46f));
    CHECK(sliderValue("mountain_height") == doctest::Approx(0.60f));

    // Archipelago is a different, waterier world: many small islands.
    activate("preset_archipelago");
    CHECK(sliderValue("continent_size") == doctest::Approx(0.0035f));
    CHECK(sliderValue("land_amount") == doctest::Approx(0.32f));
}

TEST_CASE("Hud readout panels are pinned to distinct corners and do not overlap")
{
    sf::Font font;
    Hud hud(font, nullptr, "", 1280.f, 720.f);
    hud.init();

    hud.infoBox({ "Grass", "Elevation: 0.42" });
    hud.stats({ "Population: 12", "Food: 30" });
    hud.timeReadout({ "Year 1, Spring, Day 1  08:00", "Speed: 1 hour/s" });

    const CInfoBox* info = hud.infoBoxWidget();
    const CInfoBox* stats = hud.statsWidget();
    const CInfoBox* time = hud.timeWidget();
    REQUIRE(info != nullptr);
    REQUIRE(stats != nullptr);
    REQUIRE(time != nullptr);

    const sf::FloatRect infoRect = info->getGlobalBounds();
    const sf::FloatRect statsRect = stats->getGlobalBounds();
    const sf::FloatRect timeRect = time->getGlobalBounds();

    // Selection info is pinned to the bottom-right, clear of the top-left
    // sliders and the top-right clock.
    CHECK(infoRect.position.y > 720.f / 2.f);
    CHECK(infoRect.position.x > 1280.f / 2.f);
    CHECK(infoRect.position.x + infoRect.size.x <= 1280.f + 0.5f);
    CHECK(infoRect.position.y + infoRect.size.y <= 720.f + 0.5f);

    // Settlement stats sit opposite, at the bottom-left.
    CHECK(statsRect.position.y > 720.f / 2.f);
    CHECK(statsRect.position.x < infoRect.position.x);

    // Clock readout stays in the top-right.
    CHECK(timeRect.position.y < 720.f / 2.f);
    CHECK(timeRect.position.x > 1280.f / 2.f);

    CHECK_FALSE(infoRect.findIntersection(statsRect).has_value());
    CHECK_FALSE(infoRect.findIntersection(timeRect).has_value());
    CHECK_FALSE(statsRect.findIntersection(timeRect).has_value());
}
