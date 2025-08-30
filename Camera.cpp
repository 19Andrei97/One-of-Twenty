#include "Camera.h"

Camera::Camera(int width, int height)
{
	m_camera.setSize(sf::Vector2f(width, height));
	m_camera.setCenter(sf::Vector2f(width / 2.f, height / 2.f)); // This set the camera to the center
}

void Camera::setCamera(int width, int height)
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
}

void Camera::zoomIn()
{
	if(m_camera.getSize().x > 500)
		m_camera.zoom(0.9f);
}

void Camera::zoomOut()
{
	//if (m_camera.getSize().x < 2000)
	m_camera.zoom(1.1f);
}
