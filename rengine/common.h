// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <glm/vec3.hpp>
//#include <glm/vec2.hpp>

#include <iostream>

struct RenderBounds {
	glm::vec3 origin;
	float radius;
	glm::vec3 extents;
	bool valid;
};

#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)