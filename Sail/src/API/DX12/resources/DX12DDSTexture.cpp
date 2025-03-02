#include "pch.h"
#include "DX12DDSTexture.h"
#include "Sail/Application.h"
#include "../DX12Utils.h"
#include "Sail/graphics/shader/compute/GenerateMipsComputeShader.h"
#include "../DX12ComputeShaderDispatcher.h"
#include "../shader/DX12ShaderPipeline.h"
#include "Sail/utils/Utils.h"
#include "Sail/resources/loaders/DDSTextureLoader12.h"

/*Texture* Texture::Create(const std::string& filename) {
	return SAIL_NEW DX12DDSTexture(filename);
}*/

DX12DDSTexture::DX12DDSTexture(const std::string& filename)
	: m_isInitialized(false) {
	context = Application::getInstance()->getAPI<DX12API>();

	std::wstring wide_string = std::wstring(filename.begin(), filename.end());
	ThrowIfFailed(DirectX::LoadDDSTextureFromFile(context->getDevice(), wide_string.c_str(), textureDefaultBuffers[0].ReleaseAndGetAddressOf(),
						   m_ddsData, m_subresources));

	// Don't create one resource per swap buffer
	useOneResource = true;

	m_textureDesc = textureDefaultBuffers[0]->GetDesc();
	m_textureDesc.MipLevels = 4;

	// A texture rarely updates its data, if at all, so it is stored in a default heap
	state[0] = D3D12_RESOURCE_STATE_COPY_DEST;
	ThrowIfFailed(context->getDevice()->CreateCommittedResource(&DX12Utils::sDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &m_textureDesc, state[0], nullptr, IID_PPV_ARGS(&textureDefaultBuffers[0])));
	textureDefaultBuffers[0]->SetName((std::wstring(L"Texture default buffer for ") + std::wstring(filename.begin(), filename.end())).c_str());

	// Create a shader resource view (descriptor that points to the texture and describes it)
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = m_textureDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = m_textureDesc.MipLevels;
	context->getDevice()->CreateShaderResourceView(textureDefaultBuffers[0].Get(), &srvDesc, srvHeapCDHs[0]);

	// Don't allow UAV access
	uavHeapCDHs[0] = {0};

}

DX12DDSTexture::~DX12DDSTexture() {

}

void DX12DDSTexture::initBuffers(ID3D12GraphicsCommandList4* cmdList) {
	//The lock_guard will make sure multiple threads wont try to initialize the same texture
	std::lock_guard<std::mutex> lock(m_initializeMutex);
	if (m_isInitialized)
		return;

	UINT64 textureUploadBufferSize;
	// this function gets the size an upload buffer needs to be to upload a texture to the gpu.
	// each row must be 256 byte aligned except for the last row, which can just be the size in bytes of the row
	// eg. textureUploadBufferSize = ((((width * numBytesPerPixel) + 255) & ~255) * (height - 1)) + (width * numBytesPerPixel);
	context->getDevice()->GetCopyableFootprints(&m_textureDesc, 0, static_cast<UINT>(m_subresources.size()), 0, nullptr, nullptr, nullptr, &textureUploadBufferSize);

	// Create the upload heap
	// TODO: release the upload heap when it has been copied to the default buffer
	// This could be done in a buffer manager owned by dx12api
	m_textureUploadBuffer.Attach(DX12Utils::CreateBuffer(context->getDevice(), textureUploadBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, DX12Utils::sUploadHeapProperties));
	m_textureUploadBuffer->SetName(L"Texture upload buffer");

	// Copy the upload buffer contents to the default heap using a helper method from d3dx12.h
	DX12Utils::UpdateSubresources(cmdList, textureDefaultBuffers[0].Get(), m_textureUploadBuffer.Get(),
					   0, 0, static_cast<UINT>(m_subresources.size()), m_subresources.data());

	DX12Utils::SetResourceUAVBarrier(cmdList, textureDefaultBuffers[0].Get());

	generateMips(cmdList);

	m_isInitialized = true;
}

bool DX12DDSTexture::hasBeenInitialized() const {
	return m_isInitialized;
}

ID3D12Resource* DX12DDSTexture::getResource() const {
	return textureDefaultBuffers[0].Get();
}

void DX12DDSTexture::generateMips(ID3D12GraphicsCommandList4* cmdList) {
	auto& mipsShader = Application::getInstance()->getResourceManager().getShaderSet<GenerateMipsComputeShader>();
	DX12ComputeShaderDispatcher csDispatcher;
	const auto& settings = mipsShader.getComputeSettings();
	csDispatcher.begin(cmdList);

	auto* dxPipeline = mipsShader.getPipeline();

	// TODO: read this from texture data
	bool isSRGB = false;

	for (uint32_t srcMip = 0; srcMip < m_textureDesc.MipLevels - 1u;) {
		dxPipeline->setCBufferVar("IsSRGB", &isSRGB, sizeof(bool));

		uint64_t srcWidth = m_textureDesc.Width >> srcMip;
		uint32_t srcHeight = m_textureDesc.Height >> srcMip;
		uint32_t dstWidth = static_cast<uint32_t>(srcWidth >> 1);
		uint32_t dstHeight = srcHeight >> 1;

		// 0b00(0): Both width and height are even.
		// 0b01(1): Width is odd, height is even.
		// 0b10(2): Width is even, height is odd.
		// 0b11(3): Both width and height are odd.
		unsigned int srcDimension = (srcHeight & 1) << 1 | (srcWidth & 1);
		dxPipeline->setCBufferVar("SrcDimension", &srcDimension, sizeof(unsigned int));

		// How many mipmap levels to compute this pass (max 4 mips per pass)
		DWORD mipCount;

		// The number of times we can half the size of the texture and get
		// exactly a 50% reduction in size.
		// A 1 bit in the width or height indicates an odd dimension.
		// The case where either the width or the height is exactly 1 is handled
		// as a special case (as the dimension does not require reduction).
		_BitScanForward(&mipCount, (dstWidth == 1 ? dstHeight : dstWidth) |
			(dstHeight == 1 ? dstWidth : dstHeight));
		// Maximum number of mips to generate is 4.
		mipCount = std::min<DWORD>(4, mipCount + 1);
		// Clamp to total number of mips left over.
		mipCount = (srcMip + mipCount) >= m_textureDesc.MipLevels ?
			m_textureDesc.MipLevels - srcMip - 1 : mipCount;

		// Dimensions should not reduce to 0.
		// This can happen if the width and height are not the same.
		dstWidth = std::max<DWORD>(1, dstWidth);
		dstHeight = std::max<DWORD>(1, dstHeight);

		glm::vec2 texelSize = glm::vec2(1.0f / (float)dstWidth, 1.0f / (float)dstHeight);

		dxPipeline->setCBufferVar("SrcMipLevel", &srcMip, sizeof(unsigned int));
		dxPipeline->setCBufferVar("NumMipLevels", &mipCount, sizeof(unsigned int));
		dxPipeline->setCBufferVar("TexelSize", &texelSize, sizeof(glm::vec2));

		const auto& heap = context->getComputeGPUDescriptorHeap();
		unsigned int indexStart = heap->getAndStepIndex(20); // TODO: read this from root parameters
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = heap->getCPUDescriptorHandleForIndex(indexStart);
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heap->getGPUDescriptorHandleForIndex(indexStart);

		transitionStateTo(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context->getDevice()->CopyDescriptorsSimple(1, cpuHandle, getSrvCDH(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		cmdList->SetComputeRootDescriptorTable(context->getRootIndexFromRegister("t0"), gpuHandle);
		cpuHandle.ptr += heap->getDescriptorIncrementSize() * 10;

		//SetShaderResourceView(GenerateMips::SrcMip, 0, texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, srcMip, 1, &srvDesc);

		for (uint32_t mip = 0; mip < mipCount; ++mip) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = m_textureDesc.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = srcMip + mip + 1;
			context->getDevice()->CreateUnorderedAccessView(textureDefaultBuffers[0].Get(), nullptr, &uavDesc, cpuHandle);
			cpuHandle.ptr += heap->getDescriptorIncrementSize();

			DX12Utils::SetResourceTransitionBarrier(cmdList, textureDefaultBuffers[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, srcMip + mip + 1);
		}

		// TODO: Pad any unused mip levels with a default UAV. Doing this keeps the DX12 runtime happy.
		/*if (mipCount < 4) {
			m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(GenerateMips::OutMip, mipCount, 4 - mipCount, m_GenerateMipsPSO->GetDefaultUAV());
		}*/

		// Dispatch compute shader to generate mip levels
		GenerateMipsComputeShader::Input input;
		input.threadGroupCountX = (unsigned int)glm::ceil(dstWidth * settings->threadGroupXScale);
		input.threadGroupCountY = (unsigned int)glm::ceil(dstHeight * settings->threadGroupYScale);
		csDispatcher.dispatch(mipsShader, input, cmdList);

		// Transition all subresources to the state that the texture think it is in
		for (uint32_t mip = 0; mip < mipCount; ++mip) {
			DX12Utils::SetResourceTransitionBarrier(cmdList, textureDefaultBuffers[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, srcMip + mip + 1);
		}

		DX12Utils::SetResourceUAVBarrier(cmdList, textureDefaultBuffers[0].Get());

		srcMip += mipCount;
	}
}
