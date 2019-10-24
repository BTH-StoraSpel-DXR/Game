#include "pch.h"
#include "EntityFactory.hpp"

#include "Sail/Application.h"
#include "Sail/graphics/geometry/Model.h"
#include "Sail/entities/components/Components.h"
#include "Sail/ai/pathfinding/NodeSystem.h"
#include "Sail/ai/states/AttackingState.h"
#include "Sail/ai/states/FleeingState.h"
#include "Sail/ai/states/SearchingState.h"
#include "Sail/entities/components/LocalOwnerComponent.h"
#include "Sail/entities/components/OnlineOwnerComponent.h"
#include "../Sail/src/Network/NWrapperSingleton.h"

Entity::SPtr EntityFactory::CreateCandle(const std::string& name, const glm::vec3& lightPos, size_t lightIndex) {
	// Candle has a model and a bounding box
	auto* shader = &Application::getInstance()->getResourceManager().getShaderSet<GBufferOutShader>();
	Model* candleModel = &Application::getInstance()->getResourceManager().getModel("candleExported.fbx", shader);
	candleModel->getMesh(0)->getMaterial()->setAlbedoTexture("sponza/textures/candleBasicTexture.tga");
	
	auto* wireframeShader = &Application::getInstance()->getResourceManager().getShaderSet<GBufferWireframe>();
	Model* boundingBoxModel = &Application::getInstance()->getResourceManager().getModel("boundingBox.fbx", wireframeShader);
	boundingBoxModel->getMesh(0)->getMaterial()->setColor(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
	boundingBoxModel->getMesh(0)->getMaterial()->setAOScale(0.5);
	boundingBoxModel->getMesh(0)->getMaterial()->setMetalnessScale(0.5);
	boundingBoxModel->getMesh(0)->getMaterial()->setRoughnessScale(0.5);


	//creates light with model and pointlight
	auto candle = ECS::Instance()->createEntity(name.c_str());
	candle->addComponent<CandleComponent>();
	candle->addComponent<ModelComponent>(candleModel);
	candle->addComponent<TransformComponent>(lightPos);
	candle->addComponent<BoundingBoxComponent>(boundingBoxModel);
	candle->addComponent<CollidableComponent>();
	candle->addComponent<CullingComponent>();
	PointLight pl;
	pl.setColor(glm::vec3(1.0f, 0.7f, 0.4f));
	pl.setPosition(glm::vec3(lightPos.x, lightPos.y + .37f, lightPos.z));
	pl.setAttenuation(0.f, 0.f, 0.2f);
	pl.setIndex(lightIndex);
	candle->addComponent<LightComponent>(pl);

	return candle;
}

Entity::SPtr EntityFactory::CreateMyPlayer(Netcode::PlayerID playerID, size_t lightIndex, glm::vec3 spawnLocation) {
	// Create my player
	auto myPlayer = EntityFactory::CreateGenericPlayer(lightIndex, spawnLocation);
	myPlayer->addComponent<NetworkSenderComponent>(Netcode::MessageType::CREATE_NETWORKED_ENTITY, Netcode::EntityType::PLAYER_ENTITY, playerID);
	myPlayer->getComponent<NetworkSenderComponent>()->addDataType(Netcode::MessageType::ANIMATION);

	Netcode::ComponentID netComponentID = myPlayer->getComponent<NetworkSenderComponent>()->m_id;
	myPlayer->addComponent<NetworkReceiverComponent>(netComponentID, Netcode::EntityType::PLAYER_ENTITY);
	myPlayer->addComponent<LocalOwnerComponent>(netComponentID);

	myPlayer->addComponent<MovementComponent>()->constantAcceleration = glm::vec3(0.0f, -9.8f, 0.0f);
	myPlayer->addComponent<CollisionComponent>();
	myPlayer->addComponent<SpeedLimitComponent>()->maxSpeed = 6.0f;


	// Create candle for the player
	auto candle = CreateCandle("PlayerCandle", glm::vec3(0.f, 2.f, 0.f), lightIndex);
	candle->addComponent<RealTimeComponent>(); // Player candle will have its position updated each frame
	candle->getComponent<CandleComponent>()->setOwner(playerID);
	myPlayer->addChildEntity(candle);

	myPlayer->getComponent<TransformComponent>()->setStartTranslation(glm::vec3(1.6f, 0.9f, 1.f) + spawnLocation);

	myPlayer->addComponent<GunComponent>();

	return myPlayer;
}


// TODO: send in entity pointer so the entity can be added to NetworkReceiverSystem before components are added to the entity
Entity::SPtr EntityFactory::CreateOtherPlayer(Netcode::ComponentID netComponentID, size_t lightIndex, glm::vec3 spawnLocation) {
	// Other players have a character model and animations
	std::string modelName = "DocTorch.fbx";
	auto* shader = &Application::getInstance()->getResourceManager().getShaderSet<GBufferOutShader>();
	Model* characterModel = &Application::getInstance()->getResourceManager().getModelCopy(modelName, shader);
	characterModel->getMesh(0)->getMaterial()->setMetalnessRoughnessAOTexture("pbr/Character/CharacterMRAO.tga");
	characterModel->getMesh(0)->getMaterial()->setAlbedoTexture("pbr/Character/CharacterTex.tga");
	characterModel->getMesh(0)->getMaterial()->setNormalTexture("pbr/Character/CharacterNM.tga");
	characterModel->setIsAnimated(true);
	AnimationStack* stack = &Application::getInstance()->getResourceManager().getAnimationStack(modelName);


	// Create the player
	auto otherPlayer = EntityFactory::CreateGenericPlayer(lightIndex, spawnLocation);

	otherPlayer->addComponent<NetworkReceiverComponent>(netComponentID, Netcode::EntityType::PLAYER_ENTITY);
	otherPlayer->addComponent<OnlineOwnerComponent>(netComponentID);
	otherPlayer->addComponent<ModelComponent>(characterModel);
	otherPlayer->addComponent<CollidableComponent>();

	AnimationComponent* ac = otherPlayer->addComponent<AnimationComponent>(stack);
	ac->currentAnimation = stack->getAnimation(3);


	// Create the player's candle
	auto candle = CreateCandle("OtherPlayerCandle", glm::vec3(0.f, 2.f, 0.f), lightIndex);
	candle->addComponent<RealTimeComponent>(); // Player candle will have its position updated each frame
	candle->getComponent<CandleComponent>()->setOwner(Netcode::getComponentOwner(netComponentID));
	candle->addComponent<OnlineOwnerComponent>(netComponentID);
	otherPlayer->addChildEntity(candle);


	// Attach the candle to the player's left hand
	ac->leftHandEntity = candle.get();
	ac->leftHandPosition = glm::identity<glm::mat4>();
	ac->leftHandPosition = glm::translate(ac->leftHandPosition, glm::vec3(0.57f, 1.03f, 0.05f));
	ac->leftHandPosition = ac->leftHandPosition * glm::toMat4(glm::quat(glm::vec3(3.14f * 0.5f, -3.14f * 0.17f, 0.0f)));

	return otherPlayer;
}

// Creates a player enitty without a candle and without a model
Entity::SPtr EntityFactory::CreateGenericPlayer(size_t lightIndex, glm::vec3 spawnLocation) {
	// All players have a bounding box
	auto* wireframeShader = &Application::getInstance()->getResourceManager().getShaderSet<GBufferWireframe>();
	Model* boundingBoxModel = &Application::getInstance()->getResourceManager().getModel("boundingBox.fbx", wireframeShader);
	boundingBoxModel->getMesh(0)->getMaterial()->setColor(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
	boundingBoxModel->getMesh(0)->getMaterial()->setAOScale(0.5);
	boundingBoxModel->getMesh(0)->getMaterial()->setMetalnessScale(0.5);
	boundingBoxModel->getMesh(0)->getMaterial()->setRoughnessScale(0.5);



	auto player = ECS::Instance()->createEntity("player");

	player->addComponent<TransformComponent>(spawnLocation);
	player->addComponent<CullingComponent>();

	// Give player a bounding box
	player->addComponent<BoundingBoxComponent>(boundingBoxModel);
	player->getComponent<BoundingBoxComponent>()->getBoundingBox()->setHalfSize(glm::vec3(0.7f, .9f, 0.7f)); // Needed?

	// Adding audio component and adding all sounds attached to the player entity
	player->addComponent<AudioComponent>();

#pragma region DEFINING PLAYER SOUNDS

	Audio::SoundInfo sound{};
	sound.fileName = "../Audio/footsteps_1.wav";
	sound.soundEffectLength = 1.0f;
	sound.volume = 0.5f;
	sound.playOnce = false;
	sound.positionalOffset = { 0.0f, -1.6f, 0.0f };
	player->getComponent<AudioComponent>()->defineSound(Audio::SoundType::RUN, sound);

	sound.fileName = "../Audio/jump.wav";
	sound.soundEffectLength = 0.7f;
	sound.playOnce = true;
	sound.positionalOffset = { 0.0f, 0.0f, 0.0f };
	player->getComponent<AudioComponent>()->defineSound(Audio::SoundType::JUMP, sound);

	sound.fileName = "../Audio/watergun_start.wav";
	sound.soundEffectLength = 0.578f;
	sound.playOnce = true;
	sound.positionalOffset = { 0.5f, -0.5f, 0.0f };
	player->getComponent<AudioComponent>()->defineSound(Audio::SoundType::SHOOT_START, sound);

	sound.fileName = "../Audio/watergun_loop.wav";
	sound.soundEffectLength = 1.4f;
	sound.playOnce = false;
	sound.positionalOffset = { 0.5f, -0.5f, 0.0f };
	player->getComponent<AudioComponent>()->defineSound(Audio::SoundType::SHOOT_LOOP, sound);

	sound.fileName = "../Audio/watergun_end.wav";
	sound.soundEffectLength = 0.722f;
	sound.playOnce = true;
	sound.positionalOffset = { 0.5f, -0.5f, 0.0f };
	player->getComponent<AudioComponent>()->defineSound(Audio::SoundType::SHOOT_END, sound);

	sound.fileName = "../Audio/water_drip_1.wav";
	sound.playOnce = true;
	sound.positionalOffset = { 0.0f, 0.0f, 0.0f };
	player->getComponent<AudioComponent>()->defineSound(Audio::SoundType::WATER_IMPACT_LEVEL, sound);

	sound.fileName = "../Audio/water_impact_enemy.wav";
	sound.playOnce = true;
	sound.positionalOffset = { 0.0f, 0.0f, 0.0f };
	player->getComponent<AudioComponent>()->defineSound(Audio::SoundType::WATER_IMPACT_ENEMY, sound);

	sound.fileName = "../Audio/water_impact_my_candle.wav";
	sound.playOnce = true;
	sound.positionalOffset = { 0.0f, 0.0f, 0.0f };
	player->getComponent<AudioComponent>()->defineSound(Audio::SoundType::WATER_IMPACT_MY_CANDLE, sound);

#pragma endregion

	return player;
}




Entity::SPtr EntityFactory::CreateBot(Model* boundingBoxModel, Model* characterModel, const glm::vec3& pos, Model* lightModel, size_t lightIndex, NodeSystem* ns) {

	auto e = ECS::Instance()->createEntity("AiCharacter");
	e->addComponent<ModelComponent>(characterModel);
	e->addComponent<TransformComponent>(pos);
	e->addComponent<BoundingBoxComponent>(boundingBoxModel)->getBoundingBox()->setHalfSize(glm::vec3(0.7f, .9f, 0.7f));
	e->addComponent<CollidableComponent>();
	e->addComponent<MovementComponent>();
	e->addComponent<SpeedLimitComponent>();
	e->addComponent<CollisionComponent>();
	e->addComponent<AiComponent>();
	e->addComponent<CullingComponent>();

	e->addComponent<AudioComponent>();

	// Placeholder sound effect for bots
	Audio::SoundInfo sound{};
	sound.fileName = "../Audio/guitar.wav";
	sound.soundEffectLength = 104.0f;
	sound.volume = 1.0f;
	sound.playOnce = false;
	sound.positionalOffset = { 0.f, 1.2f, 0.f };
	sound.isPlaying = true; // Start playing the sound immediately

	e->getComponent<AudioComponent>()->defineSound(Audio::SoundType::AMBIENT, sound);

	e->getComponent<MovementComponent>()->constantAcceleration = glm::vec3(0.0f, -9.8f, 0.0f);
	e->getComponent<SpeedLimitComponent>()->maxSpeed = 3.0f;

	e->addComponent<GunComponent>();
	auto aiCandleEntity = EntityFactory::CreateCandle("AiCandle", glm::vec3(0.f, 2.f, 0.f), lightIndex);

	e->addChildEntity(aiCandleEntity);
	auto fsmComp = e->addComponent<FSMComponent>();

	// =========Create states and transitions===========

	SearchingState* searchState = fsmComp->createState<SearchingState>(ns);
	AttackingState* attackState = fsmComp->createState<AttackingState>();
	fsmComp->createState<FleeingState>(ns);
	
	// TODO: unnecessary to create new transitions for each FSM if they're all identical
	//Attack State
	FSM::Transition* attackToFleeing = SAIL_NEW FSM::Transition;
	attackToFleeing->addBoolCheck(aiCandleEntity->getComponent<CandleComponent>()->getPtrToIsLit(), false);
	FSM::Transition* attackToSearch = SAIL_NEW FSM::Transition;
	attackToSearch->addFloatGreaterThanCheck(attackState->getDistToHost(), 100.0f);
	
	// Search State
	FSM::Transition* searchToAttack = SAIL_NEW FSM::Transition;
	searchToAttack->addFloatLessThanCheck(searchState->getDistToHost(), 100.0f);
	FSM::Transition* searchToFleeing = SAIL_NEW FSM::Transition;
	searchToFleeing->addBoolCheck(aiCandleEntity->getComponent<CandleComponent>()->getPtrToIsLit(), false);
	
	// Fleeing State
	FSM::Transition* fleeingToSearch = SAIL_NEW FSM::Transition;
	fleeingToSearch->addBoolCheck(aiCandleEntity->getComponent<CandleComponent>()->getPtrToIsLit(), true);
	
	fsmComp->addTransition<AttackingState, FleeingState>(attackToFleeing);
	fsmComp->addTransition<AttackingState, SearchingState>(attackToSearch);
	
	fsmComp->addTransition<SearchingState, AttackingState>(searchToAttack);
	fsmComp->addTransition<SearchingState, FleeingState>(searchToFleeing);
	
	fsmComp->addTransition<FleeingState, SearchingState>(fleeingToSearch);
	// =========[END] Create states and transitions===========


	return e;
}

Entity::SPtr EntityFactory::CreateStaticMapObject(const std::string& name, Model* model, Model* boundingBoxModel, const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale) {
	auto e = ECS::Instance()->createEntity(name);
	e->addComponent<ModelComponent>(model);
	e->addComponent<TransformComponent>(pos, rot, scale);
	e->addComponent<BoundingBoxComponent>(boundingBoxModel);
	e->addComponent<CollidableComponent>();
	e->addComponent<CullingComponent>();

	return e;
}

Entity::SPtr EntityFactory::CreateProjectile(const glm::vec3& pos, const glm::vec3& velocity, bool hasLocalOwner, Netcode::ComponentID ownersNetId, float lifetime, float randomSpread) {
	auto e = ECS::Instance()->createEntity("projectile");
	glm::vec3 randPos;

	randPos.r = Utils::rnd() * randomSpread;
	randPos.g = Utils::rnd() * randomSpread;
	randPos.b = Utils::rnd() * randomSpread;

	e->addComponent<MetaballComponent>();
	e->addComponent<BoundingBoxComponent>()->getBoundingBox()->setHalfSize(glm::vec3(0.15, 0.15, 0.15));
	e->addComponent<LifeTimeComponent>(lifetime);
	e->addComponent<ProjectileComponent>(10.0f, hasLocalOwner); // TO DO should not be manually set to true
	e->getComponent<ProjectileComponent>()->ownedBy = ownersNetId;
	e->addComponent<TransformComponent>(pos + randPos);
	if (hasLocalOwner == true) {
		e->addComponent<LocalOwnerComponent>(ownersNetId);
	}
	else {
		e->addComponent<OnlineOwnerComponent>(ownersNetId);
	}
	

	MovementComponent* movement = e->addComponent<MovementComponent>();
	movement->velocity = velocity;
	movement->constantAcceleration = glm::vec3(0.f, -9.8f, 0.f);

	CollisionComponent* collision = e->addComponent<CollisionComponent>();
	collision->drag = 2.0f;
	// NOTE: 0.0f <= Bounciness <= 1.0f
	collision->bounciness = 0.1f;
	collision->padding = 0.2f;

	return e;
}
