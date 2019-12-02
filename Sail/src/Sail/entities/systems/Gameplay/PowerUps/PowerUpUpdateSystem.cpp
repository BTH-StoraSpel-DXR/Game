#include "pch.h" 
#include "PowerUpUpdateSystem.h"
#include "Sail/entities/components/PowerUpComponent.h"

PowerUpUpdateSystem::PowerUpUpdateSystem() {
	registerComponent<PowerUpComponent>(true, true, true);
}

PowerUpUpdateSystem::~PowerUpUpdateSystem() {
}

void PowerUpUpdateSystem::update(float dt) {

	for (auto& e: entities) {
		if (auto* powC = e->getComponent<PowerUpComponent>()) {
			for (auto& pow : powC->powerUps) {
				pow.time -= dt;
				if (pow.time < 0) {
					pow.time = 0;
				}
			}
		}
	}
}

unsigned int PowerUpUpdateSystem::getByteSize() const {
	/* TODO: Fix component size */
	return sizeof(*this);
}
