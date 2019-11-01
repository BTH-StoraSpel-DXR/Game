
#include "pch.h"
#include "CandleSystem.h"

#include "Sail/entities/components/Components.h"
#include "Sail/entities/components/AnimationComponent.h"
#include "Sail/entities/components/LocalOwnerComponent.h"
#include "Sail/entities/components/NetworkSenderComponent.h"

#include "../Sail/src/Network/NWrapperSingleton.h"
#include "Sail/entities/Entity.h"
#include "Sail/graphics/camera/CameraController.h"
#include "Sail/entities/ECS.h"
#include "Sail/entities/systems/physics/UpdateBoundingBoxSystem.h"
#include "Sail/Application.h"
#include "Sail/utils/GameDataTracker.h"

#include "../../Physics/Octree.h"
#include "Sail/Application.h"
#include "../../Physics/Intersection.h"
#include "../../Physics/Physics.h"

#include "glm/gtx/vector_angle.hpp"

CandleSystem::CandleSystem() : BaseComponentSystem() {
	// TODO: System owner should check if this is correct
	registerComponent<CandleComponent>(true, true, true);
	registerComponent<TransformComponent>(true, true, false);
	registerComponent<LightComponent>(true, true, true);
	registerComponent<NetworkSenderComponent>(false, true, false);
}

CandleSystem::~CandleSystem() {

}

void CandleSystem::setPlayerEntityID(int entityID, Entity* entityPtr) {
	m_playerEntityID = entityID;
	m_playerEntityPtr = entityPtr;

}

// should be updated after collision detection has been done
void CandleSystem::update(float dt) {
	int LivingCandles = entities.size();

	for (auto e : entities) {
		auto candle = e->getComponent<CandleComponent>();

		candle->m_wasHitByMeThisTick = false;

		if (!candle->getIsAlive()) {
			continue;
		}

		//If a candle is out of health.
		if (candle->getHealth() <= 0.f) {
			candle->setCarried(true);

			if (candle->getNumRespawns() != m_maxNumRespawns) {
				auto playerEntity = e->getParent();
				playerEntity->getComponent<AudioComponent>()->m_sounds[Audio::RE_IGNITE_CANDLE].isPlaying = true;
			}
			// Did this candle's owner die?
			if (candle->getNumRespawns() == m_maxNumRespawns) {
				candle->setIsAlive(false);
				LivingCandles--;

				//Only let the host sent PLAYER_DIED message
				if (NWrapperSingleton::getInstance().isHost()) {
					NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
						Netcode::MessageType::PLAYER_DIED,
						SAIL_NEW Netcode::MessagePlayerDied{
							e->getParent()->getComponent<NetworkReceiverComponent>()->m_id,
							candle->getWasHitByNetID()
						}
					);

					// Save the placement for the player who lost
					GameDataTracker::getInstance().logPlacement(Netcode::getComponentOwner(
						e->getParent()->getComponent<NetworkReceiverComponent>()->m_id));

					if (LivingCandles <= 1) { // Match IS over
						NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
							Netcode::MessageType::MATCH_ENDED,
							nullptr,
							true
						);

					}
				}
			} else if ((candle->getDownTime() >= m_candleForceRespawnTimer || candle->getIsLit())) {
				if (NWrapperSingleton::getInstance().isHost()) {
					NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
						Netcode::MessageType::IGNITE_CANDLE,
						SAIL_NEW Netcode::MessageIgniteCandle{
							e->getParent()->getComponent<NetworkReceiverComponent>()->m_id,
						},
						true
						);
				}
			} else {
				candle->addToDownTime(dt);
			}
		}

		if (candle->isCarried() != candle->getWasCarriedLastUpdate()) {
			putDownCandle(e);

			// Inform other players that we've put down our candle
			if (e->getParent()->getComponent<LocalOwnerComponent>()) {
				NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
					Netcode::MessageType::CANDLE_HELD_STATE,
					SAIL_NEW Netcode::MessageCandleHeldState{
						e->getParent()->getComponent<NetworkSenderComponent>()->m_id,
						candle->isCarried(),
						e->getComponent<TransformComponent>()->getTranslation()
					},
					false // We've already put down our own candle so no need to do it again
				);
			}
		}

		if (candle->getInvincibleTimer() > 0.f) {
			candle->decrementInvincibleTimer(dt);
		}

		// COLOR/INTENSITY
		float cHealth = candle->getHealth();
		cHealth = (cHealth < 0.f) ? 0.f : cHealth;
		float tempHealthRatio = (cHealth / MAX_HEALTH);
		e->getComponent<LightComponent>()->getPointLight().setColor(glm::vec3(tempHealthRatio, tempHealthRatio * 0.7f, tempHealthRatio * 0.4f));

		candle->setWasCarriedLastUpdate(candle->isCarried());
		glm::vec3 flamePos = e->getComponent<TransformComponent>()->getMatrixWithUpdate() * glm::vec4(0, 0.37f, 0, 1);
		e->getComponent<LightComponent>()->getPointLight().setPosition(flamePos);
	}
}

void CandleSystem::putDownCandle(Entity* e) {
	auto candleComp = e->getComponent<CandleComponent>();
	auto candleTransComp = e->getComponent<TransformComponent>();
	auto parentTransComp = e->getParent()->getComponent<TransformComponent>();

	/* TODO: Raycast and see if the hit location is ground within x units */
	if (!candleComp->isCarried()) {
		if (candleComp->getIsLit()) {
			glm::vec3 parentPos = parentTransComp->getTranslation();
			glm::vec3 dir = candleTransComp->getParent()->getForward();
			dir.y = 0.0f;
			dir = glm::normalize(dir)*0.5f;
			glm::vec3 candleTryPosition = glm::vec3(parentPos.x - dir.x, parentPos.y, parentPos.z - dir.z);

			bool blocked = false;
			glm::vec3 down(0.f, -1.f, 0.f);
			float heightOffsetFromPlayerFeet = 1.f;

			{
				Octree::RayIntersectionInfo tempInfo;
				// Shoot a ray straight down 1 meter ahead of the player to check for floor
				m_octree->getRayIntersection(glm::vec3(candleTryPosition.x, candleTryPosition.y + heightOffsetFromPlayerFeet, candleTryPosition.z), down, &tempInfo, nullptr, 0.01f);
				if (tempInfo.closestHitIndex != -1) {
					float floorCheckVal = glm::angle(tempInfo.info[tempInfo.closestHitIndex].shape->getNormal(), -down);
					// If there's a low angle between the up-vector and the normal of the surface, it can be counted as floor
					bool isFloor = (floorCheckVal < 0.1f) ? true : false;
					if (!isFloor) {
						blocked = true;
					} else {
						// Update the height of the candle position
						candleTryPosition.y = candleTryPosition.y + (heightOffsetFromPlayerFeet - tempInfo.closestHit);
					}
				} else {
					blocked = true;
				}
			}

			{
				Octree::RayIntersectionInfo tempInfo;
				// Check if the position is visible for the player
				auto playerHead = glm::vec3(parentPos.x, parentPos.y + 1.8f, parentPos.z);
				auto playerHeadToCandle = candleTryPosition - playerHead;
				float eps = 0.0001f;
				m_octree->getRayIntersection(playerHead, glm::normalize(playerHeadToCandle), &tempInfo, nullptr);
				float phtcLength = glm::length(playerHeadToCandle);
				if (tempInfo.closestHit - phtcLength + eps < 0.f) {
					// Can't see the position where we try to place the candle
					blocked = true;
				}
			}

			// Place down the candle if it's not blocked
			if (!blocked) {
				candleTransComp->removeParent();
				candleTransComp->setTranslation(candleTryPosition);
				candleTransComp->setRotations(glm::vec3{ 0.f,0.f,0.f });
				e->getParent()->getComponent<AnimationComponent>()->rightHandEntity = nullptr;

				ECS::Instance()->getSystem<UpdateBoundingBoxSystem>()->update(0.0f);
			} else {
				candleComp->setCarried(true);
			}
		} else {
			candleComp->setCarried(true);
		}
	} else {
		// Pick up the candle
		if (glm::length(parentTransComp->getTranslation() - candleTransComp->getTranslation()) < 2.0f || !candleComp->getIsLit()) {
			candleTransComp->setParent(parentTransComp);
			e->getParent()->getComponent<AnimationComponent>()->rightHandEntity = e;
		} else {
			candleComp->setCarried(false);
		}
	}
}

void CandleSystem::init(GameState* gameStatePtr, Octree* octree) {
	m_octree = octree;
	this->setGameStatePtr(gameStatePtr);
}
