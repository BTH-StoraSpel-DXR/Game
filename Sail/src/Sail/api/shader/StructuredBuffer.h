#pragma once

#include "Sail/graphics/shader/BindShader.h"

namespace ShaderComponent {

	class StructuredBuffer {
	public:
		static StructuredBuffer* StructuredBuffer::Create(void* initData, unsigned int size, unsigned int numElements, unsigned int stride, BIND_SHADER bindShader, unsigned int slot = 0, bool isRW = false);
		virtual ~StructuredBuffer() { }

		virtual void updateData(const void* newData, unsigned int numElements, unsigned int offset = 0, int frameIndex = -1) = 0;

		virtual void bind(void* cmdList = nullptr) const = 0;
	};

}