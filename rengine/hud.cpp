#include "rengine.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_vulkan.h"
#include "cvars.h"

void REngine::hud_update()
{
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
}