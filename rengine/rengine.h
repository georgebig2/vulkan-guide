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


	AllocatedBufferUntyped create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags required_flags = 0);
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	DeletionQueue _mainDeletionQueue;


	VkDevice _device;
	VmaAllocator _allocator; //vma lib allocator

    UploadContext _uploadContext;

    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;
};
