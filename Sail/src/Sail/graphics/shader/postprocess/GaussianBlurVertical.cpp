#include "pch.h"
#include "GaussianBlurVertical.h"
#include "Sail/Application.h"


GaussianBlurVertical::GaussianBlurVertical()
	: Shader("postprocess/GaussianBlurVertical.hlsl")
{
	// Finish the shader creation
	finish();

	// Specify dispatch requirements
	m_settings.usesCBV_SRV_UAV = true;
	m_settings.numInputTextures = 1;
	m_settings.numOutputTextures = 1;

	// Compute shader runs 256 x threads, therefore divide resolution by that when dispatching
	m_settings.threadGroupYScale = 1.f / 256.f;

	m_output = std::make_unique<PostProcessPipeline::PostProcessOutput>();
	m_output->outputTexture = getPipeline()->getRenderableTexture("output");

}
GaussianBlurVertical::~GaussianBlurVertical() {}


const Shader::ComputeSettings* GaussianBlurVertical::getComputeSettings() const {
	return &m_settings;
}

std::pair<std::string, void*> GaussianBlurVertical::getComputeInputForIndex(Shader::ComputeShaderInput& input, unsigned int index) {
	auto& thisInput = static_cast<PostProcessPipeline::PostProcessInput&>(input);
	switch (index) {
	case 0:
		if (thisInput.inputTexture) {
			return { "input", thisInput.inputTexture };
		}
		else {
			return { "input", thisInput.inputRenderableTexture };
		}
	}
	SAIL_LOG_ERROR("Tried to get compute input from unknown index - " + std::to_string(index));
	return { "", nullptr };
}

RenderableTexture* GaussianBlurVertical::getComputeOutputForIndex(Shader::ComputeShaderOutput& output, unsigned int index) {
	auto& thisOutput = static_cast<PostProcessPipeline::PostProcessOutput&>(output);
	switch (index) {
	case 0:
		return thisOutput.outputTexture;
	}
	SAIL_LOG_ERROR("Tried to get compute output from unknown index - " + std::to_string(index));
	return nullptr;
}

Shader::ComputeShaderOutput* GaussianBlurVertical::getComputeOutput() {
	return m_output.get();
}
