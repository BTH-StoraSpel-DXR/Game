#include "pch.h"
#include "DX12RaytracingRenderer.h"
#include "Sail/Application.h"
#include "../DX12Utils.h"
#include "Sail/graphics/postprocessing/PostProcessPipeline.h"
#include "API/DX12/DX12VertexBuffer.h"
#include "Sail/KeyBinds.h"
#include "Sail/entities/systems/Gameplay/LevelSystem/LevelSystem.h"
#include "Sail/events/EventDispatcher.h"

// Current goal is to make this render a fully raytraced image of all geometry (without materials) within a scene

DX12RaytracingRenderer::DX12RaytracingRenderer(DX12RenderableTexture** inputs)
	: m_dxr("Basic", inputs) {

	EventDispatcher::Instance().subscribe(Event::Type::WINDOW_RESIZE, this);

	Application* app = Application::getInstance();
	m_context = app->getAPI<DX12API>();
	m_context->initCommand(m_commandDirect, D3D12_COMMAND_LIST_TYPE_DIRECT, L"Raytracing Renderer DIRECT command list or allocator");
	m_context->initCommand(m_commandCompute, D3D12_COMMAND_LIST_TYPE_COMPUTE, L"Raytracing Renderer main COMPUTE command list or allocator");

	auto windowWidth = app->getWindow()->getWindowWidth();
	auto windowHeight = app->getWindow()->getWindowHeight();
	m_outputTexture = std::unique_ptr<DX12RenderableTexture>(static_cast<DX12RenderableTexture*>(RenderableTexture::Create(windowWidth, windowHeight, "Raytracing renderer output texture", Texture::R16G16B16A16_FLOAT)));
	m_outputBloomTexture = std::unique_ptr<DX12RenderableTexture>(static_cast<DX12RenderableTexture*>(RenderableTexture::Create(windowWidth, windowHeight, "Raytracing renderer bloom output texture", Texture::R16G16B16A16_FLOAT)));

	m_currNumDecals = 0;
	memset(m_decals, 0, sizeof(DXRShaderCommon::DecalData) * MAX_DECALS);
}

DX12RaytracingRenderer::~DX12RaytracingRenderer() {
	EventDispatcher::Instance().unsubscribe(Event::Type::WINDOW_RESIZE, this);
}

void DX12RaytracingRenderer::present(PostProcessPipeline* postProcessPipeline, RenderableTexture* output) {
	auto frameIndex = m_context->getSwapIndex();

	if (postProcessPipeline) {
		// Make sure output textures are in a higher precision format to accomodate values > 1
		m_outputTexture->changeFormat(Texture::R16G16B16A16_FLOAT);
		m_outputBloomTexture->changeFormat(Texture::R16G16B16A16_FLOAT);
	} else {
		// Make sure output textures are in a format that can be directly copied to the back buffer
		// Please note that no tone mapping is applied when post process is turned off.
		m_outputTexture->changeFormat(Texture::R8G8B8A8);
		m_outputBloomTexture->changeFormat(Texture::R8G8B8A8);
	}

	// There is one allocator per swap buffer
	auto& allocatorCompute = m_commandCompute.allocators[m_context->getFrameIndex()];
	auto& allocatorDirect = m_commandDirect.allocators[m_context->getFrameIndex()];
	auto& cmdListCompute = m_commandCompute.list;
	auto& cmdListDirect = m_commandDirect.list;

	// Reset allocators and lists for this frame
	allocatorCompute->Reset();
	allocatorDirect->Reset();
	cmdListCompute->Reset(allocatorCompute.Get(), nullptr);
	cmdListDirect->Reset(allocatorDirect.Get(), nullptr);

	// Wait for the G-Buffer pass to finish execution on the direct queue
	auto fenceVal = m_context->getDirectQueue()->signal();
	m_context->getComputeQueue()->wait(fenceVal);


	int gpuGroupIndex = 0;
	for (auto& group : m_metaballGroups) {
		//std::sort(group.second.balls.begin(), group.second.balls.end(),
		//	[](const DXRBase::Metaball& a, const DXRBase::Metaball& b) -> const bool
		//	{
		//		return a.distToCamera < b.distToCamera;
		//	});

		/*commandQueue.emplace_back();
		RenderCommand& cmd = commandQueue.back();
		cmd.type = RENDER_COMMAND_TYPE_NON_MODEL_METABALL;
		cmd.flags = Renderer::MESH_DYNAMIC;
		cmd.nonModel.material = nullptr;
		cmd.transform = glm::identity<glm::mat4>();
		cmd.transform = glm::transpose(cmd.transform);
		cmd.hasUpdatedSinceLastRender.resize(m_context->getNumGPUBuffers(), true);*/

		//Calculate the needed size of m_next_metaball_aabb.
		glm::vec3& pos = group.second.balls.front().pos;

		D3D12_RAYTRACING_AABB nextMetaballAabb;
		nextMetaballAabb.MaxX = pos.x + METABALL_RADIUS;
		nextMetaballAabb.MaxY = pos.y + METABALL_RADIUS;
		nextMetaballAabb.MaxZ = pos.z + METABALL_RADIUS;
		nextMetaballAabb.MinX = pos.x - METABALL_RADIUS;
		nextMetaballAabb.MinY = pos.y - METABALL_RADIUS;
		nextMetaballAabb.MinZ = pos.z - METABALL_RADIUS;
		for (size_t i = 1; i < group.second.balls.size() && i < MAX_NUM_METABALLS; i++) {
			glm::vec3& pos = group.second.balls[i].pos;

			if (nextMetaballAabb.MaxX < pos.x + METABALL_RADIUS) {
				nextMetaballAabb.MaxX = pos.x + METABALL_RADIUS;
			}
			if (nextMetaballAabb.MaxY < pos.y + METABALL_RADIUS) {
				nextMetaballAabb.MaxY = pos.y + METABALL_RADIUS;
			}
			if (nextMetaballAabb.MaxZ < pos.z + METABALL_RADIUS) {
				nextMetaballAabb.MaxZ = pos.z + METABALL_RADIUS;
			}

			if (nextMetaballAabb.MinX > pos.x - METABALL_RADIUS) {
				nextMetaballAabb.MinX = pos.x - METABALL_RADIUS;
			}
			if (nextMetaballAabb.MinY > pos.y - METABALL_RADIUS) {
				nextMetaballAabb.MinY = pos.y - METABALL_RADIUS;
			}
			if (nextMetaballAabb.MinZ > pos.z - METABALL_RADIUS) {
				nextMetaballAabb.MinZ = pos.z - METABALL_RADIUS;
			}
		}

		group.second.aabb = nextMetaballAabb;
		group.second.index = gpuGroupIndex++;
	}

	if (Input::WasKeyJustPressed(KeyBinds::RELOAD_DXR_SHADER)) {
		m_dxr.reloadShaders();
	}

	// Copy updated flag from vertex buffers to renderCommand
	// This marks all commands that should have their bottom layer updated in-place
	for (auto& renderCommand : commandQueue) {
		if (renderCommand.type == RENDER_COMMAND_TYPE_MODEL) {
			auto& vb = static_cast<DX12VertexBuffer&>(renderCommand.model.mesh->getVertexBuffer());
			if (vb.hasBeenUpdated()) {
				renderCommand.hasUpdatedSinceLastRender[frameIndex] = true;
				vb.resetHasBeenUpdated();
			}
		}
	}

	if (camera && lightSetup) {
		m_dxr.updateSceneData(*camera, *lightSetup, m_metaballGroups, teamColors, (postProcessPipeline) ? false : true);
	}

	m_dxr.updateDecalData(m_decals, m_currNumDecals > MAX_DECALS - 1 ? MAX_DECALS : m_currNumDecals);
	m_dxr.updateWaterData();
	m_dxr.updateAccelerationStructures(commandQueue, cmdListCompute.Get(), m_metaballGroups);
	m_dxr.dispatch(m_outputTexture.get(), m_outputBloomTexture.get(), cmdListCompute.Get());
	// AS has now been updated this frame, reset flag
	for (auto& renderCommand : commandQueue) {
		renderCommand.hasUpdatedSinceLastRender[frameIndex] = false;
	}

	// TODO: move this to a graphics queue when current cmdList is executed on the compute queue

	RenderableTexture* renderOutput = m_outputTexture.get();
	if (postProcessPipeline) {
		// Run post processing
		postProcessPipeline->setBloomInput(m_outputBloomTexture.get());
		RenderableTexture* ppOutput = postProcessPipeline->run(m_outputTexture.get(), cmdListCompute.Get());
		if (ppOutput) {
			renderOutput = ppOutput;
		}
	}
	DX12RenderableTexture* dxRenderOutput = static_cast<DX12RenderableTexture*>(renderOutput);
	dxRenderOutput->transitionStateTo(cmdListCompute.Get(), D3D12_RESOURCE_STATE_COMMON);

	// Execute compute command list
	cmdListCompute->Close();
	m_context->executeCommandLists({ cmdListCompute.Get() }, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	// Place a signal to syncronize copying the raytracing output to the backbuffer when it is available
	fenceVal = m_context->getComputeQueue()->signal();

	// Copy post processing output to back buffer
	dxRenderOutput->transitionStateTo(cmdListDirect.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
	auto* renderTarget = m_context->getCurrentRenderTargetResource();
	DX12Utils::SetResourceTransitionBarrier(cmdListDirect.Get(), renderTarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	cmdListDirect->CopyResource(renderTarget, dxRenderOutput->getResource());
	// Lastly - transition back buffer to present
	DX12Utils::SetResourceTransitionBarrier(cmdListDirect.Get(), renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

	// Wait for compute to finish
	m_context->getDirectQueue()->wait(fenceVal);
	// Execute direct command list
	cmdListDirect->Close();
	m_context->executeCommandLists({ cmdListDirect.Get() }, D3D12_COMMAND_LIST_TYPE_DIRECT);
}

void DX12RaytracingRenderer::begin(Camera* camera) {
	Renderer::begin(camera);
	m_metaballGroups.clear();
}

bool DX12RaytracingRenderer::onEvent(const Event& event) {
	switch (event.type) {
	case Event::Type::WINDOW_RESIZE: onResize((const WindowResizeEvent&)event); break;
	default: break;
	}

	// Pass along events
	m_dxr.onEvent(event);
	return true;
}

void DX12RaytracingRenderer::submit(Mesh* mesh, const glm::mat4& modelMatrix, RenderFlag flags, int teamColorID, bool castShadows) {
	RenderCommand cmd;
	cmd.type = RENDER_COMMAND_TYPE_MODEL;
	cmd.model.mesh = mesh;
	cmd.transform = glm::transpose(modelMatrix);
	cmd.flags = flags;
	cmd.teamColorID = teamColorID;
	cmd.castShadows = castShadows;
	// Resize to match numSwapBuffers (specific to dx12)
	cmd.hasUpdatedSinceLastRender.resize(m_context->getNumGPUBuffers(), false);
	commandQueue.push_back(cmd);
}

void DX12RaytracingRenderer::submitMetaball(RenderCommandType type, Material* material, const glm::vec3& pos, RenderFlag flags, int group) {
	assert(type != RenderCommandType::RENDER_COMMAND_TYPE_MODEL);

	if (type == RenderCommandType::RENDER_COMMAND_TYPE_NON_MODEL_METABALL) {
		DXRBase::Metaball ball;
		ball.pos = pos;
		ball.distToCamera = glm::length(ball.pos - camera->getPosition());

		m_metaballGroups[group].balls.emplace_back(ball);
	}
}

void DX12RaytracingRenderer::submitDecal(const glm::vec3& pos, const glm::mat3& rot, const glm::vec3& halfSize) {
	DXRShaderCommon::DecalData decalData;
	decalData.position = pos;
	decalData.rot = rot;
	decalData.halfSize = halfSize;
	m_decals[m_currNumDecals % MAX_DECALS] = decalData;
	m_currNumDecals++;
}

void DX12RaytracingRenderer::submitWaterPoint(const glm::vec3& pos) {
	m_dxr.addWaterAtWorldPosition(pos);
}

void DX12RaytracingRenderer::setTeamColors(const std::vector<glm::vec3>& teamColors) {
	Renderer::setTeamColors(teamColors);
}

bool DX12RaytracingRenderer::checkIfOnWater(const glm::vec3& pos) {

	return m_dxr.checkWaterAtWorldPosition(pos);
}

void DX12RaytracingRenderer::updateMetaballAABB() {
	
}

void DX12RaytracingRenderer::setGBufferInputs(DX12RenderableTexture** inputs) {
	m_dxr.setGBufferInputs(inputs);
}

DXRBase* DX12RaytracingRenderer::getDXRBase() {
	return &m_dxr;
}

bool DX12RaytracingRenderer::onResize(const WindowResizeEvent& event) {
	m_outputTexture->resize(event.width, event.height);
	m_outputBloomTexture->resize(event.width, event.height);
	return true;
}