#include "pch.h"
#include "CollisionSystem.h"
#include "..//..//components/TransformComponent.h"
#include "..//..//components/MovementComponent.h"
#include "..//..//components/CollisionComponent.h"
#include "..//..//components/BoundingBoxComponent.h"
#include "..//..//components/CollisionSpheresComponent.h"
#include "..//..//Physics/Intersection.h"

CollisionSystem::CollisionSystem() {
	registerComponent<MovementComponent>(true, true, true);
	registerComponent<TransformComponent>(true, true, true);
	registerComponent<CollisionComponent>(true, true, true);
	registerComponent<BoundingBoxComponent>(true, true, true);

	m_octree = nullptr;
}

CollisionSystem::~CollisionSystem() {
}

void CollisionSystem::provideOctree(Octree* octree) {
	m_octree = octree;
}

void CollisionSystem::update(float dt) {
	for (auto& e: entities) {
		auto movement = e->getComponent<MovementComponent>();
		auto collision = e->getComponent<CollisionComponent>();
		auto boundingBox = e->getComponent<BoundingBoxComponent>();
		auto csc = e->getComponent<CollisionSpheresComponent>();

		collision->collisions.clear();

		if (collision->padding < 0.0f) {
			collision->padding = glm::length(boundingBox->getBoundingBox()->getHalfSize());
		}

		float updateableDt = dt;

		if ((boundingBox || csc) && m_octree) {
			collisionUpdate(e, updateableDt);

			surfaceFromCollision(e);

			if (boundingBox && rayCastCheck(e, *boundingBox->getBoundingBox(), updateableDt)) {
				//Object is moving fast, ray cast for collisions
				rayCastUpdate(e, *boundingBox->getBoundingBox(), updateableDt);
				movement->oldVelocity = movement->velocity;
			}
		}

		movement->updateableDt = updateableDt;
	}
}

const bool CollisionSystem::collisionUpdate(Entity* e, const float& dt) {
	//Update collision data
	std::vector<Octree::CollisionInfo> collisions;

	bool hasSpheres = e->hasComponent<CollisionSpheresComponent>();
	if (hasSpheres) {
		m_octree->getCollisionsSpheres(e, &collisions);
	}
	else {
		m_octree->getCollisions(e, &collisions);
	}

	return handleCollisions(e, collisions, dt);
}

const bool CollisionSystem::handleCollisions(Entity* e, const std::vector<Octree::CollisionInfo>& collisions, const float& dt) {
	bool returnValue = false;

	MovementComponent* movement = e->getComponent<MovementComponent>();
	CollisionComponent* collision = e->getComponent<CollisionComponent>();

	collision->onGround = false;
	std::vector<int> groundIndices;

	if (collisions.size() > 0) {
		//Get the combined normals
		glm::vec3 sumVec(0.0f);
		for (unsigned int i = 0; i < collisions.size(); i++) {
			sumVec += collisions[i].normal;

			//Add collision to current collisions
			collision->collisions.push_back(collisions[i]);
		}

		for (unsigned int i = 0; i < collisions.size(); i++) {
			if (collisions[i].normal.y > 0.7f) {
				collision->onGround = true;
				bool newGround = true;
				for (unsigned int j = 0; j < groundIndices.size(); j++) {
					if (collisions[i].normal == collisions[groundIndices[j]].normal) {
						newGround = false;
					}
				}
				if (newGround) {
					//Save collision for friction calculation
					groundIndices.push_back(i);
				}
			}

			//Stop movement towards triangle
			float projectionSize = glm::dot(movement->velocity, -collisions[i].normal);

			if (projectionSize > 0.0f) { //Is pushing against wall
				returnValue = true;
				movement->velocity += collisions[i].normal * projectionSize * (1.0f + collision->bounciness); //Limit movement towards wall
			}

			//Tight angle corner special case
			float dotProduct = glm::dot(collisions[i].normal, glm::normalize(sumVec));
			if (dotProduct < 0.7072f && dotProduct > 0.0f) { //Colliding in a tight angle corner
				glm::vec3 normalToNormal = sumVec - glm::dot(sumVec, collisions[i].normal) * collisions[i].normal;
				normalToNormal = glm::normalize(normalToNormal);

				//Stop movement towards corner
				projectionSize = glm::dot(movement->velocity, -normalToNormal);

				if (projectionSize > 0.0f) {
					returnValue = true;
					movement->velocity += normalToNormal * projectionSize * (1.0f + collision->bounciness);
				}
			}
		}
	}
	//------------------

	//----Drag----
	if (collision->onGround) { //Ground drag
		unsigned int nrOfGroundCollisions = groundIndices.size();
		for (unsigned int i = 0; i < nrOfGroundCollisions; i++) {
			glm::vec3 velAlongPlane = movement->velocity - collisions[groundIndices[i]].normal * glm::dot(collisions[groundIndices[i]].normal, movement->velocity);
			float sizeOfVel = glm::length(velAlongPlane);
			if (sizeOfVel > 0.0f) {
				float slowdown = glm::min((collision->drag / nrOfGroundCollisions) * dt, sizeOfVel);
				movement->velocity -= slowdown * glm::normalize(velAlongPlane);
				returnValue = true;
			}
		}
	}
	//------------

	return returnValue;
}

const bool CollisionSystem::rayCastCheck(Entity* e, const BoundingBox& boundingBox, float& dt) const {
	MovementComponent* movementComp = e->getComponent<MovementComponent>();
	
	if (glm::abs(movementComp->velocity.x * dt) > glm::abs(boundingBox.getHalfSize().x)
		|| glm::abs(movementComp->velocity.y * dt) > glm::abs(boundingBox.getHalfSize().y)
		|| glm::abs(movementComp->velocity.z * dt) > glm::abs(boundingBox.getHalfSize().z)) {
		//Object is moving at a speed that risks missing collisions
		return true;
	}
	return false;
}

void CollisionSystem::rayCastUpdate(Entity* e, BoundingBox& boundingBox, float& dt) {
	
	MovementComponent* movement = e->getComponent<MovementComponent>();
	TransformComponent* transform = e->getComponent<TransformComponent>();
	CollisionComponent* collision = e->getComponent<CollisionComponent>();
	
	float velocityAmp = glm::length(movement->velocity) * dt;

	//Ray cast to find upcoming collisions, use padding for "swept sphere"
	Octree::RayIntersectionInfo intersectionInfo;
	m_octree->getRayIntersection(boundingBox.getPosition(), movement->velocity, &intersectionInfo, e, collision->padding);

	if (intersectionInfo.closestHit <= velocityAmp && intersectionInfo.closestHit >= 0.0f) { //Found upcoming collision
		//Calculate new dt
		float newDt = ((intersectionInfo.closestHit) / velocityAmp) * dt;

		//Move untill first overlap
		boundingBox.setPosition(boundingBox.getPosition() + movement->velocity * newDt);
		transform->translate(movement->velocity * newDt);

		dt -= newDt;

		//Collision update
		bool paddingTooBig = true;

		for (unsigned int i = 0; i < intersectionInfo.info.size(); i++) {
			if (Intersection::AabbWithTriangle(boundingBox, intersectionInfo.info[i].positions[0], intersectionInfo.info[i].positions[1], intersectionInfo.info[i].positions[2])) {
				collision->collisions.push_back(intersectionInfo.info[i]);

				//Stop movement towards triangle
				float projectionSize = glm::dot(movement->velocity, -intersectionInfo.info[i].normal);

				if (projectionSize > 0.0f) { //Is pushing against wall
					movement->velocity += intersectionInfo.info[i].normal * projectionSize * (1.0f + collision->bounciness); //Limit movement towards wall
					paddingTooBig = false;
				}
			}
		}

		if (paddingTooBig) {
			collision->padding *= 0.5f;
		}

		rayCastUpdate(e, boundingBox, dt);
	}
}

void CollisionSystem::surfaceFromCollision(Entity* e) {
	glm::vec3 distance(0.0f);
	auto& collisions = e->getComponent<CollisionComponent>()->collisions;
	auto bb = e->getComponent<BoundingBoxComponent>();
	auto cs = e->getComponent<CollisionSpheresComponent>();
	auto movement = e->getComponent<MovementComponent>();
	auto transform = e->getComponent<TransformComponent>();

	for (unsigned int i = 0; i < collisions.size(); i++) {
		float depth = findIntersectionDepth(e, collisions[i]);

		if (depth <= glm::dot(movement->oldVelocity, -collisions[i].normal)) {
			if (cs) {
				for (int j = 0; j < 2; j++) {
					cs->spheres[j].position += collisions[i].normal * depth;
				}
			}
			else {
				bb->getBoundingBox()->setPosition(bb->getBoundingBox()->getPosition() + collisions[i].normal * depth);
			}
			distance += collisions[i].normal * depth;
		}
	}

	transform->translate(distance);
}

float CollisionSystem::findIntersectionDepth(Entity* e, const Octree::CollisionInfo& collision) {
	float stepSize = 0.005f;
	float depth = 0.0f;
	auto csc = e->getComponent<CollisionSpheresComponent>();
	auto bbc = e->getComponent<BoundingBoxComponent>();

	//Find approx how far into the wall the character is
	bool colliding = true;
	while (colliding) {
		//Push the character out of the wall
		if (csc) {
			for (int j = 0; j < 2; j++) {
				csc->spheres[j].position += collision.normal * stepSize;
			}
		}
		else {
			bbc->getBoundingBox()->setPosition(bbc->getBoundingBox()->getPosition() + collision.normal * stepSize);
		}

		//Check if there is still collision
		bool currentCollision = false;

		if (csc) {
			const glm::vec3 tri[] = {
				collision.positions[0], collision.positions[1], collision.positions[2]
			};
			currentCollision = Intersection::TriangleWithSphere(tri, csc->spheres[0]) || Intersection::TriangleWithSphere(tri, csc->spheres[1]);
		}
		else {
			currentCollision = Intersection::AabbWithTriangle(*bbc->getBoundingBox(), collision.positions[0], collision.positions[1], collision.positions[2]);
		}

		if (!currentCollision) {
			colliding = false;

			//Go back to start position when depth is found
			if (csc) {
				for (int j = 0; j < 2; j++) {
					csc->spheres[j].position -= collision.normal * depth;
				}
			}
			else {
				bbc->getBoundingBox()->setPosition(bbc->getBoundingBox()->getPosition() - collision.normal * depth);
			}
		}
		else {
			depth += stepSize;
		}
	}

	return depth;
}