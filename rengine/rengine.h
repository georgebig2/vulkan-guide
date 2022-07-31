#pragma once
#include <vector>
#include <functional>
#include <deque>

#include <vk_types.h>


struct DeletionQueue
{
    std::deque<std::function<void()>> deletors;

    template<typename F>
    void push_function(F&& function) {
        static_assert(sizeof(F) < 200, "DONT CAPTURE TOO MUCH IN THE LAMBDA");
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


struct UploadContext {
    VkFence _uploadFence;
    VkCommandPool _commandPool;
};


class REngine {
public:

    VkExtent2D _windowExtent{ 1700 * 2 / 3 , 900 * 2 / 3 };
    VkExtent2D _shadowExtent{ 1024 * 4,1024 * 4 };


	AllocatedBufferUntyped create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags required_flags = 0);
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	DeletionQueue _mainDeletionQueue;

    virtual void init_vulkan();
    
    virtual bool create_surface(VkInstance instance, VkSurfaceKHR* surface) = 0;

    void init_swapchain();

	VkDevice _device;
	VmaAllocator _allocator; //vma lib allocator

    VkInstance _instance;
    VkPhysicalDevice _chosenGPU;

    UploadContext _uploadContext;

    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

    VkSurfaceKHR _surface;
    VkSwapchainKHR _swapchain;
    VkFormat _swachainImageFormat;
    VkFormat _depthFormat;

    VkPhysicalDeviceProperties _gpuProperties;

    std::vector<VkFramebuffer> _framebuffers;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;

    VkFormat _renderFormat;
    AllocatedImage _rawRenderImage;
    VkSampler _smoothSampler;
    VkFramebuffer _forwardFramebuffer;
    VkFramebuffer _shadowFramebuffer;

    AllocatedImage _depthImage;
    AllocatedImage _depthPyramid;
    VkSampler _shadowSampler;
    AllocatedImage _shadowImage;

    int depthPyramidWidth;
    int depthPyramidHeight;
    int depthPyramidLevels;

    VkSampler _depthSampler;
    VkImageView depthPyramidMips[16] = {};

};
