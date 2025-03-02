#pragma once

#include <memory>
#include <bitset>
#include "components/Component.h"

#include "../utils/Utils.h"

//#define MOVE(x) std::move(x)

class ECS;


class Entity {
public:
	typedef std::shared_ptr<Entity> SPtr;
public:
	virtual ~Entity();

	bool operator==(Entity::SPtr other) {
		return getID() == other->getID();
	}

	template<typename ComponentType, typename... Targs>
	ComponentType* addComponent(Targs... args);

	template<typename ComponentType>
	void removeComponent();

	void removeComponent(const ComponentTypeID id);

	template<typename ComponentType>
	ComponentType* getComponent();

	template<typename ComponentType>
	bool hasComponent() const;


	bool hasComponents(std::bitset<MAX_NUM_COMPONENTS_TYPES> componentTypes) const;
#ifdef DEVELOPMENT
	const BaseComponent::Ptr* getComponents() const;
#endif

	Entity* getParent();

	bool isAboutToBeDestroyed() const;
	void queueDestruction();
	void removeAllComponents();

	void addChildEntity(Entity* child);
	void removeChildEntity(Entity* toRemove);
	void removeAllChildren();
	void removeDeleteAllChildren();
	/* Currently dangerous, will probably be altered in future */
	std::vector<Entity*>& getChildEntities();

	void setName(const std::string& name);
	const std::string& getName() const;
	int getID() const;
	int getECSIndex() const;
	Entity(const std::string& name = "");

public:
	bool tryToAddToSystems = true;
private:
	// Only ECS should be able to create entities
	friend class ECS;
	static SPtr Create(ECS* ecs, const std::string& name = "");

	void addToSystems();
	void removeFromSystems();

	void setECSIndex(int index);

	void setParent(Entity* entity);

	BaseComponent::Ptr* m_components;
	std::bitset<MAX_NUM_COMPONENTS_TYPES> m_componentTypes;
	std::string m_name;
	bool m_destructionQueued = false;
	int m_id;
	int m_ECSIndex;
	ECS* m_ecs;
	Entity* m_parent;

	std::vector<Entity*> m_children;
};

template<typename ComponentType, typename... Targs>
inline ComponentType* Entity::addComponent(Targs... args) {
	if (m_components[ComponentType::ID]) {
		SAIL_LOG_WARNING("Tried to add a duplicate component to an entity");
	} else {
		m_components[ComponentType::ID] = std::make_unique<ComponentType>(args...);

		m_componentTypes |= ComponentType::getBID();

		// Place this entity within the correct systems if told to
		if (tryToAddToSystems) {
			addToSystems();
		}
	}

	// Return pointer to the component
	return static_cast<ComponentType*>(m_components[ComponentType::ID].get());
}

template<typename ComponentType>
inline void Entity::removeComponent() {
	if ( hasComponent<ComponentType>() ) {
		m_components[ComponentType::ID].reset(nullptr);

		// Set the component type bit to 0 if it was 1
		std::bitset<MAX_NUM_COMPONENTS_TYPES> bits = 0;
		bits |= ComponentType::getBID();				// set a 1 to the type bit
		bits = ~bits;									// set a 0 to the type bit and rest to 1
		m_componentTypes = m_componentTypes & bits;		// keep the values of each not except the type bit which is now 0

		// Remove this entity from systems which required the removed component
		removeFromSystems();
	}
}

template<typename ComponentType>
inline ComponentType* Entity::getComponent() {
	if (hasComponent<ComponentType>()) {
		return static_cast<ComponentType*>(m_components[ComponentType::ID].get());
	}
	return nullptr;
}

template<typename ComponentType>
inline bool Entity::hasComponent() const {
	bool bitTrue = (m_componentTypes & ComponentType::getBID()).any();
	bool ptrTrue = m_components[ComponentType::ID] != nullptr;
	return bitTrue && ptrTrue;
}
