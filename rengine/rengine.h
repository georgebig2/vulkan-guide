#pragma once

#include <vector>
#include <functional>
#include <deque>
#include <string_view>
//#include <unordered_map>

#include "common.h"
#include <player_camera.h>
#include "vk_types.h"

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

//forward declarations
namespace vkutil {
    class DescriptorLayoutCache;
    class DescriptorAllocator;
    class VulkanProfiler;
    class MaterialSystem;
}

struct CullParams {
    glm::mat4 viewmat;
    glm::mat4 projmat;
    bool occlusionCull;
    bool frustrumCull;
    float drawDist;
    bool aabb;
    glm::vec3 aabbmin;
    glm::vec3 aabbmax;
};

struct EngineStats {
    float frametime;
    int objects;
    int drawcalls;
    int draws;
    int triangles;
};
struct Mesh;
namespace vkutil
{
    struct Material;
}
struct MeshObject {
    Mesh* mesh{ nullptr };

    vkutil::Material* material;
    uint32_t customSortKey;
    glm::mat4 transformMatrix;

    RenderBounds bounds;

    uint32_t bDrawForwardPass : 1;
    uint32_t bDrawShadowPass : 1;
};

struct GPUObjectData {
    glm::mat4 modelMatrix;
    glm::vec4 origin_rad; // bounds
    glm::vec4 extents;  // bounds
};

namespace assets {
    struct AssetFile;
}

struct /*alignas(16)*/DrawCullData
{
    glm::mat4 viewMat;
    float P00, P11, znear, zfar; // symmetric projection parameters
    float frustum[4]; // data for left/right/top/bottom frustum planes
    //float lodBase, lodStep; // lod distance i = base * pow(step, i)
    //float pyramidWidth, pyramidHeight; // depth pyramid size in texels
    uint32_t pyramid;

    //uint32_t drawCount;

    uint32_t flags;
    //	int cullingEnabled;
        //int lodEnabled;
    //	int occlusionEnabled;
    //	int distanceCheck;
    //	int AABBcheck;
    float aabbmin_x;
    float aabbmin_y;
    float aabbmin_z;
    float aabbmax_x;
    float aabbmax_y;
    float aabbmax_z;
};

struct GPUCameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
};

struct DirectionalLight {
    glm::vec3 lightPosition;
    glm::vec3 lightDirection;
    glm::vec3 shadowExtent;
    glm::mat4 get_projection();

    glm::mat4 get_view();
};

struct GPUSceneData {
    glm::vec4 fogColor; // w is for exponent
    glm::vec4 fogDistances; //x for min, y for max, zw unused.
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection; //w for sun power
    glm::vec4 sunlightColor;
    glm::mat4 sunlightShadowMatrix;
};

struct Texture {
    AllocatedImage image;
    VkImageView imageView;
};

struct FrameData;
class RenderScene;
class ShaderCache;
struct MeshPass;

class REngine {
public:

    VkExtent2D _windowExtent{ 1700 * 2 / 3 , 900 * 2 / 3 };
    VkExtent2D _shadowExtent{ 1024 * 2,1024 * 2 };
    int _frameNumber{ 0 };
    uint32_t _swapchainImageIndex = 0;
    uint32_t _swapchainImageIndex_prev = 0;
    bool _isInitialized{ false };

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	DeletionQueue _surfaceDeletionQueue;
    DeletionQueue _mainDeletionQueue;

    virtual void init(bool debug);
    virtual void update();
    virtual void cleanup();
    virtual bool create_surface(VkInstance instance, VkSurfaceKHR* surface) = 0;
    virtual void resize_window(int w, int h);
    virtual float get_dpi_factor() { return 1.f; }

    void recreate_swapchain();

    void draw();
    void hud_update();

    FrameData& get_current_frame();
    //FrameData& get_last_frame();

    ShaderCache* get_shader_cache();
    RenderScene* get_render_scene();

    virtual std::string asset_path(std::string_view path);
    virtual std::string shader_path(std::string_view path);

    virtual bool load_asset(const char* path, assets::AssetFile& outputFile);
    virtual std::vector<uint32_t> load_file(const char* path);

    bool handle_surface_changes(bool force_update = false);

    PlayerCamera _camera;

	VkDevice _device;
	VmaAllocator _allocator; //vma lib allocator

    VkInstance _instance;
    VkPhysicalDevice _chosenGPU;

    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

    VkSurfaceKHR _surface{};
    vkb::Swapchain _swapchain;
    VkFormat _swachainImageFormat;
    VkFormat _depthFormat;
    VkSurfaceTransformFlagBitsKHR _pretransformFlag{ VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR };
   
    VkPhysicalDeviceProperties _gpuProperties;

    std::vector<VkFramebuffer> _framebuffers;
    //std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;

    VkFormat _renderFormat;
    AllocatedImage _rawRenderImage;
    VkFramebuffer _forwardFramebuffer = 0;
    VkFramebuffer _shadowFramebuffer = 0;

    AllocatedImage _depthImage;
    AllocatedImage _depthPyramid;
    AllocatedImage _shadowImage;

    VkSampler _smoothSampler;
    VkSampler _smoothSampler2;
    VkSampler _shadowSampler;
    VkSampler _depthSampler;

    int depthPyramidWidth;
    int depthPyramidHeight;
    int depthPyramidLevels;

    VkImageView depthPyramidMips[16] = {};

    void init_commands();
    void init_sync_structures();
    void recreate_framebuffers();
    void init_forward_renderpass();
    void init_copy_renderpass();
    void init_shadow_renderpass();
    void init_pipelines();
    void init_imgui();
    
    void load_meshes();
    bool load_compute_shader(const char* shaderPath, VkPipeline& pipeline, VkPipelineLayout& layout);
    void upload_mesh(Mesh& mesh);
    Mesh* get_mesh(const std::string& name);
    bool load_prefab(const char* path, glm::mat4 root);

    void refresh_renderbounds(MeshObject* object);
    bool load_image_to_cache(const char* name, const char* path);

    vkutil::MaterialSystem* _materialSystem;

    EngineStats stats;

    //ShaderCache _shaderCache;

    VkRenderPass _renderPass;
    VkRenderPass _shadowPass;
    VkRenderPass _copyPass;

    vkutil::DescriptorAllocator* _descriptorAllocator;
    vkutil::DescriptorLayoutCache* _descriptorLayoutCache;


    vkutil::VulkanProfiler* _profiler;

    std::vector<VkBufferMemoryBarrier> uploadBarriers;
    std::vector<VkBufferMemoryBarrier> cullReadyBarriers;
    std::vector<VkBufferMemoryBarrier> postCullBarriers;


    void draw_objects_forward(VkCommandBuffer cmd, MeshPass& pass);
    void ready_cull_data(MeshPass& pass, VkCommandBuffer cmd);
    void draw_objects_shadow(VkCommandBuffer cmd, MeshPass& pass);
    void execute_draw_commands(VkCommandBuffer cmd, MeshPass& pass, VkDescriptorSet ObjectDataSet, std::vector<uint32_t> dynamic_offsets, VkDescriptorSet GlobalSet);
    void execute_compute_cull(VkCommandBuffer cmd, MeshPass& pass, CullParams& params);

    void ready_mesh_draw(VkCommandBuffer cmd);
    void reallocate_buffer(AllocatedBufferUntyped& buffer, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags required_flags = 0);
    template<typename T>
    T* map_buffer(AllocatedBuffer<T>& buffer);
    void unmap_buffer(AllocatedBufferUntyped& buffer);
    void copy_render_to_swapchain(uint32_t swapchainImageIndex, VkCommandBuffer cmd);
    void shadow_pass(VkCommandBuffer cmd);
    void reduce_depth(VkCommandBuffer cmd);
    void forward_pass(VkClearValue clearValue, VkCommandBuffer cmd);
    void init_descriptors();
    size_t pad_uniform_buffer_size(size_t originalSize);
    void init_scene();

    VkPipeline _cullPipeline;
    VkPipelineLayout _cullLayout;
    VkPipeline _sparseUploadPipeline;
    VkPipelineLayout _sparseUploadLayout;
    VkPipeline _blitPipeline;
    VkPipelineLayout _blitLayout;
    VkPipeline _depthReducePipeline;
    VkPipelineLayout _depthReduceLayout;
    VkDescriptorSetLayout _singleTextureSetLayout;

    DirectionalLight _mainLight;
    GPUSceneData _sceneParameters;

};
