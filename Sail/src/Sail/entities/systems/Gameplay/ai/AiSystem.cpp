#include "pch.h"
#include "AiSystem.h"
#include "../../../components/AiComponent.h"
#include "../../../components/TransformComponent.h"
#include "../../../components/FSMComponent.h"
#include "../../../components/MovementComponent.h"
#include "../../../components/SpeedLimitComponent.h"
#include "Sail/ai/pathfinding/NodeSystem.h"
#include "Sail/entities/components/MapComponent.h"

#include "../../../ECS.h"
#include "../../../components/BoundingBoxComponent.h"
#include "../../physics/UpdateBoundingBoxSystem.h"
#include "../../physics/OctreeAddRemoverSystem.h"
#include "Sail/utils/Utils.h"
#include "../../Physics/Octree.h"
#include "Sail/Application.h"
#include "../../Physics/Intersection.h"
#include "../../Physics/Physics.h"

#include "Sail/graphics/geometry/factory/CubeModel.h"
#include "Sail/graphics/shader/dxr/GBufferOutShader.h"
#include "Sail/graphics/shader/dxr/GBufferWireframe.h"

#include "Sail/utils/Storage/SettingStorage.h"

#include <glm/gtx/vector_angle.hpp>

#ifdef _DEBUG_NODESYSTEM
#include "Sail/entities/components/ModelComponent.h"
#endif


AiSystem::AiSystem() {
	registerComponent<TransformComponent>(true, true, true);
	registerComponent<MovementComponent>(true, true, true);
	registerComponent<SpeedLimitComponent>(true, true, true);
	registerComponent<AiComponent>(true, true, true);
	registerComponent<FSMComponent>(true, true, true);

	m_nodeSystem = std::make_unique<NodeSystem>();

	m_timeBetweenPathUpdate = 3.f;
}

AiSystem::~AiSystem() {}

void AiSystem::initNodeSystem(Octree* octree) {
#ifdef _DEBUG_NODESYSTEM
	m_nodeSystem->setDebugModelAndScene(&Application::getInstance()->getResourceManager().getShaderSet<GBufferWireframe>());
#endif
	m_octree = octree;
	auto nodeSystemCube = ModelFactory::CubeModel::Create(glm::vec3(0.1f), &Application::getInstance()->getResourceManager().getShaderSet<GBufferOutShader>());
	float sizeX = Application::getInstance()->getSettings().gameSettingsDynamic["map"]["sizeX"].value;
	float sizeZ = Application::getInstance()->getSettings().gameSettingsDynamic["map"]["sizeY"].value;
	float tileSize = Application::getInstance()->getSettings().gameSettingsDynamic["map"]["tileSize"].value;
	float realXMax = sizeX * tileSize;
	float realZMax = sizeZ * tileSize;
	float nodeSize = 0.5f;
	float nodePadding = tileSize / 2.f;
	int xMax = static_cast<int>(std::ceil(realXMax / (nodePadding))) + 1;
	int zMax = static_cast<int>(std::ceil(realZMax / (nodePadding))) + 1;
	int size = xMax * zMax;

	int currX = 0;
	int currZ = 0;

	// Currently needed cause map doesn't start creation from (0,0)
	float startOffsetX = -tileSize / 2.f;
	float startOffsetZ = -tileSize / 2.f;
	float startOffsetY = 0.f;
	//bool* walkable = SAIL_NEW bool[size];

	// Entity used for collision checking
	auto e = ECS::Instance()->createEntity("DeleteMeFirstFrameDummy");
	float collisionBoxHeight = 0.9f;
	float collisionBoxHalfHeight = collisionBoxHeight / 2.f;
	//e->addComponent<BoundingBoxComponent>(bbModel)->getBoundingBox()->setHalfSize(glm::vec3(0.7f, collisionBoxHalfHeight, 0.7f));
	e->addComponent<BoundingBoxComponent>(nodeSystemCube.get())->getBoundingBox()->setHalfSize(glm::vec3(nodeSize / 2.f, collisionBoxHalfHeight, nodeSize / 2.f));


	/*Nodesystem*/
	ECS::Instance()->getSystem<UpdateBoundingBoxSystem>()->update(0.f);
	ECS::Instance()->getSystem<OctreeAddRemoverSystem>()->update(0.f);


	std::vector<NodeSystem::Node> nodes;
	std::vector<std::vector<unsigned int>> connections;
	std::vector<unsigned int> conns;
	for (int i = 0; i < size; i++) {
		conns.clear();

		/*
			Node position
		*/
		currX = i % xMax;
		currZ = static_cast<int>(floor(i / xMax));

		glm::vec3 nodePos = getNodePos(currX, currZ, nodeSize, nodePadding, startOffsetX, startOffsetZ);

		/*
			Is there floor here?
		*/
		bool blocked = false;
		Octree::RayIntersectionInfo tempInfo;
		glm::vec3 down(0.f, -1.f, 0.f);
		m_octree->getRayIntersection(glm::vec3(nodePos.x + 0.01f, nodePos.y + collisionBoxHalfHeight, nodePos.z), down, &tempInfo, e.get(), 0.1f);
		if (tempInfo.closestHitIndex != -1) {
			float floorCheckVal = glm::angle(tempInfo.info[tempInfo.closestHitIndex].shape->getNormal(), -down);
			// If there's a low angle between the up-vector and the normal of the surface, it can be counted as floor
			bool isFloor = (floorCheckVal < 0.1f) ? true : false;
			if (!isFloor) {
				blocked = true;
			} else {
				// Update the height of the node position
				nodePos.y = nodePos.y + (collisionBoxHalfHeight - tempInfo.closestHit);
			}
		} else {
			blocked = true;
		}

		/*
			Is node blocked
		*/
		glm::vec3 bbPos = nodePos;
		//bbPos.x -= nodeSize;
		bbPos.y += collisionBoxHalfHeight + 0.1f; // Plus a little offset to avoid the floor
		//bbPos.z -= nodeSize;
		e->getComponent<BoundingBoxComponent>()->getBoundingBox()->setPosition(bbPos);
		std::vector < Octree::CollisionInfo> vec;
		m_octree->getCollisions(e.get(), e->getComponent<BoundingBoxComponent>()->getBoundingBox(), &vec, true, true);

		for (Octree::CollisionInfo& info : vec) {
			int j = (info.entity->getName().compare("Map_") || info.entity->getName().compare("Clu"));
			if (j >= 0) {
				//Not walkable
				blocked = true;
				break;
			}
		}

		/*
			Node connections
		*/
		if (!blocked) {
			// Each node currently only have 4 connections
			for (int x = currX - 1; x < currX + 2; x++) {
				for (int z = currZ - 1; z < currZ + 2; z++) {
					if (x == currX && z == currZ) {
						continue;
					}

					if (x > -1 && x < xMax && z > -1 && z < zMax) {
						// Doesn't work with backfaces :( - but its kinda fine cause no "truly walkable" nodes has a connection to the
						// in-wall nodes, only the other way round
						bool doConnect = nodeConnectionCheck(nodePos,
															 getNodePos(x, z, nodeSize, nodePadding, startOffsetX, startOffsetZ));						
						if (doConnect) {
							// get index
							int index = z * xMax + x;
							conns.push_back(index);
						}
					}
				}
			}
		}

		nodes.emplace_back(nodePos, blocked, i);
		connections.push_back(conns);
	}

	std::vector<unsigned int> toRemove;
	for (auto& n : connections) {
		toRemove.clear();
		for (int c = 0; c < n.size(); c++) {
			if (nodes[n[c]].blocked) {
				toRemove.emplace_back(c);
			}
		}
		auto it = n.begin();
		for (int i = toRemove.size() - 1; i > -1; i--) {
			n.erase(it + toRemove[i]);
		}
	}

	e->queueDestruction();

	m_nodeSystem->setNodes(nodes, connections, xMax, zMax);
}

std::vector<Entity*>& AiSystem::getEntities() {
	return entities;
}

void AiSystem::update(float dt) {
	//std::vector<std::future<void>> futures;
	auto start = std::chrono::high_resolution_clock::now();
	for ( auto& entity : entities ) {
		// Might be dangerous for threads
		//futures.push_back(Application::getInstance()->pushJobToThreadPool([this, entity, dt] (int id) { this->aiUpdateFunc(entity, dt); }));
		aiUpdateFunc(entity, dt);
	}
	m_updateTimes[m_currUpdateTimeIndex % NUM_UPDATE_TIMES] = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count());
	m_currUpdateTimeIndex++;
	/*for ( auto& a : futures ) {
		a.get();
	}*/
}

NodeSystem* AiSystem::getNodeSystem() {
	return m_nodeSystem.get();
}

void AiSystem::stop() {
	m_nodeSystem->stop();
}

#ifdef DEVELOPMENT
unsigned int AiSystem::getByteSize() const {
	unsigned int size = sizeof(*this);
	size += m_nodeSystem->getByteSize();
	return size;
}

const float AiSystem::getAveragePathSearchTime() const {
	return m_nodeSystem->getAverageSearchTime();
}
const float AiSystem::getAverageAiUpdateTime() const {
	float updateTime = 0.f;
	for (unsigned int i = 0; i < std::min(m_currUpdateTimeIndex, NUM_UPDATE_TIMES); i++) {
		updateTime += m_updateTimes[i];
	}
	return updateTime / static_cast<float>(std::min(m_currUpdateTimeIndex, NUM_UPDATE_TIMES));
}
#endif

void AiSystem::aiUpdateFunc(Entity* e, const float dt) {
	e->getComponent<FSMComponent>()->update(dt, e);
	
	AiComponent* ai = e->getComponent<AiComponent>();

	if ( ai->timeTakenOnPath > m_timeBetweenPathUpdate && ai->doWalk ) {
		ai->updatePath = true;
	}

	updatePath(e);
	updatePhysics(e, dt);
}

glm::vec3 AiSystem::getDesiredDir(AiComponent* aiComp, TransformComponent* transComp) {
	glm::vec3 desiredDir = aiComp->currPath[aiComp->currNodeIndex].position - transComp->getTranslation();
	if ( desiredDir == glm::vec3(0.f) ) {
		desiredDir = glm::vec3(1.0f, 0.f, 0.f);
	}
	desiredDir = glm::normalize(desiredDir);
	return desiredDir; // TODO: Check this - should probably not return a reference??
}

bool AiSystem::nodeConnectionCheck(glm::vec3 nodePos, glm::vec3 otherNodePos) {
	// Setup
	float dst = glm::distance(nodePos, otherNodePos);
	glm::vec3 dir = glm::normalize(otherNodePos - nodePos);

	// Intersection check
	Octree::RayIntersectionInfo tempInfo;
	m_octree->getRayIntersection(glm::vec3(nodePos.x, nodePos.y + 0.5f, nodePos.z), dir, &tempInfo);

	// Nothing between the two nodes
	if (tempInfo.closestHit > dst || tempInfo.closestHit < 0.0f) {
		return true;
	}

	// The nodes shouldn't be connected
	return false;
}

glm::vec3 AiSystem::getNodePos(const int x, const int z, float nodeSize, float nodePadding, float startOffsetX, float startOffsetZ) {
	float xPos = static_cast<float>(x) * (nodePadding) + startOffsetX;
	float zPos = static_cast<float>(z) * (nodePadding) + startOffsetZ;
	return glm::vec3(xPos, 0.f, zPos);
}

void AiSystem::updatePath(Entity* e) {
	AiComponent* ai = e->getComponent<AiComponent>();
	TransformComponent* transform = e->getComponent<TransformComponent>();
	if (ai->updatePath ) {
#ifdef _DEBUG_NODESYSTEM
		for (int i = 0; i < ai->currPath.size(); i++) {
			m_nodeSystem->getNodeEntities()[ai->currPath[i].index]->getComponent<ModelComponent>()->getModel()->getMesh(0)->getMaterial()->setColor(glm::vec4(0.f, 1.f, 0.f, 1.f));
		}
#endif
		ai->timeTakenOnPath = 0.f;
		ai->reachedPathingTarget = false;

		// ai->posTarget is updated in each FSM state
		ai->lastTargetPos = ai->posTarget;

		auto tempPath = m_nodeSystem->getPath(transform->getTranslation(), ai->posTarget);

		ai->currNodeIndex = 0;

		// Fix problem of always going toward closest node
		if ( tempPath.size() > 1 && glm::distance(tempPath[1].position, transform->getTranslation()) < glm::distance(tempPath[1].position, tempPath[0].position) ) {
			ai->currNodeIndex += 1;
		}

		ai->currPath = tempPath;

#ifdef _DEBUG_NODESYSTEM
		SAIL_LOG("Currpath size: " + std::to_string(ai->currPath.size()));
		std::string daPath = "Path: {";
		for (int i = 0; i < ai->currPath.size(); i++) {
			m_nodeSystem->getNodeEntities()[ai->currPath[i].index]->getComponent<ModelComponent>()->getModel()->getMesh(0)->getMaterial()->setColor(glm::vec4(0.f, 0.f, 1.f, 1.f));
			daPath += std::to_string(ai->currPath[i].index) + ", ";
		}
		daPath += "}";
		SAIL_LOG(daPath);
#endif

		ai->updatePath = false;
	}
}

void AiSystem::updatePhysics(Entity* e, float dt) {
	AiComponent* ai = e->getComponent<AiComponent>();
	MovementComponent* movement = e->getComponent<MovementComponent>();
	TransformComponent* transform = e->getComponent<TransformComponent>();
	SpeedLimitComponent* speedLimit = e->getComponent<SpeedLimitComponent>();

	// Check if there is a path currently active and if the ai should be walking
	if ( ai->currPath.size() > 0 && ai->doWalk ) {
		// Check if the ai hasn't reached the current pathing target yet
		if ( !ai->reachedPathingTarget ) {
			ai->timeTakenOnPath += dt;

			// Check if the distance between current node target and ai is low enough to begin targeting next node
			if ( glm::distance(transform->getTranslation(), ai->currPath[ai->currNodeIndex].position) < ai->targetReachedThreshold ) {
				ai->lastVisitedNode = ai->currPath[ai->currNodeIndex];
				ai->reachedPathingTarget = true;
			// Else continue walking
			} else {
				float acceleration = 70.0f - ( glm::length(movement->velocity) / speedLimit->maxSpeed ) * 20.0f;
				movement->accelerationToAdd = getDesiredDir(ai, transform) * acceleration;
			}
		// Else increment the current node path
		} else if (ai->currPath.size() > 0 ) {
			// Update next node target
			if (ai->currNodeIndex < ai->currPath.size() - 1 ) {
#ifdef _DEBUG_NODESYSTEM
				m_nodeSystem->getNodeEntities()[ai->currNodeIndex]->getComponent<ModelComponent>()->getModel()->getMesh(0)->getMaterial()->setColor(glm::vec4(0.f, 1.f, 0.f, 1.f));
#endif
				ai->currNodeIndex++;
			}
			ai->reachedPathingTarget = false;
		} else {
			ai->currPath.clear();
		}

		float newYaw = getAiYaw(movement, transform->getRotations().y, dt);
		transform->setRotations(0.f, newYaw, 0.f);

	// If the ai shouldn't walk, just stop walking
	} else {
		// Set velocity to 0
		movement->velocity = glm::vec3(0.f, movement->velocity.y, 0.f);
		ai->timeTakenOnPath = 3.f;
	}
}

float AiSystem::getAiYaw(MovementComponent* moveComp, float currYaw, float dt) {
	float newYaw = currYaw;
	if ( glm::length2(moveComp->velocity) > 0.f ) {
		float desiredYaw = 0.f;
		float turnRate = glm::pi<float>();//glm::two_pi<float>() / 2.f; // 2 pi
		auto normalizedVel = glm::normalize(moveComp->velocity);
		float moveCompX = normalizedVel.x;
		float moveCompZ = normalizedVel.z;
		moveCompZ = moveCompZ != 0 ? moveCompZ : 0.1f;
		if (moveCompZ < 0.f/* || moveCompX < 0.f*/) {
			desiredYaw = glm::atan(moveCompX / moveCompZ) - glm::pi<float>();// + 0.7854f;
		} else {
			desiredYaw = glm::atan(moveCompX / moveCompZ);// - glm::pi<float>();// - 0.7854f;
		}
		desiredYaw = Utils::wrapValue(desiredYaw, 0.f, glm::two_pi<float>());
		float diff = desiredYaw - currYaw;

		if ( std::abs(diff) > glm::pi<float>() ) {
			diff = currYaw - desiredYaw;
		}

		float toTurn = 0.f;
		if ( diff > 0 ) {
			toTurn = turnRate * dt;
		} else if ( diff < 0 ) {
			toTurn = -turnRate * dt;
		}

		if ( std::abs(diff) > std::abs(toTurn) ) {
			newYaw = Utils::wrapValue(currYaw + toTurn, 0.f, glm::two_pi<float>());
		} else {
			newYaw = Utils::wrapValue(currYaw + diff, 0.f, glm::two_pi<float>());
		}

	}
	return newYaw;
}
