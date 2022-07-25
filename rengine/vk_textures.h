﻿// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include "rengine.h"

namespace vkutil {

	struct MipmapInfo {
		size_t dataSize;
		size_t dataOffset;
	};

	bool load_image_from_file(REngine& engine, const char* file, AllocatedImage& outImage);	
	bool load_image_from_asset(REngine& engine, const char* file, AllocatedImage& outImage);


	AllocatedImage upload_image(int texWidth, int texHeight, VkFormat image_format, REngine& engine, AllocatedBufferUntyped& stagingBuffer);

	AllocatedImage upload_image_mipmapped(int texWidth, int texHeight, VkFormat image_format, REngine& engine, AllocatedBufferUntyped& stagingBuffer, std::vector<MipmapInfo> mips);
}