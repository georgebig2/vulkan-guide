
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
	else if (ev->type == SDL_MOUSEMOTION)
	{
		static float prevx = ev->motion.x;
		static float prevy = ev->motion.y;
		if (!camera.bLocked)
		{
			camera.pitch -= (ev->motion.y - prevy) * 0.003f;
			camera.yaw -= (ev->motion.x - prevx) * 0.003f;
			//LOG_INFO("%f", camera.yaw);
		}
		prevx = ev->motion.x;
		prevy = ev->motion.y;
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
}

void VulkanEngine::init_imgui()
{
	REngine::init_imgui();
	ImGui_ImplSDL2_InitForVulkan(_window);
	_imguiDeletionQueue.push_function([=]() {
		ImGui_ImplSDL2_Shutdown();
		});
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
	bool inFocus = false;

	// main loop
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
				//if (e.type == SDL_APP_WILLENTERFOREGROUND)
				//{
				//	inFocus = true;
				//}
				//else if (e.type == SDL_APP_WILLENTERBACKGROUND)
				//{
				//	inFocus = false;
				//}
				auto f = SDL_GetWindowFlags(_window);
				inFocus = f & SDL_WINDOW_INPUT_FOCUS;

				ImGui_ImplSDL2_ProcessEvent(&e);

				//if (inFocus) {
					process_input_event(_camera, &e);
				//}

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
						//resize_window(0, 0);
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

			ImGuiIO& io = ImGui::GetIO();
			//io.DeltaTime = 1.0f / 60.0f;
			ImGui_ImplSDL2_NewFrame(_window);
			//io.DisplaySize = ImVec2(_windowExtent.height, _windowExtent.width);
			hud_update();
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


