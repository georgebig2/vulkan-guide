#pragma once
#include "vk_pushbuffer.h"
#include <deque>
#include <functional>

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	template<typename F>
	void push_function(F&& function) {
		static_assert(sizeof(F) < 300, "DONT CAPTURE TOO MUCH IN THE LAMBDA");
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};

namespace vkutil
{
	class DescriptorAllocator;
}

struct FrameData
{
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	DeletionQueue _frameDeletionQueue;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	vkutil::PushBuffer dynamicData;
	vkutil::DescriptorAllocator* dynamicDescriptorAllocator;

	std::vector<uint32_t> debugDataOffsets;
	std::vector<std::string> debugDataNames;

	VkImageView _swapchainImageView;

	AllocatedImage _rawRenderImage;
	AllocatedImage _depthImage;
	AllocatedImage _shadowImage;

	VkFramebuffer _framebuffer;
	VkFramebuffer _forwardFramebuffer;
	VkFramebuffer _shadowFramebuffer;

	//AllocatedImage _depthPyramid;
	//VkImageView depthPyramidMips[16] = {};
};