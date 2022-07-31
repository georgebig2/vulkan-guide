﻿
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_descriptors.h>

#include "VkBootstrap.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include "vk_textures.h"
#include "vk_shaders.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_vulkan.h"
#include "prefab_asset.h"
#include "material_asset.h"

#include "Tracy.hpp"
#include "TracyVulkan.hpp"
#include "vk_profiler.h"

#include "fmt/core.h"
#include "fmt/os.h"
#include "fmt/color.h"

#include "logger.h"
#include "cvars.h"



AutoCVar_Int CVAR_OcclusionCullGPU("culling.enableOcclusionGPU", "Perform occlusion culling in gpu", 1, CVarFlags::EditCheckbox);


AutoCVar_Int CVAR_CamLock("camera.lock", "Locks the camera", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_OutputIndirectToFile("culling.outputIndirectBufferToFile", "output the indirect data to a file. Autoresets", 0, CVarFlags::EditCheckbox);

AutoCVar_Float CVAR_DrawDistance("gpu.drawDistance", "Distance cull", 5000);

AutoCVar_Int CVAR_FreezeShadows("gpu.freezeShadows", "Stop the rendering of shadows", 0, CVarFlags::EditCheckbox);


constexpr bool bUseValidationLayers = false;

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




void VulkanEngine::init()
{
	ZoneScopedN("Engine Init");

	LogHandler::Get().set_time();

	LOG_INFO("Engine Init");

	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);
	LOG_SUCCESS("SDL inited");
	SDL_WindowFlags window_flags = (SDL_WINDOW_VULKAN);

	_window = SDL_CreateWindow(
		"",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	//_renderables.reserve(10000);

	_meshes.reserve(1000);

	init_vulkan();

	_profiler = new vkutil::VulkanProfiler();
	_profiler->init(_device, _gpuProperties.limits.timestampPeriod);

	_shaderCache.init(_device);
	_renderScene.init();

	init_forward_renderpass();
	init_copy_renderpass();
	init_shadow_renderpass();

	init_framebuffers();

	init_commands();

	init_sync_structures();

	init_descriptors();

	init_pipelines();

	LOG_INFO("Engine Initialized, starting Load");


	load_images();

	load_meshes();

	init_scene();

	init_imgui();

	_renderScene.build_batches();
	_renderScene.merge_meshes(this);

	_isInitialized = true;

	_camera = {};
	_camera.position = { 0.f,6.f,5.f };

	_mainLight.lightPosition = { 0,50,0 };
	_mainLight.lightDirection = glm::vec3(0.3, -1, 0.3);
	_mainLight.shadowExtent = { 700 ,700 ,700 };
}
void VulkanEngine::cleanup()
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

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
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

		TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "gpu frame");
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

	TracyVkCollect(_graphicsQueueContext, get_current_frame()._mainCommandBuffer);

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


void VulkanEngine::forward_pass(VkClearValue clearValue, VkCommandBuffer cmd)
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
		TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Forward Pass");
		draw_objects_forward(cmd, _renderScene._forwardPass);
		draw_objects_forward(cmd, _renderScene._transparentForwardPass);
	}


	{
		TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Imgui Draw");
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}

	//finalize the render pass
	vkCmdEndRenderPass(cmd);
}


void VulkanEngine::shadow_pass(VkCommandBuffer cmd)
{
	vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu shadow pass");
	vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Shadow Primitives");
	if (CVAR_FreezeShadows.Get()) return;
	if (!*CVarSystem::Get()->GetIntCVar("gpu.shadowcast"))
	{
		return;
	}

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
		TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Shadow  Pass");
		draw_objects_shadow(cmd, _renderScene._shadowPass);
	}

	//finalize the render pass
	vkCmdEndRenderPass(cmd);
}

void VulkanEngine::copy_render_to_swapchain(uint32_t swapchainImageIndex, VkCommandBuffer cmd)
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

void VulkanEngine::run()
{

	LOG_INFO("Starting Main Loop ");

	bool bQuit = false;

	// Using time point and system_clock 
	std::chrono::time_point<std::chrono::system_clock> start, end;

	start = std::chrono::system_clock::now();
	end = std::chrono::system_clock::now();
	//main loop
	while (!bQuit)
	{
		ZoneScopedN("Main Loop");
		end = std::chrono::system_clock::now();
		std::chrono::duration<float> elapsed_seconds = end - start;
		stats.frametime = elapsed_seconds.count() * 1000.f;

		start = std::chrono::system_clock::now();
		//Handle events on queue
		SDL_Event e;
		{
			ZoneScopedNC("Event Loop", tracy::Color::White);
			while (SDL_PollEvent(&e) != 0)
			{

				ImGui_ImplSDL2_ProcessEvent(&e);
				_camera.process_input_event(&e);


				//close the window when user alt-f4s or clicks the X button			
				if (e.type == SDL_QUIT)
				{
					bQuit = true;
				}
				else if (e.type == SDL_KEYDOWN)
				{
					if (e.key.keysym.sym == SDLK_SPACE)
					{
						_selectedShader += 1;
						if (_selectedShader > 1)
						{
							_selectedShader = 0;
						}
					}
					if (e.key.keysym.sym == SDLK_TAB)
					{
						if (CVAR_CamLock.Get())
						{
							LOG_INFO("Mouselook disabled");
							CVAR_CamLock.Set(false);
						}
						else {
							LOG_INFO("Mouselook enabled");
							CVAR_CamLock.Set(true);
						}
					}
				}
			}
		}
		{
			ZoneScopedNC("Imgui Logic", tracy::Color::Grey);

			//imgui new frame 
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplSDL2_NewFrame(_window);
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

			constexpr auto gsMaxHistory = 256;
			static uint32_t curFrame = 0;
			static bool paused = false;
			if (!paused) {
				curFrame = (curFrame + 1) % gsMaxHistory;
			}
			struct Graph
			{
				//const char* name;
				std::array<float, gsMaxHistory> history;
				double avg = 0;
				bool checked = true;
			};
			static std::unordered_map<std::string, Graph> graphs;

			{
				ImGui::Begin("engine", 0, ImGuiWindowFlags_AlwaysAutoResize);

				ImGui::Text("Frame: %f", stats.frametime);
				graphs["Frame"].history[curFrame] = stats.frametime;

				ImGui::Text("Objects: %d", stats.objects);
				//ImGui::Text("Drawcalls: %d", stats.drawcalls);
				ImGui::Text("Batches: %d", stats.draws);
				//ImGui::Text("Triangles: %d", stats.triangles);		

				CVAR_OutputIndirectToFile.Set(false);
				if (ImGui::Button("Output Indirect"))
				{
					CVAR_OutputIndirectToFile.Set(true);
				}

				ImGui::Separator();
				for (auto& [k, v] : _profiler->timing)
				{
					ImGui::Text("TIME %s %.2f ms", k.c_str(), v);
					graphs[k.c_str()].history[curFrame] = v;
				}
				for (auto& [k, v] : _profiler->stats)
				{
					ImGui::Text("STAT %s %d,%03d,%03d", k.c_str(), v / 1000000, (v / 1000) % 1000, v % 1000);
					//ImGui::Text("STAT %s %d", k.c_str(), v);
				}

				ImGui::End();
			}

			{
				const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
				ImVec2 windowSize = main_viewport->Size;
				ImVec2 windowPos = { 0, windowSize.y / 2 };
				windowSize.y /= 2.7f;
				ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowBgAlpha(0.6f);

				static bool opened = true;
				if (ImGui::Begin("History graph show", &opened, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {

					if (ImGui::Button(paused ? "resume" : "pause")) {
						paused = !paused;
					}
					static int maxValue = 60;
					ImGui::DragInt("scale", &maxValue, 1.f);
					if (maxValue < 1) {
						maxValue = 1;
					}

					auto wp = ImGui::GetWindowPos();
					auto ws = ImGui::GetWindowSize();
					ImVec2 wp2 = ws + wp;

					ImDrawList* draw_list = ImGui::GetWindowDrawList();

					auto y1 = wp2.y - ws.y * 1000.f / 30 / maxValue;
					auto y2 = wp2.y - ws.y * 1000.f / 60 / maxValue;
					auto y3 = wp2.y - ws.y * 60.f / maxValue;
					auto y4 = wp2.y;
					draw_list->AddLine(ImVec2(wp.x, y1), ImVec2(wp2.x, y1), IM_COL32(180, 180, 180, 255));
					draw_list->AddLine(ImVec2(wp.x, y2), ImVec2(wp2.x, y2), IM_COL32(180, 180, 180, 255));
					draw_list->AddLine(ImVec2(wp.x, y3), ImVec2(wp2.x, y3), IM_COL32(180, 180, 180, 255));
					draw_list->AddLine(ImVec2(wp.x, y4), ImVec2(wp2.x, y4), IM_COL32(180, 180, 180, 255));

					ImU32 colors[] = {
						IM_COL32(200, 150, 200, 255),
						IM_COL32(20, 108, 255, 255),
						IM_COL32(18, 255, 18, 255),
						IM_COL32(255, 255, 128, 255),
						IM_COL32(255, 128, 255, 255),
						IM_COL32(248, 150, 23, 255),
						IM_COL32(255, 58, 58, 255),
					};

					std::array<ImVec2, gsMaxHistory> points;
					int cIdx = 0;
					for (auto& [k, g] : graphs)
					{
						g.avg = 0;
						auto scale = 1;// indicator->unit->GetGraphScale();
						for (int i = 0; i < gsMaxHistory - 1; ++i) {

							points[i + 0].y = wp2.y - ws.y * g.history[(i + curFrame) % gsMaxHistory] * scale / maxValue;
							points[i + 1].y = wp2.y - ws.y * g.history[(i + 1 + curFrame) % gsMaxHistory] * scale / maxValue;
							points[i + 0].x = wp.x + (i + 0) * ws.x / gsMaxHistory;
							points[i + 1].x = wp.x + (i + 1) * ws.x / gsMaxHistory;
							g.avg += g.history[(i + curFrame) % gsMaxHistory];
						}
						g.avg /= (gsMaxHistory - 1);

						if (g.checked) {
							draw_list->AddPolyline(&points[0], gsMaxHistory, colors[cIdx], 0, 1.5f);
						}
						cIdx = (cIdx + 1) % (sizeof(colors) / sizeof(colors[0]));
					}

					cIdx = 0;
					for (auto& [k, g] : graphs)
					{
						//if (indicator->unit->IsPrinting())
						{
							ImGui::PushID(cIdx);
							ImGui::Checkbox("", &g.checked);
							ImGui::PopID();
							ImGui::SameLine();

							auto val = g.avg;// *indicator->unit->GetGraphScale();
							const char* format = "%s %.2f";
							ImGui::TextColored(ImColor(colors[cIdx]), format, k.c_str(), val);
							cIdx = (cIdx + 1) % (sizeof(colors) / sizeof(colors[0]));
						}
					}

					ImGui::End();
				}
			}
		}

		{
			ZoneScopedNC("Flag Objects", tracy::Color::Blue);
			//test flagging some objects for changes

			int N_changes = 1000;
			for (int i = 0; i < N_changes; i++)
			{
				int rng = rand() % _renderScene.renderables.size();

				Handle<RenderObject> h;
				h.handle = rng;

				auto* obj = _renderScene.get_object(h);
				auto prev = obj->transformMatrix;
				//glm::mat4 tr = glm::translate(glm::mat4{ 1.0 }, glm::vec3(0, 15, 0));
				float scale = sin(start.time_since_epoch().count() / 10000000 + h.handle) * 0.0005f + 1.f;
				glm::mat4 sm = glm::scale(glm::mat4{ 1.0 }, glm::vec3(scale));
				//glm::mat4 rot = glm::rotate(glm::radians(90.f), glm::vec3{ 1,0,0 });
				auto newm = prev * sm;
				_renderScene.update_transform(h, newm);

				//_renderScene.update_object(h);
			}
			_camera.bLocked = CVAR_CamLock.Get();

			_camera.update_camera(stats.frametime);

			_mainLight.lightPosition = _camera.position;
		}

		draw();
	}
}

FrameData& VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}


FrameData& VulkanEngine::get_last_frame()
{
	return _frames[(_frameNumber - 1) % 2];
}


void VulkanEngine::process_input_event(SDL_Event* ev)
{
	if (ev->type == SDL_KEYDOWN)
	{
		switch (ev->key.keysym.sym)
		{

		}
	}
	else if (ev->type == SDL_KEYUP)
	{
		switch (ev->key.keysym.sym)
		{
		case SDLK_UP:
		case SDLK_w:
			_camera.inputAxis.x -= 1.f;
			break;
		case SDLK_DOWN:
		case SDLK_s:
			_camera.inputAxis.x += 1.f;
			break;
		case SDLK_LEFT:
		case SDLK_a:
			_camera.inputAxis.y += 1.f;
			break;
		case SDLK_RIGHT:
		case SDLK_d:
			_camera.inputAxis.y -= 1.f;
			break;
		}
	}
	else if (ev->type == SDL_MOUSEMOTION) {
		if (!CVAR_CamLock.Get())
		{
			_camera.pitch -= ev->motion.yrel * 0.003f;
			_camera.yaw -= ev->motion.xrel * 0.003f;
		}
	}

	_camera.inputAxis = glm::clamp(_camera.inputAxis, { -1.0,-1.0,-1.0 }, { 1.0,1.0,1.0 });
}

bool VulkanEngine::create_surface(VkInstance instance, VkSurfaceKHR* surface)
{
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);
	LOG_SUCCESS("SDL Surface initialized");
	return true;
}

void VulkanEngine::init_vulkan()
{
	REngine::init_vulkan();

	//vkb::InstanceBuilder builder;
	////make the vulkan instance, with basic debug features
	//auto inst_ret = builder.set_app_name("Example Vulkan Application")

	//	.request_validation_layers(bUseValidationLayers)
	//	.use_default_debug_messenger()
	//	.build();


	LOG_SUCCESS("Vulkan Instance initialized");

	//vkb::Instance vkb_inst = inst_ret.value();

	//grab the instance 
	//_instance = vkb_inst.instance;
	//create_surface(_instance, &_surface);

	//use vkbootstrap to select a gpu. 
	//We want a gpu that can write to the SDL surface and supports vulkan 1.2
	//vkb::PhysicalDeviceSelector selector{ vkb_inst };
	//VkPhysicalDeviceFeatures feats{};

	//feats.pipelineStatisticsQuery = true;
	//feats.multiDrawIndirect = true;
	//feats.drawIndirectFirstInstance = true;
	//feats.samplerAnisotropy = true;
	//selector.set_required_features(feats);

	//vkb::PhysicalDevice physicalDevice = selector
	//	.set_minimum_version(1, 1)
	//	.set_surface(_surface)
	//	.add_required_extension(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME)
	//	
	//	.select()
	//	.value();

	LOG_SUCCESS("GPU found");

	//create the final vulkan device

	//vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	//vkb::Device vkbDevice = deviceBuilder.build().value();
	//
	//// Get the VkDevice handle used in the rest of a vulkan application
	//_device = vkbDevice.device;
	//_chosenGPU = physicalDevice.physical_device;

	//// use vkbootstrap to get a Graphics queue
	//_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();

	//_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	////initialize the memory allocator
	//VmaAllocatorCreateInfo allocatorInfo = {};
	//allocatorInfo.physicalDevice = _chosenGPU;
	//allocatorInfo.device = _device;
	//allocatorInfo.instance = _instance;
	//vmaCreateAllocator(&allocatorInfo, &_allocator);


	//
	//vkGetPhysicalDeviceProperties(_chosenGPU, &_gpuProperties);

	LOG_INFO("The gpu has a minimum buffer alignement of {}", _gpuProperties.limits.minUniformBufferOffsetAlignment);
}

void VulkanEngine::init_forward_renderpass()
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


void VulkanEngine::init_copy_renderpass()
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


void VulkanEngine::init_shadow_renderpass()
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

void VulkanEngine::init_framebuffers()
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

void VulkanEngine::init_commands()
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
	_graphicsQueueContext = TracyVkContext(_chosenGPU, _device, _graphicsQueue, _frames[0]._mainCommandBuffer);


	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily);
	//create pool for upload context
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext._commandPool));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _uploadContext._commandPool, nullptr);
		});
}

void VulkanEngine::init_sync_structures()
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


void VulkanEngine::init_pipelines()
{
	_materialSystem = new vkutil::MaterialSystem();
	_materialSystem->init(this);
	_materialSystem->build_default_templates();

	//fullscreen triangle pipeline for blits
	ShaderEffect* blitEffect = new ShaderEffect();
	blitEffect->add_stage(_shaderCache.get_shader(shader_path("fullscreen.vert.spv")), VK_SHADER_STAGE_VERTEX_BIT);
	blitEffect->add_stage(_shaderCache.get_shader(shader_path("blit.frag.spv")), VK_SHADER_STAGE_FRAGMENT_BIT);
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

bool VulkanEngine::load_compute_shader(const char* shaderPath, VkPipeline& pipeline, VkPipelineLayout& layout)
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




void VulkanEngine::load_meshes()
{
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


void VulkanEngine::load_images()
{
	load_image_to_cache("white", asset_path("Sponza/white.tx").c_str());
}


bool VulkanEngine::load_image_to_cache(const char* name, const char* path)
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

void VulkanEngine::upload_mesh(Mesh& mesh)
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

Mesh* VulkanEngine::get_mesh(const std::string& name)
{
	auto it = _meshes.find(name);
	if (it == _meshes.end()) {
		return nullptr;
	}
	else {
		return &(*it).second;
	}
}

void VulkanEngine::init_scene()
{
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


void VulkanEngine::ready_cull_data(RenderScene::MeshPass& pass, VkCommandBuffer cmd)
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

//AllocatedBufferUntyped VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags required_flags)
//{
//	//allocate vertex buffer
//	VkBufferCreateInfo bufferInfo = {};
//	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//	bufferInfo.pNext = nullptr;
//	bufferInfo.size = allocSize;
//
//	bufferInfo.usage = usage;
//
//
//	//let the VMA library know that this data should be writeable by CPU, but also readable by GPU
//	VmaAllocationCreateInfo vmaallocInfo = {};
//	vmaallocInfo.usage = memoryUsage;
//	vmaallocInfo.requiredFlags = required_flags;
//	AllocatedBufferUntyped newBuffer;
//
//	//allocate the buffer
//	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
//		&newBuffer._buffer,
//		&newBuffer._allocation,
//		nullptr));
//	newBuffer._size = allocSize;
//	return newBuffer;
//}


void VulkanEngine::reallocate_buffer(AllocatedBufferUntyped& buffer, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags required_flags /*= 0*/)
{
	AllocatedBufferUntyped newBuffer = create_buffer(allocSize, usage, memoryUsage, required_flags);

	get_current_frame()._frameDeletionQueue.push_function([=]() {

		vmaDestroyBuffer(_allocator, buffer._buffer, buffer._allocation);
		});

	buffer = newBuffer;
}

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize)
{
	// Calculate required alignment based on minimum device offset alignment
	size_t minUboAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if (minUboAlignment > 0) {
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}
	return alignedSize;
}


//void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
//{
//	
//	ZoneScopedNC("Inmediate Submit", tracy::Color::White);
//
//	VkCommandBuffer cmd;
//
//	//allocate the default command buffer that we will use for rendering
//	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);
//
//	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &cmd));
//
//	//begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
//	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
//
//	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
//
//	function(cmd);
//	
//
//	VK_CHECK(vkEndCommandBuffer(cmd));
//
//	VkSubmitInfo submit = vkinit::submit_info(&cmd);
//
//
//	//submit command buffer to the queue and execute it.
//	// _renderFence will now block until the graphic commands finish execution
//	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _uploadContext._uploadFence));
//
//	vkWaitForFences(_device, 1, &_uploadContext._uploadFence, true, 9999999999);
//	vkResetFences(_device, 1, &_uploadContext._uploadFence);
//
//	vkResetCommandPool(_device, _uploadContext._commandPool, 0);
//}


bool VulkanEngine::load_prefab(const char* path, glm::mat4 root)
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

	_renderScene.register_object_batch(prefab_renderables.data(), static_cast<uint32_t>(prefab_renderables.size()));



	return true;
}


std::string VulkanEngine::asset_path(std::string_view path)
{
	return "../../assets_export/" + std::string(path);
}



std::string VulkanEngine::shader_path(std::string_view path)
{
	return "../../shaders/" + std::string(path);
}

void VulkanEngine::refresh_renderbounds(MeshObject* object)
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


void VulkanEngine::unmap_buffer(AllocatedBufferUntyped& buffer)
{
	vmaUnmapMemory(_allocator, buffer._allocation);
}

void VulkanEngine::init_descriptors()
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
		auto dynamicDataBuffer = create_buffer(1000000, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
		_frames[i].dynamicData.init(_allocator, dynamicDataBuffer, _gpuProperties.limits.minUniformBufferOffsetAlignment);

		//20 megabyte of debug output
		_frames[i].debugOutputBuffer = create_buffer(200000000, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
	}
}

void VulkanEngine::init_imgui()
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

	//this initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(_window);

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
