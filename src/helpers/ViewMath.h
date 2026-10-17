#pragma once

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/View.hpp>

#include <SFML/System/Vector2.hpp>

// View-space helpers shared by the HUD and the scenes, so the top-left origin
// computation and the screen->world conversion live in one place.
namespace ViewMath
{
    // The world-space coordinate of the view's top-left corner. Widgets are laid
    // out relative to this, so a HUD position is the same in any camera pan.
    [[nodiscard]] inline sf::Vector2f viewOrigin(const sf::View& view)
    {
        return view.getCenter() - view.getSize() / 2.f;
    }

    // Convert a window pixel to world coordinates through a view. Thin wrapper so
    // callers do not repeat the `mapPixelToCoords(pixel, view)` idiom.
    [[nodiscard]] inline sf::Vector2f worldFromPixel(const sf::RenderTarget& target,
                                                     const sf::Vector2i& pixel,
                                                     const sf::View& view)
    {
        return target.mapPixelToCoords(pixel, view);
    }
}
