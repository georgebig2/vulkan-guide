#include "Tracy.hpp"
#include "TracyVulkan.hpp"

#include "rengine.h"
#include "vk_profiler.h"
#include "logger.h"
#include "cvars.h"
#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_descriptors.h>
#include <vk_scene.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "frame_data.h"
#include "render_graph.h"

AutoCVar_Int CVAR_FreezeCull("culling.freeze", "Locks culling", 0, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowBias("gpu.shadowBias", "Distance cull", 5.25f);
AutoCVar_Float CVAR_SlopeBias("gpu.shadowBiasSlope", "Distance cull", 4.75f);
AutoCVar_Int CVAR_FreezeShadows("gpu.freezeShadows", "Stop the rendering of shadows", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_Shadowcast("gpu.shadowcast", "Use shadowcasting", 1, CVarFlags::EditCheckbox);

RPGName RES_DEPTH				= "Depth";
RPGName RES_SHADOW_MAP			= "ShadowMap";
RPGName RES_DEPTH_PYRAMID		= "DepthPyramid";
RPGName RES_DEPTH_PYRAMID_SHADOW = "DepthShadowPyramid";

RPGName PASS_PROLOGUE			= "Prologue";
RPGName PASS_EPILOGUE			= "Epilogue";
RPGName PASS_DEPTH_PYRAMID		= "#DepthPyramid";
RPGName PASS_FORWARD			    = "Forward";
RPGName PASS_SHADOW				= "Shadow";


struct alignas(16) DepthReduceData
{
	glm::vec2 imageSize;
};
inline uint32_t getGroupCount(uint32_t threadCount, uint32_t localSize)
{
	return (threadCount + localSize - 1) / localSize;
}
struct PoolViewKey {
	int resource;
	int level;
	bool operator==(const PoolViewKey& other) const
	{
		return (resource == other.resource
			&& level == other.level);
	}
};
struct PoolViewKeyHash {
	std::size_t operator()(const PoolViewKey& k) const
	{
		return ((std::hash<int>()(k.resource)
			^ (std::hash<int>()(k.level) << 1)) >> 1);
		//^ (hash<int>()(k.third) << 1);
	}
};

std::unordered_map<RPGName, int> registry_textures;
std::unordered_map<PoolViewKey, int, PoolViewKeyHash> registry_views;

struct PoolTexture 
{
	RPGTexture rgTex;
	VkImage image = 0;
};
struct PoolView
{
	RPGView rgView;
	uint64_t writePasses = 0;
	uint64_t readPasses = 0;
	VkImageView imageView = 0;
};

std::vector<PoolTexture> textures;
std::vector<PoolView> views;

RPGHandle get_texture_handle(RPGName name)
{
	int h = -1;
	auto it = registry_textures.find(name);
	if (it == registry_textures.end()) {
		h = (int)textures.size();
		registry_textures.insert({ name, h });
		textures.resize(h + 1);
	}
	else {
		h = it->second;
	}
	return h;
}

RPGHandle get_view_handle(PoolViewKey key)
{
	int h = -1;
	auto it = registry_views.find(key);
	if (it == registry_views.end()) {
		h = (int)views.size();
		registry_views.insert({ key, h });
		views.resize(h + 1);
	}
	else {
		h = it->second;
	}
	return h;
}

VkImage get_pool_image(RPGName name, RPGHandle* texHandle, RPGTexture* rgTex)
{
	auto h = get_texture_handle(name);
	if (rgTex)
		*rgTex = textures[h].rgTex;
	if (texHandle)
		*texHandle = h;
	return textures[h].image;
}

VkImageView get_pool_image_view(RPGHandle tex, int level)
{
	auto h = get_view_handle({ tex, level });
	return views[h].imageView;
}

RenderPassGraph::RenderPassGraph(REngine* e) 
	: engine(e)
{
	//auto pass = add_pass(PASS_PROLOGUE, RGPASS_FLAG_GRAPHICS, [=](RenderPassGraph& g) {});
}


RPGHandle RenderPassGraph::create_texture(RPGName name, const RPGTexture::Desc& desc)
{
	auto h = get_texture_handle(name);
	textures[h].rgTex = RPGTexture(name, desc);
	return h;
}

RPGHandle RenderPassGraph::create_texture(RPGName name, VkImage image)
{
	auto h = get_texture_handle(name);
	RPGTexture::Desc desc;
	textures[h].rgTex = RPGTexture(name, desc);
	textures[h].image = image;
	return h;
}

RPGHandle RenderPassGraph::create_view(RPGHandle tex, const RPGView::Desc& desc)
{
	auto h = get_view_handle({ tex, desc.level });
	views[h].rgView = RPGView(textures[tex].rgTex, tex, desc);
	return h;
}

RPGHandle RenderPassGraph::create_view(RPGHandle tex, VkImageView view)
{
	auto h = get_view_handle({ tex, -1 });
	RPGView::Desc desc;
	views[h].rgView = RPGView(textures[tex].rgTex, tex, desc);
	views[h].imageView = view;
	return h;
}

VkImage RenderPassGraph::get_image(RPGHandle handle) const
{
	return textures[handle].image;
}

VkImageView RenderPassGraph::get_image_view(RPGHandle handle) const
{
	return views[handle].imageView;
}


template <typename F>
RPGHandle RenderPassGraph::add_pass(RPGName name, RPGPassFlags flags, F&& func)
{
	static_assert(sizeof(F) < 300, "DONT CAPTURE TOO MUCH IN THE LAMBDA");
	RPGPass pass(name, flags);
	pass.func = func;				// todo: remove heap!!!
	passes[numPasses++] = pass;			
	return numPasses - 1;
}

void RenderPassGraph::pass_write(RPGHandle& pass, RPGHandle& view)
{
	passes[pass].write(view);
	auto& v = views[view];
	//auto t = v.texHandle;
	//auto& tex = textures[t];
	assert(pass >= 0 && pass < 64);
	v.writePasses |= uint64_t(1) << pass;
}
void RenderPassGraph::pass_read(RPGHandle& pass, RPGHandle& view)
{
	passes[pass].read(view);
	auto& v = views[view];
	//auto t = v.texHandle;
	//auto& tex = textures[t];
	assert(pass >= 0 && pass < 64);
	v.readPasses |= uint64_t(1) << pass;
}

std::tuple<RPGIdx,bool,bool> RenderPassGraph::find_next_resource_pass(RPGHandle res, RPGIdx curPassIdx, const OrderList& order)
{
	bool reads = false;
	bool writes = false;

	for (RPGIdx i = curPassIdx + 1; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& nextPass = passes[pIdx];
	
		for (int8_t r = 0; r < nextPass.numReads; ++r) {
			auto rV = nextPass.reads[r];
			if (res == rV) {
				reads = true;
				break;
			}
		}
		for (int8_t w = 0; w < nextPass.numWrites; ++w) {
			auto wV = nextPass.writes[w];
			if (res == wV) {
				writes = true;
				break;
			}
		}

		if (reads || writes)
			return std::make_tuple(i, reads, writes);
	}

	return std::make_tuple(numPasses, false, false);
}



void RenderPassGraph::execute()
{
	auto& cmd = engine->get_current_frame()._mainCommandBuffer;
	vkutil::VulkanScopeTimer timer(cmd, engine->_profiler, "RG execute");

	test(); // only debug!

	//add_pass(PASS_EPILOGUE, RGPASS_FLAG_GRAPHICS, [=](RenderPassGraph& g){});

	// todo: cull passes

	OrderList order;
	auto newNumPasses = sort_dependences(order);
	assert(newNumPasses == numPasses);
	optimize(order);
	validate_graph(*this);		// only debug!

	for (int i = 0; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& pass = passes[pIdx];

		// create write resources
		for (int8_t w = 0; w < pass.numWrites; ++w)
		{
			auto wV = pass.writes[w];
			auto& wView = views[wV];
			auto wT = wView.rgView.texHandle;
			check_physical_texture(wT, pass);
			check_physical_view(wV);
		}

		pass.func(*this);

		// resource transition between passes
		// vk render passes for graphics passes
		// r->w, r->r, w->w
		// grouping barriers after pass
		// split barriers?
		// parallel passes?
		// parallel queues (compute/graphics)
		// resourse aliasing
		// barriers between frames?
		for (int8_t w = 0; w < pass.numWrites; ++w)
		{
			auto wV = pass.writes[w];

			auto [ii, nextReads, nextWrites] = find_next_resource_pass(wV, i, order);
			if (nextReads || nextWrites)
			{
				auto nextPidx = order[ii];
				auto& nextPass = passes[nextPidx];
				assert(!nextWrites);					// w -> w?

				// w -> r
				if (nextReads)
				{
					auto& wView = views[wV];
					auto wT = wView.rgView.texHandle;
					auto& wTex = textures[wT];
					bool compute2compute = (pass.flags & RGPASS_FLAG_COMPUTE) && (nextPass.flags & RGPASS_FLAG_COMPUTE);
					bool graphics2compute = !(pass.flags & RGPASS_FLAG_COMPUTE) && (nextPass.flags & RGPASS_FLAG_COMPUTE);
					if (compute2compute)
					{
						VkImageMemoryBarrier wrb = vkinit::image_barrier(wTex.image,
							VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
							VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
							VK_IMAGE_ASPECT_COLOR_BIT);
						vkCmdPipelineBarrier(cmd,
							VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &wrb);
					}
					else if (graphics2compute)		// do transition in EndRenderPass?
					{
						VkImageMemoryBarrier wrb = vkinit::image_barrier(wTex.image,
							VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
							VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							VK_IMAGE_ASPECT_DEPTH_BIT);
						vkCmdPipelineBarrier(cmd,
							VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &wrb);
					}
				}
			}
				
		}

		// r->w, r->r ?
		for (int8_t r = 0; r < pass.numReads; ++r)
		{
		}
	}
}

bool has_duplicates(const OrderList& order, int len)
{
	OrderList copy = order;
	std::sort(&copy[0], &copy[len]);
	bool hasDuplicates = std::adjacent_find(&copy[0], &copy[len]) != &copy[len];
	return hasDuplicates;
}

// use topological sorting for ordering passes writes/reads
RPGIdx RenderPassGraph::sort_dependences(OrderList& out)
{
	// search enter passes
	OrderList zerosQ;
	RPGIdx backIdx = 0;
	for (RPGIdx pIdx = 0; pIdx < numPasses; ++pIdx)
	{
		auto& pass = passes[pIdx];
		pass.depthLevel = 0;
		pass.inDegrees = pass.numReads;
		bool ignore = (!pass.numReads && !pass.numWrites);
		if (!ignore && !pass.inDegrees) {
			zerosQ[backIdx++] = pIdx;		// we can change order
		}
		out[pIdx] = pIdx;
	}
	assert(!has_duplicates(out, numPasses));

	//assert(backIdx <= 1);	// one enter for now
	//assert(backIdx > 0);
	if (!backIdx)
		return 0;

	OrderList order;
	RPGIdx frontIdx = 0;
	RPGIdx orderIdx = 0;
	for (RPGIdx pIdx = 0; pIdx < numPasses; ++pIdx)
	{
		if (frontIdx >= backIdx)
			break;

		auto zero = zerosQ[frontIdx++];
		order[orderIdx++] = zero;
		assert(!has_duplicates(order, orderIdx));

		auto& pass = passes[zero];
		for (int8_t w = 0; w < pass.numWrites; ++w)
		{
			auto viewHandle = pass.writes[w];
			auto& v = views[viewHandle];

			for (RPGIdx pIdx = 0; pIdx < 64; ++pIdx)
			{
				if (v.readPasses & (uint64_t(1) << pIdx))	// we can change order of neighbours
				{
					auto& neighbour = passes[pIdx];
					assert(neighbour.inDegrees > 0);
					if (!--neighbour.inDegrees) {
						zerosQ[backIdx++] = pIdx;
						neighbour.depthLevel = pass.depthLevel + 1;
					}
				}
			}
		}
	}

	OrderList sorted = { 0 };
	RPGIdx pIdx = 0;
	for (RPGIdx i = 0; pIdx < numPasses && i < orderIdx; ++pIdx)
	{
		auto& pass = passes[pIdx];
		bool ignore = (!pass.numReads && !pass.numWrites);
		if (!ignore) {
			auto nIdx = order[i++];
			out[pIdx] = nIdx;
			sorted[nIdx] = 1;
		}
		else {
			sorted[pIdx] = 1;
		}
	}
	for (RPGIdx i = 0; i < numPasses; ++i)
	{
		auto& pass = passes[i];
		if (!sorted[i]) {
			out[pIdx++] = i;
		}
	}

	assert(!has_duplicates(out, numPasses));
	return numPasses;
}

// try to "shrink" split barriers (more passes between w/r)
std::tuple<int, int> RenderPassGraph::optimize(OrderList& out)
{
	// generate all valid shuffles?

	auto random_shuffle = [&]()
	{
		for (RPGIdx i = numPasses - 1; i > 0; --i)
		{
			auto ii = std::rand() % (i + 1);
			auto o1 = out[i];
			auto o2 = out[ii];
			auto& p1 = passes[o1];
			auto& p2 = passes[o2];
			bool ignore1 = (!p1.numReads && !p1.numWrites);
			bool ignore2 = (!p2.numReads && !p2.numWrites);
			if (p1.depthLevel == p2.depthLevel && !ignore1 && !ignore2)
				std::swap(out[i], out[ii]);
		}
	};
	
	auto baseCost = calc_cost(out);

	OrderList maxOrder;
	int maxCost = 0;
	for (int i = 0; i < 100; ++i)
	{
		int cost = calc_cost(out);
		if (cost > maxCost)
		{
			maxCost = cost;
			maxOrder = out;
		}
		random_shuffle();
	}
	if (maxCost > 0)
		out = maxOrder;

	assert(!has_duplicates(out, numPasses));
	return std::make_tuple(baseCost, maxCost);
}

int RenderPassGraph::calc_cost(OrderList& order)
{
	int cost = 0;
	for (int i = 0; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& pass = passes[pIdx];
		bool ignore = (!pass.numReads && !pass.numWrites);
		if (ignore)
			continue;

		for (int8_t w = 0; w < pass.numWrites; ++w)
		{
			auto wV = pass.writes[w];

			auto [ii, nextReads, nextWrites] = find_next_resource_pass(wV, i, order);
			assert(!nextWrites);
			if (nextReads)
			{
				cost += ii - i;
			}
		}
	}
	return cost;
}



#include "svg.h"
using namespace svg;

void RenderPassGraph::export_svg(const char* fileName, OrderList& order)
{
	const int maxThreads = 1;
	const auto width = 2170;
	const auto height = 1100;
	const auto xo = width / 9;
	const auto yo = height / 10;
	const auto wo = 85 * width / 100;
	const auto ho = 8 * height / 10;
	const auto dpx = width / 128;
	const auto dry = height / 128;
	const auto wax = width / 512;
	const auto hax = height / 128;
	const auto hr = 2 * ho / 3;
	const auto yr = yo + ho - hr;
	auto hp = ho / 20;
	Color arrowColorW(255, 0, 0, 0.7f);
	Color arrowColorR(255, 255, 0, 0.7f);

	Dimensions dimensions(width, height);
	Document doc(fileName, Layout(dimensions, Layout::TopLeft));
	doc << svg::Rectangle(Point(0, 0), width, height, Color::Black);

	// collect resources
	RPGHandle minResIdx = -1, maxResIdx = 0;
	for (int i = 0; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& pass = passes[pIdx];
		for (int8_t r = 0; r < pass.numReads; ++r) {
			auto rV = pass.reads[r];
			minResIdx = std::min(minResIdx, rV);
			maxResIdx = std::max(maxResIdx, rV);
		}
		for (int8_t w = 0; w < pass.numWrites; ++w) {
			auto wV = pass.writes[w];
			minResIdx = std::min(minResIdx, wV);
			maxResIdx = std::max(maxResIdx, wV);
		}
	}

	doc << svg::Rectangle(Point(xo, yo + hp/2), wo, height/512, Color::White);
	doc << (svg::Polygon(Color::White, Stroke(1, arrowColorR)) << Point(xo + wo, yo + hp/2) << Point(xo + wo-wax, yo + hp / 2 - hax) << Point(xo + wo-wax, yo + hp / 2+hax));

	for (int i = 0; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& pass = passes[pIdx];

		// draw pass
		Color passColor = Color::Green;
		if (!pass.numWrites)
			passColor = Color::Grey;//.alpha = 0.5f;
		if (pass.depthLevel % 2) {
			passColor.red /= 2; passColor.green /= 2; passColor.blue /= 2;
		}
		auto wp = (wo - dpx * (numPasses - 1)) / numPasses;
		auto xp = xo + (wp + dpx) * i;
		auto yp = yo;
		doc << svg::Rectangle(Point(xp, yp), wp, hp, passColor);
		char label[128]; std::sprintf(label, "%s %d", pass.name, pass.depthLevel);
		doc << Text(Point(xp+wp/3, yp + hp / 2), label, Color::White, Font(14, "Verdana"));

		// draw resources
		for (int8_t w = 0; w < pass.numWrites; ++w)
		{
			auto wV = pass.writes[w];
			auto hw = (hr - dry * (maxResIdx - minResIdx)) / (maxResIdx - minResIdx + 1);
			auto yw = yr + hr * (wV - minResIdx) / (maxResIdx - minResIdx + 1);

			auto xw = xo;
			auto ww = wo;
			//doc << svg::Rectangle(Point(xw, yw), ww, hw, Color::Grey);
			auto ii = i;
			while (1)
			{
				auto [iii, nextReads, nextWrites] = find_next_resource_pass(wV, ii, order);
				assert(!nextWrites);
				if (!nextReads)
					break;
				xw = xp;
				ww = (wp + dpx) * (iii - i + 1);
				ii = iii;
			}

			doc << svg::Rectangle(Point(xw, yw), ww, hw, ii != i ? Color::Blue : Color::Grey);
			char label[128]; std::sprintf(label, "%s-%d", views[wV].rgView.name, wV);
			doc << Text(Point(xo/2, yw + hw / 2), label, Color::White, Font(12, "Verdana"));
		}
	}

	// draw arrows
	for (int i = 0; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& pass = passes[pIdx];

		auto wp = (wo - dpx * (numPasses - 1)) / numPasses;
		auto xp = xo + (wp + dpx) * i;
		auto yp = yo;
		for (int8_t w = 0; w < pass.numWrites; ++w) {
			auto wV = pass.writes[w];
			auto yw = yp + hp;
			auto hw = yr + hr * (wV - minResIdx) / (maxResIdx - minResIdx + 1) - yw;
			auto xw = xp + wp - 1*wax -  wp*w / pass.numWrites / 2;
			doc << svg::Rectangle(Point(xw, yw), wax, hw-hax, arrowColorW);
			doc << (svg::Polygon(arrowColorW, Stroke(1, arrowColorW)) << Point(xw+wax/2, yw+hw) << Point(xw-wax, yw+hw-hax) << Point(xw+2*wax, yw+hw-hax));
		}
		for (int8_t r = 0; r < pass.numReads; ++r) {
			auto rV = pass.reads[r];
			auto yw = yp + hp;
			auto hw = yr + hr * (rV - minResIdx) / (maxResIdx - minResIdx + 1) - yw;
			auto xw = xp + 1 * wax + wp * r / pass.numReads / 2;
			doc << svg::Rectangle(Point(xw, yw + hax), wax, hw - hax, arrowColorR);
			doc << (svg::Polygon(arrowColorR, Stroke(1, arrowColorR)) << Point(xw + wax / 2, yw) << Point(xw - wax, yw + hax) << Point(xw + 2 * wax, yw + hax));
		}
	}
	//doc << Circle(Point(80, 80), 20, Fill(Color(100, 200, 120)), Stroke(1, Color(200, 250, 150)));
	doc.save();
}

// generate random render graph and test it
bool RenderPassGraph::test()
{
	const RPGHandle numViews = 25;
	const char* passesNames[] = { "a","b","c","d","e","f", "g", "j", "i", "k", "l", "m", "n", "o" };// , "p", "r", "q", "s", "t", "y", "v", "w", "z"
	const uint8_t maxPassReads = 3;
	const uint8_t maxPassWrites = 2;
	int numTries = 100;

	std::srand(time(0));

	RenderPassGraph g(engine);
	auto desc = RPGTexture::Desc();
	auto tex = g.create_texture("TestTexture", desc);

	// create passes
	RPGIdx numPasses = sizeof(passesNames) / sizeof(const char*);
	for (RPGIdx i = 0; i < numPasses; ++i)
	{
		auto p = g.add_pass(passesNames[i], RGPASS_FLAG_GRAPHICS, [=](RenderPassGraph& g) {});
	}

	// todo: multiple writers to one resource?
	// each view gets one pass writer(producer)
	for (RPGHandle i = 0; i < numViews; ++i)
	{
		auto desc = RPGView::Desc(i);
		auto v = g.create_view(tex, desc);
		views[v].readPasses = 0;
		views[v].writePasses = 0;
		for (int t = 0; t < numTries; ++t)
		{
			RPGHandle pw = std::rand() % numPasses + 0;
			auto& pass = g.passes[pw];
			//assert(std::find(&pass.writes[0], &pass.writes[pass.numWrites], v) == &pass.writes[pass.numWrites]);
			if (pass.numWrites < maxPassWrites 
				//&& std::find(&pass.writes[0], &pass.writes[pass.numWrites], v) == &pass.writes[pass.numWrites]
			//	&& std::find(&pass.reads[0], &pass.reads[pass.numReads], v) == &pass.reads[pass.numReads]
				) 
			{
				//!(views[v].writePasses & (uint64_t(1) << pw))) {
				g.pass_write(pw, v);
				break;
			}
		}
	}

	// cpu w/r
	// each view(resource) is read by several passes (except last one)
	for (RPGHandle i = 0; i < numViews-1; ++i)
	{
		auto desc = RPGView::Desc(i);
		auto v = g.create_view(tex, desc);

		uint8_t numR = std::rand() % maxPassReads;
		for (uint8_t r = 0; r <= numR; ++r)
		{
			for (int t = 0; t < numTries; ++t) 
			{
				RPGHandle pr = std::rand() % (numPasses - 1) + 1;  // first pass is graph enter
				auto& pass = g.passes[pr];
				if (pass.numReads < maxPassReads 
					&& std::find(&pass.reads[0], &pass.reads[pass.numReads], v) == &pass.reads[pass.numReads]
					&& std::find(&pass.writes[0], &pass.writes[pass.numWrites], v) == &pass.writes[pass.numWrites]
					)
				{
					auto restorePass = pass;
					auto restoreView = views[v];

					g.pass_read(pr, v);
					auto valid = validate_graph(g);

					// rewind (maybe dangerous!)
					if (!valid) {
						pass = restorePass;
						views[v] = restoreView;
						if (t == numTries - 1) {
							//assert(0);
						}
						continue;
					}
					break;
				}
			}
		}
	}

	static bool firstTime = true;

	// todo: cull passes
	OrderList order;
	auto newNumPasses = g.sort_dependences(order);
	if (firstTime) {
		g.export_svg("dep.svg", order);
	}

	auto [baseCost, optCost] = g.optimize(order);
	if (firstTime) {
		g.export_svg("opt.svg", order);
	}

	//LOG_INFO("rdp");
	//for (RPGIdx i = 0; i < numPasses; ++i)
	//{
	//	auto pIdx = order[i];
	//	auto& pass = g.passes[pIdx];
	//	LOG_LINE(pass.name);
	//}
	//LOG_LINE(" {}->{} ", baseCost, optCost);

	firstTime = false;

	auto res = validate_graph(g);
	assert(res);
	return res;
}

bool RenderPassGraph::validate_graph(RenderPassGraph& g)
{
	OrderList order;
	auto res = g.sort_dependences(order) > 0;
	if (res)
	{
		for (int i = 0; i < g.numPasses; ++i)
		{
			auto pIdx = order[i];
			auto& pass = g.passes[pIdx];
			for (int8_t r = 0; r < pass.numReads; ++r)
			{
				auto rV = pass.reads[r];
				auto [ii, nextReads, nextWrites] = g.find_next_resource_pass(rV, i, order);
				// read before write?
				if (nextWrites)
					return false;

				// read and write the same resource in one pass?
				for (int8_t w = 0; w < pass.numWrites; ++w)
				{
					auto wV = pass.writes[w];
					if (wV == rV)
						return false;
				}
			}
		}
	}
	return res;
}



void RenderPassGraph::check_physical_texture(RPGHandle h, RPGPass& pass)
{
	auto& tex = textures[h];
	if (tex.image)
		return;

	// cache textures (per frame?)
	VkExtent3D extent = { (uint32_t)tex.rgTex.desc.w, (uint32_t)tex.rgTex.desc.h, (uint32_t)tex.rgTex.desc.d };
	VkImageCreateInfo info = vkinit::image_create_info(tex.rgTex.desc.format,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,		//???
		extent);
	info.mipLevels = tex.rgTex.desc.levels;

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	AllocatedImage newImage;
	VK_CHECK(vmaCreateImage(engine->_allocator, &info, &allocInfo, &newImage._image, &newImage._allocation, nullptr));

	bool compute = (pass.flags & RGPASS_FLAG_COMPUTE);
	assert(compute);
	if (compute) 
 	{
		VkImageMemoryBarrier initB = vkinit::image_barrier(newImage._image,
			0, VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_ASPECT_COLOR_BIT);
		vkCmdPipelineBarrier(engine->get_current_frame()._mainCommandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &initB);
	}

	auto allocator = engine->_allocator;
	auto alloc = newImage._allocation;
	engine->_surfaceDeletionQueue.push_function([=]() {
		textures[h].image = 0;
		vmaDestroyImage(allocator, newImage._image, alloc);
		});

	assert(newImage._image);
	tex.image = newImage._image;
}


void RenderPassGraph::check_physical_view(RPGHandle h)
{
	auto& view = views[h];
	auto& tex = textures[view.rgView.texHandle];
	assert(tex.image);

	if (!view.imageView && tex.rgTex.desc.format != VK_FORMAT_UNDEFINED)
	{
		VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(tex.rgTex.desc.format, tex.image,
			VK_IMAGE_ASPECT_COLOR_BIT); //??
		viewInfo.subresourceRange.levelCount = view.rgView.desc.level == -1 ? tex.rgTex.desc.levels : 1;
		viewInfo.subresourceRange.baseMipLevel = view.rgView.desc.level == -1 ? 0 : view.rgView.desc.level;
		VkImageView newView;
		VK_CHECK(vkCreateImageView(engine->_device, &viewInfo, nullptr, &newView));

		auto device = engine->_device;
		engine->_surfaceDeletionQueue.push_function([=]() {
			views[h].imageView = 0;
			vkDestroyImageView(device, newView, nullptr);
			});

		assert(newView);
		view.imageView = newView;
	}
}



uint32_t previousPow2(uint32_t v)
{
	uint32_t r = 1;
	while (r * 2 < v)
		r *= 2;
	return r;
}
int getImageMipLevels(uint32_t width, uint32_t height)
{
	int result = 1;
	while (width > 1 || height > 1) {
		result++;
		width /= 2;
		height /= 2;
	}
	return result;
}

void reduce_depth(RenderPassGraph& graph, VkCommandBuffer cmd, RPGHandle inDepthTex, VkExtent2D ext, RPGName outRes)
{
	// Note: previousPow2 makes sure all reductions are at most by 2x2 which makes sure they are conservative
	auto depthPyramidWidth = previousPow2(ext.width);
	auto depthPyramidHeight = previousPow2(ext.height);
	auto depthPyramidLevels = getImageMipLevels(depthPyramidWidth, depthPyramidHeight);

	auto desc = RPGTexture::Desc(depthPyramidWidth, depthPyramidHeight, VK_FORMAT_R32_SFLOAT, depthPyramidLevels);
	auto pyrTex = graph.create_texture(outRes, desc);

	for (int i = 0; i < depthPyramidLevels; ++i)
	{
		auto desc = RPGView::Desc(i);
		auto destView = graph.create_view(pyrTex, desc);	//rt/uav + bind index

		RPGHandle sourceView;
		if (i == 0) {
			auto desc = RPGView::Desc(-1);
			sourceView = graph.create_view(inDepthTex, desc);	//srv + bind index
		}
		else {
			auto desc = RPGView::Desc(i - 1);
			sourceView = graph.create_view(pyrTex, desc);
		}

		auto pass = graph.add_pass(PASS_DEPTH_PYRAMID, RGPASS_FLAG_COMPUTE,
			[=](RenderPassGraph& g)
			{
				if (i == 0) {
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.engine->_depthReducePipeline);
				}

				VkDescriptorImageInfo destTarget = {};
				destTarget.imageView = g.get_image_view(destView);
				assert(destTarget.imageView);
				destTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;	//compute

				VkDescriptorImageInfo sourceTarget = {};
				sourceTarget.sampler = g.engine->_depthSampler;
				sourceTarget.imageView = g.get_image_view(sourceView);
				assert(sourceTarget.imageView);
				sourceTarget.imageLayout = i == 0 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL; //compute?

				VkDescriptorSet depthSet;
				vkutil::DescriptorBuilder::begin(g.engine->_descriptorLayoutCache, g.engine->get_current_frame().dynamicDescriptorAllocator)
					.bind_image(0, &destTarget, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
					.bind_image(1, &sourceTarget, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
					.build(depthSet);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.engine->_depthReduceLayout, 0, 1, &depthSet, 0, nullptr);

				uint32_t levelWidth = depthPyramidWidth >> i;
				uint32_t levelHeight = depthPyramidHeight >> i;
				if (levelHeight < 1) levelHeight = 1;
				if (levelWidth < 1) levelWidth = 1;
				DepthReduceData reduceData = { glm::vec2(levelWidth, levelHeight) };
				vkCmdPushConstants(cmd, g.engine->_depthReduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(reduceData), &reduceData);

				vkCmdDispatch(cmd, getGroupCount(reduceData.imageSize.x, 32), getGroupCount(reduceData.imageSize.y, 32), 1);

				//auto destImage = g.get_image(pyrTex);
				//VkImageMemoryBarrier reduceBarrier = vkinit::image_barrier(destImage,
				//	VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				//	VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
				//	VK_IMAGE_ASPECT_COLOR_BIT);
				//vkCmdPipelineBarrier(cmd,
				//	VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				//	VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &reduceBarrier);

			});
		graph.pass_write(pass, destView);
		graph.pass_read(pass, sourceView);
		
		// default view for srvs with all mips
		if (i == depthPyramidLevels - 1) 
		{
			auto desc = RPGView::Desc(-1);
			auto defaultView = graph.create_view(pyrTex, desc);
			graph.pass_write(pass, defaultView);
		}
	}

}




void REngine::init_forward_renderpass()
{
	VkSamplerCreateInfo shadsamplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
	shadsamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	shadsamplerInfo.compareEnable = true;
	shadsamplerInfo.compareOp = VK_COMPARE_OP_LESS;
	vkCreateSampler(_device, &shadsamplerInfo, nullptr, &_shadowSampler);

	//we define an attachment description for our main color image
	//the attachment is loaded as "clear" when renderpass start
	//the attachment is stored when renderpass ends
	//the attachment layout starts as "undefined", and transitions to "Present" so its possible to display it
	//we dont care about stencil, and dont use multisampling

	VkAttachmentDescription color_attachment = {};
	color_attachment.format = nocopy ? _swachainImageFormat : _renderFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = nocopy ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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
	depth_attachment.finalLayout =  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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

void REngine::init_shadow_renderpass()
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

	for (auto& frame : _frames)
	{
		//for the depth image, we want to allocate it from gpu local memory
		VmaAllocationCreateInfo dimg_allocinfo = {};
		dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		//shadow map

		VkExtent3D shadowExtent = { _shadowExtent.width, _shadowExtent.height, 1 };
		VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowExtent);

		vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &frame._shadowImage._image, &frame._shadowImage._allocation, nullptr);
		VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, frame._shadowImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);
		VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &frame._shadowImage._defaultView));
		{
			VkFramebufferCreateInfo sh_info = vkinit::framebuffer_create_info(_shadowPass, _shadowExtent);
			sh_info.pAttachments = &frame._shadowImage._defaultView;
			sh_info.attachmentCount = 1;
			VK_CHECK(vkCreateFramebuffer(_device, &sh_info, nullptr, &frame._shadowFramebuffer));
		}
		{
			auto v = frame._shadowImage._defaultView;
			auto i = frame._shadowImage._image;
			auto a = frame._shadowImage._allocation;
			auto f = frame._shadowFramebuffer;
			_mainDeletionQueue.push_function([=]() {
				vkDestroyFramebuffer(_device, f, nullptr);
				vkDestroyImageView(_device, v, nullptr);
				vmaDestroyImage(_allocator, i, a);
				});
		}
	}
}


void REngine::init_copy_renderpass(VkFormat swachainImageFormat)
{
	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	//samplerInfo.maxAnisotropy = 10;
	//samplerInfo.anisotropyEnable = true;
	vkCreateSampler(_device, &samplerInfo, nullptr, &_smoothSampler);

//	samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
//	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
//	//info.anisotropyEnable = true;
////	samplerInfo.maxAnisotropy = 10;
////	samplerInfo.anisotropyEnable = true;
//	samplerInfo.mipLodBias = 2;
//	samplerInfo.maxLod = 30.f;
//	samplerInfo.minLod = 3;
//	vkCreateSampler(_device, &samplerInfo, nullptr, &_smoothSampler2);


	VkSamplerCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.magFilter = VK_FILTER_LINEAR;
	createInfo.minFilter = VK_FILTER_LINEAR;
	createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.minLod = 0;
	createInfo.maxLod = 16.f;
	VkSamplerReductionModeCreateInfoEXT createInfoReduction = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT };
	auto reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN_EXT;
	if (reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT)
	{
		createInfoReduction.reductionMode = reductionMode;
		createInfo.pNext = &createInfoReduction;
	}
	VK_CHECK(vkCreateSampler(_device, &createInfo, 0, &_depthSampler));

	//we define an attachment description for our main color image
//the attachment is loaded as "clear" when renderpass start
//the attachment is stored when renderpass ends
//the attachment layout starts as "undefined", and transitions to "Present" so its possible to display it
//we dont care about stencil, and dont use multisampling

	VkAttachmentDescription color_attachment = {};
	color_attachment.format = swachainImageFormat;
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

void REngine::forward_pass(RenderPassGraph& graph, VkClearValue clearValue, VkCommandBuffer cmd)
{
	auto depthTex = graph.create_texture(RES_DEPTH, get_current_frame()._depthImage._image);
//	auto desc = RPGView::Desc(-1); 	// ?
	auto depthView = graph.create_view(depthTex, get_current_frame()._depthImage._defaultView);

	auto pass = graph.add_pass(PASS_FORWARD, RGPASS_FLAG_GRAPHICS,
		[=](RenderPassGraph& g)
		{
			vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu forward pass");
			vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Forward Primitives");
			VkClearValue depthClear;
			depthClear.depthStencil.depth = 0.f;

			//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
			VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_renderPass, _windowExtent, 
				nocopy ? get_current_frame()._framebuffer : get_current_frame()._forwardFramebuffer);	//!!!

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
				//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Forward Pass");
				draw_objects_forward(cmd, get_render_scene()->_forwardPass);
				draw_objects_forward(cmd, get_render_scene()->_transparentForwardPass);
			}
			{
				//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Imgui Draw");
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
			}
			//finalize the render pass
			vkCmdEndRenderPass(cmd);

			//// do it early in the end of prev (forward) pass
			//VkImageMemoryBarrier depthReadBarrier = vkinit::image_barrier(get_current_frame()._depthImage._image,
			//	VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			//	VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			//	VK_IMAGE_ASPECT_DEPTH_BIT);
			//vkCmdPipelineBarrier(cmd,
			//	VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			//	VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depthReadBarrier);

		});
	graph.pass_write(pass, depthView);
	// do vkCreateRenderPass (_renderPass) and vkCreateFramebuffer(_forwardFramebuffer) in compile graph state? (if not compute)
	// pipeline?
	//VkImageView attachments[2] = { frame._rawRenderImage._defaultView, frame._depthImage._defaultView };
//	pass.set_rt(_forwardFramebuffer);
	
	//pass.set_srv(get_current_frame()._shadowImage._defaultView, 2);
}

void REngine::draw_objects_forward(VkCommandBuffer cmd, MeshPass& pass)
{
	ZoneScopedNC("DrawObjects", tracy::Color::Blue);
	VkDescriptorBufferInfo instanceInfo = pass.compactedInstanceBuffer.get_info();
	if (instanceInfo.range == 0)
		return;

	glm::mat4 view = _camera.get_view_matrix(this);
	glm::mat4 projection = _camera.get_projection_matrix(this);
	glm::mat4 pre_rotate = _camera.get_pre_rotation_matrix(this);


	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = pre_rotate * projection * view;

	_sceneParameters.sunlightShadowMatrix = _mainLight.get_projection() * _mainLight.get_view();

	float framed = (_frameNumber / 120.f);
	_sceneParameters.ambientColor = glm::vec4{ 0.5 };
	_sceneParameters.sunlightColor = glm::vec4{ 1.f };
	_sceneParameters.sunlightDirection = glm::vec4(_mainLight.lightDirection * 1.f, 1.f);

	_sceneParameters.sunlightColor.w = CVAR_Shadowcast.Get() ? 0.f : 1.f;

	//push data to dynmem
	uint32_t scene_data_offset = get_current_frame().dynamicData.push(_sceneParameters);
	uint32_t camera_data_offset = get_current_frame().dynamicData.push(camData);

	VkDescriptorBufferInfo objectBufferInfo = get_render_scene()->objectDataBuffer.get_info();
	VkDescriptorBufferInfo sceneInfo = get_current_frame().dynamicData.source.get_info();
	sceneInfo.range = sizeof(GPUSceneData);

	VkDescriptorBufferInfo camInfo = get_current_frame().dynamicData.source.get_info();
	camInfo.range = sizeof(GPUCameraData);


	VkDescriptorImageInfo shadowImage;
	shadowImage.sampler = _shadowSampler;

	shadowImage.imageView = get_current_frame()._shadowImage._defaultView;
	shadowImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; //?

	VkDescriptorSet GlobalSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &camInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
		.bind_buffer(1, &sceneInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.bind_image(2, &shadowImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(GlobalSet);

	VkDescriptorSet ObjectDataSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.bind_buffer(1, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.build(ObjectDataSet);
	vkCmdSetDepthBias(cmd, 0, 0, 0);

	std::vector<uint32_t> dynamic_offsets;
	dynamic_offsets.push_back(camera_data_offset);
	dynamic_offsets.push_back(scene_data_offset);
	execute_draw_commands(cmd, pass, ObjectDataSet, dynamic_offsets, GlobalSet);
}

void REngine::shadow_pass(RenderPassGraph& graph, VkCommandBuffer cmd)
{
	auto pass = graph.add_pass(PASS_SHADOW, RGPASS_FLAG_GRAPHICS,
		[=](RenderPassGraph& g)
		{
			vkutil::VulkanScopeTimer timer(cmd, _profiler, "gpu shadow pass");
			vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Shadow Primitives");

			if (CVAR_FreezeShadows.Get() || !*CVarSystem::Get()->GetIntCVar("gpu.shadowcast"))
				return;

			//clear depth at 1
			VkClearValue depthClear;
			depthClear.depthStencil.depth = 1.f;
			VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_shadowPass, _shadowExtent, get_current_frame()._shadowFramebuffer); //!!

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

			if (get_render_scene()->_shadowPass.batches.size() > 0)
			{
				draw_objects_shadow(cmd, get_render_scene()->_shadowPass);
			}

			vkCmdEndRenderPass(cmd);
		});
}

void REngine::draw_objects_shadow(VkCommandBuffer cmd, MeshPass& pass)
{
	ZoneScopedNC("DrawObjects", tracy::Color::Blue);
	VkDescriptorBufferInfo instanceInfo = pass.compactedInstanceBuffer.get_info();
	if (instanceInfo.range == 0)
		return;

	glm::mat4 view = _mainLight.get_view();

	glm::mat4 projection = _mainLight.get_projection();

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	//push data to dynmem
	uint32_t camera_data_offset = get_current_frame().dynamicData.push(camData);


	VkDescriptorBufferInfo objectBufferInfo = get_render_scene()->objectDataBuffer.get_info();

	VkDescriptorBufferInfo camInfo = get_current_frame().dynamicData.source.get_info();
	camInfo.range = sizeof(GPUCameraData);

	VkDescriptorSet GlobalSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &camInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT)
		.build(GlobalSet);

	VkDescriptorSet ObjectDataSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.bind_buffer(1, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.build(ObjectDataSet);

	vkCmdSetDepthBias(cmd, CVAR_ShadowBias.GetFloat(), 0, CVAR_SlopeBias.GetFloat());

	std::vector<uint32_t> dynamic_offsets;
	dynamic_offsets.push_back(camera_data_offset);

	execute_draw_commands(cmd, pass, ObjectDataSet, dynamic_offsets, GlobalSet);
}

glm::vec4 normalizePlane(glm::vec4 p)
{
	return p / glm::length(glm::vec3(p));
}

void REngine::execute_compute_cull(VkCommandBuffer cmd, MeshPass& pass, CullParams& params)
{
	if (CVAR_FreezeCull.Get())
		return;
	if (pass.batches.size() == 0)
		return;

	//TracyVkZone(_graphicsQueueContext, cmd, "Cull Dispatch");
	VkDescriptorBufferInfo objectBufferInfo = get_render_scene()->objectDataBuffer.get_info();

	VkDescriptorBufferInfo dynamicInfo = get_current_frame().dynamicData.source.get_info();
	dynamicInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.passObjectsBuffer.get_info();
	VkDescriptorBufferInfo finalInfo = pass.compactedInstanceBuffer.get_info();		//!!
	VkDescriptorBufferInfo indirectInfo = pass.drawIndirectBuffer.get_info();

	RPGTexture rgImage;
	RPGHandle texHandle;
	auto image = get_pool_image(RES_DEPTH_PYRAMID, &texHandle, &rgImage);
	if (!image)
		return;

	auto imageView = get_pool_image_view(texHandle, -1); //srv
	assert(imageView);
	VkDescriptorImageInfo depthPyramid;
	depthPyramid.sampler = _depthSampler;
	depthPyramid.imageView = imageView;
	depthPyramid.imageLayout = VK_IMAGE_LAYOUT_GENERAL;


	VkDescriptorSet COMPObjectDataSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_buffer(1, &indirectInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_buffer(2, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_buffer(3, &finalInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_image(4, &depthPyramid, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
		.bind_buffer(5, &dynamicInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(COMPObjectDataSet);


	glm::mat4 projection = params.projmat;
	glm::mat4 projectionT = transpose(projection);

	glm::vec4 frustumX = normalizePlane(projectionT[3] + projectionT[0]); // x + w < 0
	glm::vec4 frustumY = normalizePlane(projectionT[3] + projectionT[1]); // y + w < 0

	DrawCullData cullData = {};
	cullData.P00 = projection[0][0];
	cullData.P11 = projection[1][1];
	cullData.znear = 0.1f;
	cullData.zfar = params.drawDist;
	cullData.frustum[0] = frustumX.x;
	cullData.frustum[1] = frustumX.z;
	cullData.frustum[2] = frustumY.y;
	cullData.frustum[3] = frustumY.z;
	cullData.flags = static_cast<uint16_t>(pass.flat_batches.size()) << 16;
	cullData.flags |= params.frustrumCull ? 1 : 0;
	//cullData.lodEnabled = false;
	cullData.flags |= params.occlusionCull ? 2 : 0;
	//cullData.lodBase = 10.f;
	//cullData.lodStep = 1.5f;
	//cullData.pyramidWidth = static_cast<float>(depthPyramidWidth);
	//cullData.pyramidHeight = static_cast<float>(depthPyramidHeight);
	cullData.pyramid = rgImage.desc.w + (rgImage.desc.h << 16);
	cullData.view = params.viewmat;//get_view_matrix();

	cullData.flags |= params.aabb ? 8 : 0;
	cullData.aabbmin_x = params.aabbmin.x;
	cullData.aabbmin_y = params.aabbmin.y;
	cullData.aabbmin_z = params.aabbmin.z;

	cullData.aabbmax_x = params.aabbmax.x;
	cullData.aabbmax_y = params.aabbmax.y;
	cullData.aabbmax_z = params.aabbmax.z;

	cullData.flags |= params.drawDist > 10000 ? 0 : 4;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullPipeline);
	vkCmdPushConstants(cmd, _cullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DrawCullData), &cullData);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullLayout, 0, 1, &COMPObjectDataSet, 0, nullptr);
	vkCmdDispatch(cmd, static_cast<uint32_t>((pass.flat_batches.size() / 256) + 1), 1, 1);


	//barrier the 2 buffers we just wrote for culling, the indirect draw one, and the instances one, so that they can be read well when rendering the pass
	{
		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(pass.compactedInstanceBuffer._buffer, _graphicsQueueFamily);
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

		VkBufferMemoryBarrier barrier2 = vkinit::buffer_barrier(pass.drawIndirectBuffer._buffer, _graphicsQueueFamily);
		barrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier2.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

		//VkBufferMemoryBarrier barriers[] = { barrier,barrier2 };
		postCullBarriers.push_back(barrier);
		postCullBarriers.push_back(barrier2);

	}
	/*if (*CVarSystem::Get()->GetIntCVar("culling.outputIndirectBufferToFile"))
	{
		uint32_t offset = get_current_frame().debugDataOffsets.back();
		VkBufferCopy debugCopy;
		debugCopy.dstOffset = offset;
		debugCopy.size = pass.batches.size() * sizeof(GPUIndirectObject);
		debugCopy.srcOffset = 0;
		vkCmdCopyBuffer(cmd, pass.drawIndirectBuffer._buffer, get_current_frame().debugOutputBuffer._buffer, 1, &debugCopy);
		get_current_frame().debugDataOffsets.push_back(offset + static_cast<uint32_t>(debugCopy.size));
		get_current_frame().debugDataNames.push_back("Cull Indirect Output");
	}*/
}



void REngine::execute_draw_commands(VkCommandBuffer cmd, MeshPass& pass, VkDescriptorSet ObjectDataSet, std::vector<uint32_t> dynamic_offsets, VkDescriptorSet GlobalSet)
{
	if (pass.batches.size() > 0)
	{
		ZoneScopedNC("Draw Commit", tracy::Color::Blue4);
		Mesh* lastMesh = nullptr;
		VkPipeline lastPipeline{ VK_NULL_HANDLE };
		VkPipelineLayout lastLayout{ VK_NULL_HANDLE };
		VkDescriptorSet lastMaterialSet{ VK_NULL_HANDLE };

		{
			VkDeviceSize offset[] = { 0, 0 };
			VkBuffer buffs[] = { get_render_scene()->mergedVertexBufferP._buffer, get_render_scene()->mergedVertexBufferA._buffer };
			vkCmdBindVertexBuffers(cmd, 0, pass.type == MeshpassType::DirectionalShadow ? 1 : 2, buffs, offset);
		}

		vkCmdBindIndexBuffer(cmd, get_render_scene()->mergedIndexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

		stats.objects += static_cast<uint32_t>(pass.flat_batches.size());
		for (int i = 0; i < pass.multibatches.size(); i++)
		{
			auto& multibatch = pass.multibatches[i];
			auto& instanceDraw = pass.batches[multibatch.first];

			VkPipeline newPipeline = instanceDraw.material.shaderPass->pipeline;
			VkPipelineLayout newLayout = instanceDraw.material.shaderPass->layout;
			VkDescriptorSet newMaterialSet = instanceDraw.material.materialSet;

			Mesh* drawMesh = get_render_scene()->get_mesh(instanceDraw.meshID)->original;

			if (newPipeline != lastPipeline)
			{
				lastPipeline = newPipeline;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 1, 1, &ObjectDataSet, 0, nullptr);

				//update dynamic binds
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 0, 1, &GlobalSet, dynamic_offsets.size(), dynamic_offsets.data());
			}
			if (newMaterialSet != lastMaterialSet)
			{
				lastMaterialSet = newMaterialSet;
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 2, 1, &newMaterialSet, 0, nullptr);
			}

			bool merged = get_render_scene()->get_mesh(instanceDraw.meshID)->isMerged;
			if (merged)
			{
				if (lastMesh != nullptr)
				{
					assert(0);
					VkDeviceSize offset = 0;
					//vkCmdBindVertexBuffers(cmd, 0, 1, &get_render_scene()->mergedVertexBuffer._buffer, &offset);

					vkCmdBindIndexBuffer(cmd, get_render_scene()->mergedIndexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
					lastMesh = nullptr;
				}
			}
			else if (lastMesh != drawMesh)
			{
				assert(0);
				//bind the mesh vertex buffer with offset 0
				VkDeviceSize offset = 0;
				//vkCmdBindVertexBuffers(cmd, 0, 1, &drawMesh->_vertexBuffer._buffer, &offset);

				if (drawMesh->_indexBuffer._buffer != VK_NULL_HANDLE) {
					vkCmdBindIndexBuffer(cmd, drawMesh->_indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);
				}
				lastMesh = drawMesh;
			}

			bool bHasIndices = drawMesh->_indices.size() > 0;
			if (!bHasIndices) {
				stats.draws++;
				stats.triangles += static_cast<int32_t>(drawMesh->_vertices_p.size() / 3) * instanceDraw.count;
				vkCmdDraw(cmd, static_cast<uint32_t>(drawMesh->_vertices_p.size()), instanceDraw.count, 0, instanceDraw.first);
			}
			else {
				stats.triangles += static_cast<int32_t>(drawMesh->_indices.size() / 3) * instanceDraw.count;

				vkCmdDrawIndexedIndirect(cmd, pass.drawIndirectBuffer._buffer, multibatch.first * sizeof(GPUIndirectObject), multibatch.count, sizeof(GPUIndirectObject));

				stats.draws++;
				stats.drawcalls += instanceDraw.count;
			}
		}
	}
}

void REngine::copy_render_to_swapchain(VkCommandBuffer cmd)
{
	if (nocopy)
		return;

	//start the main renderpass. 
	//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
	VkRenderPassBeginInfo copyRP = vkinit::renderpass_begin_info(_copyPass, _windowExtent, get_current_frame()._framebuffer);

	vkCmdBeginRenderPass(cmd, &copyRP, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)_windowExtent.width;
	viewport.height = (float)_windowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	//std::swap(viewport.width, viewport.height);

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = _windowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetDepthBias(cmd, 0, 0, 0);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _blitPipeline);

	VkDescriptorImageInfo sourceImage;
	sourceImage.sampler = _smoothSampler;

	sourceImage.imageView = get_current_frame()._rawRenderImage._defaultView;
	sourceImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorSet blitSet;
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
		.bind_image(0, &sourceImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(blitSet);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _blitLayout, 0, 1, &blitSet, 0, nullptr);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
}