#pragma once
#include <array>

typedef int16_t	RGHandle;
typedef const char* RGName;
typedef uint8_t RGIdx;

extern RGName RES_DEPTH;
extern RGName RES_SHADOW_MAP;
extern RGName RES_DEPTH_PYRAMID;
extern RGName RES_DEPTH_PYRAMID_SHADOW;
extern RGName PASS_DEPTH_PYRAMID;


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
		int w = -1, h = 1, d = 1;
		int levels = 1;
		VkFormat format = VK_FORMAT_UNDEFINED;
	} desc;

	RGTexture(RGName name, const Desc& d) : RGResource(name), desc(d) {}
	RGTexture() {}

	//AllocatedImage image;
};


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

	//AllocatedImage image;
	//VkImageViewCreateInfo viewInfo = {}; // debug only
};

enum RGPassFlags
{
	RGPASS_FLAG_GRAPHICS = 0,
	RGPASS_FLAG_COMPUTE = 1,
};

//class RenderGraph;
class RGPass
{
	friend class RenderGraph;
public:
	RGPass(RGName n, RGPassFlags f) : name(n), flags(f) {}
	RGPass() {}

	void write(RGHandle& view)
	{
		assert(view >= 0);
		writes[numWrites++] = view;
	}
	void read(RGHandle& view)
	{
		assert(view >= 0);
		reads[numReads++] = view;
		inDegrees = numReads;
	}

private:
	int8_t numWrites = 0;
	int8_t numReads = 0;
	RGPassFlags flags = {};

	int8_t inDegrees = 0;

	std::array<RGHandle, 8> writes = {};
	std::array<RGHandle, 8> reads = {};

	std::function<void(RenderGraph&)> func; // todo: remove heap!!!	//use stack allocator
	RGName name;
};


class RenderGraph
{
public:
	RenderGraph(REngine* e);

	RGHandle create_texture(RGName name, const RGTexture::Desc& desc); 	//ERDGTextureFlags Flags
	RGHandle create_texture(RGName name, VkImage image);
	
	RGHandle create_view(RGHandle tex, const RGView::Desc& desc);
	RGHandle create_view(RGHandle tex, VkImageView view);

	VkImage get_image(RGHandle handle) const;
	VkImageView get_image_view(RGHandle handle) const;

	template <typename F>
	RGHandle add_pass(RGName name, RGPassFlags flags, F&& func);

	void pass_write(RGHandle& pass, RGHandle& view);
	void pass_read(RGHandle& pass, RGHandle& view);

	void execute();

	REngine* engine;

private:
	static constexpr int MAX_PASSES = 32;

	void sort_passes(std::array<RGIdx, MAX_PASSES>& order);
	std::tuple<RGIdx, bool, bool> find_next_resource_pass(RGHandle res, RGIdx curPassIdx, const std::array<RGIdx, MAX_PASSES>& order);

	void check_physical_texture(RGHandle tex, RGPass& pass);
	void check_physical_view(RGHandle view);

	RGIdx numPasses = 0;
	std::array<RGPass, MAX_PASSES>	   passes;
};