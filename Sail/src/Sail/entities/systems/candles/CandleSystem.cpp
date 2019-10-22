#include "pch.h"
#include "CandleSystem.h"

#include "Sail/entities/components/Components.h"
#include "Sail/entities/components/LocalOwnerComponent.h"

#include "../Sail/src/Network/NWrapperSingleton.h"
#include "Sail/entities/Entity.h"
#include "Sail/graphics/camera/CameraController.h"
#include "Sail/entities/ECS.h"
#include "Sail/entities/systems/physics/UpdateBoundingBoxSystem.h"
#include "Sail/Application.h"

CandleSystem::CandleSystem() : BaseComponentSystem() {
	// TODO: System owner should check if this is correct
	registerComponent<CandleComponent>(true, true, true);
	registerComponent<TransformComponent>(true, true, false);
	registerComponent<LightComponent>(true, true, true);
}

CandleSystem::~CandleSystem() {

}

void CandleSystem::setPlayerEntityID(int entityID, Entity* entityPtr) {
	m_playerEntityID = entityID;
	m_playerEntityPtr = entityPtr;

}

// turn on the light of a specified candle if it doesn't have one already
void CandleSystem::lightCandle(const std::string& name) {
	for (auto e : entities) {
		if (e->getName() == name) {
			e->getComponent<LightComponent>()->getPointLight().setColor(glm::vec3(1.0f, 1.0f, 1.0f));
			break;
		}
	}
}

// should be updated after collision detection has been done
void CandleSystem::update(float dt) {
	int LivingCandles = entities.size();

	for (auto e : entities) {
		auto candle = e->getComponent<CandleComponent>();

		if (candle->getIsAlive()) {
			//If a candle is out of health.
			if (candle->getHealth() <= 0.f) {
				candle->setIsLit(false);
				candle->setCarried(true);
				
				// Did current player die?
				if (candle->getNumRespawns() == m_maxNumRespawns) {
					candle->setIsAlive(false);
					LivingCandles--;

					//Only let the host sent PLAYER_DIED message
					if (NWrapperSingleton::getInstance().isHost()) {
						NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
							Netcode::MessageType::PLAYER_DIED,
							SAIL_NEW Netcode::MessageDataPlayerDied{
								e->getParent()->getComponent<NetworkReceiverComponent>()->m_id
							}
						);

						//This should remove the candle entity from game
						e->getParent()->removeDeleteAllChildren();

						// Check if the extinguished candle is owned by the player
						if (e->getParent()->getComponent<NetworkReceiverComponent>()->m_id >> 18 == NWrapperSingleton::getInstance().getMyPlayerID()) {
							//If it is me that died, become spectator.
							e->getParent()->addComponent<SpectatorComponent>();
							e->getParent()->getComponent<MovementComponent>()->constantAcceleration = glm::vec3(0.f);
							e->getParent()->getComponent<MovementComponent>()->velocity = glm::vec3(0.f);
							e->getParent()->removeComponent<GunComponent>();

							// Get position and rotation to look at middle of the map from above
							{
								auto parTrans = e->getParent()->getComponent<TransformComponent>();
								auto pos = glm::vec3(parTrans->getMatrix()[3]);
								pos.y = 20.f;
								parTrans->setTranslation(pos);
								MapComponent temp;
								auto middleOfLevel = glm::vec3(temp.tileSize * temp.xsize / 2.f, 0.f, temp.tileSize * temp.ysize / 2.f);
								auto dir = glm::normalize(middleOfLevel - pos);
								auto rots = Utils::getRotations(dir);
								parTrans->setRotations(glm::vec3(0.f, -rots.y, rots.x));
							}

						} else {
							//If it wasnt me that died, compleatly remove the player entity from game.
							e->getParent()->queueDestruction();
						}

						if (LivingCandles <= 1) { // Match IS over
							//TODO: move MATCH_ENDED event to host side and not to client side.
							NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
								Netcode::MessageType::MATCH_ENDED,
								nullptr
							);

							m_gameStatePtr->requestStackPop();
							m_gameStatePtr->requestStackPush(States::EndGame);
						}
					}
				}
			} else if ((candle->getDoActivate() || candle->getDownTime() >= m_candleForceRespawnTimer) && !candle->getIsLit()) {
				candle->setIsLit(true);
				candle->setHealth(MAX_HEALTH);
				candle->incrementRespawns();
				candle->resetDownTime();
				candle->resetDoActivate();
			} else if (!candle->getIsLit()) {
				candle->addToDownTime(dt);
			}

			if (candle->isCarried() != candle->getWasCarriedLastUpdate()) {
				putDownCandle(e);
			}

			if (candle->getInvincibleTimer() > 0.f) {
				candle->decrementInvincibleTimer(dt);
			}

			// COLOR/INTENSITY
			float cHealth = candle->getHealth();
			cHealth = (cHealth < 0.f) ? 0.f : cHealth;
			float tempHealthRatio = (cHealth / MAX_HEALTH);
			e->getComponent<LightComponent>()->getPointLight().setColor(glm::vec3(tempHealthRatio, tempHealthRatio, tempHealthRatio));

			candle->setWasCarriedLastUpdate(candle->isCarried());
			glm::vec3 flamePos = glm::vec3(e->getComponent<TransformComponent>()->getMatrix()[3]) + glm::vec3(0, 0.5f, 0);
			e->getComponent<LightComponent>()->getPointLight().setPosition(flamePos);
		}
	}
}

void CandleSystem::putDownCandle(Entity* e) {
	auto candleComp = e->getComponent<CandleComponent>();

	auto candleTransComp = e->getComponent<TransformComponent>();
	auto parentTransComp = e->getParent()->getComponent<TransformComponent>();
	/* TODO: Raycast and see if the hit location is ground within x units */
	if (!candleComp->isCarried()) {
		if (candleComp->getIsLit()) {
			float yaw = -candleTransComp->getParent()->getRotations().y;
			glm::vec3 dir = glm::vec3(cos(yaw), 0.f, sin(yaw));
			candleTransComp->removeParent();			
			candleTransComp->setTranslation(parentTransComp->getTranslation() + dir);
			ECS::Instance()->getSystem<UpdateBoundingBoxSystem>()->update(0.0f);
		} else {
			candleComp->setCarried(true);
		}
	} else {
		if (glm::length(parentTransComp->getTranslation() - candleTransComp->getTranslation()) < 2.0f || !candleComp->getIsLit()) {
			candleTransComp->setTranslation(glm::vec3(0.f, 2.0f, 0.f));
			candleTransComp->setParent(parentTransComp);
		} else {
			candleComp->setCarried(false);
		}
	}

	if (e->getParent()->getComponent<LocalOwnerComponent>()) {
		NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
			Netcode::MessageType::CANDLE_HELD_STATE,
			SAIL_NEW Netcode::MessageDataCandleHeldState{
				e->getParent()->getComponent<NetworkReceiverComponent>()->m_id,
				candleComp->isCarried(),
				e->getComponent<TransformComponent>()->getTranslation()
			}
		);
	}
}

void CandleSystem::init(GameState* gameStatePtr) {

	this->setGameStatePtr(gameStatePtr);
}
