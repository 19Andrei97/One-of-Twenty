#pragma once

#include "Components.h"
#include <memory>
#include <string>

namespace Type
{
	enum Human
	{
		Generic = 0,
		Farmer,
		Lumberjack
	};

	enum Animal
	{
		Dog = 0,
		Cat
	};
}


class Entity
{
	friend class EntityManager;

	bool		m_alive{ true };
	int			m_type;
	size_t		m_id;

	// CONSTRUCTOR
	Entity(const size_t id = 0, const int type = Type::Human::Generic);

public:

	// Component Pointers
	std::shared_ptr<CTransform> cTransform;
	std::shared_ptr<CShape>		cShape;
	std::shared_ptr<CCollision> cCollision;
	std::shared_ptr<CLifespan>	cLifespan;

	// GETTERS
	bool isAlive() const		{ return m_alive; }
	const size_t id() const		{ return m_id; }
	const int type() const		{ return m_type; }

	// MAIN 
	void kill() { m_alive = false; }

};
