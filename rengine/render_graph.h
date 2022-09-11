#pragma once
#include <array>

typedef uint16_t	RPGHandle;
typedef const char* RPGName;
typedef uint8_t RPGIdx;
constexpr uint8_t RPGIdxNone = -1;

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
constexpr int MAX_RESOURCES = 64;

template <typename T, int SIZE>
struct FixedArray : public std::array<T, SIZE>
{
	T num = 0;
	void push_back(T val)
	{
		if (num < SIZE)
			(*this)[num++] = val;
	}
	bool is_full() const { return num >= SIZE; }
};

struct OrderList : public std::array<RPGIdx, MAX_PASSES>
{
	RPGIdx num = 0;
	bool isOverflow = false;

	void push_back(RPGIdx val)
	{
		isOverflow = num >= MAX_PASSES;
		if (!isOverflow)
			(*this)[num++] = val;
	}
};

class RenderPassGraph;

struct BaseLambda
{
	virtual void execute(RenderPassGraph&) {}
};
template <typename F>
struct Lambda : public BaseLambda
{
	Lambda(const F&& f) : func(std::move(f)) {}
	F func;
	void execute(RenderPassGraph& g) override
	{
		func(g);
	}
};
//struct GroupLambda : public BaseLambda
//{
//	int8_t numFuncs = 0;
//	std::array<BaseLambda*, 16> funcs = {};
//	void execute(RenderPassGraph& g) override
//	{
//		for (int8_t i = 0; i < numFuncs; ++i) {
//			funcs[i]->execute(g);
//		}
//	}
//};

class RPGPass
{
	friend class RenderPassGraph;
public:
	RPGPass(RPGName n, RPGPassFlags f) : name(n), flags(f) {}
	RPGPass() {}

	void write(RPGIdx& res)
	{
		assert(res >= 0);
		assert(std::find(&writes[0], &writes[writes.num], res) == &writes[writes.num]);
		writes.push_back(res);
	}
	void read(RPGIdx& res)
	{
		assert(res >= 0);
		assert(std::find(&reads[0], &reads[reads.num], res) == &reads[reads.num]);
		reads.push_back(res);
	}

private:
	//int8_t numWrites = 0;
	//int8_t numReads = 0;
	RPGPassFlags flags = {};

	int8_t inDegrees = 0;
	RPGIdx depthLevel = 0;
	bool isMerged = false;

	FixedArray<RPGIdx, 8> writes = {};
	FixedArray<RPGIdx, 8> reads = {};

	BaseLambda* func = nullptr;
	RPGName name;
};


class RenderPassGraph
{
public:
	RenderPassGraph(REngine* e);
	~RenderPassGraph();

	RPGHandle create_texture(RPGName name, const RPGTexture::Desc& desc); 	//ERDGTextureFlags Flags
	RPGHandle create_texture(RPGName name, VkImage image);
	
	RPGHandle create_view(RPGHandle tex, const RPGView::Desc& desc);
	RPGHandle create_view(RPGHandle tex, VkImageView view);

	VkImage get_image(RPGHandle handle) const;
	VkImageView get_image_view(RPGHandle handle) const;

	template <typename F>
	RPGHandle add_pass(RPGName name, RPGPassFlags flags, F&& lambda)
	{
		static_assert(sizeof(F) < 512, "DONT CAPTURE TOO MUCH IN THE LAMBDA");
		auto [page, pos, mem] = allocate_memory(sizeof(Lambda<F>));
		if (firstMemPage == -1) {
			firstMemPage = page;
			firstMemPagePos = pos;
		}
		BaseLambda* f = new (mem) Lambda<F>(std::move(lambda));
		RPGPass pass(name, flags);
		pass.func = f;
		passes[numPasses++] = pass;
		return numPasses - 1;
	}

	RPGIdx pass_write(RPGHandle& pass, RPGHandle& view);
	bool pass_read(RPGHandle& pass, RPGHandle& view);

	void execute();

	REngine* engine;

private:
	template <bool Random=false>
	RPGIdx sort_dependences(OrderList& order);
	void sort_dependences_backward(OrderList& order);

	std::tuple<int, int> optimize(OrderList& order);
	void alias_resources(const OrderList& order);
	int  calc_cost_wr(const OrderList& order);
	int  calc_cost_alias(const OrderList& order);

	void merge_passes();
	void cull_passes(const OrderList& order);

	struct ResSpan {
		RPGIdx s = RPGIdxNone, e = 0;
	};
	void calc_res_spans(const OrderList& iOrder, std::array<ResSpan, MAX_RESOURCES>& spans);

	std::tuple<RPGIdx, bool, bool> find_next_resource_pass(RPGIdx res, RPGIdx curPassIdx, const OrderList& order);

	void check_physical_texture(RPGHandle tex, RPGPass& pass);
	void check_physical_view(RPGHandle view);

	bool validate();

	bool test();
	void export_svg(const char* fileName, OrderList& order);

	std::tuple<int, size_t, char*> allocate_memory(size_t);
	bool rollback_memory(int page, size_t pagePos);

	void hud();

	int firstMemPage = -1;
	size_t firstMemPagePos = 0;

	RPGIdx numPasses = 0;
	std::array<RPGPass, MAX_PASSES>	passes;
	
	RPGIdx numResources = 0;
	std::array<RPGHandle, MAX_RESOURCES> resources;
};


VkImage get_pool_image(RPGName name, RPGHandle* texHandle, RPGTexture* rgTex);
VkImageView get_pool_image_view(RPGHandle tex, int level);