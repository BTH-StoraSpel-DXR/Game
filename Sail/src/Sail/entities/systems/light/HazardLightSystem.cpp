#include "pch.h"
#include "HazardLightSystem.h"

#include "Sail.h"
#include "Sail/entities/components/SpotlightComponent.h"
#include "Sail/entities/components/TransformComponent.h"
#include "Sail/entities/components/MovementComponent.h"
#include "Sail/entities/components/AudioComponent.h"
#include "Sail/entities/ECS.h"
#include "Sail/graphics/light/LightSetup.h"
#include "Sail/utils/Storage/SettingStorage.h"
#include "Sail/entities/systems/Gameplay/LevelSystem/LevelSystem.h"

HazardLightSystem::HazardLightSystem() : BaseComponentSystem() {
	registerComponent<SpotlightComponent>(true, true, true);
	registerComponent<TransformComponent>(true, true, false);
	registerComponent<ParticleEmitterComponent>(true, true, true);
}

HazardLightSystem::~HazardLightSystem() {
}


//check and update all lights for all entities
void HazardLightSystem::updateLights(LightSetup* lightSetup, float alpha, float dt) {
	int id = 0;
	lightSetup->getSLs().clear();
	for (auto e : entities) {
		SpotlightComponent* sc = e->getComponent<SpotlightComponent>();
		MovementComponent* mc = e->getComponent<MovementComponent>();
		ParticleEmitterComponent* emitter = e->getComponent<ParticleEmitterComponent>();
		mc->rotation.y = 0.f;

		// Set particle emitter settings
		if (emitter) {
			auto* level = ECS::Instance()->getSystem<LevelSystem>();
			auto& roomSize = level->getRoomInfo(sc->roomID).size;
			float sprinklerXspread = (roomSize.x * 0.5f);
			float sprinklerZspread = (roomSize.y * 0.5f);

			emitter->size = 1.0f;
			emitter->constantVelocity = { 0.0f, -0.7f, 0.0f };
			emitter->acceleration = { 0.0f, -0.4f, 0.0f };
			emitter->spread = { sprinklerXspread, 1.0f, sprinklerZspread };
			emitter->spawnRate = 1.f / 100.f;
			emitter->lifeTime = 2.0f;
		}

		// Update active lights
		if (!sc->isOn) {
			continue;
		}

		AudioComponent* ac = e->getComponent<AudioComponent>();
		if (sc->alarmTimer <= Application::getInstance()->getSettings().gameSettingsDynamic["map"]["sprinklerIncrement"].value){
			sc->alarmTimer += dt;
			ac->m_sounds[Audio::SoundType::ALARM].isPlaying = true;
			if (sc->alarmTimer > Application::getInstance()->getSettings().gameSettingsDynamic["map"]["sprinklerIncrement"].value) {
				ac->m_sounds[Audio::SoundType::ALARM].isPlaying = false;
				ac->m_sounds[Audio::SoundType::SPRINKLER_START].isPlaying = true;
			}
		}
		else {
			ac->m_sounds[Audio::SoundType::SPRINKLER_WATER].isPlaying = true;
		}



		mc->rotation.y = 4.f;

		TransformComponent* t = e->getComponent<TransformComponent>();


		sc->m_lightEntityRotated = sc->light;
		sc->m_lightEntityRotated.setIndex(id++);
		sc->m_lightEntityRotated.setDirection(glm::rotate(t->getInterpolatedRotation(alpha), sc->light.getDirection()));
		sc->m_lightEntityRotated.setPosition(sc->light.getPosition() + t->getInterpolatedTranslation(alpha));
		
		lightSetup->addSpotLight(sc->m_lightEntityRotated);
	}
}

void HazardLightSystem::toggleONOFF() {
	for (auto e : entities) {
		SpotlightComponent* sc = e->getComponent<SpotlightComponent>();
		sc->isOn = !sc->isOn;
	}
}

void HazardLightSystem::enableHazardLights(std::vector<int> activeRooms) {
	for (auto e : entities) {
		SpotlightComponent* sc = e->getComponent<SpotlightComponent>();
		std::vector<int>::iterator it = std::find(activeRooms.begin(), activeRooms.end(), sc->roomID);
		if (it != activeRooms.end()) {
			sc->isOn = true;
		}
	}
}