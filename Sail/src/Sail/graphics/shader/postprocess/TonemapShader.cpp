#include "pch.h"
#include "TonemapShader.h"
#include "Sail/Application.h"
#include "Sail/graphics/postprocessing/PostProcessPipeline.h"

const Shader::ComputeSettings* TonemapShader::getComputeSettings() const {
	return &m_settings;
}

std::pair<std::string, void*> TonemapShader::getComputeInputForIndex(Shader::ComputeShaderInput& input, unsigned int index) {
	auto& thisInput = static_cast<PostProcessPipeline::PostProcessInput&>(input);
	switch (index) {
	case 0:
		if (thisInput.inputTexture) {
			return { "input", thisInput.inputTexture };
		} else {
			return { "input", thisInput.inputRenderableTexture };
		}
	}
	SAIL_LOG_ERROR("Tried to get compute input from unknown index - " + std::to_string(index));
	return { "", nullptr };
}

RenderableTexture* TonemapShader::getComputeOutputForIndex(Shader::ComputeShaderOutput& output, unsigned int index) {
	auto& thisOutput = static_cast<PostProcessPipeline::PostProcessOutput&>(output);
	switch (index) {
	case 0:
		return thisOutput.outputTexture;
	}
	SAIL_LOG_ERROR("Tried to get compute output from unknown index - " + std::to_string(index));
	return nullptr;
}

Shader::ComputeShaderOutput* TonemapShader::getComputeOutput() {
	return m_output.get();
}

TonemapShader::TonemapShader()
	: Shader("postprocess/Tonemapper.hlsl")
{
	// Finish the shader creation
	finish();

	// Specify dispatch requirements
	m_settings.usesCBV_SRV_UAV = true;
	m_settings.numInputTextures = 1;
	m_settings.numOutputTextures = 0;

	// Compute shader runs 256 x threads, therefore divide resolution by that when dispatching
	m_settings.threadGroupXScale = 1.f / 256.f;

	m_output = std::make_unique<PostProcessPipeline::PostProcessOutput>();

}
TonemapShader::~TonemapShader() {}