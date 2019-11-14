#include "pch.h"
#include "RendererWrapper.h"

#include "api/Renderer.h"
#include "graphics/postprocessing/PostProcessPipeline.h"
#include "API/DX12/renderer/DX12ParticleRenderer.h"
#include "API/DX12/renderer/DX12GBufferRenderer.h"

RendererWrapper::RendererWrapper() {
}

RendererWrapper::~RendererWrapper() {
}

void RendererWrapper::initialize() {
	m_rendererRaster = std::unique_ptr<Renderer>(Renderer::Create(Renderer::FORWARD));
	m_rendererRaytrace = std::unique_ptr<Renderer>(Renderer::Create(Renderer::HYBRID));
	m_rendererScreenSpace = std::unique_ptr<Renderer>(Renderer::Create(Renderer::SCREEN_SPACE));
	m_rendererParticles = std::unique_ptr<Renderer>(Renderer::Create(Renderer::PARTICLES));
	m_currentRenderer = m_rendererRaytrace.get();

	// TODO: somehow make this not dx12 specific
	// Tell particle renderer to use depth output from gbuffer renderer
	auto* gbuffers = static_cast<DX12GBufferRenderer*>(m_rendererRaster.get())->getGBufferOutputs();
	static_cast<DX12ParticleRenderer*>(m_rendererParticles.get())->setDepthTexture(gbuffers[0]);

	m_postProcessPipeline = std::make_shared<PostProcessPipeline>();

	m_doPostProcessing = true;
}

/*
	0 : Raytrace
	1 : Raster
*/
void RendererWrapper::changeRenderer(unsigned int index) {

	switch (index) {
	case 0:
		m_currentRenderer = m_rendererRaytrace.get();
		break;
	case 1:
		m_currentRenderer = m_rendererRaster.get();
		break;
	default:
		break;
	}
}

void RendererWrapper::togglePostProcessing(const bool flag) {
	m_doPostProcessing = flag;
}

bool& RendererWrapper::getDoPostProcessing() {
	return m_doPostProcessing;
}

Renderer* RendererWrapper::getCurrentRenderer() {
	return m_currentRenderer;
}

Renderer* RendererWrapper::getParticleRenderer() {
	return m_rendererParticles.get();
}

PostProcessPipeline* RendererWrapper::getPostProcessPipeline() {
	return m_postProcessPipeline.get();
}

bool RendererWrapper::checkIfOnWater(const glm::vec3& worldPos) const {
	return m_rendererRaytrace->checkIfOnWater(worldPos);
}

bool RendererWrapper::onEvent(const Event& event) {
	if (m_rendererRaster) {
		m_rendererRaster->onEvent(event);
	}
	if (m_rendererRaytrace) {
		m_rendererRaytrace->onEvent(event);
	}

	return true;
}

void RendererWrapper::setLightSetup(LightSetup* lights) {
	m_currentRenderer->setLightSetup(lights);
}


Renderer* RendererWrapper::getScreenSpaceRenderer() {
	return m_rendererScreenSpace.get();
}