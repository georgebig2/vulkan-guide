#include <vk_engine.h>

int main(int argc, char* argv[])
{
	VulkanEngine engine;

	engine.init(true);	
	
	engine.run();	

	engine.cleanup();	

	return 0;
}
