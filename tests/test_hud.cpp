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

    CHECK(hud.hasSliderCallback("continent_frequency"));
    CHECK(hud.hasSliderCallback("cont_multiplier"));
    CHECK(hud.hasSliderCallback("warp_frequency"));
    CHECK(hud.hasSliderCallback("mineral_frequency"));
    CHECK(hud.hasSliderCallback("mineral_multiplier"));

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
    CHECK(hud.getVisibleSliderCount() == hud.getSliderCount());
    CHECK(hud.getVisibleInputCount() == hud.getInputCount());

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
