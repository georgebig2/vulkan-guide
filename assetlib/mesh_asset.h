#pragma once
#include <asset_loader.h>
#include <cmath>

namespace assets
{

#pragma pack(push, 1)
	struct Vertex_f32_PNCV
	{
		float position[3];
		float normal[3];
		float color[3];
		float uv[2];
	};
	struct Vertex_P32N8C8V16 {

		float position[3];
		uint8_t normal[2];
		uint8_t color[3];
		float uv[2];
	};
	struct Vertex_P32
	{
		float position[3];
	};
	struct Vertex_V16N8C8
	{
		uint16_t uv[2];
		uint8_t normal[2];
		uint8_t color[4];
	};
#pragma pack(pop)

	enum class VertexFormat : uint32_t
	{
		Unknown = 0,
		PNCV_F32,		// everything at 32 bits
		P32N8C8V16,		// position at 32 bits, normal at 8 bits, color at 8 bits, uvs at 16 bits float
		P32_V16N8C8,	// position in separate stream
	};

	struct MeshBounds {
		
		float origin[3];
		float radius;
		float extents[3];
	};

	struct MeshInfo
	{
		uint64_t vertexBuferSize[2];
		uint64_t indexBuferSize;
		MeshBounds bounds;
		VertexFormat vertexFormat;
		char indexSize;
		CompressionMode compressionMode;
		std::string originalFile;
	};

	MeshInfo read_mesh_info(AssetFile* file);

	void unpack_mesh(MeshInfo* info, const char* sourcebuffer, size_t sourceSize, char* vertexBufers[], int maxVB, char*& indexBuffer);

	AssetFile pack_mesh(MeshInfo* info, char* vertexData[], int numVB, char* indexData);

	template <class T>
	MeshBounds calculateBounds(T* vertices, size_t count)
	{
		MeshBounds bounds;

		float min[3] = { std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max() };
		float max[3] = { std::numeric_limits<float>::min(),std::numeric_limits<float>::min(),std::numeric_limits<float>::min() };

		for (int i = 0; i < count; i++)
		{
			min[0] = std::min(min[0], vertices[i].position[0]);
			min[1] = std::min(min[1], vertices[i].position[1]);
			min[2] = std::min(min[2], vertices[i].position[2]);

			max[0] = std::max(max[0], vertices[i].position[0]);
			max[1] = std::max(max[1], vertices[i].position[1]);
			max[2] = std::max(max[2], vertices[i].position[2]);
		}

		bounds.extents[0] = (max[0] - min[0]) / 2.0f;
		bounds.extents[1] = (max[1] - min[1]) / 2.0f;
		bounds.extents[2] = (max[2] - min[2]) / 2.0f;

		bounds.origin[0] = bounds.extents[0] + min[0];
		bounds.origin[1] = bounds.extents[1] + min[1];
		bounds.origin[2] = bounds.extents[2] + min[2];

		//go through the vertices again to calculate the exact bounding sphere radius
		float r2 = 0;
		for (int i = 0; i < count; i++) {

			float offset[3];
			offset[0] = vertices[i].position[0] - bounds.origin[0];
			offset[1] = vertices[i].position[1] - bounds.origin[1];
			offset[2] = vertices[i].position[2] - bounds.origin[2];

			//pithagoras
			float distance = offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
			r2 = std::max(r2, distance);
		}

		bounds.radius = std::sqrt(r2);

		return bounds;
	}

}