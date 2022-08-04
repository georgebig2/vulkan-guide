
#pragma once

#include "rengine.h"

namespace tracy { class VkCtx; }

class VulkanEngine : public REngine {
public:

	int _selectedShader{ 0 };
	struct SDL_Window* _window{ nullptr };

	tracy::VkCtx* _graphicsQueueContext;

	void init() override;
	/*void update() override;*/
	void cleanup() override;
	void run();

	bool create_surface(VkInstance instance, VkSurfaceKHR* surface) override;
};

