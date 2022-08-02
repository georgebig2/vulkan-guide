// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <functional>
#include <deque>
#include <memory>
#include <vk_mesh.h>
#include <unordered_map>
#include <material_system.h>



//#include <glm/glm.hpp>
//#include <glm/gtx/transform.hpp>

#include <SDL_events.h>
#include <frustum_cull.h>

#include "rengine.h"

namespace vkutil { struct Material; }


namespace tracy { class VkCtx; }


namespace assets { struct PrefabInfo; }

namespace vkutil {
	class DescriptorLayoutCache;
	class DescriptorAllocator;
	class VulkanProfiler;
}

struct MeshPushConstants {
	glm::vec4 data;
	glm::mat4 render_matrix;
};

namespace assets {

	enum class TransparencyMode :uint8_t;
}

struct Texture {
	AllocatedImage image;
	VkImageView imageView;
};

struct MeshDrawCommands {
	struct RenderBatch {
		MeshObject* object;
		uint64_t sortKey;
		uint64_t objectIndex;
	};

	std::vector<RenderBatch> batch;
};


const int MAX_OBJECTS = 150000;

class VulkanEngine : public REngine {
public:

	int _selectedShader{ 0 };
	struct SDL_Window* _window{ nullptr };

	tracy::VkCtx* _graphicsQueueContext;
	vkutil::MaterialSystem* _materialSystem;
	MeshDrawCommands currentCommands;

	//initializes everything in the engine
	void init();
	virtual void cleanup();

	//run main loop
	void run();

	std::unordered_map<std::string, Mesh> _meshes;
	std::unordered_map<std::string, Texture> _loadedTextures;
	std::unordered_map<std::string, assets::PrefabInfo*> _prefabCache;
	//functions

	//returns nullptr if it cant be found
	Mesh* get_mesh(const std::string& name);
	bool load_prefab(const char* path, glm::mat4 root);

	static std::string asset_path(std::string_view path);

	void refresh_renderbounds(MeshObject* object);

	bool load_compute_shader(const char* shaderPath, VkPipeline& pipeline, VkPipelineLayout& layout);

	bool create_surface(VkInstance instance, VkSurfaceKHR* surface) override;

private:
	void init_vulkan() override;
	void init_forward_renderpass();
	void init_copy_renderpass();
	void init_shadow_renderpass();
	void init_framebuffers();
	void init_pipelines();
	void init_scene();
	void init_imgui();
	void load_meshes();
	void load_images();
	bool load_image_to_cache(const char* name, const char* path);
	void upload_mesh(Mesh& mesh);
};

