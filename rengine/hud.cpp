#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_vulkan.h"
#include "cvars.h"
#include "rengine.h"
#include <array>
#include "Tracy.hpp"
#include "TracyVulkan.hpp"
#include "vk_profiler.h"

AutoCVar_Int CVAR_OutputIndirectToFile("culling.outputIndirectBufferToFile", "output the indirect data to a file. Autoresets", 0, CVarFlags::EditCheckbox);


void REngine::hud_update()
{
	ImGuiIO& io = ImGui::GetIO();
	io.FontGlobalScale = get_dpi_factor();

	ImGui::GetStyle() = ImGuiStyle();
	ImGui::GetStyle().ScaleAllSizes(get_dpi_factor());

	ImGui_ImplVulkan_NewFrame(_pretransformFlag);
	io.DisplaySize = ImVec2(_windowExtent.width, _windowExtent.height);
	//if (_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) {
	//	io.DisplaySize = ImVec2(_windowExtent.height, _windowExtent.width);
	//}
	//else if (_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
	//}
	//else if (_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) {
	//	io.DisplaySize = ImVec2(_windowExtent.height, _windowExtent.width);
	//}

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
		std::array<float, gsMaxHistory> history = {};
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
		//std::swap(windowSize.x, windowSize.y);
		ImVec2 windowPos = { 0, windowSize.y / 2 };
		windowSize.y /= 2.4f;
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
			//std::swap(ws.x, ws.y);
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
				//g.avg = 0;
				auto scale = 1;// indicator->unit->GetGraphScale();
				for (int i = 0; i < gsMaxHistory - 1; ++i) {

					points[i + 0].y = wp2.y - ws.y * g.history[(i + curFrame) % gsMaxHistory] * scale / maxValue;
					points[i + 1].y = wp2.y - ws.y * g.history[(i + 1 + curFrame) % gsMaxHistory] * scale / maxValue;
					points[i + 0].x = wp.x + (i + 0) * ws.x / gsMaxHistory;
					points[i + 1].x = wp.x + (i + 1) * ws.x / gsMaxHistory;
					//g.avg += g.history[(i + curFrame) % gsMaxHistory];
				}
				g.avg = g.avg * 0.75f + g.history[curFrame % gsMaxHistory] * 0.25f;///= (gsMaxHistory - 1);

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