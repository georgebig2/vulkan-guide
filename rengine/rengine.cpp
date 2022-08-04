#define VMA_IMPLEMENTATION
#include "vk.h"

#include "rengine.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include "Tracy.hpp"
#include "TracyVulkan.hpp"

//#include "vk_profiler.h"
#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_descriptors.h>

//#include <vulkan/vulkan.h>

#include <vk_pushbuffer.h>

#include "fmt/core.h"
#include "fmt/os.h"
#include "fmt/color.h"

#include "vk_profiler.h"
#include "logger.h"
#include "cvars.h"
#include <vk_shaders.h>
#include <vk_scene.h>
#include "vk_textures.h"
#include "prefab_asset.h"
#include "material_asset.h"

constexpr bool bUseValidationLayers = false;

AutoCVar_Float CVAR_DrawDistance("gpu.drawDistance", "Distance cull", 5000);
AutoCVar_Int CVAR_FreezeCull("culling.freeze", "Locks culling", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_FreezeShadows("gpu.freezeShadows", "Stop the rendering of shadows", 0, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowBias("gpu.shadowBias", "Distance cull", 5.25f);
AutoCVar_Float CVAR_SlopeBias("gpu.shadowBiasSlope", "Distance cull", 4.75f);
AutoCVar_Int CVAR_Shadowcast("gpu.shadowcast", "Use shadowcasting", 1, CVarFlags::EditCheckbox);


//we want to immediately abort when there is an error. In normal engines this would give an error message to the user, or perform a dump of state.
using namespace std;
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


struct FrameData {
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	DeletionQueue _frameDeletionQueue;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	vkutil::PushBuffer dynamicData;
	//AllocatedBufferUntyped dynamicDataBuffer;

	AllocatedBufferUntyped debugOutputBuffer;

	vkutil::DescriptorAllocator* dynamicDescriptorAllocator;

	std::vector<uint32_t> debugDataOffsets;
	std::vector<std::string> debugDataNames;
};
FrameData _frames[FRAME_OVERLAP];

ShaderCache _shaderCache;
RenderScene _renderScene;
std::unordered_map<std::string, Texture> _loadedTextures;
std::unordered_map<std::string, Mesh> _meshes;
std::unordered_map<std::string, assets::PrefabInfo*> _prefabCache;


struct UploadContext {
	VkFence _uploadFence;
	VkCommandPool _commandPool;
};
UploadContext _uploadContext;

FrameData& REngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}


FrameData& REngine::get_last_frame()
{
	return _frames[(_frameNumber - 1) % 2];
}

template<typename T>
T* REngine::map_buffer(AllocatedBuffer<T>& buffer)
{
	void* data;
	vmaMapMemory(_allocator, buffer._allocation, &data);
	return(T*)data;
}

//void REngine::init()
//{
//	//ZoneScopedN("Engine Init");
//	//LogHandler::Get().set_time();
//	//LOG_INFO("Engine Init");
//	//init_vulkan();
//}

void REngine::update()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui::NewFrame();
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("Debug"))
		{
			if (ImGui::BeginMenu("CVAR"))
			{
				CVarSystem::Get()->DrawImguiEditor();
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("GRAPH"))
			{
				static bool perf = false;
				ImGui::Checkbox("Perf", &perf);
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	_mainLight.lightPosition = _camera.position;
	draw();
}

void REngine::init_scene()
{
	{
		glm::mat4 tr = glm::translate(glm::mat4{ 1.0 }, glm::vec3(0, 15, 0));
		glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(10.f));
		//glm::mat4 rot = glm::rotate(glm::radians(90.f), glm::vec3{ 1,0,0 });
		//glm::mat4 m = glm::scale(glm::mat4{ 1.0 }, glm::vec3(10));
		//load_prefab(asset_path("san.pfb").c_str(), scale*tr);
	}

	{
		glm::mat4 tr = glm::translate(glm::mat4{ 1.0 }, glm::vec3(0, 0, 0));
		glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.75f));
		glm::mat4 rot = glm::rotate(glm::radians(90.f), glm::vec3{ 1,0,0 });
		glm::mat4 m = glm::scale(glm::mat4{ 1.0 }, glm::vec3(1));;
		//load_prefab(asset_path("mine.pfb").c_str(), (scale * rot * tr));
	}

	int dimHelmets = 5;
	for (int x = -dimHelmets; x <= dimHelmets; x++) {
		for (int y = -dimHelmets; y <= dimHelmets; y++) {

			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x * 5, 10, y * 5));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(10));

			load_prefab(asset_path("FlightHelmet_GLTF/FlightHelmet.pfb").c_str(), (translation * scale));
		}
	}

	{
		glm::mat4 sponzaMatrix = glm::scale(glm::mat4{ 1.0 }, glm::vec3(1));;
		load_prefab(asset_path("Sponza.pfb").c_str(), sponzaMatrix);
	}

	//glm::mat4 unrealFixRotation = glm::rotate(glm::radians(-90.f), glm::vec3{ 1,0,0 });

	//load_prefab(asset_path("scifi/TopDownScifi.pfb").c_str(),  glm::translate(glm::vec3{0,20,0}));
	/*int dimcities = 2;
	for (int x = -dimcities; x <= dimcities; x++) {
		for (int y = -dimcities; y <= dimcities; y++) {

			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x * 300, y, y * 300));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(10));


			glm::mat4 cityMatrix = translation;// * glm::scale(glm::mat4{ 1.0f }, glm::vec3(.01f));
			//load_prefab(asset_path("scifi/TopDownScifi.pfb").c_str(), unrealFixRotation * glm::scale(glm::mat4{ 1.0 }, glm::vec3(.01)));
			//load_prefab(asset_path("PolyCity/PolyCity.pfb").c_str(), cityMatrix);
			//load_prefab(asset_path("CITY/polycity.pfb").c_str(), cityMatrix);
		//	load_prefab(asset_path("scifi/TopDownScifi.pfb").c_str(), cityMatrix);
		}
	}*/


	//for (int x = -20; x <= 20; x++) {
	//	for (int y = -20; y <= 20; y++) {
	//
	//		RenderObject tri;
	//		tri.mesh = get_mesh("triangle");
	//		tri.material = get_material("defaultmesh");
	//		glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x, 0, y));
	//		glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
	//		tri.transformMatrix = translation * scale;
	//
	//		refresh_renderbounds(&tri);
	//		_renderScene.register_object(&tri, PassTypeFlags::Forward);
	//	}
	//}
}


void REngine::init()
{
	vkb::InstanceBuilder builder;
	auto inst_ret = builder.set_app_name("REngine")
		.request_validation_layers(bUseValidationLayers)
		.use_default_debug_messenger()
		.build();
	//LOG_SUCCESS("Vulkan Instance initialized");

	vkb::Instance vkb_inst = inst_ret.value();
	_instance = vkb_inst.instance;

	create_surface(_instance, &_surface);

	VkPhysicalDeviceFeatures feats = {};
	feats.pipelineStatisticsQuery = true;
	feats.multiDrawIndirect = true;
	feats.drawIndirectFirstInstance = true;
	feats.samplerAnisotropy = true;

	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	selector.set_required_features(feats);

	vkb::PhysicalDevice physicalDevice = selector.set_surface(_surface)
		.set_minimum_version(1, 1)
		.add_required_extension(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME)
		.select()
		.value();
	//LOG_SUCCESS("GPU found");

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device vkbDevice = deviceBuilder.build().value();
	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	//initialize the memory allocator

	VmaVulkanFunctions vma_vulkan_func{};
	//vma_vulkan_func.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
	//vma_vulkan_func.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
	//vma_vulkan_func.vkAllocateMemory = vkAllocateMemory;
	//vma_vulkan_func.vkBindBufferMemory = vkBindBufferMemory;
	//vma_vulkan_func.vkBindImageMemory = vkBindImageMemory;
	//vma_vulkan_func.vkCreateBuffer = vkCreateBuffer;
	//vma_vulkan_func.vkCreateImage = vkCreateImage;
	//vma_vulkan_func.vkDestroyBuffer = vkDestroyBuffer;
	//vma_vulkan_func.vkDestroyImage = vkDestroyImage;
	//vma_vulkan_func.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
	//vma_vulkan_func.vkFreeMemory = vkFreeMemory;
	//vma_vulkan_func.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
	//vma_vulkan_func.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
	//vma_vulkan_func.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
	//vma_vulkan_func.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
	//vma_vulkan_func.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
	//vma_vulkan_func.vkMapMemory = vkMapMemory;
	//vma_vulkan_func.vkUnmapMemory = vkUnmapMemory;
	//vma_vulkan_func.vkCmdCopyBuffer = vkCmdCopyBuffer;

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.pVulkanFunctions = &vma_vulkan_func;
	
	vmaCreateAllocator(&allocatorInfo, &_allocator);


	vkGetPhysicalDeviceProperties(_chosenGPU, &_gpuProperties);
	//LOG_INFO("The gpu has a minimum buffer alignement of {}", _gpuProperties.limits.minUniformBufferOffsetAlignment);

	_shaderCache.init(_device);
	init_swapchain();
	init_descriptors();
	get_render_scene()->init();
	init_forward_renderpass();
	init_copy_renderpass();
	init_shadow_renderpass();
	init_framebuffers();
	init_commands();
	init_sync_structures();
	init_pipelines();

	init_imgui();
	load_meshes();

	load_image_to_cache("white", asset_path("Sponza/white.tx").c_str());

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_NEAREST);

	VkSampler blockySampler;
	vkCreateSampler(_device, &samplerInfo, nullptr, &blockySampler);

	samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);

	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	//info.anisotropyEnable = true;
	samplerInfo.mipLodBias = 2;
	samplerInfo.maxLod = 30.f;
	samplerInfo.minLod = 3;
	VkSampler smoothSampler;

	vkCreateSampler(_device, &samplerInfo, nullptr, &smoothSampler);

	{
		vkutil::MaterialData texturedInfo;
		texturedInfo.baseTemplate = "texturedPBR_opaque";
		texturedInfo.parameters = nullptr;

		vkutil::SampledTexture whiteTex;
		whiteTex.sampler = smoothSampler;
		whiteTex.view = _loadedTextures["white"].imageView;

		texturedInfo.textures.push_back(whiteTex);

		vkutil::Material* newmat = _materialSystem->build_material("textured", texturedInfo);
	}
	{
		vkutil::MaterialData matinfo;
		matinfo.baseTemplate = "texturedPBR_opaque";
		matinfo.parameters = nullptr;

		vkutil::SampledTexture whiteTex;
		whiteTex.sampler = smoothSampler;
		whiteTex.view = _loadedTextures["white"].imageView;

		matinfo.textures.push_back(whiteTex);

		vkutil::Material* newmat = _materialSystem->build_material("default", matinfo);

	}

	init_scene();

	LOG_INFO("Engine Initialized, starting Load");


	get_render_scene()->merge_meshes(this);

	get_render_scene()->build_batches();

	_isInitialized = true;

	_camera = {};
	_camera.position = { 0.f,6.f,5.f };

	_mainLight.lightPosition = { 0,50,0 };
	_mainLight.lightDirection = glm::vec3(0.3, -1, 0.3);
	_mainLight.shadowExtent = { 700 ,700 ,700 };
}

Mesh* REngine::get_mesh(const std::string& name)
{
	auto it = _meshes.find(name);
	if (it == _meshes.end()) {
		return nullptr;
	}
	else {
		return &(*it).second;
	}
}

std::string REngine::asset_path(std::string_view path)
{
	return "../../assets_export/" + std::string(path);
}

void REngine::refresh_renderbounds(MeshObject* object)
{
	//dont try to update invalid bounds
	if (!object->mesh->bounds.valid) return;

	RenderBounds originalBounds = object->mesh->bounds;

	//convert bounds to 8 vertices, and transform those
	std::array<glm::vec3, 8> boundsVerts;

	for (int i = 0; i < 8; i++) {
		boundsVerts[i] = originalBounds.origin;
	}

	boundsVerts[0] += originalBounds.extents * glm::vec3(1, 1, 1);
	boundsVerts[1] += originalBounds.extents * glm::vec3(1, 1, -1);
	boundsVerts[2] += originalBounds.extents * glm::vec3(1, -1, 1);
	boundsVerts[3] += originalBounds.extents * glm::vec3(1, -1, -1);
	boundsVerts[4] += originalBounds.extents * glm::vec3(-1, 1, 1);
	boundsVerts[5] += originalBounds.extents * glm::vec3(-1, 1, -1);
	boundsVerts[6] += originalBounds.extents * glm::vec3(-1, -1, 1);
	boundsVerts[7] += originalBounds.extents * glm::vec3(-1, -1, -1);

	//recalc max/min
	glm::vec3 min{ std::numeric_limits<float>().max() };
	glm::vec3 max{ -std::numeric_limits<float>().max() };

	glm::mat4 m = object->transformMatrix;

	//transform every vertex, accumulating max/min
	for (int i = 0; i < 8; i++) {
		boundsVerts[i] = m * glm::vec4(boundsVerts[i], 1.f);

		min = glm::min(boundsVerts[i], min);
		max = glm::max(boundsVerts[i], max);
	}

	glm::vec3 extents = (max - min) / 2.f;
	glm::vec3 origin = min + extents;

	float max_scale = 0;
	max_scale = std::max(glm::length(glm::vec3(m[0][0], m[0][1], m[0][2])), max_scale);
	max_scale = std::max(glm::length(glm::vec3(m[1][0], m[1][1], m[1][2])), max_scale);
	max_scale = std::max(glm::length(glm::vec3(m[2][0], m[2][1], m[2][2])), max_scale);

	float radius = max_scale * originalBounds.radius;


	object->bounds.extents = extents;
	object->bounds.origin = origin;
	object->bounds.radius = radius;
	object->bounds.valid = true;
}

bool REngine::load_image_to_cache(const char* name, const char* path)
{
	ZoneScopedNC("Load Texture", tracy::Color::Yellow);
	Texture newtex;

	if (_loadedTextures.find(name) != _loadedTextures.end()) return true;

	bool result = vkutil::load_image_from_asset(*this, path, newtex.image);

	if (!result)
	{
		LOG_ERROR("Error When texture {} at path {}", name, path);
		return false;
	}
	else {
		LOG_SUCCESS("Loaded texture {} at path {}", name, path);
	}
	newtex.imageView = newtex.image._defaultView;
	//VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, newtex.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	//imageinfo.subresourceRange.levelCount = newtex.image.mipLevels;
	//vkCreateImageView(_device, &imageinfo, nullptr, &newtex.imageView);

	_loadedTextures[name] = newtex;
	return true;
}

bool REngine::load_prefab(const char* path, glm::mat4 root)
{
	int rng = rand();

	ZoneScopedNC("Load Prefab", tracy::Color::Red);

	auto pf = _prefabCache.find(path);
	if (pf == _prefabCache.end())
	{
		assets::AssetFile file;
		bool loaded = assets::load_binaryfile(path, file);

		if (!loaded) {
			LOG_FATAL("Error When loading prefab file at path {}", path);
			return false;
		}
		else {
			LOG_SUCCESS("Prefab {} loaded to cache", path);
		}

		_prefabCache[path] = new assets::PrefabInfo;
		*_prefabCache[path] = assets::read_prefab_info(&file);
	}

	assets::PrefabInfo* prefab = _prefabCache[path];

	std::unordered_map<uint64_t, glm::mat4> node_worldmats;
	std::vector<std::pair<uint64_t, glm::mat4>> pending_nodes;
	for (auto& [k, v] : prefab->node_matrices)
	{
		glm::mat4 nodematrix{ 1.f };

		auto& nm = prefab->matrices[v];
		memcpy(&nodematrix, &nm, sizeof(glm::mat4));

		//check if it has parents
		auto matrixIT = prefab->node_parents.find(k);
		if (matrixIT == prefab->node_parents.end()) {
			//add to worldmats 
			node_worldmats[k] = root * nodematrix;
		}
		else {
			//enqueue
			pending_nodes.push_back({ k,nodematrix });
		}
	}

	//process pending nodes list until it empties
	while (pending_nodes.size() > 0)
	{
		for (int i = 0; i < pending_nodes.size(); i++)
		{
			uint64_t node = pending_nodes[i].first;
			uint64_t parent = prefab->node_parents[node];

			//try to find parent in cache
			auto matrixIT = node_worldmats.find(parent);
			if (matrixIT != node_worldmats.end()) {

				//transform with the parent
				glm::mat4 nodematrix = (matrixIT)->second * pending_nodes[i].second;

				node_worldmats[node] = nodematrix;

				//remove from queue, pop last
				pending_nodes[i] = pending_nodes.back();
				pending_nodes.pop_back();
				i--;
			}
		}

	}

	std::vector<MeshObject> prefab_renderables;
	prefab_renderables.reserve(prefab->node_meshes.size());

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	VkSampler smoothSampler;
	vkCreateSampler(_device, &samplerInfo, nullptr, &smoothSampler);

	for (auto& [k, v] : prefab->node_meshes)
	{
		//load mesh
		if (v.mesh_path.find("Sky") != std::string::npos) {
			continue;
		}
		if (!get_mesh(v.mesh_path.c_str()))
		{
			Mesh mesh{};
			mesh.load_from_meshasset(asset_path(v.mesh_path).c_str());
			upload_mesh(mesh);
			_meshes[v.mesh_path.c_str()] = mesh;
		}

		//load material
		auto materialName = v.material_path.c_str();
		vkutil::Material* objectMaterial = _materialSystem->get_material(materialName);
		if (!objectMaterial)
		{
			assets::AssetFile materialFile;
			bool loaded = assets::load_binaryfile(asset_path(materialName).c_str(), materialFile);

			if (loaded)
			{
				assets::MaterialInfo material = assets::read_material_info(&materialFile);
				auto texture = material.textures["baseColor"];
				if (texture.size() <= 3)
				{
					texture = "Sponza/white.tx";
				}

				loaded = load_image_to_cache(texture.c_str(), asset_path(texture).c_str());
				if (loaded)
				{
					vkutil::SampledTexture tex;
					tex.view = _loadedTextures[texture].imageView;
					tex.sampler = smoothSampler;

					vkutil::MaterialData info;
					info.parameters = nullptr;

					if (material.transparency == assets::TransparencyMode::Transparent)
					{
						info.baseTemplate = "texturedPBR_transparent";
					}
					else {
						info.baseTemplate = "texturedPBR_opaque";
					}

					info.textures.push_back(tex);

					objectMaterial = _materialSystem->build_material(materialName, info);

					if (!objectMaterial)
					{
						LOG_ERROR("Error When building material {}", v.material_path);
					}
				}
				else
				{
					LOG_ERROR("Error When loading image at {}", v.material_path);
				}
			}
			else
			{
				LOG_ERROR("Error When loading material at path {}", v.material_path);
			}
		}

		MeshObject loadmesh;
		//transparent objects will be invisible

		loadmesh.bDrawForwardPass = true;
		loadmesh.bDrawShadowPass = true;


		glm::mat4 nodematrix = root;// { 1.f };
		auto matrixIT = node_worldmats.find(k);
		if (matrixIT != node_worldmats.end()) {
			auto& nm = (*matrixIT).second;
			memcpy(&nodematrix, &nm, sizeof(glm::mat4));
		}
		else
		{
			//memcpy(&nodematrix, &root, sizeof(glm::mat4));
		}

		loadmesh.mesh = get_mesh(v.mesh_path.c_str());
		loadmesh.transformMatrix = nodematrix;
		loadmesh.material = objectMaterial;

		refresh_renderbounds(&loadmesh);

		//sort key from location
		int32_t lx = int(loadmesh.bounds.origin.x / 10.f);
		int32_t ly = int(loadmesh.bounds.origin.y / 10.f);

		uint32_t key = uint32_t(std::hash<int32_t>()(lx) ^ std::hash<int32_t>()(ly ^ 1337));

		loadmesh.customSortKey = 0;// rng;// key;


		prefab_renderables.push_back(loadmesh);
		//_renderables.push_back(loadmesh);
	}

	get_render_scene()->register_object_batch(prefab_renderables.data(), static_cast<uint32_t>(prefab_renderables.size()));
	return true;
}


void REngine::load_meshes()
{
	_meshes.reserve(1000);

	Mesh triMesh{};
	triMesh.bounds.valid = false;
	//make the array 3 vertices long
	triMesh._vertices.resize(3);

	//vertex positions
	triMesh._vertices[0].position = { 1.f,1.f, 0.0f };
	triMesh._vertices[1].position = { -1.f,1.f, 0.0f };
	triMesh._vertices[2].position = { 0.f,-1.f, 0.0f };

	//vertex colors, all green
	triMesh._vertices[0].color = { 0.f,1.f, 0.0f }; //pure green
	triMesh._vertices[1].color = { 0.f,1.f, 0.0f }; //pure green
	triMesh._vertices[2].color = { 0.f,1.f, 0.0f }; //pure green
	//we dont care about the vertex normals
	upload_mesh(triMesh);
	_meshes["triangle"] = triMesh;
}

void REngine::upload_mesh(Mesh& mesh)
{
	ZoneScopedNC("Upload Mesh", tracy::Color::Orange);

	const size_t vertex_buffer_size = mesh._vertices.size() * sizeof(Vertex);
	const size_t index_buffer_size = mesh._indices.size() * sizeof(uint32_t);
	const size_t bufferSize = vertex_buffer_size + index_buffer_size;
	//allocate vertex buffer
	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	//this is the total size, in bytes, of the buffer we are allocating
	vertexBufferInfo.size = vertex_buffer_size;
	//this buffer is going to be used as a Vertex Buffer
	vertexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	//allocate vertex buffer
	VkBufferCreateInfo indexBufferInfo = {};
	indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexBufferInfo.pNext = nullptr;
	//this is the total size, in bytes, of the buffer we are allocating
	indexBufferInfo.size = index_buffer_size;
	//this buffer is going to be used as a Vertex Buffer
	indexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	//let the VMA library know that this data should be writeable by CPU, but also readable by GPU
	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBufferUntyped stagingBuffer;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &vertexBufferInfo, &vmaallocInfo,
		&mesh._vertexBuffer._buffer,
		&mesh._vertexBuffer._allocation,
		nullptr));
	//copy vertex data
	char* data;
	vmaMapMemory(_allocator, mesh._vertexBuffer._allocation, (void**)&data);
	memcpy(data, mesh._vertices.data(), vertex_buffer_size);
	vmaUnmapMemory(_allocator, mesh._vertexBuffer._allocation);

	if (index_buffer_size != 0)
	{
		//allocate the buffer
		VK_CHECK(vmaCreateBuffer(_allocator, &indexBufferInfo, &vmaallocInfo,
			&mesh._indexBuffer._buffer,
			&mesh._indexBuffer._allocation,
			nullptr));
		vmaMapMemory(_allocator, mesh._indexBuffer._allocation, (void**)&data);
		memcpy(data, mesh._indices.data(), index_buffer_size);
		vmaUnmapMemory(_allocator, mesh._indexBuffer._allocation);
	}
}

void REngine::init_pipelines()
{
	_materialSystem = new vkutil::MaterialSystem();
	_materialSystem->init(this);
	_materialSystem->build_default_templates();

	//fullscreen triangle pipeline for blits
	ShaderEffect* blitEffect = new ShaderEffect();
	blitEffect->add_stage(get_shader_cache()->get_shader(shader_path("fullscreen.vert.spv")), VK_SHADER_STAGE_VERTEX_BIT);
	blitEffect->add_stage(get_shader_cache()->get_shader(shader_path("blit.frag.spv")), VK_SHADER_STAGE_FRAGMENT_BIT);
	blitEffect->reflect_layout(_device, nullptr, 0);


	PipelineBuilder pipelineBuilder;

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
	pipelineBuilder._rasterizer.cullMode = VK_CULL_MODE_NONE;
	//we dont use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();


	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	//build blit pipeline
	pipelineBuilder.setShaders(blitEffect);

	//blit pipeline uses hardcoded triangle so no need for vertex input
	pipelineBuilder.clear_vertex_input();

	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(false, false, VK_COMPARE_OP_ALWAYS);

	_blitPipeline = pipelineBuilder.build_pipeline(_device, _copyPass);
	_blitLayout = blitEffect->builtLayout;

	_mainDeletionQueue.push_function([=]() {
		//vkDestroyPipeline(_device, meshPipeline, nullptr);
		vkDestroyPipeline(_device, _blitPipeline, nullptr);
		});


	//load the compute shaders
	load_compute_shader(shader_path("indirect_cull.comp.spv").c_str(), _cullPipeline, _cullLayout);
	load_compute_shader(shader_path("depthReduce.comp.spv").c_str(), _depthReducePipeline, _depthReduceLayout);
	load_compute_shader(shader_path("sparse_upload.comp.spv").c_str(), _sparseUploadPipeline, _sparseUploadLayout);
}


bool REngine::load_compute_shader(const char* shaderPath, VkPipeline& pipeline, VkPipelineLayout& layout)
{
	ShaderModule computeModule;
	if (!vkutil::load_shader_module(_device, shaderPath, &computeModule))

	{
		std::cout << "Error when building compute shader shader module" << std::endl;
		return false;
	}

	ShaderEffect* computeEffect = new ShaderEffect();;
	computeEffect->add_stage(&computeModule, VK_SHADER_STAGE_COMPUTE_BIT);

	computeEffect->reflect_layout(_device, nullptr, 0);

	ComputePipelineBuilder computeBuilder;
	computeBuilder._pipelineLayout = computeEffect->builtLayout;
	computeBuilder._shaderStage = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_COMPUTE_BIT, computeModule.module);


	layout = computeEffect->builtLayout;
	pipeline = computeBuilder.build_pipeline(_device);

	vkDestroyShaderModule(_device, computeModule.module, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_device, pipeline, nullptr);

		vkDestroyPipelineLayout(_device, layout, nullptr);
		});

	return true;
}


void REngine::init_framebuffers()
{
	const uint32_t swapchain_imagecount = static_cast<uint32_t>(_swapchainImages.size());
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	//create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
	VkFramebufferCreateInfo fwd_info = vkinit::framebuffer_create_info(_renderPass, _windowExtent);
	VkImageView attachments[2];
	attachments[0] = _rawRenderImage._defaultView;
	attachments[1] = _depthImage._defaultView;

	fwd_info.pAttachments = attachments;
	fwd_info.attachmentCount = 2;
	VK_CHECK(vkCreateFramebuffer(_device, &fwd_info, nullptr, &_forwardFramebuffer));

	//create the framebuffer for shadow pass	
	VkFramebufferCreateInfo sh_info = vkinit::framebuffer_create_info(_shadowPass, _shadowExtent);
	sh_info.pAttachments = &_shadowImage._defaultView;
	sh_info.attachmentCount = 1;
	VK_CHECK(vkCreateFramebuffer(_device, &sh_info, nullptr, &_shadowFramebuffer));

	for (uint32_t i = 0; i < swapchain_imagecount; i++) {

		//create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
		VkFramebufferCreateInfo fb_info = vkinit::framebuffer_create_info(_copyPass, _windowExtent);
		fb_info.pAttachments = &_swapchainImageViews[i];
		fb_info.attachmentCount = 1;
		VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			});
	}
}

void REngine::init_shadow_renderpass()
{
	VkAttachmentDescription depth_attachment = {};
	// Depth attachment
	depth_attachment.flags = 0;
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 0;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

	//hook the depth attachment into the subpass
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	//1 dependency, which is from "outside" into the subpass. And we can read or write color
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	//2 attachments from said array
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &depth_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_shadowPass));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(_device, _shadowPass, nullptr);
		});
}


void REngine::init_copy_renderpass()
{
	//we define an attachment description for our main color image
//the attachment is loaded as "clear" when renderpass start
//the attachment is stored when renderpass ends
//the attachment layout starts as "undefined", and transitions to "Present" so its possible to display it
//we dont care about stencil, and dont use multisampling

	VkAttachmentDescription color_attachment = {};
	color_attachment.format = _swachainImageFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	//1 dependency, which is from "outside" into the subpass. And we can read or write color
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;


	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	//2 attachments from said array
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	//render_pass_info.dependencyCount = 1;
	//render_pass_info.pDependencies = &dependency;

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_copyPass));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(_device, _copyPass, nullptr);
		});
}

void REngine::init_imgui()
{
	//1: create descriptor pool for IMGUI
	// the size of the pool is very oversize, but its copied from imgui demo itself.
	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = 11;// std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));


	// 2: initialize imgui library

	//this initializes the core structures of imgui
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = NULL;

	//this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;

	ImGui_ImplVulkan_Init(&init_info, _renderPass);

	//execute a gpu command to upload imgui font textures
	immediate_submit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
		});

	//clear font textures from cpu data
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	//add the destroy the imgui created structures
	_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});

}


void REngine::init_forward_renderpass()
{
	//we define an attachment description for our main color image
	//the attachment is loaded as "clear" when renderpass start
	//the attachment is stored when renderpass ends
	//the attachment layout starts as "undefined", and transitions to "Present" so its possible to display it
	//we dont care about stencil, and dont use multisampling

	VkAttachmentDescription color_attachment = {};
	color_attachment.format = _renderFormat;//_swachainImageFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;//PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};
	// Depth attachment
	depth_attachment.flags = 0;
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 1;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	//hook the depth attachment into the subpass
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	//1 dependency, which is from "outside" into the subpass. And we can read or write color
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;


	//array of 2 attachments, one for the color, and other for depth
	VkAttachmentDescription attachments[2] = { color_attachment,depth_attachment };

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	//2 attachments from said array
	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	//render_pass_info.dependencyCount = 1;
	//render_pass_info.pDependencies = &dependency;

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(_device, _renderPass, nullptr);
		});
}


ShaderCache* REngine::get_shader_cache()
{
	return &_shaderCache;
}

RenderScene* REngine::get_render_scene()
{
	return &_renderScene;
}

void REngine::cleanup()
{
	if (_isInitialized) {

		//make sure the gpu has stopped doing its things
		for (auto& frame : _frames)
		{
			vkWaitForFences(_device, 1, &frame._renderFence, true, 1000000000);
		}

		_mainDeletionQueue.flush();

		for (auto& frame : _frames)
		{
			frame.dynamicDescriptorAllocator->cleanup();
		}

		_descriptorAllocator->cleanup();
		_descriptorLayoutCache->cleanup();


		vkDestroySurfaceKHR(_instance, _surface, nullptr);

		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);
	}
}

std::string REngine::shader_path(std::string_view path)
{
	return "../../shaders/" + std::string(path);
}


uint32_t previousPow2(uint32_t v)
{
	uint32_t r = 1;

	while (r * 2 < v)
		r *= 2;

	return r;
}

uint32_t getImageMipLevels(uint32_t width, uint32_t height)
{
	uint32_t result = 1;

	while (width > 1 || height > 1)
	{
		result++;
		width /= 2;
		height /= 2;
	}

	return result;
}

glm::mat4 DirectionalLight::get_projection()
{
	glm::mat4 projection = glm::orthoLH_ZO(-shadowExtent.x, shadowExtent.x, -shadowExtent.y, shadowExtent.y, -shadowExtent.z, shadowExtent.z);
	return projection;
}

glm::mat4 DirectionalLight::get_view()
{
	glm::vec3 camPos = lightPosition;

	glm::vec3 camFwd = lightDirection;

	glm::mat4 view = glm::lookAt(camPos, camPos + camFwd, glm::vec3(1, 0, 0));
	return view;
}

void REngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

	VkSurfaceFormatKHR format = {};
	format.format = VK_FORMAT_R8G8B8A8_SRGB;
	format.colorSpace = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		//.use_default_format_selection()
		.set_desired_format(format)
		.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
	_swachainImageFormat = vkbSwapchain.image_format;

	//render image
	{
		//depth image size will match the window
		VkExtent3D renderImageExtent = {
			_windowExtent.width,
			_windowExtent.height,
			1
		};
		_renderFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		VkImageCreateInfo ri_info = vkinit::image_create_info(_renderFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, renderImageExtent);

		//for the depth image, we want to allocate it from gpu local memory
		VmaAllocationCreateInfo dimg_allocinfo = {};
		dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		//allocate and create the image
		vmaCreateImage(_allocator, &ri_info, &dimg_allocinfo, &_rawRenderImage._image, &_rawRenderImage._allocation, nullptr);

		//build a image-view for the depth image to use for rendering
		VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_renderFormat, _rawRenderImage._image, VK_IMAGE_ASPECT_COLOR_BIT);

		VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_rawRenderImage._defaultView));
	}

	_mainDeletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
		});

	//depth image size will match the window
	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	VkExtent3D shadowExtent = {
		_shadowExtent.width,
		_shadowExtent.height,
		1
	};

	//hardcoding the depth format to 32 bit float
	_depthFormat = VK_FORMAT_D32_SFLOAT;

	//for the depth image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// depth image ------ 
	{
		//the depth image will be a image with the format we selected and Depth Attachment usage flag
		VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, depthImageExtent);

		vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);
		VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);;
		VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage._defaultView));
	}
	//shadow image
	{
		//the depth image will be a image with the format we selected and Depth Attachment usage flag
		VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowExtent);

		vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_shadowImage._image, &_shadowImage._allocation, nullptr);
		VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _shadowImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);
		VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_shadowImage._defaultView));
	}


	// Note: previousPow2 makes sure all reductions are at most by 2x2 which makes sure they are conservative
	depthPyramidWidth = previousPow2(_windowExtent.width);
	depthPyramidHeight = previousPow2(_windowExtent.height);
	depthPyramidLevels = getImageMipLevels(depthPyramidWidth, depthPyramidHeight);

	VkExtent3D pyramidExtent = {
		static_cast<uint32_t>(depthPyramidWidth),
		static_cast<uint32_t>(depthPyramidHeight),
		1
	};
	//the depth image will be a image with the format we selected and Depth Attachment usage flag
	VkImageCreateInfo pyramidInfo = vkinit::image_create_info(VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, pyramidExtent);

	pyramidInfo.mipLevels = depthPyramidLevels;

	vmaCreateImage(_allocator, &pyramidInfo, &dimg_allocinfo, &_depthPyramid._image, &_depthPyramid._allocation, nullptr);
	VkImageViewCreateInfo priview_info = vkinit::imageview_create_info(VK_FORMAT_R32_SFLOAT, _depthPyramid._image, VK_IMAGE_ASPECT_COLOR_BIT);
	priview_info.subresourceRange.levelCount = depthPyramidLevels;
	VK_CHECK(vkCreateImageView(_device, &priview_info, nullptr, &_depthPyramid._defaultView));

	for (int32_t i = 0; i < depthPyramidLevels; ++i)
	{
		VkImageViewCreateInfo level_info = vkinit::imageview_create_info(VK_FORMAT_R32_SFLOAT, _depthPyramid._image, VK_IMAGE_ASPECT_COLOR_BIT);
		level_info.subresourceRange.levelCount = 1;
		level_info.subresourceRange.baseMipLevel = i;

		VkImageView pyramid;
		vkCreateImageView(_device, &level_info, nullptr, &pyramid);
		depthPyramidMips[i] = pyramid;
		assert(depthPyramidMips[i]);
	}


	VkSamplerCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.magFilter = VK_FILTER_LINEAR;
	createInfo.minFilter = VK_FILTER_LINEAR;
	createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.minLod = 0;
	createInfo.maxLod = 16.f;

	VkSamplerReductionModeCreateInfoEXT createInfoReduction = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT };

	auto reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN_EXT;
	if (reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT)
	{
		createInfoReduction.reductionMode = reductionMode;
		createInfo.pNext = &createInfoReduction;
	}
	VK_CHECK(vkCreateSampler(_device, &createInfo, 0, &_depthSampler));

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	vkCreateSampler(_device, &samplerInfo, nullptr, &_smoothSampler);

	VkSamplerCreateInfo shadsamplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
	shadsamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	shadsamplerInfo.compareEnable = true;
	shadsamplerInfo.compareOp = VK_COMPARE_OP_LESS;
	vkCreateSampler(_device, &shadsamplerInfo, nullptr, &_shadowSampler);


	//add to deletion queues
	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, _depthImage._defaultView, nullptr);
		vmaDestroyImage(_allocator, _depthImage._image, _depthImage._allocation);
		});
}


AllocatedBufferUntyped create_buffer(REngine* engine, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags required_flags = 0)
{
	//allocate vertex buffer
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;


	//let the VMA library know that this data should be writeable by CPU, but also readable by GPU
	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.requiredFlags = required_flags;
	AllocatedBufferUntyped newBuffer;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(engine->_allocator, &bufferInfo, &vmaallocInfo,
		&newBuffer._buffer,
		&newBuffer._allocation,
		nullptr));
	newBuffer._size = allocSize;
	return newBuffer;
}


void REngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	
	ZoneScopedNC("Inmediate Submit", tracy::Color::White);

	VkCommandBuffer cmd;

	//allocate the default command buffer that we will use for rendering
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &cmd));

	//begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);
	

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);


	//submit command buffer to the queue and execute it.
	// _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _uploadContext._uploadFence));

	vkWaitForFences(_device, 1, &_uploadContext._uploadFence, true, 9999999999);
	vkResetFences(_device, 1, &_uploadContext._uploadFence);

	vkResetCommandPool(_device, _uploadContext._commandPool, 0);
}

void REngine::init_commands()
{
	//create a command pool for commands submitted to the graphics queue.
	//we also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);


	for (int i = 0; i < FRAME_OVERLAP; i++) {


		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		//allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
			});


	}
	
	//_graphicsQueueContext = TracyVkContext(_chosenGPU, _device, _graphicsQueue, _frames[0]._mainCommandBuffer);


	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily);
	//create pool for upload context
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext._commandPool));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _uploadContext._commandPool, nullptr);
		});
}



void REngine::init_sync_structures()
{
	//create syncronization structures
	//one fence to control when the gpu has finished rendering the frame,
	//and 2 semaphores to syncronize rendering with swapchain
	//we want the fence to start signalled so we can wait on it on the first frame
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++) {

		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		//enqueue the destruction of the fence
		_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
			});


		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		//enqueue the destruction of semaphores
		_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
			});
	}


	VkFenceCreateInfo uploadFenceCreateInfo = vkinit::fence_create_info();

	VK_CHECK(vkCreateFence(_device, &uploadFenceCreateInfo, nullptr, &_uploadContext._uploadFence));
	_mainDeletionQueue.push_function([=]() {
		vkDestroyFence(_device, _uploadContext._uploadFence, nullptr);
		});
}

size_t REngine::pad_uniform_buffer_size(size_t originalSize)
{
	// Calculate required alignment based on minimum device offset alignment
	size_t minUboAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if (minUboAlignment > 0) {
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}
	return alignedSize;
}

void REngine::init_descriptors()
{
	_descriptorAllocator = new vkutil::DescriptorAllocator{};
	_descriptorAllocator->init(_device);

	_descriptorLayoutCache = new vkutil::DescriptorLayoutCache{};
	_descriptorLayoutCache->init(_device);


	VkDescriptorSetLayoutBinding textureBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set3info = {};
	set3info.bindingCount = 1;
	set3info.flags = 0;
	set3info.pNext = nullptr;
	set3info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set3info.pBindings = &textureBind;

	_singleTextureSetLayout = _descriptorLayoutCache->create_descriptor_layout(&set3info);

	const size_t sceneParamBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(GPUSceneData));


	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		_frames[i].dynamicDescriptorAllocator = new vkutil::DescriptorAllocator{};
		_frames[i].dynamicDescriptorAllocator->init(_device);

		//1 megabyte of dynamic data buffer
		auto dynamicDataBuffer = create_buffer(this, 1000000, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
		_frames[i].dynamicData.init(_allocator, dynamicDataBuffer, _gpuProperties.limits.minUniformBufferOffsetAlignment);

		//20 megabyte of debug output
		_frames[i].debugOutputBuffer = create_buffer(this, 200000000, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
	}
}

void REngine::draw()
{
	ZoneScopedN("Engine Draw");

	stats.drawcalls = 0;
	stats.draws = 0;
	stats.objects = 0;
	stats.triangles = 0;


	ImGui::Render();

	{
		ZoneScopedN("Fence Wait");
		//wait until the gpu has finished rendering the last frame. Timeout of 1 second
		VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
		VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

		get_current_frame().dynamicData.reset();

		_renderScene.build_batches();

		//check the debug data
		void* data;
		vmaMapMemory(_allocator, get_current_frame().debugOutputBuffer._allocation, &data);
		for (int i = 1; i < get_current_frame().debugDataNames.size(); i++)
		{
			uint32_t begin = get_current_frame().debugDataOffsets[i - 1];
			uint32_t end = get_current_frame().debugDataOffsets[i];

			auto name = get_current_frame().debugDataNames[i];
			if (name.compare("Cull Indirect Output") == 0)
			{
				void* buffer = malloc(end - begin);
				memcpy(buffer, (uint8_t*)data + begin, end - begin);

				GPUIndirectObject* objects = (GPUIndirectObject*)buffer;
				int objectCount = (end - begin) / sizeof(GPUIndirectObject);

				std::string filename = fmt::format("{}_CULLDATA_{}.txt", _frameNumber, i);

				auto out = fmt::output_file(filename);
				for (int o = 0; o < objectCount; o++)
				{
					out.print("DRAW: {} ------------ \n", o);
					out.print("	OG Count: {} \n", _renderScene._forwardPass.batches[o].count);
					out.print("	Visible Count: {} \n", objects[o].command.instanceCount);
					out.print("	First: {} \n", objects[o].command.firstInstance);
					out.print("	Indices: {} \n", objects[o].command.indexCount);
				}

				free(buffer);
			}
		}

		vmaUnmapMemory(_allocator, get_current_frame().debugOutputBuffer._allocation);
		get_current_frame().debugDataNames.clear();
		get_current_frame().debugDataOffsets.clear();

		get_current_frame().debugDataNames.push_back("");
		get_current_frame().debugDataOffsets.push_back(0);
	}
	get_current_frame()._frameDeletionQueue.flush();
	get_current_frame().dynamicDescriptorAllocator->reset_pools();

	//now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));
	uint32_t swapchainImageIndex;
	{
		ZoneScopedN("Aquire Image");
		//request image from the swapchain
		VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, -1, get_current_frame()._presentSemaphore, nullptr, &swapchainImageIndex));
	}

	//naming it cmd for shorter writing
	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

	//begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	//make a clear-color from frame number. This will flash with a 120 frame period.
	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { { 0.1f, 0.1f, 0.1f, 1.0f } };

	_profiler->grab_queries(cmd);

	{
		postCullBarriers.clear();
		cullReadyBarriers.clear();

		//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "gpu frame");
		ZoneScopedNC("Render Frame", tracy::Color::White);

		vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu frame");

		{
			vkutil::VulkanScopeTimer timer2(cmd, _profiler, "gpu ready");

			ready_mesh_draw(cmd);

			ready_cull_data(_renderScene._forwardPass, cmd);
			ready_cull_data(_renderScene._transparentForwardPass, cmd);
			ready_cull_data(_renderScene._shadowPass, cmd);

			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, cullReadyBarriers.size(), cullReadyBarriers.data(), 0, nullptr);
		}

		{
			CullParams forwardCull;
			forwardCull.projmat = _camera.get_projection_matrix(true);
			forwardCull.viewmat = _camera.get_view_matrix();
			forwardCull.frustrumCull = true;
			forwardCull.occlusionCull = true;
			forwardCull.drawDist = CVAR_DrawDistance.Get();
			forwardCull.aabb = false;
			execute_compute_cull(cmd, _renderScene._forwardPass, forwardCull);
			execute_compute_cull(cmd, _renderScene._transparentForwardPass, forwardCull);
		}

		//glm::vec3 extent = _mainLight.shadowExtent * 10.f;
		//glm::mat4 projection = glm::orthoLH_ZO(-extent.x, extent.x, -extent.y, extent.y, -extent.z, extent.z);
		{
			if (*CVarSystem::Get()->GetIntCVar("gpu.shadowcast"))
			{
				vkutil::VulkanScopeTimer timer2(cmd, _profiler, "gpu shadow cull");

				CullParams shadowCull;
				shadowCull.projmat = _mainLight.get_projection();
				shadowCull.viewmat = _mainLight.get_view();
				shadowCull.frustrumCull = true;
				shadowCull.occlusionCull = false;
				shadowCull.drawDist = 9999999;
				shadowCull.aabb = true;

				glm::vec3 aabbcenter = _mainLight.lightPosition;
				glm::vec3 aabbextent = _mainLight.shadowExtent * 1.5f;
				shadowCull.aabbmax = aabbcenter + aabbextent;
				shadowCull.aabbmin = aabbcenter - aabbextent;
				execute_compute_cull(cmd, _renderScene._shadowPass, shadowCull);
			}
		}

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, postCullBarriers.size(), postCullBarriers.data(), 0, nullptr);

		shadow_pass(cmd);
		forward_pass(clearValue, cmd);

		reduce_depth(cmd);

		copy_render_to_swapchain(swapchainImageIndex, cmd);
	}

	//TracyVkCollect(_graphicsQueueContext, get_current_frame()._mainCommandBuffer);

	//finalize the command buffer (we can no longer add commands, but it can now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));

	//prepare the submission to the queue. 
	//we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
	//we will signal the _renderSemaphore, to signal that rendering has finished
	{
		ZoneScopedN("Queue Submit");
		VkSubmitInfo submit = vkinit::submit_info(&cmd);
		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		submit.pWaitDstStageMask = &waitStage;

		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &get_current_frame()._presentSemaphore;

		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &get_current_frame()._renderSemaphore;
		//submit command buffer to the queue and execute it.
		// _renderFence will now block until the graphic commands finish execution
		VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));
	}
	//prepare present
	// this will put the image we just rendered to into the visible window.
	// we want to wait on the _renderSemaphore for that, 
	// as its necessary that drawing commands have finished before the image is displayed to the user
	{
		ZoneScopedN("Queue Present");
		VkPresentInfoKHR presentInfo = vkinit::present_info();

		presentInfo.pSwapchains = &_swapchain;
		presentInfo.swapchainCount = 1;

		presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
		presentInfo.waitSemaphoreCount = 1;

		presentInfo.pImageIndices = &swapchainImageIndex;
		VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
	}

	//increase the number of frames drawn
	_frameNumber++;
}

void REngine::reallocate_buffer(AllocatedBufferUntyped& buffer, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags required_flags /*= 0*/)
{
	AllocatedBufferUntyped newBuffer = create_buffer(this, allocSize, usage, memoryUsage, required_flags);

	get_current_frame()._frameDeletionQueue.push_function([=]() {

		vmaDestroyBuffer(_allocator, buffer._buffer, buffer._allocation);
		});

	buffer = newBuffer;
}

void REngine::unmap_buffer(AllocatedBufferUntyped& buffer)
{
	vmaUnmapMemory(_allocator, buffer._allocation);
}

#include <future>
void REngine::ready_mesh_draw(VkCommandBuffer cmd)
{

	//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Data Refresh");
	ZoneScopedNC("Draw Upload", tracy::Color::Blue);

	//upload object data to gpu

	if (_renderScene.dirtyObjects.size() > 0)
	{
		ZoneScopedNC("Refresh Object Buffer", tracy::Color::Red);

		size_t copySize = _renderScene.renderables.size() * sizeof(GPUObjectData);
		if (_renderScene.objectDataBuffer._size < copySize)
		{
			reallocate_buffer(_renderScene.objectDataBuffer, copySize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		}

		//if 80% of the objects are dirty, then just reupload the whole thing
		if (_renderScene.dirtyObjects.size() >= _renderScene.renderables.size() * 0.8)
		{
			AllocatedBuffer<GPUObjectData> newBuffer = create_buffer(this, copySize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

			GPUObjectData* objectSSBO = map_buffer(newBuffer);
			_renderScene.fill_objectData(objectSSBO);
			unmap_buffer(newBuffer);

			get_current_frame()._frameDeletionQueue.push_function([=]() {

				vmaDestroyBuffer(_allocator, newBuffer._buffer, newBuffer._allocation);
				});


			//copy from the uploaded cpu side instance buffer to the gpu one
			VkBufferCopy indirectCopy;
			indirectCopy.dstOffset = 0;
			indirectCopy.size = _renderScene.renderables.size() * sizeof(GPUObjectData);
			indirectCopy.srcOffset = 0;
			vkCmdCopyBuffer(cmd, newBuffer._buffer, _renderScene.objectDataBuffer._buffer, 1, &indirectCopy);
		}
		else {
			//update only the changed elements

			std::vector<VkBufferCopy> copies;
			copies.reserve(_renderScene.dirtyObjects.size());

			uint64_t buffersize = sizeof(GPUObjectData) * _renderScene.dirtyObjects.size();
			uint64_t vec4size = sizeof(glm::vec4);
			uint64_t intsize = sizeof(uint32_t);
			uint64_t wordsize = sizeof(GPUObjectData) / sizeof(uint32_t);
			uint64_t uploadSize = _renderScene.dirtyObjects.size() * wordsize * intsize;
			AllocatedBuffer<GPUObjectData> newBuffer = create_buffer(this, buffersize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
			AllocatedBuffer<uint32_t> targetBuffer = create_buffer(this, uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

			get_current_frame()._frameDeletionQueue.push_function([=]() {

				vmaDestroyBuffer(_allocator, newBuffer._buffer, newBuffer._allocation);
				vmaDestroyBuffer(_allocator, targetBuffer._buffer, targetBuffer._allocation);
				});

			uint32_t* targetData = map_buffer(targetBuffer);
			GPUObjectData* objectSSBO = map_buffer(newBuffer);
			uint32_t launchcount = static_cast<uint32_t>(_renderScene.dirtyObjects.size() * wordsize);
			{
				ZoneScopedNC("Write dirty objects", tracy::Color::Red);
				uint32_t sidx = 0;
				for (int i = 0; i < _renderScene.dirtyObjects.size(); i++)
				{
					_renderScene.write_object(objectSSBO + i, _renderScene.dirtyObjects[i]);
					uint32_t dstOffset = static_cast<uint32_t>(wordsize * _renderScene.dirtyObjects[i].handle);

					for (int b = 0; b < wordsize; b++)
					{
						uint32_t tidx = dstOffset + b;
						targetData[sidx] = tidx;
						sidx++;
					}
				}
				launchcount = sidx;
			}
			unmap_buffer(newBuffer);
			unmap_buffer(targetBuffer);

			VkDescriptorBufferInfo indexData = targetBuffer.get_info();

			VkDescriptorBufferInfo sourceData = newBuffer.get_info();

			VkDescriptorBufferInfo targetInfo = _renderScene.objectDataBuffer.get_info();

			VkDescriptorSet COMPObjectDataSet;
			vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
				.bind_buffer(0, &indexData, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.bind_buffer(1, &sourceData, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.bind_buffer(2, &targetInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.build(COMPObjectDataSet);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _sparseUploadPipeline);

			vkCmdPushConstants(cmd, _sparseUploadLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &launchcount);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _sparseUploadLayout, 0, 1, &COMPObjectDataSet, 0, nullptr);
			vkCmdDispatch(cmd, ((launchcount) / 256) + 1, 1, 1);
		}

		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(_renderScene.objectDataBuffer._buffer, _graphicsQueueFamily);
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		uploadBarriers.push_back(barrier);
		_renderScene.clear_dirty_objects();
	}

	MeshPass* passes[3] = { &_renderScene._forwardPass,&_renderScene._transparentForwardPass,&_renderScene._shadowPass };
	for (int p = 0; p < 3; p++)
	{
		auto& pass = *passes[p];

		//reallocate the gpu side buffers if needed

		if (pass.drawIndirectBuffer._size < pass.batches.size() * sizeof(GPUIndirectObject))
		{
			reallocate_buffer(pass.drawIndirectBuffer, pass.batches.size() * sizeof(GPUIndirectObject), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		}
		if (pass.compactedInstanceBuffer._size < pass.flat_batches.size() * sizeof(uint32_t))
		{
			reallocate_buffer(pass.compactedInstanceBuffer, pass.flat_batches.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		}
		if (pass.passObjectsBuffer._size < pass.flat_batches.size() * sizeof(GPUInstance))
		{
			reallocate_buffer(pass.passObjectsBuffer, pass.flat_batches.size() * sizeof(GPUInstance), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		}
	}

	std::vector<std::future<void>> async_calls;
	async_calls.reserve(9);

	std::vector<AllocatedBufferUntyped> unmaps;

	for (int p = 0; p < 3; p++)
	{
		MeshPass& pass = *passes[p];
		MeshPass* ppass = passes[p];

		RenderScene* pScene = &_renderScene;
		//if the pass has changed the batches, need to reupload them
		if (pass.needsIndirectRefresh && pass.batches.size() > 0)
		{
			ZoneScopedNC("Refresh Indirect Buffer", tracy::Color::Red);

			AllocatedBuffer<GPUIndirectObject> newBuffer = create_buffer(this, sizeof(GPUIndirectObject) * pass.batches.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
			GPUIndirectObject* indirect = map_buffer(newBuffer);

			async_calls.push_back(std::async(std::launch::async, [=] {
				pScene->fill_indirectArray(indirect, *ppass);
				}));
			//async_calls.push_back([&]() {
			//	_renderScene.fill_indirectArray(indirect, pass);
			//});

			unmaps.push_back(newBuffer);
			//unmap_buffer(newBuffer);

			if (pass.clearIndirectBuffer._buffer != VK_NULL_HANDLE)
			{
				AllocatedBufferUntyped deletionBuffer = pass.clearIndirectBuffer;
				//add buffer to deletion queue of this frame
				get_current_frame()._frameDeletionQueue.push_function([=]() {
					vmaDestroyBuffer(_allocator, deletionBuffer._buffer, deletionBuffer._allocation);
					});
			}

			pass.clearIndirectBuffer = newBuffer;
			pass.needsIndirectRefresh = false;
		}

		if (pass.needsInstanceRefresh && pass.flat_batches.size() > 0)
		{
			ZoneScopedNC("Refresh Instancing Buffer", tracy::Color::Red);

			AllocatedBuffer<GPUInstance> newBuffer = create_buffer(this, sizeof(GPUInstance) * pass.flat_batches.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

			GPUInstance* instanceData = map_buffer(newBuffer);
			async_calls.push_back(std::async(std::launch::async, [=] {
				pScene->fill_instancesArray(instanceData, *ppass);
				}));
			unmaps.push_back(newBuffer);
			//_renderScene.fill_instancesArray(instanceData, pass);
			//unmap_buffer(newBuffer);

			get_current_frame()._frameDeletionQueue.push_function([=]() {
				vmaDestroyBuffer(_allocator, newBuffer._buffer, newBuffer._allocation);
				});

			//copy from the uploaded cpu side instance buffer to the gpu one
			VkBufferCopy indirectCopy;
			indirectCopy.dstOffset = 0;
			indirectCopy.size = pass.flat_batches.size() * sizeof(GPUInstance);
			indirectCopy.srcOffset = 0;
			vkCmdCopyBuffer(cmd, newBuffer._buffer, pass.passObjectsBuffer._buffer, 1, &indirectCopy);

			VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(pass.passObjectsBuffer._buffer, _graphicsQueueFamily);
			barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			uploadBarriers.push_back(barrier);
			pass.needsInstanceRefresh = false;
		}
	}

	for (auto& s : async_calls)
	{
		s.get();
	}
	for (auto b : unmaps)
	{
		unmap_buffer(b);
	}

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, static_cast<uint32_t>(uploadBarriers.size()), uploadBarriers.data(), 0, nullptr);//1, &readBarrier);
	uploadBarriers.clear();
}


void REngine::ready_cull_data(MeshPass& pass, VkCommandBuffer cmd)
{
	//copy from the cleared indirect buffer into the one we will use on rendering. This one happens every frame
	VkBufferCopy indirectCopy;
	indirectCopy.dstOffset = 0;
	indirectCopy.size = pass.batches.size() * sizeof(GPUIndirectObject);
	indirectCopy.srcOffset = 0;
	vkCmdCopyBuffer(cmd, pass.clearIndirectBuffer._buffer, pass.drawIndirectBuffer._buffer, 1, &indirectCopy);

	{
		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(pass.drawIndirectBuffer._buffer, _graphicsQueueFamily);
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		cullReadyBarriers.push_back(barrier);
		//vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
	}
}

glm::vec4 normalizePlane(glm::vec4 p)
{
	return p / glm::length(glm::vec3(p));
}

void REngine::execute_compute_cull(VkCommandBuffer cmd, MeshPass& pass, CullParams& params)
{
	if (CVAR_FreezeCull.Get())
		return;
	if (pass.batches.size() == 0)
		return;

	//TracyVkZone(_graphicsQueueContext, cmd, "Cull Dispatch");
	VkDescriptorBufferInfo objectBufferInfo = _renderScene.objectDataBuffer.get_info();

	VkDescriptorBufferInfo dynamicInfo = get_current_frame().dynamicData.source.get_info();
	dynamicInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.passObjectsBuffer.get_info();
	VkDescriptorBufferInfo finalInfo = pass.compactedInstanceBuffer.get_info();
	VkDescriptorBufferInfo indirectInfo = pass.drawIndirectBuffer.get_info();

	VkDescriptorImageInfo depthPyramid;
	depthPyramid.sampler = _depthSampler;
	depthPyramid.imageView = _depthPyramid._defaultView;
	depthPyramid.imageLayout = VK_IMAGE_LAYOUT_GENERAL;


	VkDescriptorSet COMPObjectDataSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_buffer(1, &indirectInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_buffer(2, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_buffer(3, &finalInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_image(4, &depthPyramid, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_buffer(5, &dynamicInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(COMPObjectDataSet);


	glm::mat4 projection = params.projmat;
	glm::mat4 projectionT = transpose(projection);

	glm::vec4 frustumX = normalizePlane(projectionT[3] + projectionT[0]); // x + w < 0
	glm::vec4 frustumY = normalizePlane(projectionT[3] + projectionT[1]); // y + w < 0

	DrawCullData cullData = {};
	cullData.P00 = projection[0][0];
	cullData.P11 = projection[1][1];
	cullData.znear = 0.1f;
	cullData.zfar = params.drawDist;
	cullData.frustum[0] = frustumX.x;
	cullData.frustum[1] = frustumX.z;
	cullData.frustum[2] = frustumY.y;
	cullData.frustum[3] = frustumY.z;
	cullData.flags = static_cast<uint16_t>(pass.flat_batches.size()) << 16;
	cullData.flags |= params.frustrumCull ? 1 : 0;
	//cullData.lodEnabled = false;
	cullData.flags |= params.occlusionCull ? 2 : 0;
	//cullData.lodBase = 10.f;
	//cullData.lodStep = 1.5f;
	//cullData.pyramidWidth = static_cast<float>(depthPyramidWidth);
	//cullData.pyramidHeight = static_cast<float>(depthPyramidHeight);
	cullData.pyramid = depthPyramidWidth + (depthPyramidHeight << 16);
	cullData.viewMat = params.viewmat;//get_view_matrix();

	cullData.flags |= params.aabb ? 8 : 0;
	cullData.aabbmin_x = params.aabbmin.x;
	cullData.aabbmin_y = params.aabbmin.y;
	cullData.aabbmin_z = params.aabbmin.z;

	cullData.aabbmax_x = params.aabbmax.x;
	cullData.aabbmax_y = params.aabbmax.y;
	cullData.aabbmax_z = params.aabbmax.z;

	cullData.flags |= params.drawDist > 10000 ? 0 : 4;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullPipeline);
	vkCmdPushConstants(cmd, _cullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DrawCullData), &cullData);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullLayout, 0, 1, &COMPObjectDataSet, 0, nullptr);
	vkCmdDispatch(cmd, static_cast<uint32_t>((pass.flat_batches.size() / 256) + 1), 1, 1);


	//barrier the 2 buffers we just wrote for culling, the indirect draw one, and the instances one, so that they can be read well when rendering the pass
	{
		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(pass.compactedInstanceBuffer._buffer, _graphicsQueueFamily);
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

		VkBufferMemoryBarrier barrier2 = vkinit::buffer_barrier(pass.drawIndirectBuffer._buffer, _graphicsQueueFamily);
		barrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier2.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

		VkBufferMemoryBarrier barriers[] = { barrier,barrier2 };

		postCullBarriers.push_back(barrier);
		postCullBarriers.push_back(barrier2);

	}
	if (*CVarSystem::Get()->GetIntCVar("culling.outputIndirectBufferToFile"))
	{
		uint32_t offset = get_current_frame().debugDataOffsets.back();
		VkBufferCopy debugCopy;
		debugCopy.dstOffset = offset;
		debugCopy.size = pass.batches.size() * sizeof(GPUIndirectObject);
		debugCopy.srcOffset = 0;
		vkCmdCopyBuffer(cmd, pass.drawIndirectBuffer._buffer, get_current_frame().debugOutputBuffer._buffer, 1, &debugCopy);
		get_current_frame().debugDataOffsets.push_back(offset + static_cast<uint32_t>(debugCopy.size));
		get_current_frame().debugDataNames.push_back("Cull Indirect Output");
	}
}


void REngine::copy_render_to_swapchain(uint32_t swapchainImageIndex, VkCommandBuffer cmd)
{
	//start the main renderpass. 
	//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
	VkRenderPassBeginInfo copyRP = vkinit::renderpass_begin_info(_copyPass, _windowExtent, _framebuffers[swapchainImageIndex]);


	vkCmdBeginRenderPass(cmd, &copyRP, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)_windowExtent.width;
	viewport.height = (float)_windowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = _windowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdSetDepthBias(cmd, 0, 0, 0);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _blitPipeline);

	VkDescriptorImageInfo sourceImage;
	sourceImage.sampler = _smoothSampler;

	sourceImage.imageView = _rawRenderImage._defaultView;
	sourceImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorSet blitSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_image(0, &sourceImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(blitSet);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _blitLayout, 0, 1, &blitSet, 0, nullptr);

	vkCmdDraw(cmd, 3, 1, 0, 0);


	vkCmdEndRenderPass(cmd);
}


void REngine::shadow_pass(VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu shadow pass");
	vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Shadow Primitives");

	if (CVAR_FreezeShadows.Get() || !*CVarSystem::Get()->GetIntCVar("gpu.shadowcast"))
		return;

	//clear depth at 1
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;
	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_shadowPass, _shadowExtent, _shadowFramebuffer);

	//connect clear values
	rpInfo.clearValueCount = 1;

	VkClearValue clearValues[] = { depthClear };

	rpInfo.pClearValues = &clearValues[0];
	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)_shadowExtent.width;
	viewport.height = (float)_shadowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = _shadowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);



	if (_renderScene._shadowPass.batches.size() > 0)
	{
		//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Shadow  Pass");
		draw_objects_shadow(cmd, _renderScene._shadowPass);
	}

	//finalize the render pass
	vkCmdEndRenderPass(cmd);
}

struct alignas(16) DepthReduceData
{
	glm::vec2 imageSize;
};
inline uint32_t getGroupCount(uint32_t threadCount, uint32_t localSize)
{
	return (threadCount + localSize - 1) / localSize;
}

void REngine::reduce_depth(VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu depth reduce");

	VkImageMemoryBarrier depthReadBarriers[] =
	{
		vkinit::image_barrier(_depthImage._image, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT),
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, depthReadBarriers);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _depthReducePipeline);

	for (int32_t i = 0; i < depthPyramidLevels; ++i)
	{
		VkDescriptorImageInfo destTarget;
		destTarget.sampler = _depthSampler;
		destTarget.imageView = depthPyramidMips[i];
		destTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorImageInfo sourceTarget;
		sourceTarget.sampler = _depthSampler;
		if (i == 0)
		{
			sourceTarget.imageView = _depthImage._defaultView;
			sourceTarget.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		else {
			sourceTarget.imageView = depthPyramidMips[i - 1];
			sourceTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}

		VkDescriptorSet depthSet;
		vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
			.bind_image(0, &destTarget, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
			.bind_image(1, &sourceTarget, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
			.build(depthSet);

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _depthReduceLayout, 0, 1, &depthSet, 0, nullptr);

		uint32_t levelWidth = depthPyramidWidth >> i;
		uint32_t levelHeight = depthPyramidHeight >> i;
		if (levelHeight < 1) levelHeight = 1;
		if (levelWidth < 1) levelWidth = 1;

		DepthReduceData reduceData = { glm::vec2(levelWidth, levelHeight) };

		vkCmdPushConstants(cmd, _depthReduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(reduceData), &reduceData);
		vkCmdDispatch(cmd, getGroupCount(levelWidth, 32), getGroupCount(levelHeight, 32), 1);

		VkImageMemoryBarrier reduceBarrier = vkinit::image_barrier(_depthPyramid._image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &reduceBarrier);
	}

	VkImageMemoryBarrier depthWriteBarrier = vkinit::image_barrier(_depthImage._image, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depthWriteBarrier);
}

void REngine::draw_objects_shadow(VkCommandBuffer cmd, MeshPass& pass)
{
	ZoneScopedNC("DrawObjects", tracy::Color::Blue);

	glm::mat4 view = _mainLight.get_view();

	glm::mat4 projection = _mainLight.get_projection();

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	//push data to dynmem
	uint32_t camera_data_offset = get_current_frame().dynamicData.push(camData);


	VkDescriptorBufferInfo objectBufferInfo = _renderScene.objectDataBuffer.get_info();

	VkDescriptorBufferInfo camInfo = get_current_frame().dynamicData.source.get_info();
	camInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.compactedInstanceBuffer.get_info();


	VkDescriptorSet GlobalSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &camInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
		.build(GlobalSet);

	VkDescriptorSet ObjectDataSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.bind_buffer(1, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.build(ObjectDataSet);

	vkCmdSetDepthBias(cmd, CVAR_ShadowBias.GetFloat(), 0, CVAR_SlopeBias.GetFloat());

	std::vector<uint32_t> dynamic_offsets;
	dynamic_offsets.push_back(camera_data_offset);

	execute_draw_commands(cmd, pass, ObjectDataSet, dynamic_offsets, GlobalSet);
}


void REngine::execute_draw_commands(VkCommandBuffer cmd, MeshPass& pass, VkDescriptorSet ObjectDataSet, std::vector<uint32_t> dynamic_offsets, VkDescriptorSet GlobalSet)
{
	if (pass.batches.size() > 0)
	{
		ZoneScopedNC("Draw Commit", tracy::Color::Blue4);
		Mesh* lastMesh = nullptr;
		VkPipeline lastPipeline{ VK_NULL_HANDLE };
		VkPipelineLayout lastLayout{ VK_NULL_HANDLE };
		VkDescriptorSet lastMaterialSet{ VK_NULL_HANDLE };

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &_renderScene.mergedVertexBuffer._buffer, &offset);

		vkCmdBindIndexBuffer(cmd, _renderScene.mergedIndexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

		stats.objects += static_cast<uint32_t>(pass.flat_batches.size());
		for (int i = 0; i < pass.multibatches.size(); i++)
		{
			auto& multibatch = pass.multibatches[i];
			auto& instanceDraw = pass.batches[multibatch.first];

			VkPipeline newPipeline = instanceDraw.material.shaderPass->pipeline;
			VkPipelineLayout newLayout = instanceDraw.material.shaderPass->layout;
			VkDescriptorSet newMaterialSet = instanceDraw.material.materialSet;

			Mesh* drawMesh = _renderScene.get_mesh(instanceDraw.meshID)->original;

			if (newPipeline != lastPipeline)
			{
				lastPipeline = newPipeline;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 1, 1, &ObjectDataSet, 0, nullptr);

				//update dynamic binds
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 0, 1, &GlobalSet, dynamic_offsets.size(), dynamic_offsets.data());
			}
			if (newMaterialSet != lastMaterialSet)
			{
				lastMaterialSet = newMaterialSet;
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 2, 1, &newMaterialSet, 0, nullptr);
			}

			bool merged = _renderScene.get_mesh(instanceDraw.meshID)->isMerged;
			if (merged)
			{
				if (lastMesh != nullptr)
				{
					VkDeviceSize offset = 0;
					vkCmdBindVertexBuffers(cmd, 0, 1, &_renderScene.mergedVertexBuffer._buffer, &offset);

					vkCmdBindIndexBuffer(cmd, _renderScene.mergedIndexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
					lastMesh = nullptr;
				}
			}
			else if (lastMesh != drawMesh) {

				//bind the mesh vertex buffer with offset 0
				VkDeviceSize offset = 0;
				vkCmdBindVertexBuffers(cmd, 0, 1, &drawMesh->_vertexBuffer._buffer, &offset);

				if (drawMesh->_indexBuffer._buffer != VK_NULL_HANDLE) {
					vkCmdBindIndexBuffer(cmd, drawMesh->_indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
				}
				lastMesh = drawMesh;
			}

			bool bHasIndices = drawMesh->_indices.size() > 0;
			if (!bHasIndices) {
				stats.draws++;
				stats.triangles += static_cast<int32_t>(drawMesh->_vertices.size() / 3) * instanceDraw.count;
				vkCmdDraw(cmd, static_cast<uint32_t>(drawMesh->_vertices.size()), instanceDraw.count, 0, instanceDraw.first);
			}
			else {
				stats.triangles += static_cast<int32_t>(drawMesh->_indices.size() / 3) * instanceDraw.count;

				vkCmdDrawIndexedIndirect(cmd, pass.drawIndirectBuffer._buffer, multibatch.first * sizeof(GPUIndirectObject), multibatch.count, sizeof(GPUIndirectObject));

				stats.draws++;
				stats.drawcalls += instanceDraw.count;
			}
		}
	}
}

void REngine::forward_pass(VkClearValue clearValue, VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu forward pass");
	vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Forward Primitives");
	//clear depth at 0
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 0.f;

	//start the main renderpass. 
	//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_renderPass, _windowExtent, _forwardFramebuffer/*_framebuffers[swapchainImageIndex]*/);

	//connect clear values
	rpInfo.clearValueCount = 2;

	VkClearValue clearValues[] = { clearValue, depthClear };

	rpInfo.pClearValues = &clearValues[0];
	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)_windowExtent.width;
	viewport.height = (float)_windowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = _windowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetDepthBias(cmd, 0, 0, 0);


	//stats.drawcalls = 0;
	//stats.draws = 0;
	//stats.objects = 0;
	//stats.triangles = 0;

	{
		//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Forward Pass");
		draw_objects_forward(cmd, _renderScene._forwardPass);
		draw_objects_forward(cmd, _renderScene._transparentForwardPass);
	}


	{
		//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Imgui Draw");
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}

	//finalize the render pass
	vkCmdEndRenderPass(cmd);
}

void REngine::draw_objects_forward(VkCommandBuffer cmd, MeshPass& pass)
{
	ZoneScopedNC("DrawObjects", tracy::Color::Blue);
	//make a model view matrix for rendering the object
	//camera view
	glm::mat4 view = _camera.get_view_matrix();
	//camera projection
	glm::mat4 projection = _camera.get_projection_matrix();


	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	_sceneParameters.sunlightShadowMatrix = _mainLight.get_projection() * _mainLight.get_view();


	float framed = (_frameNumber / 120.f);
	_sceneParameters.ambientColor = glm::vec4{ 0.5 };
	_sceneParameters.sunlightColor = glm::vec4{ 1.f };
	_sceneParameters.sunlightDirection = glm::vec4(_mainLight.lightDirection * 1.f, 1.f);

	_sceneParameters.sunlightColor.w = CVAR_Shadowcast.Get() ? 0 : 1;

	//push data to dynmem
	uint32_t scene_data_offset = get_current_frame().dynamicData.push(_sceneParameters);

	uint32_t camera_data_offset = get_current_frame().dynamicData.push(camData);


	VkDescriptorBufferInfo objectBufferInfo = _renderScene.objectDataBuffer.get_info();

	VkDescriptorBufferInfo sceneInfo = get_current_frame().dynamicData.source.get_info();
	sceneInfo.range = sizeof(GPUSceneData);

	VkDescriptorBufferInfo camInfo = get_current_frame().dynamicData.source.get_info();
	camInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.compactedInstanceBuffer.get_info();


	VkDescriptorImageInfo shadowImage;
	shadowImage.sampler = _shadowSampler;

	shadowImage.imageView = _shadowImage._defaultView;
	shadowImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorSet GlobalSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &camInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
		.bind_buffer(1, &sceneInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.bind_image(2, &shadowImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(GlobalSet);

	VkDescriptorSet ObjectDataSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.bind_buffer(1, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.build(ObjectDataSet);
	vkCmdSetDepthBias(cmd, 0, 0, 0);

	std::vector<uint32_t> dynamic_offsets;
	dynamic_offsets.push_back(camera_data_offset);
	dynamic_offsets.push_back(scene_data_offset);
	execute_draw_commands(cmd, pass, ObjectDataSet, dynamic_offsets, GlobalSet);
}