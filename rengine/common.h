// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <glm/vec3.hpp>
//#include <glm/vec2.hpp>


struct RenderBounds {
	glm::vec3 origin;
	float radius;
	glm::vec3 extents;
	bool valid;
};
