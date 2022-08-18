#pragma once
#include "common.h"
#include <vk_types.h>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>


#pragma pack(push, 1)
struct VertexP
{
	glm::vec3 position;
};
struct VertexA
{
	//glm::vec2 uv;
	glm::vec<2, uint16_t> uv;
	glm::vec<2, uint8_t> oct_normal;
	glm::vec<4, uint8_t> color;
};
#pragma pack(pop)


struct VertexInputDescription
{
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
	VkPipelineVertexInputStateCreateFlags flags = 0;
};

enum class CRenderPass : char
{
	SHADOW,
	FORWARD,
};

VertexInputDescription get_vertex_description(CRenderPass pass);

class REngine;

struct Mesh
{
	std::vector<VertexP> _vertices_p;
	std::vector<VertexA> _vertices_a;
	std::vector<uint32_t> _indices;

	AllocatedBuffer<VertexP> _vertexBuffer_p;
	AllocatedBuffer<VertexA> _vertexBuffer_a;
	AllocatedBuffer<uint32_t> _indexBuffer;

	RenderBounds bounds;

	bool load_from_meshasset(REngine* engine, const char* filename);
};