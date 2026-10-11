#include <pch.h>

#include "Camera.h"

#include <algorithm>

Camera::Camera(int width, int height)
{
	m_camera.setSize(sf::Vector2f(width, height));
	m_camera.setCenter(sf::Vector2f(width / 2.f, height / 2.f)); // This set the camera to the center
}

sf::IntRect Camera::getWorldBounds() const
{
	sf::Vector2i pos	= static_cast<sf::Vector2i>(m_camera.getCenter());
    sf::Vector2i size	= static_cast<sf::Vector2i>(m_camera.getSize());

    return {
        sf::Vector2i{ pos.x - size.x / 2, pos.y - size.y / 2 },
		sf::Vector2i{ size.x, size.y }
    };
}

void Camera::move(float x, float y)
{
	m_camera.move(sf::Vector2f{ x, y });

	// Keep the view inside the world. The half-size is the view's own extent, so
	// the visible area never shows anything past the map edge. A view larger than
	// the world is centred instead of clamped.
	if (m_bounds.size.x <= 0 || m_bounds.size.y <= 0)
		return;

	const sf::Vector2f size = m_camera.getSize();
	const float minX = static_cast<float>(m_bounds.position.x) + size.x / 2.f;
	const float maxX = static_cast<float>(m_bounds.position.x + m_bounds.size.x) - size.x / 2.f;
	const float minY = static_cast<float>(m_bounds.position.y) + size.y / 2.f;
	const float maxY = static_cast<float>(m_bounds.position.y + m_bounds.size.y) - size.y / 2.f;

	sf::Vector2f center = m_camera.getCenter();
	center.x = (minX > maxX) ? static_cast<float>(m_bounds.position.x) + static_cast<float>(m_bounds.size.x) / 2.f
	                         : std::clamp(center.x, minX, maxX);
	center.y = (minY > maxY) ? static_cast<float>(m_bounds.position.y) + static_cast<float>(m_bounds.size.y) / 2.f
	                         : std::clamp(center.y, minY, maxY);
	m_camera.setCenter(center);
}

void Camera::setWorldBounds(const sf::IntRect& bounds)
{
	m_bounds = bounds;
	// Re-apply now so a camera already outside snaps back inside (the clamp
	// otherwise only runs on the next move, which never comes while idle).
	move(0.f, 0.f);
}

void Camera::zoomIn()
{
	if(m_camera.getSize().x > 500)
		m_camera.zoom(0.9f);
	// The view is smaller now, so a camera at the edge may need to un-clamp.
	move(0.f, 0.f);
}

void Camera::zoomOut()
{
	//if (m_camera.getSize().x < 2000)
	m_camera.zoom(1.1f);
	// A wider view near the edge must be pulled back inside the world.
	move(0.f, 0.f);
}
