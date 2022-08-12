
//#define VMA_IMPLEMENTATION
#include "vk.h"
#include <vk_types.h>
//#include <vk_initializers.h>
#include <vk_descriptors.h>

#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include "vk_textures.h"
#include "vk_shaders.h"


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

#include "vk_scene.h"

AutoCVar_Int CVAR_OcclusionCullGPU("culling.enableOcclusionGPU", "Perform occlusion culling in gpu", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_CamLock("camera.lock", "Locks the camera", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_OutputIndirectToFile("culling.outputIndirectBufferToFile", "output the indirect data to a file. Autoresets", 0, CVarFlags::EditCheckbox);

//constexpr bool bUseValidationLayers = false;


#include "SDL.h"
void process_input_event(PlayerCamera& camera, SDL_Event * ev)
{
	if (ev->type == SDL_KEYDOWN)
	{
		switch (ev->key.keysym.sym)
		{
		case SDLK_UP:
		case SDLK_w:
			camera.inputAxis.x += 1.f;
			break;
		case SDLK_DOWN:
		case SDLK_s:
			camera.inputAxis.x -= 1.f;
			break;
		case SDLK_LEFT:
		case SDLK_a:
			camera.inputAxis.y -= 1.f;
			break;
		case SDLK_RIGHT:
		case SDLK_d:
			camera.inputAxis.y += 1.f;
			break;
		case SDLK_q:
			camera.inputAxis.z -= 1.f;
			break;

		case SDLK_e:
			camera.inputAxis.z += 1.f;
			break;
		case SDLK_LSHIFT:
			camera.bSprint = true;
			break;
		}
	}
	else if (ev->type == SDL_KEYUP)
	{
		switch (ev->key.keysym.sym)
		{
		case SDLK_UP:
		case SDLK_w:
			camera.inputAxis.x -= 1.f;
			break;
		case SDLK_DOWN:
		case SDLK_s:
			camera.inputAxis.x += 1.f;
			break;
		case SDLK_LEFT:
		case SDLK_a:
			camera.inputAxis.y += 1.f;
			break;
		case SDLK_RIGHT:
		case SDLK_d:
			camera.inputAxis.y -= 1.f;
			break;
		case SDLK_q:
			camera.inputAxis.z += 1.f;
			break;

		case SDLK_e:
			camera.inputAxis.z -= 1.f;
			break;
		case SDLK_LSHIFT:
			camera.bSprint = false;
			break;
		}
	}
	else if (ev->type == SDL_MOUSEMOTION) {
		if (!camera.bLocked)
		{
			camera.pitch -= ev->motion.yrel * 0.003f;
			camera.yaw -= ev->motion.xrel * 0.003f;
		}
	}

	camera.inputAxis = glm::clamp(camera.inputAxis, { -1.0,-1.0,-1.0 }, { 1.0,1.0,1.0 });
}

std::string VulkanEngine::asset_path(std::string_view path)
{
	return "../../assets_export/" + std::string(path);
}

std::string VulkanEngine::shader_path(std::string_view path)
{
	return "../../shaders/" + std::string(path);
}

void VulkanEngine::init(bool debug)
{
	ZoneScopedN("Engine Init");

	LogHandler::Get().set_time();

	LOG_INFO("Engine Init");

	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);
	LOG_SUCCESS("SDL inited");

	_window = SDL_CreateWindow(
		"",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
	);

	//_renderables.reserve(10000);

	REngine::init(debug);

	//this initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(_window);

}

void VulkanEngine::cleanup()
{
	if (_isInitialized) {

		REngine::cleanup();
		SDL_DestroyWindow(_window);
	}
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
				process_input_event(_camera, &e);


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

			//ImGuiIO& io = ImGui::GetIO();
			//io.DeltaTime = 1.0f / 60.0f;
			//io.DisplaySize = ImVec2(_windowExtent.width, _windowExtent.height);
			ImGui::GetStyle() = ImGuiStyle();
			ImGui::GetStyle().ScaleAllSizes(1);

			ImGui_ImplSDL2_NewFrame(_window);
			ImGui_ImplVulkan_NewFrame();
			ImGui::NewFrame();

			hud_update();

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

				ImGui::Text("Frame: %.2f ms", stats.frametime);
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
				int rng = rand() % get_render_scene()->renderables.size();

				Handle<RenderObject> h;
				h.handle = rng;

				auto* obj = get_render_scene()->get_object(h);
				auto prev = obj->transformMatrix;
				//glm::mat4 tr = glm::translate(glm::mat4{ 1.0 }, glm::vec3(0, 15, 0));
				float scale = sin(start.time_since_epoch().count() / 10000000 + h.handle) * 0.0005f + 1.f;
				glm::mat4 sm = glm::scale(glm::mat4{ 1.0 }, glm::vec3(scale));
				//glm::mat4 rot = glm::rotate(glm::radians(90.f), glm::vec3{ 1,0,0 });
				auto newm = prev * sm;
				get_render_scene()->update_transform(h, newm);

				//_renderScene.update_object(h);
			}
		}

		_camera.bLocked = CVAR_CamLock.Get();
		_camera.update_camera(stats.frametime);
		_mainLight.lightPosition = _camera.position;

		draw();
	}
}


bool VulkanEngine::create_surface(VkInstance instance, VkSurfaceKHR* surface)
{
	SDL_Vulkan_CreateSurface(_window, _instance.instance, &_surface);
	LOG_SUCCESS("SDL Surface initialized");
	return true;
}

//void VulkanEngine::init_vulkan()
//{
//	REngine::init_vulkan();
//	LOG_SUCCESS("Vulkan Instance initialized");
//	LOG_SUCCESS("GPU found");
//	LOG_INFO("The gpu has a minimum buffer alignement of {}", _gpuProperties.limits.minUniformBufferOffsetAlignment);
//}
//


