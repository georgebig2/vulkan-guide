#pragma once
//#define VK_USE_PLATFORM_ANDROID_KHR
//#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#include <VkBootstrap.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare" // comparison of unsigned expression < 0 is always false
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#endif
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
//#define VK_USE_PLATFORM_ANDROID_KHR
//#define VMA_VULKAN_VERSION 1002000 // Vulkan 1.2
//#define VMA_VULKAN_VERSION 1001000 // Vulkan 1.1
//#define VMA_VULKAN_VERSION 1000000 // Vulkan 1.0
//#include <vulkan/vulkan_android.h>
#include "vk_mem_alloc.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

