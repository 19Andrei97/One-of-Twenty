#include <doctest/doctest.h>

#include "ViewMath.h"

#include <SFML/Graphics/View.hpp>

TEST_CASE("viewOrigin is the top-left corner of the view")
{
    // Size 100x50 centred at (200, 300): the corner is half the size to the left
    // and above the centre.
    sf::View view;
    view.setSize(sf::Vector2f{ 100.f, 50.f });
    view.setCenter(sf::Vector2f{ 200.f, 300.f });

    CHECK(ViewMath::viewOrigin(view) == sf::Vector2f{ 150.f, 275.f });
}

TEST_CASE("viewOrigin follows the centre and size")
{
    sf::View view;
    view.setSize(sf::Vector2f{ 800.f, 600.f });
    view.setCenter(sf::Vector2f{ 0.f, 0.f });

    // Centred on the origin: the corner is half a view to the upper-left.
    CHECK(ViewMath::viewOrigin(view) == sf::Vector2f{ -400.f, -300.f });

    view.setCenter(sf::Vector2f{ -10.f, 20.f });
    CHECK(ViewMath::viewOrigin(view) == sf::Vector2f{ -410.f, -280.f });

    view.setSize(sf::Vector2f{ 40.f, 40.f });
    CHECK(ViewMath::viewOrigin(view) == sf::Vector2f{ -30.f, 0.f });
}
