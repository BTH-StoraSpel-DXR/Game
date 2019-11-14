#include "pch.h"
#include "DX12ParticleRenderer.h"
#include "Sail/Application.h"
#include "API/DX12/resources/DX12Texture.h"
#include "API/DX12/shader/DX12ShaderPipeline.h"
#include "API/DX12/DX12Mesh.h"
#include "Sail/graphics/geometry/PBRMaterial.h"
#include "Sail/graphics/shader/Shader.h"
#include "API/DX12/DX12VertexBuffer.h"
#include "API/DX12/resources/DX12RenderableTexture.h"

DX12ParticleRenderer::DX12ParticleRenderer() {
	Application* app = Application::getInstance();
	m_context = app->getAPI<DX12API>();

	m_context->initCommand(m_command, D3D12_COMMAND_LIST_TYPE_DIRECT, L"ScreenSpace Renderer main command list");
	
	auto windowWidth = app->getWindow()->getWindowWidth();
	auto windowHeight = app->getWindow()->getWindowHeight();

	m_outputTexture = static_cast<DX12RenderableTexture*>(RenderableTexture::Create(windowWidth, windowHeight, "Particle renderer output ", Texture::R8G8B8A8));
}

DX12ParticleRenderer::~DX12ParticleRenderer() {}

void DX12ParticleRenderer::present(PostProcessPipeline* postProcessPipeline, RenderableTexture* output) {
	assert(!output); // Render to texture is currently not implemented for DX12!

	auto frameIndex = m_context->getSwapIndex();

	auto& allocator = m_command.allocators[m_context->getFrameIndex()];
	auto& cmdList = m_command.list;

	// Reset allocators and lists for this frame
	allocator->Reset();
	cmdList->Reset(allocator.Get(), nullptr);

	m_context->renderToBackBuffer(cmdList.Get());
	
	//cmdList->OMSetRenderTargets(1, &m_context->getCurrentRenderTargetCDH(), true, &m_depthTexture->getDsvCDH());
		//cmdList->OMSetRenderTargets(1, &m_outputTexture->getRtvCDH(), true, &m_depthTexture->getDsvCDH());

	cmdList->RSSetViewports(1, m_context->getViewport());
	cmdList->RSSetScissorRects(1, m_context->getScissorRect());
	m_context->prepareToRender(cmdList.Get());

	// TODO: optimize!
	int meshIndex = 0;
	for (auto& renderCommand : commandQueue) {
		auto& vbuffer = static_cast<DX12VertexBuffer&>(renderCommand.model.mesh->getVertexBuffer());
		vbuffer.init(cmdList.Get());
		if (renderCommand.type != RENDER_COMMAND_TYPE_MODEL) {
			continue;
		}
		auto* tex = static_cast<DX12Texture*>(renderCommand.model.mesh->getMaterial()->getTexture(0));
		if (tex && !tex->hasBeenInitialized()) {
			tex->initBuffers(cmdList.Get(), meshIndex);
			meshIndex++;
		}
	}

	m_outputTexture->clear({ 0.0f, 0.0f, 0.0f, 1.0f }, cmdList.Get());

	cmdList->SetGraphicsRootSignature(m_context->getGlobalRootSignature());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Bind the descriptor heap that will contain all SRVs for this frame
	m_context->getMainGPUDescriptorHeap()->bind(cmdList.Get());
	
	meshIndex = 0;
	for (auto& command : commandQueue) {
		DX12ShaderPipeline* shaderPipeline = static_cast<DX12ShaderPipeline*>(command.model.mesh->getMaterial()->getShader()->getPipeline());
		shaderPipeline->bind_new(cmdList.Get(), meshIndex);

		// Used in most shaders
		shaderPipeline->trySetCBufferVar_new("sys_mWorld", &glm::transpose(command.transform), sizeof(glm::mat4), meshIndex);
		shaderPipeline->trySetCBufferVar_new("sys_mView", &camera->getViewMatrix(), sizeof(glm::mat4), meshIndex);
		shaderPipeline->trySetCBufferVar_new("sys_mProj", &camera->getProjMatrix(), sizeof(glm::mat4), meshIndex);

		static_cast<DX12Mesh*>(command.model.mesh)->draw_new(*this, cmdList.Get(), meshIndex);
		meshIndex++;
	}

	// Lastly - transition back buffer to present
	m_context->prepareToPresent(cmdList.Get());
	// Close command list
	cmdList->Close();

	m_context->executeCommandLists({m_command.list.Get()});
}

bool DX12ParticleRenderer::onEvent(const Event& event) {
	return false;
}

void DX12ParticleRenderer::setDepthTexture(DX12RenderableTexture* tex) {
	m_depthTexture = tex;
}
