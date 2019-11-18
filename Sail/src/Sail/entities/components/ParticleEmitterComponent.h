#pragma once

#include "Component.h"

#include <glm/glm.hpp>

#include "Sail/api/ComputeShaderDispatcher.h"
#include "API/DX12/DX12API.h"
#include "Sail/utils/Timer.h"

class DX12VertexBuffer;
class ParticleComputeShader;
class ID3D12GraphicsCommandList4;

class ParticleEmitterComponent : public Component<ParticleEmitterComponent> {
public:
	ParticleEmitterComponent();
	~ParticleEmitterComponent();

	float size;
	glm::vec3 offset;
	glm::vec3 position;
	glm::vec3 spread;
	glm::vec3 constantVelocity;
	glm::vec3 velocity;
	glm::vec3 acceleration;
	float lifeTime;
	float spawnRate;
	float spawnTimer;

#ifdef DEVELOPMENT
	const unsigned int getByteSize() const override {
		/* TODO: Fix component size */
		unsigned int size = sizeof(*this);
		size += sizeof(CPUOutput);
		size += sizeof(NewParticleInfo) * m_cpuOutput->newParticles.size();
		size += sizeof(unsigned int) * m_cpuOutput->toRemove.size();
		size += sizeof(float) * m_particleLife->size();
		size += m_model->getByteSize();
		return size;
	}
#endif

private:
	void init();

	void syncWithGPUUpdate(unsigned int swapBufferIndex);

public:
	void spawnParticles(int particlesToSpawn);

	void updateTimers(float dt);
	void updateOnGPU(ID3D12GraphicsCommandList4* cmdList, const glm::vec3& cameraPos);

	void submit() const;

	void setTexture(std::string textureName);

private:
	std::unique_ptr<ComputeShaderDispatcher> m_dispatcher;
	std::unique_ptr < ParticleComputeShader> m_particleShader;

	DX12VertexBuffer* m_outputVertexBuffer;
	unsigned int m_outputVertexBufferSize;

	wComPtr<ID3D12Resource>* m_physicsBufferDefaultHeap;
	int m_particlePhysicsSize;

	std::unique_ptr<Model> m_model;

	Timer m_timer;
	INT64 m_startTime;

	int m_gpuUpdates;

	std::vector<float>* m_particleLife;


	struct NewParticleInfo {
		glm::vec3 pos;
		glm::vec3 spread;
		float spawnTime;
	};

	struct CPUOutput {
		std::vector<NewParticleInfo> newParticles;
		std::vector<unsigned int> toRemove;
		unsigned int previousNrOfParticles;
		float lastFrameTime;
	};

	CPUOutput* m_cpuOutput;

	struct ParticleData {
		glm::vec3 position;
		float padding0;
		glm::vec3 velocity;
		float padding1;
		glm::vec3 acceleration;
		float spawnTime;
	};

	static const unsigned int m_maxParticlesSpawnPerFrame = 312;

	struct ComputeInput {
		ParticleData particles[m_maxParticlesSpawnPerFrame];
		unsigned int particlesToRemove[m_maxParticlesSpawnPerFrame];
		glm::vec3 cameraPos;
		unsigned int numParticles;
		unsigned int numParticlesToRemove;
		unsigned int previousNrOfParticles;
		unsigned int maxOutputVertices;
		float frameTime;
		float size;
	};

	ComputeInput m_inputData;
};