#pragma once

#include "Components_Entities.h"

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>

class Camera
{
	sf::View	m_camera;
	float		m_velocity{500.f};

	// The world rectangle the camera is confined to, in world pixels. An empty
	// rect (the default) means "no bounds": the camera pans freely, so a caller
	// that never sets bounds keeps the old behaviour. Set from the map's finite
	// world so the view cannot leave the generated map.
	sf::IntRect	m_bounds;

public:
	// CONSTRUCTORS
	Camera() = default;
	Camera(int width, int height);

	// INPUT STATE
	CInput		cInput;

	// SETTERS
	void move(float x, float y);
	void zoomIn();
	void zoomOut();

	// Confine the camera's centre so the view stays inside `bounds` (world
	// pixels). An empty rect clears the confinement. Applies immediately, so a
	// camera already outside snaps back inside.
	void setWorldBounds(const sf::IntRect& bounds);

	// GETTERS
	const sf::View& getCamera() const { return m_camera; }
	float getVelocity() const { return m_velocity; }
	sf::IntRect		getWorldBounds() const;

	// Convert a window pixel to world coordinates through this camera's view.
	// The single home for the screen->world conversion, so callers do not have
	// to reach for the raw sf::View.
	[[nodiscard]] sf::Vector2f worldFromPixel(const sf::RenderTarget& target,
	                                          const sf::Vector2i& pixel) const
	{
		return target.mapPixelToCoords(pixel, m_camera);
	}
};