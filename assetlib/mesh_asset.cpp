#include "mesh_asset.h"
#include "json.hpp"
#include "lz4.h"


static assets::VertexFormat parse_format(const char* f) {

	if (strcmp(f, "PNCV_F32") == 0)
	{
		return assets::VertexFormat::PNCV_F32;
	}
	else if (strcmp(f, "P32N8C8V16") == 0)
	{
		return assets::VertexFormat::P32N8C8V16;
	}
	else if (strcmp(f, "P32_V16N8C8") == 0)
	{
		return assets::VertexFormat::P32_V16N8C8;
	}
	else
	{
		return assets::VertexFormat::Unknown;
	}
}

assets::MeshInfo assets::read_mesh_info(AssetFile* file)
{
	nlohmann::json metadata = nlohmann::json::parse(file->json);
	MeshInfo info;
	info.vertexBuferSize[0] = metadata["vertex_buffer_size0"];
	info.vertexBuferSize[1] = metadata["vertex_buffer_size1"];
	info.indexBuferSize = metadata["index_buffer_size"];
	info.indexSize = (uint8_t) metadata["index_size"];
	info.originalFile = metadata["original_file"];

	std::string compressionString = metadata["compression"];
	info.compressionMode = parse_compression(compressionString.c_str());

	std::vector<float> boundsData;
	boundsData.reserve(7);
	boundsData = metadata["bounds"].get<std::vector<float>>();

	info.bounds.origin[0] = boundsData[0];
	info.bounds.origin[1] = boundsData[1];
	info.bounds.origin[2] = boundsData[2];
		
	info.bounds.radius = boundsData[3];
	
	info.bounds.extents[0] = boundsData[4];
	info.bounds.extents[1] = boundsData[5];
	info.bounds.extents[2] = boundsData[6];

	std::string vertexFormat = metadata["vertex_format"];
	info.vertexFormat = parse_format(vertexFormat.c_str());
    return info;
}

void assets::unpack_mesh(MeshInfo* info, const char* sourcebuffer, size_t sourceSize, char* vertexBufers[], int maxVB, char*& indexBuffer)
{
	//decompressing into temporal vector. TODO: streaming decompress directly on the buffers
	static std::vector<char> decompressedBuffer(1024*1024*96,0);
	auto size = info->vertexBuferSize[0] + info->vertexBuferSize[1] + info->indexBuferSize;
	if (decompressedBuffer.size() < size)
		decompressedBuffer.resize(size);

	LZ4_decompress_safe(sourcebuffer, decompressedBuffer.data(), static_cast<int>(sourceSize), 	static_cast<int>(size));
	assert(maxVB >= 2);
	vertexBufers[0] = decompressedBuffer.data();
	vertexBufers[1] = decompressedBuffer.data() + info->vertexBuferSize[0];

	indexBuffer = decompressedBuffer.data() + info->vertexBuferSize[0] + +info->vertexBuferSize[1];
}

assets::AssetFile assets::pack_mesh(MeshInfo* info, char* vertexData[], int numVB, char* indexData)
{
    AssetFile file;
	file.type[0] = 'M';
	file.type[1] = 'E';
	file.type[2] = 'S';
	file.type[3] = 'H';
	file.version = 2;

	nlohmann::json metadata;
	if (info->vertexFormat == VertexFormat::P32N8C8V16) {
		metadata["vertex_format"] = "P32N8C8V16";
	}
	else if (info->vertexFormat == VertexFormat::PNCV_F32)
	{
		metadata["vertex_format"] = "PNCV_F32";
	}
	else if (info->vertexFormat == VertexFormat::P32_V16N8C8)
	{
		metadata["vertex_format"] = "P32_V16N8C8";
	}
	metadata["vertex_buffer_size0"] = info->vertexBuferSize[0];
	metadata["vertex_buffer_size1"] = info->vertexBuferSize[1];
	metadata["index_buffer_size"] = info->indexBuferSize;
	metadata["index_size"] = info->indexSize;
	metadata["original_file"] = info->originalFile;

	std::vector<float> boundsData;
	boundsData.resize(7);

	boundsData[0] = info->bounds.origin[0];
	boundsData[1] = info->bounds.origin[1];
	boundsData[2] = info->bounds.origin[2];

	boundsData[3] = info->bounds.radius;

	boundsData[4] = info->bounds.extents[0];
	boundsData[5] = info->bounds.extents[1];
	boundsData[6] = info->bounds.extents[2];

	metadata["bounds"] = boundsData;

	size_t fullsize = info->vertexBuferSize[0] + info->vertexBuferSize[1] + info->indexBuferSize;

	std::vector<char> merged_buffer;
	merged_buffer.resize(fullsize);

	assert(numVB >= 2);
	memcpy(merged_buffer.data(), vertexData[0], info->vertexBuferSize[0]);
	memcpy(merged_buffer.data() + info->vertexBuferSize[0], vertexData[1], info->vertexBuferSize[1]);
	memcpy(merged_buffer.data() + info->vertexBuferSize[0] + info->vertexBuferSize[1], indexData, info->indexBuferSize);

	//compress buffer and copy it into the file struct
	size_t compressStaging = LZ4_compressBound(static_cast<int>(fullsize));

	file.binaryBlob.resize(compressStaging);

	int compressedSize = LZ4_compress_default(merged_buffer.data(), file.binaryBlob.data(), static_cast<int>(merged_buffer.size()), static_cast<int>(compressStaging));
	file.binaryBlob.resize(compressedSize);

	metadata["compression"] = "LZ4";

	file.json = metadata.dump();

	return file;
}
