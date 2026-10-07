#include <doctest/doctest.h>

#include "Scene.h"
#include "Scene_Menu.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
class MockScene : public Scene
{
public:
    int updateCalls{ 0 };
    float lastDeltaTime{ 0.f };
    int renderCalls{ 0 };
    int inputCalls{ 0 };
    int enterCalls{ 0 };
    int exitCalls{ 0 };

    void update(float deltaTime) override
    {
        ++updateCalls;
        lastDeltaTime = deltaTime;
    }

    void sRender(sf::RenderTarget& /*target*/) override
    {
        ++renderCalls;
    }

    void sUserInput(const sf::Event& /*event*/) override
    {
        ++inputCalls;
    }

    void onEnter() override
    {
        ++enterCalls;
    }

    void onExit() override
    {
        ++exitCalls;
    }
};
} // namespace

TEST_CASE("Scene base class state management")
{
    MockScene scene;

    CHECK_FALSE(scene.isPaused());
    CHECK_FALSE(scene.hasEnded());

    scene.setPaused(true);
    CHECK(scene.isPaused());

    scene.togglePaused();
    CHECK_FALSE(scene.isPaused());

    scene.end();
    CHECK(scene.hasEnded());
}

TEST_CASE("Scene lifecycle and execution")
{
    auto scene1 = std::make_shared<MockScene>();
    auto scene2 = std::make_shared<MockScene>();

    // Initial state
    CHECK(scene1->enterCalls == 0);
    CHECK(scene1->exitCalls == 0);

    scene1->onEnter();
    CHECK(scene1->enterCalls == 1);

    scene1->update(0.016f);
    CHECK(scene1->updateCalls == 1);
    CHECK(scene1->lastDeltaTime == doctest::Approx(0.016f));

    sf::Event::KeyPressed keyEvent{
        sf::Keyboard::Key::W,
        sf::Keyboard::Scancode::W,
        false, false, false, false
    };
    scene1->sUserInput(keyEvent);
    CHECK(scene1->inputCalls == 1);

    scene1->onExit();
    CHECK(scene1->exitCalls == 1);
}

TEST_CASE("Scene_Menu navigation and selection")
{
    sf::Font font;
    Scene_Menu menu(nullptr, font, 800.f, 600.f);

    CHECK(menu.getSelectedIndex() == 0);
    CHECK(menu.getOptions().size() >= 3);

    // Navigate down
    menu.selectNext();
    CHECK(menu.getSelectedIndex() == 1);

    menu.selectNext();
    CHECK(menu.getSelectedIndex() == 2);

    // Wrap around
    menu.selectNext();
    CHECK(menu.getSelectedIndex() == 0);

    // Navigate up wraps to end
    menu.selectPrev();
    CHECK(menu.getSelectedIndex() == menu.getOptions().size() - 1);

    // Event input navigation
    sf::Event::KeyPressed downKey{
        sf::Keyboard::Key::Down,
        sf::Keyboard::Scancode::Down,
        false, false, false, false
    };
    menu.sUserInput(downKey);
    CHECK(menu.getSelectedIndex() == 0);

    sf::Event::KeyPressed sKey{
        sf::Keyboard::Key::S,
        sf::Keyboard::Scancode::S,
        false, false, false, false
    };
    menu.sUserInput(sKey);
    CHECK(menu.getSelectedIndex() == 1);

    sf::Event::KeyPressed upKey{
        sf::Keyboard::Key::Up,
        sf::Keyboard::Scancode::Up,
        false, false, false, false
    };
    menu.sUserInput(upKey);
    CHECK(menu.getSelectedIndex() == 0);
}
