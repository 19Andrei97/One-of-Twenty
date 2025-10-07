#pragma once

#include "../helpers/Components_Entities.h"

class Camera
{
	sf::View	m_camera;
	float		m_velocity{500.f};

public:
	// CONSTRUCTORS
	Camera() = default;
	Camera(int width, int height);

	// COMPONENTS POINTERS
	std::shared_ptr<CInput>		cInput;

	// SETTERS
	void setCamera(int width, int height);
	void move(float x, float y);
	void zoomIn();
	void zoomOut();

	// GETTERS
	const sf::View& getCamera()		{ return m_camera; };
	const float		getVelocity()		{ return m_velocity; };
	sf::IntRect		getWorldBounds() const;
};