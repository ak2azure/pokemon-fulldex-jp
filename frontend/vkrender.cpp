// SDL's renderer done with Vulkan: see vkrender.h.
#include "vkrender.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include "VulkanSupport.h"
#include "render_spv.h"

#ifdef SSR_PS5_NATIVE
extern "C" {
// RADV, linked into the title: its driver entry point is where every command comes from.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);
int sceSystemServiceHideSplashScreen();
}
#endif

using namespace melonDS::Vk;

namespace VkRender
{
namespace
{
constexpr VkFormat PictureFormat = VK_FORMAT_B8G8R8A8_UNORM; // (ARGB8888 in memory)
constexpr int InFlight = 2;

struct Tex
{
    Image Img;
    int W = 0, H = 0;
    SDL_BlendMode Blend = SDL_BLENDMODE_NONE;
    bool Linear = false;
    Uint8 R = 255, G = 255, B = 255, A = 255;
    VkDescriptorSet Set[2] {}; // nearest, linear
};

// What a frame does, in order: a texture's pixels replaced, or triangles drawn.
struct Step
{
    bool Upload;
    Tex* T;
    SDL_Rect Rect;           // (Upload)
    VkDeviceSize Offset;     // (Upload) in the frame's staging memory
    SDL_BlendMode Blend;     // (Draw)
    bool Linear;
    uint32_t First, Count;
};

struct FrameRes
{
    VkCommandBuffer Cmd = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    VkSemaphore Acquired = VK_NULL_HANDLE, Rendered = VK_NULL_HANDLE;
    Buffer Verts, Staging, Read;
    std::vector<Tex*> Graveyard; // freed once this frame's fence has passed
    bool Pending = false;
};

struct State
{
    Context* C = nullptr;
    bool Started = false, Screen = false;
    int W = 0, H = 0;
    Image Canvas;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkSampler Samplers[2] {};
    VkShaderModule Vs = VK_NULL_HANDLE, Fs = VK_NULL_HANDLE;
    VkCommandPool CmdPool = VK_NULL_HANDLE;
    std::map<SDL_BlendMode, VkPipeline> Pipelines;
    Tex* White = nullptr;
    FrameRes Frames[InFlight];
    int Cur = 0;
    std::vector<SDL_Vertex> Verts;
    std::vector<Step> Steps;
    std::vector<uint8_t> Staging;
    std::vector<Tex*> Fresh, Dying;
    SDL_Color DrawColor { 0, 0, 0, 255 };
    SDL_BlendMode DrawBlend = SDL_BLENDMODE_NONE;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> SwapImages;
    VkExtent2D Extent {};
    bool SplashHidden = false;
    int Renderers = 0;
};
State S;
struct RendererTag { int Unused; } TheRenderer; // (the SDL_Renderer handed out: there is one)

bool Fail(std::string& info, const char* what, VkResult r = VK_ERROR_UNKNOWN)
{
    char line[160];
    snprintf(line, sizeof line, "Vulkan picture: %s (%d)", what, (int)r);
    info = line;
    fprintf(stderr, "[vkrender] %s\n", line);
    return false;
}

VkBlendFactor Factor(SDL_BlendFactor f)
{
    switch (f)
    {
    case SDL_BLENDFACTOR_ZERO: return VK_BLEND_FACTOR_ZERO;
    case SDL_BLENDFACTOR_ONE: return VK_BLEND_FACTOR_ONE;
    case SDL_BLENDFACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
    case SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case SDL_BLENDFACTOR_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
    case SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case SDL_BLENDFACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
    case SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case SDL_BLENDFACTOR_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
    case SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp Op(SDL_BlendOperation o)
{
    switch (o)
    {
    case SDL_BLENDOPERATION_SUBTRACT: return VK_BLEND_OP_SUBTRACT;
    case SDL_BLENDOPERATION_REV_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case SDL_BLENDOPERATION_MINIMUM: return VK_BLEND_OP_MIN;
    case SDL_BLENDOPERATION_MAXIMUM: return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

// A blend mode's long form, as SDL 2 keeps it (SDL_render.c): the colour's operation in the low
// four bits, then its source and destination factors, the alpha's the same from bit 16. The named
// modes are given their long forms the way SDL gives them.
uint32_t Long(SDL_BlendMode mode)
{
    auto compose = [](SDL_BlendFactor sc, SDL_BlendFactor dc, SDL_BlendOperation co, SDL_BlendFactor sa,
                       SDL_BlendFactor da, SDL_BlendOperation ao) {
        return (uint32_t)co | ((uint32_t)sc << 4) | ((uint32_t)dc << 8) | ((uint32_t)ao << 16) | ((uint32_t)sa << 20) |
            ((uint32_t)da << 24);
    };
    switch (mode)
    {
    case SDL_BLENDMODE_NONE: return compose(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);
    case SDL_BLENDMODE_BLEND: return compose(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    case SDL_BLENDMODE_ADD: return compose(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    case SDL_BLENDMODE_MOD: return compose(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_COLOR, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    case SDL_BLENDMODE_MUL: return compose(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    default: return (uint32_t)mode;
    }
}

// A pipeline for each blend mode (SDL_BLENDMODE_NONE without blending).
VkPipeline Pipeline(SDL_BlendMode mode)
{
    auto it = S.Pipelines.find(mode);
    if (it != S.Pipelines.end()) return it->second;
    VkPipelineShaderStageCreateInfo stages[2] {};
    stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, S.Vs, "main", nullptr };
    stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, S.Fs, "main", nullptr };
    const VkVertexInputBindingDescription binding = { 0, sizeof(SDL_Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    const VkVertexInputAttributeDescription attrs[3] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SDL_Vertex, position) },
        { 1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(SDL_Vertex, color) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SDL_Vertex, tex_coord) },
    };
    VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att {};
    att.colorWriteMask = 0xF;
    att.blendEnable = VK_TRUE;
    const uint32_t l = Long(mode);
    att.colorBlendOp = Op((SDL_BlendOperation)(l & 0xF));
    att.srcColorBlendFactor = Factor((SDL_BlendFactor)((l >> 4) & 0xF));
    att.dstColorBlendFactor = Factor((SDL_BlendFactor)((l >> 8) & 0xF));
    att.alphaBlendOp = Op((SDL_BlendOperation)((l >> 16) & 0xF));
    att.srcAlphaBlendFactor = Factor((SDL_BlendFactor)((l >> 20) & 0xF));
    att.dstAlphaBlendFactor = Factor((SDL_BlendFactor)((l >> 24) & 0xF));
    if (mode == SDL_BLENDMODE_NONE) att.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    const VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo prc { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    prc.colorAttachmentCount = 1;
    prc.pColorAttachmentFormats = &PictureFormat;
    VkGraphicsPipelineCreateInfo gpi { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpi.pNext = &prc;
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vps;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &dy;
    gpi.layout = S.Layout;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(S.C->Device, VK_NULL_HANDLE, 1, &gpi, nullptr, &p) != VK_SUCCESS)
        fprintf(stderr, "[vkrender] no pipeline for blend mode 0x%x\n", (unsigned)mode);
    S.Pipelines[mode] = p;
    return p;
}

void FreeTex(Tex* t)
{
    vkFreeDescriptorSets(S.C->Device, S.Pool, 2, t->Set);
    FreeImage(*S.C, t->Img);
    delete t;
}

// The screen: the display's largest mode, the one nearest 60 Hz of those (the emulation keeps
// its own pace, a frame for each vblank at 60), on the display plane 0, as Porpoise does.
bool MakeSwapchain(std::string& info)
{
    Context& c = *S.C;
    uint32_t n = 0;
    VkResult r = vkGetPhysicalDeviceDisplayPropertiesKHR(c.Gpu, &n, nullptr);
    if (r != VK_SUCCESS || n == 0) return Fail(info, "no display", r);
    std::vector<VkDisplayPropertiesKHR> displays(n);
    vkGetPhysicalDeviceDisplayPropertiesKHR(c.Gpu, &n, displays.data());
    const VkDisplayKHR display = displays[0].display;
    uint32_t mn = 0;
    vkGetDisplayModePropertiesKHR(c.Gpu, display, &mn, nullptr);
    std::vector<VkDisplayModePropertiesKHR> modes(mn);
    vkGetDisplayModePropertiesKHR(c.Gpu, display, &mn, modes.data());
    if (mn == 0) return Fail(info, "the display has no modes");
    int best = 0;
    for (uint32_t i = 1; i < mn; i++)
    {
        const auto& a = modes[i].parameters;
        const auto& b = modes[best].parameters;
        const uint64_t areaA = (uint64_t)a.visibleRegion.width * a.visibleRegion.height;
        const uint64_t areaB = (uint64_t)b.visibleRegion.width * b.visibleRegion.height;
        if (areaA != areaB ? areaA > areaB : std::abs((int)a.refreshRate - 60000) < std::abs((int)b.refreshRate - 60000))
            best = (int)i;
    }
    for (uint32_t i = 0; i < mn; i++)
        fprintf(stderr, "[vkrender] display mode %ux%u at %.3f Hz%s\n", modes[i].parameters.visibleRegion.width,
            modes[i].parameters.visibleRegion.height, modes[i].parameters.refreshRate / 1000.0, (int)i == best ? " (chosen)" : "");
    S.Extent = modes[best].parameters.visibleRegion;
    VkDisplaySurfaceCreateInfoKHR dsi { VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR };
    dsi.displayMode = modes[best].displayMode;
    dsi.planeIndex = 0;
    dsi.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    dsi.globalAlpha = 1.0f;
    dsi.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    dsi.imageExtent = S.Extent;
    r = vkCreateDisplayPlaneSurfaceKHR(c.Instance, &dsi, nullptr, &S.Surface);
    if (r != VK_SUCCESS) return Fail(info, "no display surface", r);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c.Gpu, S.Surface, &caps);
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(c.Gpu, S.Surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(c.Gpu, S.Surface, &fn, formats.data());
    if (fn == 0) return Fail(info, "the display surface has no formats");
    // (A format that is not sRGB: the picture's values go to the screen as they are.)
    VkSurfaceFormatKHR format = formats[0];
    for (const VkSurfaceFormatKHR& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            format = f;
            break;
        }
    VkSwapchainCreateInfoKHR sci { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    sci.surface = S.Surface;
    sci.minImageCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount) sci.minImageCount = std::min(sci.minImageCount, caps.maxImageCount);
    sci.imageFormat = format.format;
    sci.imageColorSpace = format.colorSpace;
    sci.imageExtent = S.Extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    r = vkCreateSwapchainKHR(c.Device, &sci, nullptr, &S.Swapchain);
    if (r != VK_SUCCESS) return Fail(info, "no swapchain", r);
    uint32_t in = 0;
    vkGetSwapchainImagesKHR(c.Device, S.Swapchain, &in, nullptr);
    S.SwapImages.resize(in);
    vkGetSwapchainImagesKHR(c.Device, S.Swapchain, &in, S.SwapImages.data());
    fprintf(stderr, "[vkrender] the screen: %ux%u, format %d, %u images\n", S.Extent.width, S.Extent.height,
        (int)format.format, in);
    return true;
}

// The frame's steps carried out into the picture; then, when asked, the picture shown on the
// screen or copied out (pixels: W x H, ARGB8888, pitch in bytes).
void Execute(bool present, void* pixels = nullptr, int pitch = 0)
{
    Context& c = *S.C;
    FrameRes& f = S.Frames[S.Cur];
    if (f.Pending)
    {
        vkWaitForFences(c.Device, 1, &f.Fence, VK_TRUE, UINT64_MAX);
        f.Pending = false;
    }
    for (Tex* t : f.Graveyard) FreeTex(t);
    f.Graveyard.clear();
    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const size_t vbytes = std::max<size_t>(S.Verts.size() * sizeof(SDL_Vertex), 64);
    if (f.Verts.Size < vbytes) MakeBuffer(c, f.Verts, vbytes + vbytes / 2, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host);
    const size_t sbytes = std::max<size_t>(S.Staging.size(), 64);
    if (f.Staging.Size < sbytes) MakeBuffer(c, f.Staging, sbytes + sbytes / 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host);
    if (!S.Verts.empty()) memcpy(f.Verts.Mapped, S.Verts.data(), S.Verts.size() * sizeof(SDL_Vertex));
    if (!S.Staging.empty()) memcpy(f.Staging.Mapped, S.Staging.data(), S.Staging.size());

    VkCommandBuffer cmd = f.Cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    // New textures start transparent black, as SDL's do.
    for (Tex* t : S.Fresh)
    {
        Transition(cmd, t->Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        const VkClearColorValue zero {};
        const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(cmd, t->Img.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        Transition(cmd, t->Img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    S.Fresh.clear();
    // (The picture is kept from frame to frame, as SDL's is: drawn over, cleared only the first time.)
    const bool first = S.Canvas.Layout == VK_IMAGE_LAYOUT_UNDEFINED;
    if (S.Canvas.Layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        Transition(cmd, S.Canvas, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    bool rendering = false, cleared = !first;
    VkPipeline bound = VK_NULL_HANDLE;
    const VkViewport vp = { 0, 0, (float)S.W, (float)S.H, 0, 1 };
    const VkRect2D sc = { { 0, 0 }, { (uint32_t)S.W, (uint32_t)S.H } };
    const float scale[2] = { 2.0f / S.W, 2.0f / S.H };
    auto begin = [&] {
        VkRenderingAttachmentInfo ca { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        ca.imageView = S.Canvas.View;
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = cleared ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        cleared = true;
        VkRenderingInfo ri { VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea = sc;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &f.Verts.Handle, &zero);
        vkCmdPushConstants(cmd, S.Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof scale, scale);
        bound = VK_NULL_HANDLE;
        rendering = true;
    };
    for (const Step& s : S.Steps)
    {
        if (s.Upload)
        {
            if (rendering)
            {
                vkCmdEndRendering(cmd);
                rendering = false;
            }
            Transition(cmd, s.T->Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy copy {};
            copy.bufferOffset = s.Offset;
            copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            copy.imageOffset = { s.Rect.x, s.Rect.y, 0 };
            copy.imageExtent = { (uint32_t)s.Rect.w, (uint32_t)s.Rect.h, 1 };
            vkCmdCopyBufferToImage(cmd, f.Staging.Handle, s.T->Img.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            Transition(cmd, s.T->Img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
            continue;
        }
        if (!rendering) begin();
        const VkPipeline p = Pipeline(s.Blend);
        if (!p) continue;
        if (p != bound)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
            bound = p;
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, S.Layout, 0, 1, &s.T->Set[s.Linear ? 1 : 0], 0, nullptr);
        vkCmdDraw(cmd, s.Count, 1, s.First, 0);
    }
    if (!rendering && !cleared) begin(); // (a picture never drawn into: black)
    if (rendering) vkCmdEndRendering(cmd);

    uint32_t image = 0;
    bool show = present && S.Swapchain;
    if (show)
    {
        const VkResult r = vkAcquireNextImageKHR(c.Device, S.Swapchain, UINT64_MAX, f.Acquired, VK_NULL_HANDLE, &image);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        {
            static int reported = 0;
            if (reported++ < 5) fprintf(stderr, "[vkrender] vkAcquireNextImageKHR: %d\n", (int)r);
            show = false;
        }
    }
    if (show || pixels)
        Transition(cmd, S.Canvas, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    if (show)
    {
        Image swap;
        swap.Handle = S.SwapImages[image];
        Transition(cmd, swap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageBlit blit {};
        blit.srcSubresource = blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.srcOffsets[1] = { S.W, S.H, 1 };
        blit.dstOffsets[1] = { (int32_t)S.Extent.width, (int32_t)S.Extent.height, 1 };
        vkCmdBlitImage(cmd, S.Canvas.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap.Handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        Transition(cmd, swap, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
    }
    if (pixels)
    {
        const VkDeviceSize bytes = (VkDeviceSize)S.W * S.H * 4;
        if (f.Read.Size < bytes)
            MakeBuffer(c, f.Read, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        VkBufferImageCopy copy {};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { (uint32_t)S.W, (uint32_t)S.H, 1 };
        vkCmdCopyImageToBuffer(cmd, S.Canvas.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.Read.Handle, 1, &copy);
        VkMemoryBarrier toHost { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, nullptr, 0, nullptr);
    }
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (show)
    {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &f.Acquired;
        si.pWaitDstStageMask = &waitStage;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &f.Rendered;
    }
    vkResetFences(c.Device, 1, &f.Fence);
    {
        std::lock_guard<std::mutex> lock(c.Lock);
        vkQueueSubmit(c.Queue, 1, &si, f.Fence);
        if (show)
        {
            VkPresentInfoKHR pi { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &f.Rendered;
            pi.swapchainCount = 1;
            pi.pSwapchains = &S.Swapchain;
            pi.pImageIndices = &image;
            vkQueuePresentKHR(c.Queue, &pi);
        }
    }
    f.Pending = true;
    for (Tex* t : S.Dying) f.Graveyard.push_back(t);
    S.Dying.clear();
    if (pixels)
    {
        vkWaitForFences(c.Device, 1, &f.Fence, VK_TRUE, UINT64_MAX);
        f.Pending = false;
        if (!f.Read.Coherent)
        {
            VkMappedMemoryRange range { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
            range.memory = f.Read.Memory;
            range.size = VK_WHOLE_SIZE;
            vkInvalidateMappedMemoryRanges(c.Device, 1, &range);
        }
        for (int y = 0; y < S.H; y++)
            memcpy(static_cast<uint8_t*>(pixels) + (size_t)y * pitch, static_cast<uint8_t*>(f.Read.Mapped) + (size_t)y * S.W * 4, (size_t)S.W * 4);
    }
    S.Verts.clear();
    S.Steps.clear();
    S.Staging.clear();
    S.Cur = (S.Cur + 1) % InFlight;
#ifdef SSR_PS5_NATIVE
    // (The console's start-up picture stays until the title has shown one of its own.)
    if (show && !S.SplashHidden)
    {
        S.SplashHidden = true;
        sceSystemServiceHideSplashScreen();
    }
#endif
}

// Triangles drawn from the texture (the white one for none) in the blend mode.
void Draw(Tex* t, SDL_BlendMode blend, bool linear, const SDL_Vertex* v, int count)
{
    if (count <= 0) return;
    const uint32_t first = (uint32_t)S.Verts.size();
    S.Verts.insert(S.Verts.end(), v, v + count);
    if (!S.Steps.empty())
    {
        Step& last = S.Steps.back();
        if (!last.Upload && last.T == t && last.Blend == blend && last.Linear == linear && last.First + last.Count == first)
        {
            last.Count += (uint32_t)count;
            return;
        }
    }
    Step s {};
    s.Upload = false;
    s.T = t;
    s.Blend = blend;
    s.Linear = linear;
    s.First = first;
    s.Count = (uint32_t)count;
    S.Steps.push_back(s);
}

void Quad(Tex* t, SDL_BlendMode blend, bool linear, float x0, float y0, float x1, float y1,
    float u0, float v0, float u1, float v1, SDL_Color col)
{
    const SDL_Vertex q[6] = {
        { { x0, y0 }, col, { u0, v0 } }, { { x1, y0 }, col, { u1, v0 } }, { { x0, y1 }, col, { u0, v1 } },
        { { x1, y0 }, col, { u1, v0 } }, { { x1, y1 }, col, { u1, v1 } }, { { x0, y1 }, col, { u0, v1 } },
    };
    Draw(t, blend, linear, q, 6);
}

SDL_Color Mod(const Tex* t) { return SDL_Color { t->R, t->G, t->B, t->A }; }

int CopyTo(SDL_Texture* texture, const SDL_Rect* src, float x0, float y0, float x1, float y1, int flip)
{
    Tex* t = reinterpret_cast<Tex*>(texture);
    if (!t) return -1;
    const SDL_Rect r = src ? *src : SDL_Rect { 0, 0, t->W, t->H };
    float u0 = (float)r.x / t->W, v0 = (float)r.y / t->H;
    float u1 = (float)(r.x + r.w) / t->W, v1 = (float)(r.y + r.h) / t->H;
    if (flip & SDL_FLIP_HORIZONTAL) std::swap(u0, u1);
    if (flip & SDL_FLIP_VERTICAL) std::swap(v0, v1);
    Quad(t, t->Blend, t->Linear, x0, y0, x1, y1, u0, v0, u1, v1, Mod(t));
    return 0;
}
}

bool Active() { return S.Started; }

bool Start(std::string& info, bool screen, int w, int h)
{
    if (S.Started) return true;
#ifdef SSR_PS5_NATIVE
    S.C = melonDS::Vk::Start(info, screen, vk_icdGetInstanceProcAddr);
#else
    S.C = melonDS::Vk::Start(info, screen);
#endif
    if (!S.C) return false;
    Context& c = *S.C;
    S.Screen = screen;
    if (screen)
    {
        if (!MakeSwapchain(info)) return false;
        w = (int)S.Extent.width;
        h = (int)S.Extent.height;
    }
    S.W = w;
    S.H = h;
    if (!MakeImage(c, S.Canvas, w, h, PictureFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT))
        return Fail(info, "no picture");
    const VkDescriptorSetLayoutBinding lb = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo lci { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    lci.bindingCount = 1;
    lci.pBindings = &lb;
    vkCreateDescriptorSetLayout(c.Device, &lci, nullptr, &S.SetLayout);
    const VkPushConstantRange push = { VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(float) };
    VkPipelineLayoutCreateInfo pci { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pci.setLayoutCount = 1;
    pci.pSetLayouts = &S.SetLayout;
    pci.pushConstantRangeCount = 1;
    pci.pPushConstantRanges = &push;
    vkCreatePipelineLayout(c.Device, &pci, nullptr, &S.Layout);
    const VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048 };
    VkDescriptorPoolCreateInfo dpi { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = 2048;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &size;
    vkCreateDescriptorPool(c.Device, &dpi, nullptr, &S.Pool);
    for (int i = 0; i < 2; i++)
    {
        VkSamplerCreateInfo sci { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sci.magFilter = sci.minFilter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(c.Device, &sci, nullptr, &S.Samplers[i]);
    }
    S.Vs = Shader(c, render_vsh_spv, sizeof render_vsh_spv);
    S.Fs = Shader(c, render_fsh_spv, sizeof render_fsh_spv);
    VkCommandPoolCreateInfo cpi { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = c.QueueFamily;
    vkCreateCommandPool(c.Device, &cpi, nullptr, &S.CmdPool);
    for (FrameRes& f : S.Frames)
    {
        VkCommandBufferAllocateInfo cai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cai.commandPool = S.CmdPool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vkAllocateCommandBuffers(c.Device, &cai, &f.Cmd);
        VkFenceCreateInfo fci { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(c.Device, &fci, nullptr, &f.Fence);
        VkSemaphoreCreateInfo semi { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(c.Device, &semi, nullptr, &f.Acquired);
        vkCreateSemaphore(c.Device, &semi, nullptr, &f.Rendered);
    }
    if (!S.Vs || !S.Fs || !S.Layout || !S.Pool || !S.CmdPool) return Fail(info, "its pipeline's parts");
    S.Started = true;
    // The white texel the filled rectangles are drawn with.
    S.White = reinterpret_cast<Tex*>(CreateTexture(reinterpret_cast<SDL_Renderer*>(&TheRenderer), SDL_PIXELFORMAT_ARGB8888, 0, 1, 1));
    const uint32_t white = 0xFFFFFFFF;
    UpdateTexture(reinterpret_cast<SDL_Texture*>(S.White), nullptr, &white, 4);
    info = c.Name;
    return true;
}

SDL_Renderer* CreateRenderer(SDL_Window*, int, Uint32)
{
    std::string info;
    if (!Start(info, true))
    {
        SDL_SetError("%s", info.c_str());
        return nullptr;
    }
    S.Renderers++;
    return reinterpret_cast<SDL_Renderer*>(&TheRenderer);
}

void DestroyRenderer(SDL_Renderer*)
{
    // (The device, the screen and the picture stay for the next renderer: the game list's is
    // destroyed before the game's is made.)
    if (S.Renderers > 0) S.Renderers--;
}

int GetRendererInfo(SDL_Renderer*, SDL_RendererInfo* info)
{
    if (!info) return -1;
    memset(info, 0, sizeof *info);
    info->name = "vulkan";
    info->flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    info->num_texture_formats = 1;
    info->texture_formats[0] = SDL_PIXELFORMAT_ARGB8888;
    info->max_texture_width = info->max_texture_height = 8192;
    return 0;
}

int GetRendererOutputSize(SDL_Renderer*, int* w, int* h)
{
    if (w) *w = S.W;
    if (h) *h = S.H;
    return 0;
}

SDL_Texture* CreateTexture(SDL_Renderer*, Uint32 format, int, int w, int h)
{
    if (!S.Started || w <= 0 || h <= 0) return nullptr;
    if (format != SDL_PIXELFORMAT_ARGB8888)
    {
        SDL_SetError("only ARGB8888 textures");
        return nullptr;
    }
    Tex* t = new Tex();
    t->W = w;
    t->H = h;
    if (!MakeImage(*S.C, t->Img, w, h, PictureFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT))
    {
        delete t;
        SDL_SetError("no memory for a %dx%d texture", w, h);
        return nullptr;
    }
    VkDescriptorSetLayout layouts[2] = { S.SetLayout, S.SetLayout };
    VkDescriptorSetAllocateInfo dai { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dai.descriptorPool = S.Pool;
    dai.descriptorSetCount = 2;
    dai.pSetLayouts = layouts;
    vkAllocateDescriptorSets(S.C->Device, &dai, t->Set);
    for (int i = 0; i < 2; i++)
    {
        const VkDescriptorImageInfo ii = { S.Samplers[i], t->Img.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = t->Set[i];
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        vkUpdateDescriptorSets(S.C->Device, 1, &w, 0, nullptr);
    }
    S.Fresh.push_back(t);
    return reinterpret_cast<SDL_Texture*>(t);
}

void DestroyTexture(SDL_Texture* texture)
{
    Tex* t = reinterpret_cast<Tex*>(texture);
    if (!t) return;
    S.Fresh.erase(std::remove(S.Fresh.begin(), S.Fresh.end(), t), S.Fresh.end());
    // (Freed once the frames that may still draw it are done.)
    S.Dying.push_back(t);
}

int UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch)
{
    Tex* t = reinterpret_cast<Tex*>(texture);
    if (!t || !pixels) return -1;
    SDL_Rect r = rect ? *rect : SDL_Rect { 0, 0, t->W, t->H };
    if (r.w <= 0 || r.h <= 0) return 0;
    Step s {};
    s.Upload = true;
    s.T = t;
    s.Rect = r;
    s.Offset = (S.Staging.size() + 15) & ~(VkDeviceSize)15;
    S.Staging.resize(s.Offset + (size_t)r.w * r.h * 4);
    for (int y = 0; y < r.h; y++)
        memcpy(S.Staging.data() + s.Offset + (size_t)y * r.w * 4, static_cast<const uint8_t*>(pixels) + (size_t)y * pitch, (size_t)r.w * 4);
    S.Steps.push_back(s);
    return 0;
}

int SetTextureBlendMode(SDL_Texture* texture, SDL_BlendMode mode)
{
    if (Tex* t = reinterpret_cast<Tex*>(texture)) t->Blend = mode;
    return 0;
}

int SetTextureScaleMode(SDL_Texture* texture, SDL_ScaleMode mode)
{
    if (Tex* t = reinterpret_cast<Tex*>(texture)) t->Linear = mode != SDL_ScaleModeNearest;
    return 0;
}

int SetTextureColorMod(SDL_Texture* texture, Uint8 r, Uint8 g, Uint8 b)
{
    if (Tex* t = reinterpret_cast<Tex*>(texture))
    {
        t->R = r;
        t->G = g;
        t->B = b;
    }
    return 0;
}

int SetTextureAlphaMod(SDL_Texture* texture, Uint8 a)
{
    if (Tex* t = reinterpret_cast<Tex*>(texture)) t->A = a;
    return 0;
}

int SetRenderDrawColor(SDL_Renderer*, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    S.DrawColor = SDL_Color { red, green, blue, alpha };
    return 0;
}

int SetRenderDrawBlendMode(SDL_Renderer*, SDL_BlendMode mode)
{
    S.DrawBlend = mode;
    return 0;
}

int RenderClear(SDL_Renderer*)
{
    Quad(S.White, SDL_BLENDMODE_NONE, false, 0, 0, (float)S.W, (float)S.H, 0, 0, 1, 1, S.DrawColor);
    return 0;
}

int RenderFillRect(SDL_Renderer* r, const SDL_Rect* rect)
{
    const SDL_Rect all = { 0, 0, S.W, S.H };
    return RenderFillRects(r, rect ? rect : &all, 1);
}

int RenderFillRects(SDL_Renderer*, const SDL_Rect* rects, int count)
{
    for (int i = 0; i < count; i++)
        Quad(S.White, S.DrawBlend, false, (float)rects[i].x, (float)rects[i].y, (float)(rects[i].x + rects[i].w),
            (float)(rects[i].y + rects[i].h), 0, 0, 1, 1, S.DrawColor);
    return 0;
}

int RenderCopy(SDL_Renderer*, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst)
{
    const SDL_Rect d = dst ? *dst : SDL_Rect { 0, 0, S.W, S.H };
    return CopyTo(t, src, (float)d.x, (float)d.y, (float)(d.x + d.w), (float)(d.y + d.h), 0);
}

int RenderCopyF(SDL_Renderer*, SDL_Texture* t, const SDL_Rect* src, const SDL_FRect* dst)
{
    const SDL_FRect d = dst ? *dst : SDL_FRect { 0, 0, (float)S.W, (float)S.H };
    return CopyTo(t, src, d.x, d.y, d.x + d.w, d.y + d.h, 0);
}

int RenderCopyEx(SDL_Renderer*, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst, double, const SDL_Point*,
    SDL_RendererFlip flip)
{
    // (The program flips but never turns pictures: the angle is not used.)
    const SDL_Rect d = dst ? *dst : SDL_Rect { 0, 0, S.W, S.H };
    return CopyTo(t, src, (float)d.x, (float)d.y, (float)(d.x + d.w), (float)(d.y + d.h), (int)flip);
}

int RenderGeometry(SDL_Renderer*, SDL_Texture* texture, const SDL_Vertex* vertices, int count, const int* indices, int indexCount)
{
    Tex* t = texture ? reinterpret_cast<Tex*>(texture) : S.White;
    const SDL_BlendMode blend = texture ? t->Blend : S.DrawBlend; // (untextured: the draw blend mode, as SDL has it)
    std::vector<SDL_Vertex> v;
    if (indices)
        for (int i = 0; i < indexCount; i++) v.push_back(vertices[indices[i]]);
    else
        v.assign(vertices, vertices + count);
    for (SDL_Vertex& x : v)
    {
        x.color.r = (Uint8)(x.color.r * t->R / 255);
        x.color.g = (Uint8)(x.color.g * t->G / 255);
        x.color.b = (Uint8)(x.color.b * t->B / 255);
        x.color.a = (Uint8)(x.color.a * t->A / 255);
    }
    Draw(t, blend, t->Linear, v.data(), (int)v.size());
    return 0;
}

int RenderReadPixels(SDL_Renderer*, const SDL_Rect* rect, Uint32 format, void* pixels, int pitch)
{
    if ((rect && (rect->x || rect->y || rect->w != S.W || rect->h != S.H)) || format != SDL_PIXELFORMAT_ARGB8888)
        return SDL_SetError("only the whole picture as ARGB8888");
    Execute(false, pixels, pitch);
    return 0;
}

void RenderPresent(SDL_Renderer*)
{
    Execute(true);
}
}


#ifdef SSR_PS5_NATIVE
// The title's link sends SDL's renderer functions here (--wrap; tools/ps5/build_native.sh): to
// Vulkan when it started (VkRender::Active), otherwise to SDL's own renderer, which then draws
// with the processor on SDL's PS5 video driver, as before.
#define SSR_VKR_WRAP(ret, name, params, args) \
    extern "C" ret __real_SDL_##name params; \
    extern "C" ret __wrap_SDL_##name params \
    { \
        if (VkRender::Active()) return VkRender::name args; \
        return __real_SDL_##name args; \
    }
SSR_VKR_WRAP(SDL_Renderer*, CreateRenderer, (SDL_Window* w, int i, Uint32 f), (w, i, f))
SSR_VKR_WRAP(void, DestroyRenderer, (SDL_Renderer* r), (r))
SSR_VKR_WRAP(int, GetRendererInfo, (SDL_Renderer* r, SDL_RendererInfo* i), (r, i))
SSR_VKR_WRAP(int, GetRendererOutputSize, (SDL_Renderer* r, int* w, int* h), (r, w, h))
SSR_VKR_WRAP(SDL_Texture*, CreateTexture, (SDL_Renderer* r, Uint32 f, int a, int w, int h), (r, f, a, w, h))
SSR_VKR_WRAP(void, DestroyTexture, (SDL_Texture* t), (t))
SSR_VKR_WRAP(int, UpdateTexture, (SDL_Texture* t, const SDL_Rect* r, const void* p, int pitch), (t, r, p, pitch))
SSR_VKR_WRAP(int, SetTextureBlendMode, (SDL_Texture* t, SDL_BlendMode m), (t, m))
SSR_VKR_WRAP(int, SetTextureScaleMode, (SDL_Texture* t, SDL_ScaleMode m), (t, m))
SSR_VKR_WRAP(int, SetTextureColorMod, (SDL_Texture* t, Uint8 r, Uint8 g, Uint8 b), (t, r, g, b))
SSR_VKR_WRAP(int, SetTextureAlphaMod, (SDL_Texture* t, Uint8 a), (t, a))
SSR_VKR_WRAP(int, SetRenderDrawColor, (SDL_Renderer* r, Uint8 a, Uint8 b, Uint8 c, Uint8 d), (r, a, b, c, d))
SSR_VKR_WRAP(int, SetRenderDrawBlendMode, (SDL_Renderer* r, SDL_BlendMode m), (r, m))
SSR_VKR_WRAP(int, RenderClear, (SDL_Renderer* r), (r))
SSR_VKR_WRAP(int, RenderFillRect, (SDL_Renderer* r, const SDL_Rect* rect), (r, rect))
SSR_VKR_WRAP(int, RenderFillRects, (SDL_Renderer* r, const SDL_Rect* rects, int n), (r, rects, n))
SSR_VKR_WRAP(int, RenderCopy, (SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* s, const SDL_Rect* d), (r, t, s, d))
SSR_VKR_WRAP(int, RenderCopyF, (SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* s, const SDL_FRect* d), (r, t, s, d))
SSR_VKR_WRAP(int, RenderCopyEx, (SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* s, const SDL_Rect* d, double a,
    const SDL_Point* c, SDL_RendererFlip f), (r, t, s, d, a, c, f))
SSR_VKR_WRAP(int, RenderGeometry, (SDL_Renderer* r, SDL_Texture* t, const SDL_Vertex* v, int n, const int* i, int ni),
    (r, t, v, n, i, ni))
SSR_VKR_WRAP(int, RenderReadPixels, (SDL_Renderer* r, const SDL_Rect* rect, Uint32 f, void* p, int pitch), (r, rect, f, p, pitch))
SSR_VKR_WRAP(void, RenderPresent, (SDL_Renderer* r), (r))
#undef SSR_VKR_WRAP
#endif
