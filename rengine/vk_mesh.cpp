#include "rengine.h"

#include <vk_mesh.h>
#include <iostream>
#include <asset_loader.h>
#include <mesh_asset.h>

constexpr bool logMeshUpload = false;


VertexInputDescription get_vertex_description(CRenderPass pass)
{
	VertexInputDescription description;

	if (pass == CRenderPass::SHADOW)
	{
		VkVertexInputBindingDescription mainBinding = {};
		mainBinding.binding = 0;
		mainBinding.stride = sizeof(VertexP);
		mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		description.bindings.push_back(mainBinding);

		VkVertexInputAttributeDescription positionAttribute = {};
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		positionAttribute.offset = offsetof(VertexP, position);
		description.attributes.push_back(positionAttribute);

		return description;
	}
	else
	{
		VkVertexInputBindingDescription binding[2] = {};
		binding[0].binding = 0;
		binding[0].stride = sizeof(VertexP);
		binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		binding[1].binding = 1;
		binding[1].stride = sizeof(VertexA);
		binding[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		description.bindings.push_back(binding[0]);
		description.bindings.push_back(binding[1]);

		VkVertexInputAttributeDescription positionAttribute = {};
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		positionAttribute.offset = offsetof(VertexP, position);
		description.attributes.push_back(positionAttribute);

		VkVertexInputAttributeDescription uvAttribute = {};
		uvAttribute.binding = 1;
		uvAttribute.location = 1;
		uvAttribute.format = VK_FORMAT_R16G16_UNORM;// VK_FORMAT_R32G32_SFLOAT;
		uvAttribute.offset = offsetof(VertexA, uv);
		description.attributes.push_back(uvAttribute);

		VkVertexInputAttributeDescription normalAttribute = {};
		normalAttribute.binding = 1;
		normalAttribute.location = 2;
		normalAttribute.format = VK_FORMAT_R8G8_UNORM;//VK_FORMAT_R32G32B32_SFLOAT;
		normalAttribute.offset = offsetof(VertexA, oct_normal);
		description.attributes.push_back(normalAttribute);

		VkVertexInputAttributeDescription colorAttribute = {};
		colorAttribute.binding = 1;
		colorAttribute.location = 3;
		colorAttribute.format = VK_FORMAT_R8G8B8A8_UNORM;//VK_FORMAT_R32G32B32_SFLOAT;
		colorAttribute.offset = offsetof(VertexA, color);
		description.attributes.push_back(colorAttribute);

		return description;
	}
}


using namespace glm;
vec2 OctNormalWrap(vec2 v)
{
	vec2 wrap;
	wrap.x = (1.0f - glm::abs(v.y)) * (v.x >= 0.0f ? 1.0f : -1.0f);
	wrap.y = (1.0f - glm::abs(v.x)) * (v.y >= 0.0f ? 1.0f : -1.0f);
	return wrap;
}

vec2 OctNormalEncode(vec3 n)
{
	n /= (glm::abs(n.x) + glm::abs(n.y) + glm::abs(n.z));

	vec2 wrapped = OctNormalWrap(n);

	vec2 result;
	result.x = n.z >= 0.0f ? n.x : wrapped.x;
	result.y = n.z >= 0.0f ? n.y : wrapped.y;

	result.x = result.x * 0.5f + 0.5f;
	result.y = result.y * 0.5f + 0.5f;

	return result;
}

vec3 OctNormalDecode(vec2 encN)
{
	encN = encN * 2.0f - 1.0f;

	// https://twitter.com/Stubbesaurus/status/937994790553227264
	vec3 n = vec3(encN.x, encN.y, 1.0f - abs(encN.x) - abs(encN.y));
	float t = glm::clamp(-n.z, 0.0f, 1.0f);

	n.x += n.x >= 0.0f ? -t : t;
	n.y += n.y >= 0.0f ? -t : t;

	n = glm::normalize(n);
	return (n);
}


//void Vertex::pack_normal(glm::vec3 n)
//{
//	vec2 oct = OctNormalEncode(n);
//
//	oct_normal.x = uint8_t(oct.x * 255);
//	oct_normal.y = uint8_t(oct.y * 255);
//}
//
//void Vertex::pack_color(glm::vec3 c)
//{
//	color.r = static_cast<uint8_t>(c.x * 255);
//	color.g = static_cast<uint8_t>(c.y * 255);
//	color.b = static_cast<uint8_t>(c.z * 255);
//}

bool Mesh::load_from_meshasset(REngine* engine, const char* filename)
{
	static assets::AssetFile file;
	bool loaded = engine->load_asset(filename, file);

	if (!loaded) {
		std::cout << "Error when loading mesh " << filename << std::endl;;
		return false;
	}
	
	assets::MeshInfo meshinfo = assets::read_mesh_info(&file);

	char* vertexBuffer[] = { 0,0 }; char* indexBuffer = 0;
	assets::unpack_mesh(&meshinfo, file.binaryBlob.data(), file.binaryBlob.size(), vertexBuffer, 2, indexBuffer);

	bounds.extents.x = meshinfo.bounds.extents[0];
	bounds.extents.y = meshinfo.bounds.extents[1];
	bounds.extents.z = meshinfo.bounds.extents[2];

	bounds.origin.x = meshinfo.bounds.origin[0];
	bounds.origin.y = meshinfo.bounds.origin[1];
	bounds.origin.z = meshinfo.bounds.origin[2];

	bounds.radius = meshinfo.bounds.radius;
	bounds.valid = true;

	_vertices_p.clear();
	_vertices_a.clear();
	_indices.clear();

	_indices.resize(meshinfo.indexBuferSize / sizeof(uint32_t));
	memcpy(&_indices[0], indexBuffer, meshinfo.indexBuferSize);

	if (meshinfo.vertexFormat == assets::VertexFormat::PNCV_F32)
	{
		assert(0);
	/*	assets::Vertex_f32_PNCV* unpackedVertices = (assets::Vertex_f32_PNCV*)vertexBuffer;

		_vertices.resize(meshinfo.vertexBuferSize / sizeof(assets::Vertex_f32_PNCV));

		for (int i = 0; i < _vertices.size(); i++) {

			_vertices[i].position.x = unpackedVertices[i].position[0];
			_vertices[i].position.y = unpackedVertices[i].position[1];
			_vertices[i].position.z = unpackedVertices[i].position[2];

			vec3 normal = vec3( 
				unpackedVertices[i].normal[0],
				unpackedVertices[i].normal[1],
				unpackedVertices[i].normal[2] );
			_vertices[i].pack_normal(normal);

			_vertices[i].pack_color(vec3{ unpackedVertices[i].color[0] ,unpackedVertices[i].color[1] ,unpackedVertices[i].color[2] });


			_vertices[i].uv.x = unpackedVertices[i].uv[0];
			_vertices[i].uv.y = unpackedVertices[i].uv[1];
		}*/
	}
	else if (meshinfo.vertexFormat == assets::VertexFormat::P32N8C8V16)
	{
		assert(0);
		//_vertices.resize(meshinfo.vertexBuferSize / sizeof(assets::Vertex_P32N8C8V16));
		//memcpy(&_vertices[0], (assets::Vertex_P32N8C8V16*)vertexBuffer, meshinfo.vertexBuferSize);
	}
	else if (meshinfo.vertexFormat == assets::VertexFormat::P32_V16N8C8)
	{
		_vertices_p.resize(meshinfo.vertexBuferSize[0] / sizeof(assets::Vertex_P32));
		memcpy(&_vertices_p[0], vertexBuffer[0], meshinfo.vertexBuferSize[0]);
		_vertices_a.resize(meshinfo.vertexBuferSize[1] / sizeof(assets::Vertex_V16N8C8));
		memcpy(&_vertices_a[0], vertexBuffer[1], meshinfo.vertexBuferSize[1]);
	}
		
	if (logMeshUpload)
	{
		//LOG_SUCCESS("Loaded mesh {} : Verts={}, Tris={}", filename, _vertices.size(), _indices.size() / 3);
	}

	return true;
}
