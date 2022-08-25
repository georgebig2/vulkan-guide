#include "Tracy.hpp"
#include "TracyVulkan.hpp"

#include "rengine.h"
#include "vk_profiler.h"
#include "logger.h"
#include "cvars.h"
#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_descriptors.h>
#include <vk_scene.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "frame_data.h"

typedef const char* RGName;
typedef int		    RGHandle;

RGName DEPTH_RES = "Depth";
RGName DEPTH_PYRAMID_RES = "DepthPyramid";
RGName DEPTH_PYRAMID_PASS = "#DepthPyramid";

class RGResource
{
public:
	RGResource(RGName n) : name(n) {}
	RGResource() : name(0) {}
	RGName name;
};

class RGTexture : public RGResource
{
public:
	struct Desc
	{
		Desc(int ww, int hh, VkFormat ff, int ll) : w(ww), h(hh), format(ff), levels(ll) {}
		Desc() {}
		int w=-1, h=1, d = 1;
		int levels = 1;
		VkFormat format;
	} desc;

	RGTexture(RGName name, const Desc& d) : RGResource(name), desc(d) {}
	RGTexture() {}

	AllocatedImage image;
};

struct PoolTextureVal
{
	VkImage image = 0;
	RGTexture::Desc desc;
};

PoolTextureVal get_pool_image(RGName name);
VkImageView get_pool_image_view(VkImage image, int level);


AutoCVar_Int CVAR_FreezeCull("culling.freeze", "Locks culling", 0, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowBias("gpu.shadowBias", "Distance cull", 5.25f);
AutoCVar_Float CVAR_SlopeBias("gpu.shadowBiasSlope", "Distance cull", 4.75f);
AutoCVar_Int CVAR_FreezeShadows("gpu.freezeShadows", "Stop the rendering of shadows", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_Shadowcast("gpu.shadowcast", "Use shadowcasting", 1, CVarFlags::EditCheckbox);


void REngine::init_forward_renderpass()
{
	VkSamplerCreateInfo shadsamplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
	shadsamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	shadsamplerInfo.compareEnable = true;
	shadsamplerInfo.compareOp = VK_COMPARE_OP_LESS;
	vkCreateSampler(_device, &shadsamplerInfo, nullptr, &_shadowSampler);

	//we define an attachment description for our main color image
	//the attachment is loaded as "clear" when renderpass start
	//the attachment is stored when renderpass ends
	//the attachment layout starts as "undefined", and transitions to "Present" so its possible to display it
	//we dont care about stencil, and dont use multisampling

	VkAttachmentDescription color_attachment = {};
	color_attachment.format = nocopy ? _swachainImageFormat : _renderFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = nocopy ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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
	depth_attachment.finalLayout =  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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

	for (auto& frame : _frames)
	{
		//for the depth image, we want to allocate it from gpu local memory
		VmaAllocationCreateInfo dimg_allocinfo = {};
		dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VkExtent3D shadowExtent = { _shadowExtent.width, _shadowExtent.height, 1 };

		//the depth image will be a image with the format we selected and Depth Attachment usage flag
		VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowExtent);

		vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &frame._shadowImage._image, &frame._shadowImage._allocation, nullptr);
		VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, frame._shadowImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);
		VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &frame._shadowImage._defaultView));
		{
			VkFramebufferCreateInfo sh_info = vkinit::framebuffer_create_info(_shadowPass, _shadowExtent);
			sh_info.pAttachments = &frame._shadowImage._defaultView;
			sh_info.attachmentCount = 1;
			VK_CHECK(vkCreateFramebuffer(_device, &sh_info, nullptr, &frame._shadowFramebuffer));
		}
		{
			auto v = frame._shadowImage._defaultView;
			auto i = frame._shadowImage._image;
			auto a = frame._shadowImage._allocation;
			auto f = frame._shadowFramebuffer;
			_mainDeletionQueue.push_function([=]() {
				vkDestroyFramebuffer(_device, f, nullptr);
				vkDestroyImageView(_device, v, nullptr);
				vmaDestroyImage(_allocator, i, a);
				});
		}
	}
}


void REngine::init_copy_renderpass(VkFormat swachainImageFormat)
{
	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	//samplerInfo.maxAnisotropy = 10;
	//samplerInfo.anisotropyEnable = true;
	vkCreateSampler(_device, &samplerInfo, nullptr, &_smoothSampler);

//	samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
//	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
//	//info.anisotropyEnable = true;
////	samplerInfo.maxAnisotropy = 10;
////	samplerInfo.anisotropyEnable = true;
//	samplerInfo.mipLodBias = 2;
//	samplerInfo.maxLod = 30.f;
//	samplerInfo.minLod = 3;
//	vkCreateSampler(_device, &samplerInfo, nullptr, &_smoothSampler2);


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

	//we define an attachment description for our main color image
//the attachment is loaded as "clear" when renderpass start
//the attachment is stored when renderpass ends
//the attachment layout starts as "undefined", and transitions to "Present" so its possible to display it
//we dont care about stencil, and dont use multisampling

	VkAttachmentDescription color_attachment = {};
	color_attachment.format = swachainImageFormat;
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

void REngine::forward_pass(VkClearValue clearValue, VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu forward pass");
	vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Forward Primitives");
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 0.f;

	//start the main renderpass. 
	//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_renderPass, _windowExtent, nocopy ? get_current_frame()._framebuffer : get_current_frame()._forwardFramebuffer);

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
		draw_objects_forward(cmd, get_render_scene()->_forwardPass);
		draw_objects_forward(cmd, get_render_scene()->_transparentForwardPass);
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
	VkDescriptorBufferInfo instanceInfo = pass.compactedInstanceBuffer.get_info();
	if (instanceInfo.range == 0)
		return;

	glm::mat4 view = _camera.get_view_matrix(this);
	glm::mat4 projection = _camera.get_projection_matrix(this);
	glm::mat4 pre_rotate = _camera.get_pre_rotation_matrix(this);


	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = pre_rotate * projection * view;

	_sceneParameters.sunlightShadowMatrix = _mainLight.get_projection() * _mainLight.get_view();

	float framed = (_frameNumber / 120.f);
	_sceneParameters.ambientColor = glm::vec4{ 0.5 };
	_sceneParameters.sunlightColor = glm::vec4{ 1.f };
	_sceneParameters.sunlightDirection = glm::vec4(_mainLight.lightDirection * 1.f, 1.f);

	_sceneParameters.sunlightColor.w = CVAR_Shadowcast.Get() ? 0.f : 1.f;

	//push data to dynmem
	uint32_t scene_data_offset = get_current_frame().dynamicData.push(_sceneParameters);
	uint32_t camera_data_offset = get_current_frame().dynamicData.push(camData);

	VkDescriptorBufferInfo objectBufferInfo = get_render_scene()->objectDataBuffer.get_info();
	VkDescriptorBufferInfo sceneInfo = get_current_frame().dynamicData.source.get_info();
	sceneInfo.range = sizeof(GPUSceneData);

	VkDescriptorBufferInfo camInfo = get_current_frame().dynamicData.source.get_info();
	camInfo.range = sizeof(GPUCameraData);


	VkDescriptorImageInfo shadowImage;
	shadowImage.sampler = _shadowSampler;

	shadowImage.imageView = get_current_frame()._shadowImage._defaultView;
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

void REngine::shadow_pass(VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu shadow pass");
	vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Shadow Primitives");

	if (CVAR_FreezeShadows.Get() || !*CVarSystem::Get()->GetIntCVar("gpu.shadowcast"))
		return;

	//clear depth at 1
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;
	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_shadowPass, _shadowExtent, get_current_frame()._shadowFramebuffer);

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

	if (get_render_scene()->_shadowPass.batches.size() > 0)
	{
		//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Shadow  Pass");
		draw_objects_shadow(cmd, get_render_scene()->_shadowPass);
	}

	//finalize the render pass
	vkCmdEndRenderPass(cmd);
}

void REngine::draw_objects_shadow(VkCommandBuffer cmd, MeshPass& pass)
{
	ZoneScopedNC("DrawObjects", tracy::Color::Blue);
	VkDescriptorBufferInfo instanceInfo = pass.compactedInstanceBuffer.get_info();
	if (instanceInfo.range == 0)
		return;

	glm::mat4 view = _mainLight.get_view();

	glm::mat4 projection = _mainLight.get_projection();

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	//push data to dynmem
	uint32_t camera_data_offset = get_current_frame().dynamicData.push(camData);


	VkDescriptorBufferInfo objectBufferInfo = get_render_scene()->objectDataBuffer.get_info();

	VkDescriptorBufferInfo camInfo = get_current_frame().dynamicData.source.get_info();
	camInfo.range = sizeof(GPUCameraData);

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
	VkDescriptorBufferInfo objectBufferInfo = get_render_scene()->objectDataBuffer.get_info();

	VkDescriptorBufferInfo dynamicInfo = get_current_frame().dynamicData.source.get_info();
	dynamicInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.passObjectsBuffer.get_info();
	VkDescriptorBufferInfo finalInfo = pass.compactedInstanceBuffer.get_info();
	VkDescriptorBufferInfo indirectInfo = pass.drawIndirectBuffer.get_info();

	auto image = get_pool_image(DEPTH_PYRAMID_RES);
	if (!image.image)
		return;

	auto imageView = get_pool_image_view(image.image, -1); //srv
	assert(imageView);
	VkDescriptorImageInfo depthPyramid;
	depthPyramid.sampler = _depthSampler;
	depthPyramid.imageView = imageView;
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
	cullData.pyramid = image.desc.w + (image.desc.h << 16);
	cullData.view = params.viewmat;//get_view_matrix();

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
	/*if (*CVarSystem::Get()->GetIntCVar("culling.outputIndirectBufferToFile"))
	{
		uint32_t offset = get_current_frame().debugDataOffsets.back();
		VkBufferCopy debugCopy;
		debugCopy.dstOffset = offset;
		debugCopy.size = pass.batches.size() * sizeof(GPUIndirectObject);
		debugCopy.srcOffset = 0;
		vkCmdCopyBuffer(cmd, pass.drawIndirectBuffer._buffer, get_current_frame().debugOutputBuffer._buffer, 1, &debugCopy);
		get_current_frame().debugDataOffsets.push_back(offset + static_cast<uint32_t>(debugCopy.size));
		get_current_frame().debugDataNames.push_back("Cull Indirect Output");
	}*/
}


struct alignas(16) DepthReduceData
{
	glm::vec2 imageSize;
};
inline uint32_t getGroupCount(uint32_t threadCount, uint32_t localSize)
{
	return (threadCount + localSize - 1) / localSize;
}

/// <summary>
/// /////////////////////////////////////////////////////////////////////////////////////////
/// </summary>



class RGView : public RGResource
{
public:
	struct Desc
	{
		Desc(int ll) : level(ll) {}
		Desc() : level(0) {}
		int level;
	} desc;

	RGView(RGTexture& tex, RGHandle h, const Desc& d) : RGResource(tex.name), texHandle(h), desc(d) {}
	RGView() {}
	RGHandle texHandle = 0;

	AllocatedImage image;
	VkImageViewCreateInfo viewInfo = {}; // debug only
};

class RenderGraph;
class RGPass
{
public:
	std::function<void(RenderGraph&)> func;
};

//typedef RGTexture& RGTextureRef;
//typedef RGPass&    RGPassRef;

//struct PoolTextureKey
//{
//	RGTexture tex;
//};
struct PoolViewKey
{
	VkImage image;
	int level;

	bool operator==(const PoolViewKey& other) const
	{
		return (image == other.image
			&& level == other.level);
	}
};

struct PoolViewKeyHash {
	std::size_t operator()(const PoolViewKey& k) const
	{
		return ((std::hash<void*>()(k.image)
			^ (std::hash<int>()(k.level) << 1)) >> 1);
		//^ (hash<int>()(k.third) << 1);
	}
};

typedef RGName PoolTextureKey;


std::unordered_map<PoolTextureKey, PoolTextureVal> pool_textures;
std::unordered_map<PoolViewKey, VkImageView, PoolViewKeyHash> pool_views;


PoolTextureVal get_pool_image(RGName name)
{
	auto key = PoolTextureKey{ name };
	auto it = pool_textures.find(key);
	if (it != pool_textures.end())
		return it->second;
	return PoolTextureVal();
}

VkImageView get_pool_image_view(VkImage image, int level)
{
	auto key = PoolViewKey{ image, level };
	auto it = pool_views.find(key);
	if (it != pool_views.end())
		return it->second;
	return 0;
}

//struct PoolViewKeyHash {
//	std::size_t operator()(const PoolViewKey& k) const
//	{
//		return k.hash();
//	}
//};


class RenderGraph
{
public:
	RenderGraph(REngine* e) : engine(e) {}

	RGHandle create_texture(RGName name, const RGTexture::Desc& desc
		//ERDGTextureFlags Flags
	)
	{
		textures[numTextures++] = RGTexture(name, desc);
		return numTextures - 1;
	}
	RGHandle create_texture(RGName name, AllocatedImage image)
	{
		RGTexture::Desc desc;
		auto tex = RGTexture(name, desc);
		tex.image = image;
		textures[numTextures++] = tex;
		return numTextures - 1;
	}
	const RGTexture& get_texture(RGHandle handle) const
	{
		return textures[handle];
	}


	RGHandle create_view(RGHandle tex, const RGView::Desc& desc)
	{
		views[numViews++] = RGView(textures[tex], tex, desc);
		return numViews - 1;
	}
	const RGView& get_view(RGHandle handle) const
	{
		return views[handle];
	}

	template </*typename ParameterStructType, */typename F>
	RGHandle add_pass(RGName name,
		/*const ParameterStructType* ParameterStruct, ERDGPassFlags Flags, */
		F&& func)
	{
		static_assert(sizeof(F) < 300, "DONT CAPTURE TOO MUCH IN THE LAMBDA");
		RGPass pass;
		pass.func = func;
		passes[numPasses++] = pass;
		return numPasses - 1;
	}

	void execute()
	{
		compile();
		for (int i = 0; i < numPasses; ++i)
		{
			auto& pass = passes[i];
			pass.func(*this);
		}
	}

private:
	REngine* engine;
	
	void compile()
	{
		// check textures
		for (int i = 0; i < numTextures; ++i)
		{
			auto& tex = textures[i];
			if (!tex.image._image)				// external tex?
			{
				// cache textures (per frame?)
				VkImage image = get_pool_image(tex.name).image;
				if (!image)
				{
					VkExtent3D extent = { tex.desc.w, tex.desc.h, tex.desc.d };
					VkImageCreateInfo info = vkinit::image_create_info(tex.desc.format,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,		//???
						extent);
					info.mipLevels = tex.desc.levels;

					VmaAllocationCreateInfo allocInfo = {};
					allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
					allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

					AllocatedImage newImage;
					VK_CHECK(vmaCreateImage(engine->_allocator, &info, &allocInfo, &newImage._image, &newImage._allocation, nullptr));

					VkImageMemoryBarrier initB = vkinit::image_barrier(newImage._image,
						0, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
						VK_IMAGE_ASPECT_COLOR_BIT);
					vkCmdPipelineBarrier(engine->get_current_frame()._mainCommandBuffer,
						VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &initB);

					image = newImage._image;
					pool_textures.emplace(tex.name, PoolTextureVal{ image, tex.desc });

					auto allocator = engine->_allocator;
					auto alloc = newImage._allocation;
					engine->_surfaceDeletionQueue.push_function([=]() {
						pool_textures.erase(tex.name);
						vmaDestroyImage(allocator, image, alloc);
						});
				}

				assert(image);
				tex.image._image = image;
			}
		}

		// check views
		for (int i = 0; i < numViews; ++i)
		{
			auto& view = views[i];
			auto& tex = textures[view.texHandle];
			view.image._image = tex.image._image;

			VkImageView imageView = tex.image._defaultView;
			if (!imageView) {
				imageView = get_pool_image_view(view.image._image, view.desc.level);
			}
			if (!imageView) 
			{
				VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(tex.desc.format, view.image._image, 
					VK_IMAGE_ASPECT_COLOR_BIT); //??
				viewInfo.subresourceRange.levelCount = view.desc.level == -1 ? tex.desc.levels : 1;
				viewInfo.subresourceRange.baseMipLevel = view.desc.level == -1 ? 0 : view.desc.level;
				VK_CHECK(vkCreateImageView(engine->_device, &viewInfo, nullptr, &view.image._defaultView));
				view.viewInfo = viewInfo;

				imageView = view.image._defaultView;
				auto key = PoolViewKey{ view.image._image, view.desc.level };
				pool_views.emplace(key, imageView);

				auto device = engine->_device;
				engine->_surfaceDeletionQueue.push_function([=]() {
					pool_views.erase(key);
					vkDestroyImageView(device, imageView, nullptr);
					});
			}

			assert(imageView);
			view.image._defaultView = imageView;
		}
	}
	
	std::array<RGTexture, 64>  textures;
	int numTextures = 0;
	std::array<RGView, 64>	   views;
	int numViews = 0;
	std::array<RGPass, 64>	   passes;
	int numPasses = 0;
};


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
	while (width > 1 || height > 1) {
		result++;
		width /= 2;
		height /= 2;
	}
	return result;
}

void REngine::reduce_depth(VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu depth reduce");

	auto width = _windowExtent.width;
	auto height = _windowExtent.height;


	// do it early in the end of prev (forward) pass
	VkImageMemoryBarrier depthReadBarrier = vkinit::image_barrier(get_current_frame()._depthImage._image,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_ASPECT_DEPTH_BIT);
	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depthReadBarrier);


	RenderGraph graph(this);

	// Note: previousPow2 makes sure all reductions are at most by 2x2 which makes sure they are conservative
	auto depthPyramidWidth = previousPow2(width);
	auto depthPyramidHeight = previousPow2(height);
	auto depthPyramidLevels = getImageMipLevels(depthPyramidWidth, depthPyramidHeight);

	auto depthTex = graph.create_texture(DEPTH_RES, get_current_frame()._depthImage);

	auto desc = RGTexture::Desc(depthPyramidWidth, depthPyramidHeight, VK_FORMAT_R32_SFLOAT, depthPyramidLevels);
	auto pyrTex = graph.create_texture(DEPTH_PYRAMID_RES, desc);
	{
		auto desc = RGView::Desc(-1); 	// default view for srvs with all mips
		auto defaultView = graph.create_view(pyrTex, desc);
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _depthReducePipeline);  //!!!

	for (int32_t i = 0; i < depthPyramidLevels; ++i)
	{
		auto desc = RGView::Desc(i);
		auto destView = graph.create_view(pyrTex, desc);

		RGHandle sourceView;
		if (i == 0)
		{
			//sourceTarget.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			auto desc = RGView::Desc(-1);
			sourceView = graph.create_view(depthTex, desc); // external image
		}
		else {
			//sourceTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			auto desc = RGView::Desc(i - 1);
			sourceView = graph.create_view(pyrTex, desc);
		}

		graph.add_pass(DEPTH_PYRAMID_PASS,
			[=](RenderGraph& g)
			{
				VkDescriptorImageInfo destTarget = {};
				destTarget.sampler = _depthSampler;
				destTarget.imageView = g.get_view(destView).image._defaultView;
				destTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

				VkDescriptorImageInfo sourceTarget = {};
				sourceTarget.sampler = _depthSampler;
				auto& rec = g.get_view(sourceView);
				sourceTarget.imageView = rec.image._defaultView;
				sourceTarget.imageLayout = i == 0 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;

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

				vkCmdDispatch(cmd, getGroupCount(reduceData.imageSize.x, 32), getGroupCount(reduceData.imageSize.y, 32), 1);

				auto destImage = g.get_texture(pyrTex).image._image;
				VkImageMemoryBarrier reduceBarrier = vkinit::image_barrier(destImage,
					VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
					VK_IMAGE_ASPECT_COLOR_BIT);
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &reduceBarrier);

			});
	}

	graph.execute();


	// next pass with depth write will be forward pass
	VkImageMemoryBarrier depthWriteBarrier = vkinit::image_barrier(get_current_frame()._depthImage._image,
		VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_IMAGE_ASPECT_DEPTH_BIT);
	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_DEPENDENCY_BY_REGION_BIT
		, 0, 0, 0, 0, 1, &depthWriteBarrier);

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

		{
			VkDeviceSize offset[] = { 0, 0 };
			VkBuffer buffs[] = { get_render_scene()->mergedVertexBufferP._buffer, get_render_scene()->mergedVertexBufferA._buffer };
			vkCmdBindVertexBuffers(cmd, 0, pass.type == MeshpassType::DirectionalShadow ? 1 : 2, buffs, offset);
		}

		vkCmdBindIndexBuffer(cmd, get_render_scene()->mergedIndexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

		stats.objects += static_cast<uint32_t>(pass.flat_batches.size());
		for (int i = 0; i < pass.multibatches.size(); i++)
		{
			auto& multibatch = pass.multibatches[i];
			auto& instanceDraw = pass.batches[multibatch.first];

			VkPipeline newPipeline = instanceDraw.material.shaderPass->pipeline;
			VkPipelineLayout newLayout = instanceDraw.material.shaderPass->layout;
			VkDescriptorSet newMaterialSet = instanceDraw.material.materialSet;

			Mesh* drawMesh = get_render_scene()->get_mesh(instanceDraw.meshID)->original;

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

			bool merged = get_render_scene()->get_mesh(instanceDraw.meshID)->isMerged;
			if (merged)
			{
				if (lastMesh != nullptr)
				{
					assert(0);
					VkDeviceSize offset = 0;
					//vkCmdBindVertexBuffers(cmd, 0, 1, &get_render_scene()->mergedVertexBuffer._buffer, &offset);

					vkCmdBindIndexBuffer(cmd, get_render_scene()->mergedIndexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
					lastMesh = nullptr;
				}
			}
			else if (lastMesh != drawMesh)
			{
				assert(0);
				//bind the mesh vertex buffer with offset 0
				VkDeviceSize offset = 0;
				//vkCmdBindVertexBuffers(cmd, 0, 1, &drawMesh->_vertexBuffer._buffer, &offset);

				if (drawMesh->_indexBuffer._buffer != VK_NULL_HANDLE) {
					vkCmdBindIndexBuffer(cmd, drawMesh->_indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
				}
				lastMesh = drawMesh;
			}

			bool bHasIndices = drawMesh->_indices.size() > 0;
			if (!bHasIndices) {
				stats.draws++;
				stats.triangles += static_cast<int32_t>(drawMesh->_vertices_p.size() / 3) * instanceDraw.count;
				vkCmdDraw(cmd, static_cast<uint32_t>(drawMesh->_vertices_p.size()), instanceDraw.count, 0, instanceDraw.first);
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

void REngine::copy_render_to_swapchain(VkCommandBuffer cmd)
{
	if (nocopy)
		return;

	//start the main renderpass. 
	//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
	VkRenderPassBeginInfo copyRP = vkinit::renderpass_begin_info(_copyPass, _windowExtent, get_current_frame()._framebuffer);

	vkCmdBeginRenderPass(cmd, &copyRP, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)_windowExtent.width;
	viewport.height = (float)_windowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	//std::swap(viewport.width, viewport.height);

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = _windowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetDepthBias(cmd, 0, 0, 0);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _blitPipeline);

	VkDescriptorImageInfo sourceImage;
	sourceImage.sampler = _smoothSampler;

	sourceImage.imageView = get_current_frame()._rawRenderImage._defaultView;
	sourceImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorSet blitSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_image(0, &sourceImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(blitSet);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _blitLayout, 0, 1, &blitSet, 0, nullptr);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
}