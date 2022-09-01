#pragma once
#include <array>

typedef uint16_t	RPGHandle;
typedef const char* RPGName;
typedef uint8_t RPGIdx;

extern RPGName RES_DEPTH;
extern RPGName RES_SHADOW_MAP;
extern RPGName RES_DEPTH_PYRAMID;
extern RPGName RES_DEPTH_PYRAMID_SHADOW;
extern RPGName PASS_DEPTH_PYRAMID;


class RPGResource
{
public:
	RPGResource(RPGName n) : name(n) {}
	RPGResource() : name(0) {}
	RPGName name;
};

class RPGTexture : public RPGResource
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

	RPGTexture(RPGName name, const Desc& d) : RPGResource(name), desc(d) {}
	RPGTexture() {}
};


class RPGView : public RPGResource
{
public:
	struct Desc
	{
		Desc(int ll) : level(ll) {}
		Desc() : level(0) {}
		int level;
	} desc;

	RPGView(RPGTexture& tex, RPGHandle h, const Desc& d) : RPGResource(tex.name), texHandle(h), desc(d) {}
	RPGView() {}
	RPGHandle texHandle = 0;
};

enum RPGPassFlags
{
	RGPASS_FLAG_GRAPHICS = 0,
	RGPASS_FLAG_COMPUTE = 1,
};

constexpr int MAX_PASSES = 32;
typedef std::array<RPGIdx, MAX_PASSES> OrderList;

class RPGPass
{
	friend class RenderPassGraph;
public:
	RPGPass(RPGName n, RPGPassFlags f) : name(n), flags(f) {}
	RPGPass() {}

	void write(RPGHandle& view)
	{
		assert(view >= 0);
		writes[numWrites++] = view;
	}
	void read(RPGHandle& view)
	{
		assert(view >= 0);
		reads[numReads++] = view;
		//inDegrees = numReads;
	}

private:
	int8_t numWrites = 0;
	int8_t numReads = 0;
	RPGPassFlags flags = {};

	int8_t inDegrees = 0;
	RPGIdx depthLevel = 0;

	std::array<RPGHandle, 8> writes = {};
	std::array<RPGHandle, 8> reads = {};

	std::function<void(RenderPassGraph&)> func; // todo: remove heap!!!	//use stack allocator
	RPGName name;
};


class RenderPassGraph
{
public:
	RenderPassGraph(REngine* e);

	RPGHandle create_texture(RPGName name, const RPGTexture::Desc& desc); 	//ERDGTextureFlags Flags
	RPGHandle create_texture(RPGName name, VkImage image);
	
	RPGHandle create_view(RPGHandle tex, const RPGView::Desc& desc);
	RPGHandle create_view(RPGHandle tex, VkImageView view);

	VkImage get_image(RPGHandle handle) const;
	VkImageView get_image_view(RPGHandle handle) const;

	template <typename F>
	RPGHandle add_pass(RPGName name, RPGPassFlags flags, F&& func);

	void pass_write(RPGHandle& pass, RPGHandle& view);
	void pass_read(RPGHandle& pass, RPGHandle& view);

	void execute();

	REngine* engine;

private:
	RPGIdx sort_dependences(OrderList& order);
	std::tuple<int, int> optimize(OrderList& order);
	int  calc_cost(OrderList& order);

	std::tuple<RPGIdx, bool, bool> find_next_resource_pass(RPGHandle res, RPGIdx curPassIdx, const OrderList& order);

	void check_physical_texture(RPGHandle tex, RPGPass& pass);
	void check_physical_view(RPGHandle view);

	bool validate_graph(RenderPassGraph& g);

	bool test();
	void export_svg(const char* fileName, OrderList& order);

	RPGIdx numPasses = 0;
	std::array<RPGPass, MAX_PASSES>	   passes;
};