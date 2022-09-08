#include "Tracy.hpp"
#include "TracyVulkan.hpp"
#include "rengine.h"
#include "vk_profiler.h"
#include "logger.h"
#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_descriptors.h>
#include "render_graph.h"
#include <random>


constexpr bool ALLOW_MULTIPLE_RESOURCE_WRITERS = true;
constexpr int MEM_PAGE_SIZE = 1024*16;


struct MemStackPage
{
	int curPos = 0;
	std::array<char, MEM_PAGE_SIZE> data;
};
struct MemStackPool
{
	MemStackPool()
	{
		pages.resize(1);
	}

	std::tuple<int,int,char*> allocate(size_t size)
	{
		assert(size <= MEM_PAGE_SIZE);

		auto& p = pages[curPage];
		if (p.curPos + size > p.data.size())
		{
			pages.resize(++curPage + 1);
			p = pages[curPage];
		}
		p.curPos += size;
		return std::make_tuple(curPage, p.curPos-size, &p.data[p.curPos - size]);
	}

	bool rollback_memory(int page, int pagePos)
	{
		curPage = page;
		pages[curPage].curPos = pagePos;
		return true;
	}

	int curPage = 0;
	std::vector<MemStackPage> pages;
} memStackPool;

std::tuple<int, int, char*> RenderPassGraph::allocate_memory(size_t size)
{
	return memStackPool.allocate(size);
}

bool RenderPassGraph::rollback_memory(int page, int pagePos)
{
	return memStackPool.rollback_memory(page, pagePos);
}

RenderPassGraph::~RenderPassGraph()
{
	rollback_memory(firstMemPage, firstMemPagePos);
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
	//RPGIdx writePass = RPGIdxNone;
	uint64_t writePasses = 0;
	uint64_t readPasses = 0;

	int8_t inDegrees = 0;

	VkImageView imageView = 0;
	RPGIdx nextAliasRes = RPGIdxNone;
	RPGIdx aliasOrigin = RPGIdxNone;
};

std::vector<PoolTexture> gTextures;
std::vector<PoolView> gViews;

RPGHandle get_texture_handle(RPGName name)
{
	int h = -1;
	auto it = registry_textures.find(name);
	if (it == registry_textures.end()) {
		h = (int)gTextures.size();
		registry_textures.insert({ name, h });
		gTextures.resize(h + 1);
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
		h = (int)gViews.size();
		registry_views.insert({ key, h });
		gViews.resize(h + 1);
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
		*rgTex = gTextures[h].rgTex;
	if (texHandle)
		*texHandle = h;
	return gTextures[h].image;
}

VkImageView get_pool_image_view(RPGHandle tex, int level)
{
	auto h = get_view_handle({ tex, level });
	return gViews[h].imageView;
}

RenderPassGraph::RenderPassGraph(REngine* e) 
	: engine(e)
{
	//auto pass = add_pass(PASS_PROLOGUE, RGPASS_FLAG_GRAPHICS, [=](RenderPassGraph& g) {});
}


RPGHandle RenderPassGraph::create_texture(RPGName name, const RPGTexture::Desc& desc)
{
	auto h = get_texture_handle(name);
	gTextures[h].rgTex = RPGTexture(name, desc);
	return h;
}

RPGHandle RenderPassGraph::create_texture(RPGName name, VkImage image)
{
	auto h = get_texture_handle(name);
	RPGTexture::Desc desc;
	gTextures[h].rgTex = RPGTexture(name, desc);
	gTextures[h].image = image;
	return h;
}

RPGHandle RenderPassGraph::create_view(RPGHandle tex, const RPGView::Desc& desc)
{
	auto h = get_view_handle({ tex, desc.level });
	gViews[h].rgView = RPGView(gTextures[tex].rgTex, tex, desc);
	return h;
}

RPGHandle RenderPassGraph::create_view(RPGHandle tex, VkImageView view)
{
	auto h = get_view_handle({ tex, -1 });
	RPGView::Desc desc;
	gViews[h].rgView = RPGView(gTextures[tex].rgTex, tex, desc);
	gViews[h].imageView = view;
	return h;
}

VkImage RenderPassGraph::get_image(RPGHandle handle) const
{
	return gTextures[handle].image;
}

VkImageView RenderPassGraph::get_image_view(RPGHandle handle) const
{
	return gViews[handle].imageView;
}


RPGIdx RenderPassGraph::pass_write(RPGHandle& pass, RPGHandle& view)
{
	//v.writePasses |= uint64_t(1) << pass;
	//RPGIdx idx = std::lower_bound(&resources[0], &resources[numResources], view) - &resources[0];
	//if (idx == numResources || resources[idx] != view) {
	//	for (auto i = numResources; i > idx; --i) resources[i] = resources[i - 1];
	//	resources[idx] = view;
	//	numResources++;
	//}
	RPGIdx idx = std::find(&resources[0], &resources[numResources], view) - &resources[0];		// todo: O(1)
	if (idx == numResources) {
		resources[numResources++] = view;
	}

	passes[pass].write(idx);
	auto& v = gViews[view];
	assert(pass >= 0 && pass < 64);
	v.writePasses |= uint64_t(1) << pass;
	//assert(v.writePass == RPGIdxNone || v.writePass == pass);
	//v.writePass = pass;
	return idx;

}
bool RenderPassGraph::pass_read(RPGHandle& pass, RPGHandle& view)
{
	bool added = false;
	RPGIdx idx = std::find(&resources[0], &resources[numResources], view) - &resources[0];		// todo: O(1)
	if (idx == numResources) {
		resources[numResources++] = view;
		added = true;
	}

	passes[pass].read(idx);
	auto& v = gViews[view];
	//auto t = v.texHandle;
	//auto& tex = textures[t];
	assert(pass >= 0 && pass < 64);
	v.readPasses |= uint64_t(1) << pass;
	return added;

}

std::tuple<RPGIdx,bool,bool> RenderPassGraph::find_next_resource_pass(RPGIdx res, RPGIdx curPassIdx, const OrderList& order)
{
	bool reads = false;
	bool writes = false;

	for (RPGIdx i = curPassIdx + 1; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& nextPass = passes[pIdx];
	
		for (int8_t r = 0; r < nextPass.numReads; ++r) {
			auto idx = nextPass.reads[r];
			if (res == idx) {
				reads = true;
				break;
			}
		}
		for (int8_t w = 0; w < nextPass.numWrites; ++w) {
			auto idx = nextPass.writes[w];
			if (res == idx) {
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

	assert(test());
	//add_pass(PASS_EPILOGUE, RGPASS_FLAG_GRAPHICS, [=](RenderPassGraph& g){});

	// todo: cull passes
	// todo: merge passes

	OrderList order;
	auto newNumPasses = sort_dependences(order);
	assert(newNumPasses == numPasses);
	optimize(order);
	//alias_resources(order);
	assert(validate());

	// resource transition between passes
	// vk render passes for graphics passes
	// r->w, r->r, w->w
	// grouping barriers after pass
	// split barriers?
	// parallel passes?
	// parallel queues (compute/graphics)
	// resourse aliasing
	// barriers between frames?
	for (int i = 0; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& pass = passes[pIdx];

		// create write resources
		for (int8_t w = 0; w < pass.numWrites; ++w)
		{
			auto idx = pass.writes[w];
			auto wV = resources[idx];
			auto& wView = gViews[wV];
			assert(wView.aliasOrigin == RPGIdxNone);		// not ready yet
			if (wView.aliasOrigin == RPGIdxNone)
			{
				auto wT = wView.rgView.texHandle;
				check_physical_texture(wT, pass);
				check_physical_view(wV);
			}
		}

		pass.func->execute(*this);

		// lets find what will be next with our write res
		for (int8_t w = 0; w < pass.numWrites; ++w)
		{
			auto idx = pass.writes[w];
			auto wV = resources[idx];
			auto [ii, nextReads, nextWrites] = find_next_resource_pass(idx, i, order);
			if (nextReads || nextWrites)
			{
				auto nextPidx = order[ii];
				auto& nextPass = passes[nextPidx];
				assert(!nextWrites);					// w -> w?

				// w -> r
				if (nextReads)
				{
					auto& wView = gViews[wV];
					auto wT = wView.rgView.texHandle;
					auto& wTex = gTextures[wT];
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

		// lets find what will be next with our read res
		for (int8_t r = 0; r < pass.numReads; ++r)
		{
			auto idx = pass.reads[r];
			auto [ii, nextReads, nextWrites] = find_next_resource_pass(idx, i, order);
			
			auto rV = resources[idx];
			auto& rView = gViews[rV];

			if (nextReads || nextWrites)
			{
				assert(!nextWrites);			// r -> w?
				
				// todo: r -> r barrier
			}
			else if (rView.nextAliasRes != RPGIdxNone) 	// resource alias?
			{
				auto idx = rView.nextAliasRes;
				auto [ii, nextReads, nextWrites] = find_next_resource_pass(idx, i, order);
				if (nextReads || nextWrites)
				{
					assert(!nextReads);			// write needs to be first
					// todo: r -> w barrier
				}
			}
		}

	}

	static bool firstTime = true;
	if (firstTime) {
		export_svg("runtime.svg", order);
		firstTime = false;
	}
	hud();
}


void RenderPassGraph::hud()
{
	//const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	//ImVec2 windowSize = main_viewport->Size;
	////std::swap(windowSize.x, windowSize.y);
	//ImVec2 windowPos = { 0, windowSize.y / 2 };
	//windowSize.y /= 2.4f;
	//ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
	//ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);
	//ImGui::SetNextWindowBgAlpha(0.4f);

	//static bool opened = true;
	//if (ImGui::Begin("History graph show", &opened, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {

	//	if (ImGui::Button(paused ? "resume" : "pause")) {
	//		paused = !paused;
	//	}
	//}
}


bool has_duplicates(const OrderList& order, int len)
{
	OrderList copy = order;
	std::sort(&copy[0], &copy[len]);
	bool hasDuplicates = std::adjacent_find(&copy[0], &copy[len]) != &copy[len];
	return hasDuplicates;
}


// use topological sorting for ordering passes writes/reads
template <bool Random>
RPGIdx RenderPassGraph::sort_dependences(OrderList& out)
{
	static auto randomEngine = std::default_random_engine(time(0));

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
			zerosQ[backIdx++] = pIdx;
		}
		out[pIdx] = pIdx;
	}
	assert(!has_duplicates(out, numPasses));

	if (Random) {
		std::shuffle(&zerosQ[0], &zerosQ[backIdx], randomEngine);
	}

	//assert(backIdx <= 1);	// one enter for now
	//assert(backIdx > 0);
	if (!backIdx)
		return 0;

	for (int r = 0; r < numResources; ++r) {
		auto viewHandle = resources[r];
		auto& v = gViews[viewHandle];
		v.inDegrees = 0;
		for (RPGIdx pIdx = 0; pIdx < 64; ++pIdx)
			if (v.writePasses & (uint64_t(1) << pIdx))
				v.inDegrees++;
	}


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

		auto prevBack = backIdx;
		auto& pass = passes[zero];
		for (int8_t w = 0; w < pass.numWrites; ++w)
		{
			auto idx = pass.writes[w];
			auto viewHandle = resources[idx];
			auto& v = gViews[viewHandle];
			//assert(v.inDegrees > 0);
			if (v.inDegrees > 0 && !--v.inDegrees) {
				for (RPGIdx pIdx = 0; pIdx < 64; ++pIdx)
				{
					if (v.readPasses & (uint64_t(1) << pIdx))
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
		if (Random) {
			std::shuffle(&zerosQ[prevBack], &zerosQ[backIdx], randomEngine);
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
			passes[i].depthLevel = RPGIdxNone;
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
		for (RPGIdx i = numPasses - 1; i > 0; --i) {
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
	auto random_shuffle2 = [&]()
	{
		for (RPGIdx i = 0; i < numPasses - 1; ++i) {
			auto o1 = out[i];
			auto o2 = out[i + 1];
			auto& p1 = passes[o1];
			auto& p2 = passes[o2];
			bool ignore1 = (!p1.numReads && !p1.numWrites);
			bool ignore2 = (!p2.numReads && !p2.numWrites);
			if (p1.depthLevel == p2.depthLevel && !ignore1 && !ignore2 && (std::rand() < RAND_MAX/2)) {
				std::swap(out[i], out[i + 1]);
			}
		}
	};

	auto baseCost = calc_cost_alias(out);

	OrderList maxOrder;
	int maxCost = 0;
	for (int i = 0; i < numPasses*10; ++i)
	{
		int cost = calc_cost_alias(out);
		if (cost >= maxCost) {
			maxCost = cost;
			maxOrder = out;
		}
		random_shuffle2();
		//auto res = sort_dependences<true>(out);
		//assert(res);
	}
	if (maxCost > 0)
		out = maxOrder;

	assert(!has_duplicates(out, numPasses));
	return std::make_tuple(baseCost, maxCost);
}

int RenderPassGraph::calc_cost_wr(const OrderList& order)
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
			auto idx = pass.writes[w];
			auto [ii, nextReads, nextWrites] = find_next_resource_pass(idx, i, order);
			//assert(!nextWrites);
			if (nextReads) {
				cost += ii - i;
			}
		}
	}
	return cost;
}


// find all resources lifetimes
void RenderPassGraph::calc_res_spans(const OrderList& iOrder, std::array<ResSpan, MAX_RESOURCES>& spans)
{
	for (RPGIdx r = 0; r < numResources; ++r)
	{
		auto& v = gViews[resources[r]];
		if (v.readPasses && v.writePasses)
		{
			//spans[r].s = iOrder[v.writePass];
			for (RPGIdx pIdx = 0; pIdx < 64; ++pIdx) {
				if (v.writePasses & (uint64_t(1) << pIdx)) {
					auto o = iOrder[pIdx];
					spans[r].s = std::min(spans[r].s, o);
				}
			}
			for (RPGIdx pIdx = 0; pIdx < 64; ++pIdx) {
				if (v.readPasses & (uint64_t(1) << pIdx)) {
					auto o = iOrder[pIdx];
					spans[r].e = std::max(spans[r].e, o);
				}
			}
			assert(spans[r].e >= spans[r].s);
		}
		else {
			spans[r].s = spans[r].e = 0;
		}
	}
}

int RenderPassGraph::calc_cost_alias(const OrderList& order)
{
	int cost = 0;

	OrderList iOrder;
	for (int i = 0; i < numPasses; ++i) 
		iOrder[order[i]] = i;

	std::array<ResSpan, MAX_RESOURCES> spans;
	calc_res_spans(iOrder, spans);

	for (RPGIdx i = 0; i < numResources; ++i)
	{
		for(RPGIdx j = 0; j < numResources; ++j)
		{
			if (j >= i)
				continue;

			auto [w1, r1] = std::make_tuple(spans[i].s, spans[i].e);
			auto [w2, r2] = std::make_tuple(spans[j].s, spans[j].e);
			assert(r1 >= w1);
			assert(r2 >= w2);
			auto intersect = std::abs((w1 + r1) - (w2 + r2)) - (r1-w1 + r2-w2);
 			cost += intersect;
		}
	}

	return cost;
}

void RenderPassGraph::alias_resources(const OrderList& order)
{
	OrderList iOrder, destOrder, aliasOrder;

	for (RPGIdx i = 0; i < numResources; ++i) {
		auto& v = gViews[resources[i]];
		v.aliasOrigin = RPGIdxNone;
		v.nextAliasRes = RPGIdxNone;
		destOrder[i] = i;
		aliasOrder[i] = i;
	}

	for (RPGIdx i = 0; i < numPasses; ++i) iOrder[order[i]] = i;
	std::array<ResSpan, MAX_RESOURCES> spans;
	calc_res_spans(iOrder, spans);

	// sort dest rest by first write
	std::sort(&destOrder[0], &destOrder[numResources],
		[&iOrder, &spans, this](RPGIdx idx1, RPGIdx idx2)
		{
			//auto& v1 = gViews[resources[idx1]];
			//auto& v2 = gViews[resources[idx2]];
			return iOrder[spans[idx1].s] < iOrder[spans[idx2].s];
		});

	// sort alias res by importance (memory?, longest w->r barrier)
	std::sort(&aliasOrder[0], &aliasOrder[numResources],
		[&iOrder, &spans, this](RPGIdx idx1, RPGIdx idx2)
		{
			return iOrder[spans[idx1].s] > iOrder[spans[idx2].s];
		});

	// do aliasing
	for (RPGIdx a = 0; a < numResources; ++a)
	{
		auto idxA = aliasOrder[a];
		if (!spans[idxA].s && !spans[idxA].e)	// "empty" res
			continue;
		auto rA = resources[idxA];
		auto& vA = gViews[rA];
		if (vA.imageView)		// dont alias physical resources
			continue;

		for (RPGIdx d = 0; d < numResources; ++d)
		{
			auto idxD = destOrder[d];
			if (!spans[idxD].s && !spans[idxD].e)	// "empty" res
				continue;
			//assert(spans[idxA].s >= spans[idxD].s);
			if (spans[idxA].s <= spans[idxD].s)
				continue;

			auto rD = resources[idxD];
			auto& vD = gViews[rD];
			if (vD.aliasOrigin != RPGIdxNone)	// already aliased?
				continue;

			// lets find end of hole
			auto& as = spans[idxA];
			auto idxP = idxD;
			while (as.s > spans[idxD].s)
			{
				auto rD = resources[idxD];
				auto& vD = gViews[rD];
				if (vD.nextAliasRes == RPGIdxNone) {
					idxP = idxD;
					idxD = RPGIdxNone;
					break;
				}
				idxP = idxD;
				idxD = vD.nextAliasRes;
			}

			if (idxD != RPGIdxNone && as.e >= spans[idxD].s)
				continue;
			if (as.s <= spans[idxP].e)
				continue;

			auto rP = resources[idxP];
			auto& vP = gViews[rP];
			auto rA = resources[idxA];
			auto& vA = gViews[rA];

			// check physical merge
			{

				//continue;
			}

			vP.nextAliasRes = idxA;
			vA.aliasOrigin = idxP;
			{
				vA.nextAliasRes = idxD;
			}
			break;
		}
	}
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
	const auto wax = width / 800;
	const auto hax = height / 128;
	const auto hr = 2 * ho / 3;
	const auto yr = yo + ho - hr;
	auto hp = ho / 20;
	Color arrowColorW(0, 255, 255, 0.9f);
	Color arrowColorR(255, 255, 0, 0.9f);

	Dimensions dimensions(width, height);
	Document doc(fileName, Layout(dimensions, Layout::TopLeft));
	doc << svg::Rectangle(Point(0, 0), width, height, Color::Black);

	auto cost = calc_cost_wr(order);
	char label[128]; std::sprintf(label, "Cost: %d", cost);
	doc << Text(Point(width/2, height/32), label, Color::White, Font(18, "Verdana"));

	// sort res by first write
	OrderList resOrder, iOrder;
	for (RPGHandle i = 0; i < numPasses; ++i) iOrder[order[i]] = i;
	for (RPGHandle i = 0; i < numResources; ++i) resOrder[i] = i;
	std::array<ResSpan, MAX_RESOURCES> spans;
	calc_res_spans(iOrder, spans);
	std::sort(&resOrder[0], &resOrder[numResources],
		[&iOrder,&spans,this](RPGIdx idx1, RPGIdx idx2)
		{
			//auto& v1 = gViews[resources[idx1]];
			//auto& v2 = gViews[resources[idx2]];
			//auto pidx1 = v1.writePass;
			//auto pidx2 = v2.writePass;
			return iOrder[spans[idx1].s] < iOrder[spans[idx2].s];
			//return iOrder[pidx1] < iOrder[pidx2];
		});
	for (RPGHandle i = 0; i < numResources; ++i) iOrder[resOrder[i]] = i;

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
		char label[128]; std::sprintf(label, "%d %s", pass.depthLevel, pass.name);
		doc << Text(Point(xp+wp/3, yp + hp / 2), label, Color::White, Font(14, "Verdana"));

		// draw resources
		//for (int8_t w = 0; w < pass.numWrites; ++w)
		//{
		//	auto idx = pass.writes[w];
		//	auto wV = resources[idx];
		//	auto& view = gViews[wV];
		//	auto hw = (hr - dry * (numResources-1)) / numResources;
		//	auto yw = yr + hr * iOrder[view.aliasOrigin == RPGIdxNone ? idx : view.aliasOrigin] / numResources;

		//	auto xw = xo;
		//	auto ww = wo;
		//	//doc << svg::Rectangle(Point(xw, yw), ww, hw, Color::Grey);
		//	auto ii = i;
		//	while (1)
		//	{
		//		auto [iii, nextReads, nextWrites] = find_next_resource_pass(idx, ii, order);
		//		//assert(!nextWrites);
		//		if (!nextReads && !nextWrites)
		//			break;
		//		xw = xp;
		//		ww = (wp + dpx) * (iii - i + 1);
		//		ii = iii;
		//	}


		//	Color resColor = ii != i ? Color::Blue : Color::Grey;
		//	resColor.alpha = 0.8f;
		//	if (view.aliasOrigin != RPGIdxNone) {
		//		resColor.alpha = 0.3f;//resColor.red / 2; resColor.green
		//		resColor.green = 128;
		//	}

		//	doc << svg::Rectangle(Point(xw, yw), ww, hw, resColor);
		//	char label[128]; std::sprintf(label, "%s-%d", view.rgView.name, wV);
		//	doc << Text(Point(xw+ww/2, yw + hw *8/10), label, Color::Silver, Font(12, "Verdana"));
		//}
	}

	// draw resources
	for (RPGIdx a = 0; a < numResources; ++a)
	{
		auto idx = resOrder[a];
		//if (!spans[idxA].s && !spans[idxA].e)	// "empty" res
		//	continue;
		auto wV = resources[idx];
		auto& view = gViews[wV];

		auto hw = (hr - dry * (numResources-1)) / numResources;
		auto yw = yr + hr * iOrder[view.aliasOrigin == RPGIdxNone ? idx : view.aliasOrigin] / numResources;

		auto wp = (wo - dpx * (numPasses - 1)) / numPasses;
		auto xp = xo + (wp + dpx) * spans[idx].s;
		auto xp2 = xo + (wp + dpx) * (spans[idx].e + 1);
		auto ww = xp2 - xp;
		if (!view.readPasses) {
			ww = wo;
		}

		Color resColor = view.readPasses ? Color::Blue : Color::Grey;
		resColor.alpha = 0.8f;
		if (view.aliasOrigin != RPGIdxNone) {
			resColor.alpha = 0.3f;
			resColor.green = 128;
		}

		doc << svg::Rectangle(Point(xp, yw), ww, hw, resColor);
		char label[128]; std::sprintf(label, "%s-%d", view.rgView.name, wV);
		doc << Text(Point(xp+ww/2, yw + hw *8/10), label, Color::Silver, Font(12, "Verdana"));
	}

	// draw arrows
	for (int i = 0; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& pass = passes[pIdx];

		auto wp = (wo - dpx * (numPasses - 1)) / numPasses;
		auto xp = xo + (wp + dpx) * i;
		auto yp = yo;
		for (int8_t w = 0; w < pass.numWrites; ++w) 
		{
			auto idx = pass.writes[w];
			auto wV = resources[idx];
			auto& view = gViews[wV];
			auto yw = yp + hp;
			auto hw = yr + hr * iOrder[view.aliasOrigin == RPGIdxNone ? idx : view.aliasOrigin] / numResources - yw;
			auto xw = xp + wp - 1*wax -  wp*w / pass.numWrites / 2;
			doc << svg::Rectangle(Point(xw, yw), wax, hw-hax, arrowColorW);
			doc << (svg::Polygon(arrowColorW, Stroke(1, arrowColorW)) << Point(xw+wax/2, yw+hw) << Point(xw-wax, yw+hw-hax) << Point(xw+2*wax, yw+hw-hax));
		}
		for (int8_t r = 0; r < pass.numReads; ++r) 
		{
			auto idx = pass.reads[r];
			auto rV = resources[idx];
			auto& view = gViews[rV];
			auto yw = yp + hp;
			auto hw = yr + hr * iOrder[view.aliasOrigin == RPGIdxNone ? idx : view.aliasOrigin] / numResources - yw;
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
	const char* passesNames[] = { "a","b","c","d","e","f", "g", "j", "i", "k", "l", "m", "n", "o" , "p", "r", "q", "s", "t" };//, "y", "v", "w", "z" };
	const uint8_t maxPassReads = 4;
	const uint8_t maxPassWrites = 4;
	int numTries = 100;

	//std::srand(time(0));

	RenderPassGraph g(engine);
	auto desc = RPGTexture::Desc();
	auto tex = g.create_texture("TestTexture", desc);

	// create passes
	RPGIdx numPasses = sizeof(passesNames) / sizeof(const char*);
	for (RPGIdx i = 0; i < numPasses; ++i)
	{
		auto p = g.add_pass(passesNames[i], RGPASS_FLAG_GRAPHICS, [=](RenderPassGraph& g) {});
	}

	// multiple writers to one resource?
	// each view gets one pass writer(producer)
	for (RPGHandle i = 0; i < numViews; ++i)
	{
		auto desc = RPGView::Desc(i);
		auto v = g.create_view(tex, desc);
		gViews[v].readPasses = 0;
		gViews[v].writePasses = 0;
		for (int t = 0; t < maxPassWrites; ++t)
		{
			RPGHandle pw = std::rand() % numPasses + 0;
			auto& pass = g.passes[pw];
			//assert(std::find(&pass.writes[0], &pass.writes[pass.numWrites], v) == &pass.writes[pass.numWrites]);
			if (pass.numWrites < maxPassWrites
				//&& std::find(&pass.writes[0], &pass.writes[pass.numWrites], v) == &pass.writes[pass.numWrites]
				//&& std::find(&pass.reads[0], &pass.reads[pass.numReads], v) == &pass.reads[pass.numReads]
				) 
			{
				if (!(gViews[v].writePasses & (uint64_t(1) << pw)))
					g.pass_write(pw, v);
				if (!ALLOW_MULTIPLE_RESOURCE_WRITERS)
					break;
			}
		}
	}

	assert(g.validate());

	// cpu w/r?
	// several queues?
	// each view(resource) is read by several passes //(except last one)
	std::unordered_map<RPGHandle, RPGIdx> iOrder;
	for (RPGHandle i = 0; i < g.numResources; ++i) iOrder[g.resources[i]] = i;
	for (RPGHandle i = 0; i < numViews-1; ++i)
	{
		auto desc = RPGView::Desc(i);
		auto v = g.create_view(tex, desc);
		auto idx = iOrder[v];

		uint8_t numR = std::rand() % maxPassReads;
		for (uint8_t r = 0; r <= numR; ++r)
		{
			for (int t = 0; t < numTries; ++t) 
			{
				RPGHandle pr = std::rand() % (numPasses - 1) + 1;  // first pass is graph enter
				auto& pass = g.passes[pr];
				if (pass.numReads < maxPassReads 
					&& std::find(&pass.reads[0], &pass.reads[pass.numReads], idx) == &pass.reads[pass.numReads]
					&& std::find(&pass.writes[0], &pass.writes[pass.numWrites], idx) == &pass.writes[pass.numWrites]
					)
				{
					auto restorePass = pass;
					auto restoreView = gViews[v];
					//assert(g.validate());
					auto added = g.pass_read(pr, v);
					auto valid = g.validate();

					// rewind (maybe dangerous!)
					if (!valid)
					{
						pass = restorePass;
						gViews[v] = restoreView;
						assert(g.numResources > 0);
						if (added) {
							std::remove(&g.resources[0], &g.resources[g.numResources], v);
							g.numResources--;
						}
						if (t == numTries - 1) {
							//assert(0);
						}
						assert(g.validate());
						continue;
					}
					break;
				}
			}
		}
	}
	assert(g.validate());

	// relax graph (remove some dependecies)
	for (int t = 0; t < numViews/2; ++t)
	{
		auto idx = std::rand() % g.numResources;
		auto v = g.resources[idx];
		for (RPGIdx pIdx = 0; pIdx < 64; ++pIdx) {
			if (gViews[v].readPasses & (uint64_t(1) << pIdx))
			{
				auto newMask = gViews[v].readPasses & (~(uint64_t(1) << pIdx));
				if (newMask) {
					gViews[v].readPasses = newMask;
					assert(g.passes[pIdx].numReads > 0);
					std::remove(&g.passes[pIdx].reads[0], &g.passes[pIdx].reads[g.passes[pIdx].numReads], idx);
					g.passes[pIdx].numReads--;
				}
			}
		}
	}
	assert(g.validate());

	static bool firstTime = true;

	// todo: cull passes
	OrderList order;
	g.sort_dependences(order);
	assert(g.validate());
	if (firstTime) {
		g.export_svg("dep.svg", order);
	}

	auto [baseCost, optCost] = g.optimize(order);
	assert(g.validate());
	if (firstTime) {
		g.export_svg("opt.svg", order);
	}

	g.alias_resources(order);
	assert(g.validate());
	if (firstTime) {
		g.export_svg("alas.svg", order);
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

	auto res = g.validate();
	assert(res);
	return res;
}

bool RenderPassGraph::validate()
{
	OrderList order;
	auto res = sort_dependences(order) > 0;
	if (!res)
		return false;
	
	for (int i = 0; i < numPasses; ++i)
	{
		auto pIdx = order[i];
		auto& pass = passes[pIdx];
		for (int8_t r = 0; r < pass.numReads; ++r)
		{
			auto idx = pass.reads[r];
			auto rV = resources[idx];

			// nobody writes this res?
			if (!gViews[rV].writePasses)
				return false;
				
			auto [ii, nextReads, nextWrites] = find_next_resource_pass(idx, i, order);
			// read before write?
			if (nextWrites)
				return false;

			// read and write the same resource in one pass?
			for (int8_t w = 0; w < pass.numWrites; ++w)
			{
				auto wV = pass.writes[w];
				if (wV == idx)
					return false;
			}
		}
	}

	return true;
}



void RenderPassGraph::check_physical_texture(RPGHandle h, RPGPass& pass)
{
	auto& tex = gTextures[h];
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
		gTextures[h].image = 0;
		vmaDestroyImage(allocator, newImage._image, alloc);
		});

	assert(newImage._image);
	tex.image = newImage._image;
}


void RenderPassGraph::check_physical_view(RPGHandle h)
{
	auto& view = gViews[h];
	auto& tex = gTextures[view.rgView.texHandle];
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
			gViews[h].imageView = 0;
			vkDestroyImageView(device, newView, nullptr);
			});

		assert(newView);
		view.imageView = newView;
	}
}



