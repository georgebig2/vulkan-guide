#pragma once
#include <array>

typedef int	RGHandle;
typedef const char* RGName;

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
		VkFormat format;
	} desc;

	RGTexture(RGName name, const Desc& d) : RGResource(name), desc(d) {}
	RGTexture() {}

	AllocatedImage image;
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

	AllocatedImage image;
	VkImageViewCreateInfo viewInfo = {}; // debug only
};

class RenderGraph;
class RGPass
{
public:
	RGPass(RGName n) : name(n) {}
	RGPass() {}
	RGName name;
	std::function<void(RenderGraph&)> func;
};



class RenderGraph
{
public:
	RenderGraph(REngine* e) : engine(e) {}

	RGHandle create_texture(RGName name, const RGTexture::Desc& desc); 	//ERDGTextureFlags Flags
	RGHandle create_texture(RGName name, AllocatedImage image);
	const RGTexture& get_texture(RGHandle handle) const;

	RGHandle create_view(RGHandle tex, const RGView::Desc& desc);
	const RGView& get_view(RGHandle handle) const;

	template </*typename ParameterStructType, */typename F>
	RGHandle add_pass(RGName name, /*const ParameterStructType* ParameterStruct, ERDGPassFlags Flags, */ F&& func);

	void execute();

	REngine* engine;

private:

	void compile();

	std::array<RGTexture, 32>  textures;
	int numTextures = 0;
	std::array<RGView, 64>	   views;
	int numViews = 0;
	std::array<RGPass, 32>	   passes;
	int numPasses = 0;
};