#include "pch.h"
#include "CandleHealthSystem.h"
#include "Sail/entities/components/Components.h"
#include "Network/NWrapperSingleton.h"
#include "Sail/utils/GameDataTracker.h"

#include "Sail/events/EventDispatcher.h"
#include "Sail/events/types/WaterHitPlayerEvent.h"

CandleHealthSystem::CandleHealthSystem() {
	registerComponent<CandleComponent>(true, true, true);
	registerComponent<NetworkSenderComponent>(false, true, false);
	registerComponent<LightComponent>(true, true, true);
	registerComponent<AudioComponent>(false, true, true);
	registerComponent<RenderInActiveGameComponent>(true, false, false); // Don't include replay candles in this system


	EventDispatcher::Instance().subscribe(Event::Type::WATER_HIT_PLAYER, this);
	EventDispatcher::Instance().subscribe(Event::Type::TORCH_EXTINGUISHED, this);
}

CandleHealthSystem::~CandleHealthSystem() {
	EventDispatcher::Instance().unsubscribe(Event::Type::WATER_HIT_PLAYER, this);
	EventDispatcher::Instance().unsubscribe(Event::Type::TORCH_EXTINGUISHED, this);
}

void CandleHealthSystem::update(float dt) {
	const bool isHost = NWrapperSingleton::getInstance().isHost();

	// The number of living candles, representing living players
	size_t livingCandles = entities.size();

	for (auto e : entities) {
		auto candle = e->getComponent<CandleComponent>();
		candle->wasHitByMeThisTick = false;

		// Scale fire particles with health
		auto particles = e->getComponent<ParticleEmitterComponent>();
		particles->spawnRate = 0.01f * (MAX_HEALTH / candle->health);

#pragma region HOST_ONLY_STUFF
		if (isHost && candle->isLit) {
			if (candle->health > 0.0f) {
				candle->invincibleTimer -= dt;

				// If someone hit the candle this tick tell all players to update the candle's health
				if (candle->wasHitThisTick) {
					// Candle has lost all its health so extinguish it
					NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
						Netcode::MessageType::SET_CANDLE_HEALTH,
						SAIL_NEW Netcode::MessageSetCandleHealth{
							e->getComponent<NetworkReceiverComponent>()->m_id,
							candle->health
						},
						false // Host already knows the candle's health so don't send to ourselves
					);
					candle->wasHitThisTick = false;
				}
			} else { // If candle used to be lit but has lost all its health
				candle->wasJustExtinguished = true;

				if (candle->respawns < m_maxNumRespawns) {
					// Candle has lost all its health so extinguish it
					NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
						Netcode::MessageType::EXTINGUISH_CANDLE,
						SAIL_NEW Netcode::MessageExtinguishCandle{
							e->getComponent<NetworkReceiverComponent>()->m_id,
							candle->wasHitByPlayerID
						},
						true
					);
					// If the player has no more respawns kill them
				} else {

					if (candle->wasHitByPlayerID < Netcode::NONE_PLAYER_ID_START && candle->wasHitByPlayerID != candle->playerEntityID) {
						GameDataTracker::getInstance().logEnemyKilled(candle->wasHitByPlayerID);
					}

					livingCandles--;

					NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
						Netcode::MessageType::PLAYER_DIED,
						SAIL_NEW Netcode::MessagePlayerDied{
							e->getParent()->getComponent<NetworkReceiverComponent>()->m_id,
							candle->wasHitByEntity
						},
						true
					);

					// Save the placement for the player who lost
					GameDataTracker::getInstance().logPlacement(
						Netcode::getComponentOwner(e->getParent()->getComponent<NetworkReceiverComponent>()->m_id)
					);

					// Only one living candle left and number of players in the game is greater than one
					if (livingCandles < 2 && entities.size() > 1) {

						for (auto e2 : entities) {
							NetworkReceiverComponent* cc = e2->getParent()->getComponent<NetworkReceiverComponent>();
							if (cc->m_id != e->getParent()->getComponent<NetworkReceiverComponent>()->m_id) {
								// Save the placement for the player who lost
								GameDataTracker::getInstance().logPlacement(
									Netcode::getComponentOwner(cc->m_id)
								);
							}
						}

						// COMMENTED OUT FOR TESTING
						//NWrapperSingleton::getInstance().queueGameStateNetworkSenderEvent(
						//	Netcode::MessageType::MATCH_ENDED,
						//	nullptr
						//);
					}
				}
			}
		}
#pragma endregion

		// Flicker effect for the torches
		static float clockLightModifier = 0;
		clockLightModifier += dt * (rand() % 100 / 100.0f);

		//						Sine wave function                    +        random variance
		float r = (sinf(clockLightModifier * 50.0f) * 0.075f + 0.55f) + (rand() % 10 - 5) / 50.0f;
		LightComponent* lc = e->getComponent<LightComponent>();
		lc->defaultColor = glm::vec3(r + 0.05f, (r - 0.05f) * 0.5f, (r - 0.1f) * 0.25f);
		// save this line for further possible further testing in future
		//light->getPointLight().setAttenuation(0.0f, 0.0f, r );

		// COLOR/INTENSITY
		float tempHealthRatio = (std::fmaxf(candle->health, 0.f) / MAX_HEALTH);
		lc->getPointLight().setColor(tempHealthRatio * lc->defaultColor);
	}
}

bool CandleHealthSystem::onEvent(const Event& event) {
	auto findCandleFromParentID = [=](const Netcode::ComponentID netCompID) {
		Entity* candle = nullptr;
		for (auto entity : entities) {
			if (auto parent = entity->getParent(); parent) {
				if (parent->getComponent<NetworkReceiverComponent>()->m_id == netCompID) {
					candle = entity;
					break;
				}
			}
		}
		return candle;
	};

	auto onWaterHitPlayer = [=](const WaterHitPlayerEvent& e) {
		Entity* candle = findCandleFromParentID(e.netCompID);

		if (!candle) {
			SAIL_LOG_WARNING("CandleHealthSystem::onWaterHitPlayer: no matching entity found");
			return;
		}

		// Damage the candle
		// TODO: Replace 10.0f with game settings damage
		if (e.hitterID == Netcode::SPRINKLER_COMP_ID) {
			candle->getComponent<CandleComponent>()->hitWithWater(1.0f, CandleComponent::DamageSource::SPRINKLER, e.hitterID);
		} else {
			candle->getComponent<CandleComponent>()->hitWithWater(10.0f, CandleComponent::DamageSource::PLAYER, e.hitterID);

		}
	};

	auto onTorchExtinguished = [=] (const TorchExtinguishedEvent& e) {
		for (auto torchE : entities) {
			if (torchE->getComponent<NetworkReceiverComponent>()->m_id == e.netIDextinguished) {
				auto candleC = torchE->getComponent<CandleComponent>();
				candleC->health = 0.0f;
				candleC->isLit = false;
				candleC->wasJustExtinguished = false; // reset for the next tick

				if (candleC->wasHitByPlayerID < Netcode::NONE_PLAYER_ID_START && candleC->wasHitByPlayerID != candleC->playerEntityID) {
					GameDataTracker::getInstance().logEnemyKilled(candleC->wasHitByPlayerID);
				} else if (candleC->wasHitByPlayerID == Netcode::MESSAGE_INSANITY_ID) {
					torchE->getParent()->getComponent<AudioComponent>()->m_sounds[Audio::INSANITY_SCREAM].isPlaying = true;
				}

				// Play the re-ignition sound if the player has any candles left
				if (candleC->respawns < m_maxNumRespawns) {
					auto playerEntity = torchE->getParent();
					playerEntity->getComponent<AudioComponent>()->m_sounds[Audio::RE_IGNITE_CANDLE].isPlaying = true;
				}
			}
		}
	};

	switch (event.type) {
	case Event::Type::WATER_HIT_PLAYER: onWaterHitPlayer((const WaterHitPlayerEvent&)event); break;
	case Event::Type::TORCH_EXTINGUISHED: onTorchExtinguished((const TorchExtinguishedEvent&)event); break;	
	default: break;
	}

	return true;
}

#ifdef DEVELOPMENT
unsigned int CandleHealthSystem::getByteSize() const {
	return BaseComponentSystem::getByteSize() + sizeof(*this);
}
#endif