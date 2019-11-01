#include "pch.h"
#include "ProjectileSystem.h"
#include "Sail/entities/components/ProjectileComponent.h"
#include "Sail/entities/components/CandleComponent.h"
#include "Sail/entities/components/CollisionComponent.h"
#include "Sail/entities/components/MovementComponent.h"
#include "Sail/entities/components/LocalOwnerComponent.h"
#include "Network/NWrapperSingleton.h"
#include "Sail/Application.h"
#include "API/DX12/renderer/DX12RaytracingRenderer.h"

ProjectileSystem::ProjectileSystem() {
	// TODO: System owner should check if this is correct
	registerComponent<ProjectileComponent>(true, true, true);
	registerComponent<CollisionComponent>(true, true, false);
	registerComponent<MovementComponent>(true, true, true);
	registerComponent<CandleComponent>(false, true, true);
	registerComponent<NetworkReceiverComponent>(false, true, false);
	registerComponent<LocalOwnerComponent>(false, true, false);

	float splashSize = 0.14f;
	m_projectileSplashSize = (1.f / splashSize) / 2.f;
}

ProjectileSystem::~ProjectileSystem() {

}

void ProjectileSystem::update(float dt) {
	for (auto& e : entities) {
		CollisionComponent* collisionComp = e->getComponent<CollisionComponent>();
		auto projectileCollisions = collisionComp->collisions;
		auto projComp = e->getComponent<ProjectileComponent>();
		for (auto& collision : projectileCollisions) {
			// Check if a decal should be created
			if (glm::length(e->getComponent<MovementComponent>()->oldVelocity) > 0.7f) {
				// TODO: Replace with some "layer-id" check rather than doing a string check
				if (collision.entity->getName().substr(0U, 4U) == "Map_" || collision.entity->getName().substr(0U, 7U) == "Clutter") {
					// Calculate rotation matrix used when placing a decal at the intersection
					//glm::mat4 rotMat = glm::rotate(glm::identity<glm::mat4>(), Utils::fastrand() * 3.14f, glm::vec3(0.0f, 0.0f, 1.0f));

					// Place water point at intersection position
					Application::getInstance()->getRenderWrapper()->getCurrentRenderer()->submitWaterPoint(collision.intersectionPosition);

					projComp->timeSinceLastDecal = 0.f;
				}
			}

// To prevent a crash when shooting bot candles in the performance test
#ifndef _PERFORMANCE_TEST
			//If projectile collided with a candle and the local player owned the projectile
			if (collision.entity->hasComponent<CandleComponent>() && e->hasComponent<LocalOwnerComponent>()) {
				// If that candle isn't our own
				if (!(collision.entity->hasComponent<LocalOwnerComponent>() && collision.entity->getComponent<CandleComponent>()->isCarried())) {

#ifdef DEVELOPMENT
					if (!collision.entity->getParent() || !collision.entity->getParent()->hasComponent<NetworkReceiverComponent>()) {
						Logger::Error("Projectile hit player who doesn't have a NetworkReceiverComponent\n");
						break;
					}
#endif
					//Inform the host about the hit.( in case you are host this will broadcast to everyone else)
					NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
						Netcode::MessageType::WATER_HIT_PLAYER,
						SAIL_NEW Netcode::MessageWaterHitPlayer{
							collision.entity->getParent()->getComponent<NetworkReceiverComponent>()->m_id
						}
					);
				}
			}
#endif

			if (Utils::rnd() < 0.5) {
				e->queueDestruction();
			}
		}

		projComp->timeSinceLastDecal += dt;
	}
}