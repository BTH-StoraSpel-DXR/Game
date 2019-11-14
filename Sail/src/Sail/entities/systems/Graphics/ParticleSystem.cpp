#include "pch.h"
#include "ParticleSystem.h"
#include "Sail/Application.h"
#include "Sail/entities/components/Components.h"
#include "Sail/entities/Entity.h"
#include "API/DX12/DX12VertexBuffer.h"
#include "API/DX12/shader/DX12ConstantBuffer.h"
#include "API/DX12/shader/DX12StructuredBuffer.h"
#include "API/DX12/resources/DescriptorHeap.h"
#include "API/DX12/DX12Utils.h"
#include "Sail/utils/Timer.h"

#include "Sail/graphics/shader/compute/ParticleComputeShader.h"
#include "Sail/graphics/shader/dxr/GBufferOutShader.h"

ParticleSystem::ParticleSystem() {
	registerComponent<ParticleEmitterComponent>(true, true, true);
	registerComponent<TransformComponent>(false, true, false);
}

ParticleSystem::~ParticleSystem() {
}

void ParticleSystem::update(float dt) {
	for (auto& e : entities) {
		auto* partComponent = e->getComponent<ParticleEmitterComponent>();

		// Place emitter at entities transform
		if (e->hasComponent<TransformComponent>()) {
			//partComponent->position = e->getComponent<TransformComponent>()->getMatrixWithoutUpdate()[3];
			partComponent->position = e->getComponent<TransformComponent>()->getMatrixWithoutUpdate() * glm::vec4(partComponent->offset, 1.f);
		}
		partComponent->updateTimers(dt);
	}
}

void ParticleSystem::updateOnGPU(ID3D12GraphicsCommandList4* cmdList, const glm::vec3& cameraPos) {
	for (auto& e : entities) {
		e->getComponent<ParticleEmitterComponent>()->updateOnGPU(cmdList, cameraPos);
	}
}

void ParticleSystem::submitAll() const {
	for (auto& e : entities) {
		e->getComponent<ParticleEmitterComponent>()->submit();
	}
}
