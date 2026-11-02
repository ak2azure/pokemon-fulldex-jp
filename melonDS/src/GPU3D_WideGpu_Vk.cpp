// ssrecomp: the widescreen view's polygons drawn by Vulkan (GPU3D_WideGpu.h): WORLD ON GPU on
// the PS5 title, which has RADV linked in and no OpenGL, and on a PC for testing it against
// the OpenGL backend and the software rasteriser (SSR_WIDE_GPU_API=vulkan with --headless,
// on Mesa's lavapipe). The same shaders as deko3D's and OpenGL's (wide_gpu/*.glsl, compiled
// for Vulkan by tools/vk_shaders.py) and the same steps as GPU3D_WideGpu_GL.cpp, with depth
// from 0 to 1. On lavapipe its pictures are OpenGL's but for a few hundred pixels on polygon
// edges of 920,000 (the two place pixels on an edge differently).
//
// Each of the three slots that take turns has its own command buffer, fence and copies of the
// frame's data, so a picture can be drawn while the next is prepared; the targets drawn into
// are shared, the queue keeping the pictures in order. The picture is copied into the slot's
// buffer, which is the processor's memory that Target hands out.
#ifdef SSR_WIDE_VK
#include "GPU3D_WideGpu.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "VulkanSupport.h"
#include "wide_gpu_spv.h"

namespace melonDS
{
using namespace Vk;

namespace
{
constexpr VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat AttrFormat = VK_FORMAT_R32G32_UINT;
constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
constexpr VkFormat FinalFormat = VK_FORMAT_B8G8R8A8_UNORM; // (as XRGB in memory)
constexpr int Slots = 3;

class WideGpuVk : public WideGpuBackend
{
public:
    const char* Name() const override { return "Vulkan"; }

    bool Init(Context& c, std::string& info)
    {
        C = &c;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(c.Gpu, &props);
        DrawStride = (u32)std::max<VkDeviceSize>(16, props.limits.minUniformBufferOffsetAlignment);

        // One layout for both passes: the frame's constants, a draw's (at a dynamic offset),
        // the vertices, polygons, texture and palette memory, and the main pass's two targets
        // read by the final pass.
        const struct { u32 binding; VkDescriptorType type; } binds[] = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER }, { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC },
            { 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER }, { 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
            { 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER }, { 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
            { 16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER }, { 17, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
        };
        VkDescriptorSetLayoutBinding lb[8];
        for (int i = 0; i < 8; i++)
            lb[i] = { binds[i].binding, binds[i].type, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo lci { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        lci.bindingCount = 8;
        lci.pBindings = lb;
        if (vkCreateDescriptorSetLayout(c.Device, &lci, nullptr, &SetLayout) != VK_SUCCESS) return Fail(info, "descriptor layout");
        VkPipelineLayoutCreateInfo pci { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pci.setLayoutCount = 1;
        pci.pSetLayouts = &SetLayout;
        if (vkCreatePipelineLayout(c.Device, &pci, nullptr, &Layout) != VK_SUCCESS) return Fail(info, "pipeline layout");
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Slots }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, Slots },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * Slots }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * Slots },
        };
        VkDescriptorPoolCreateInfo dpi { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpi.maxSets = Slots;
        dpi.poolSizeCount = 4;
        dpi.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(c.Device, &dpi, nullptr, &Pool) != VK_SUCCESS) return Fail(info, "descriptor pool");
        VkSamplerCreateInfo sci { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(c.Device, &sci, nullptr, &Sampler) != VK_SUCCESS) return Fail(info, "sampler");

        VsMain = Shader(c, wide_vsh_spv, sizeof wide_vsh_spv);
        FsMain = Shader(c, wide_fsh_spv, sizeof wide_fsh_spv);
        FsEarly = Shader(c, wide_fsh_early_spv, sizeof wide_fsh_early_spv);
        VsFinal = Shader(c, final_vsh_spv, sizeof final_vsh_spv);
        FsFinal = Shader(c, final_fsh_spv, sizeof final_fsh_spv);
        if (!VsMain || !FsMain || !FsEarly || !VsFinal || !FsFinal) return Fail(info, "shaders");
        FinalPipeline = MakePipeline(VsFinal, FsFinal, false, false, 0, true);
        if (!FinalPipeline) return Fail(info, "the final pass's pipeline");

        VkCommandPoolCreateInfo cpi { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpi.queueFamilyIndex = c.QueueFamily;
        if (vkCreateCommandPool(c.Device, &cpi, nullptr, &CmdPool) != VK_SUCCESS) return Fail(info, "command pool");
        for (SlotData& s : S)
        {
            VkCommandBufferAllocateInfo cai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            cai.commandPool = CmdPool;
            cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cai.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(c.Device, &cai, &s.Cmd) != VK_SUCCESS) return Fail(info, "command buffer");
            VkFenceCreateInfo fci { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (vkCreateFence(c.Device, &fci, nullptr, &s.Fence) != VK_SUCCESS) return Fail(info, "fence");
            VkDescriptorSetAllocateInfo dai { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            dai.descriptorPool = Pool;
            dai.descriptorSetCount = 1;
            dai.pSetLayouts = &SetLayout;
            if (vkAllocateDescriptorSets(c.Device, &dai, &s.Set) != VK_SUCCESS) return Fail(info, "descriptor set");
            const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if (!MakeBuffer(c, s.FrameUbo, sizeof(WideGpuFrameConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host) ||
                !MakeBuffer(c, s.Tex, 512 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host) ||
                !MakeBuffer(c, s.Pal, 128 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host))
                return Fail(info, "buffers");
        }
        info = c.Name;
        return true;
    }

    ~WideGpuVk() override
    {
        if (!C) return;
        vkDeviceWaitIdle(C->Device);
        for (SlotData& s : S)
        {
            for (Buffer* b : { &s.FrameUbo, &s.Draws, &s.Vertices, &s.Polygons, &s.Tex, &s.Pal, &s.Out }) FreeBuffer(*C, *b);
            if (s.Fence) vkDestroyFence(C->Device, s.Fence, nullptr);
        }
        FreeTargets();
        for (auto& [key, p] : Pipelines) vkDestroyPipeline(C->Device, p, nullptr);
        if (FinalPipeline) vkDestroyPipeline(C->Device, FinalPipeline, nullptr);
        for (VkShaderModule m : { VsMain, FsMain, FsEarly, VsFinal, FsFinal })
            if (m) vkDestroyShaderModule(C->Device, m, nullptr);
        if (CmdPool) vkDestroyCommandPool(C->Device, CmdPool, nullptr);
        if (Sampler) vkDestroySampler(C->Device, Sampler, nullptr);
        if (Pool) vkDestroyDescriptorPool(C->Device, Pool, nullptr);
        if (Layout) vkDestroyPipelineLayout(C->Device, Layout, nullptr);
        if (SetLayout) vkDestroyDescriptorSetLayout(C->Device, SetLayout, nullptr);
    }

    bool Done(int slot) override
    {
        return !S[slot].Pending || vkGetFenceStatus(C->Device, S[slot].Fence) == VK_SUCCESS;
    }

    u32* Target(int slot, int w, int h) override
    {
        SlotData& s = S[slot];
        const VkDeviceSize size = (VkDeviceSize)w * h * 4;
        if (s.Out.Size != size)
        {
            Wait(slot);
            // (Read by the processor: cached memory where there is some.)
            if (!MakeBuffer(*C, s.Out, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
                return nullptr;
        }
        return static_cast<u32*>(s.Out.Mapped);
    }

    bool Submit(WideGpuFrame& f, int slot) override
    {
        SlotData& s = S[slot];
        Wait(slot);
        if (f.W != W || f.H != H)
        {
            vkDeviceWaitIdle(C->Device);
            if (!MakeTargets(f.W, f.H)) return false;
        }
        if (!s.Out.Mapped || s.Out.Size != (VkDeviceSize)f.W * f.H * 4) Target(slot, f.W, f.H);
        // (Vulkan's y points down where OpenGL's points up, and its rows are in memory from the
        // top where OpenGL reads them from the bottom: so the same direction as OpenGL's gives
        // the same rows in memory.)
        f.Constants.Scale[2] = -1.0f;
        f.Constants.Scale[3] = 0.0f; // depth from 0 to 1

        // The frame's data, copied into the slot's buffers.
        memcpy(s.FrameUbo.Mapped, &f.Constants, sizeof f.Constants);
        const size_t nv = std::max<size_t>(1, f.Vertices.size()) * sizeof(WideGpuVertex);
        const size_t np = std::max<size_t>(1, f.Polygons.size()) * sizeof(WideGpuPolygon);
        const size_t nd = std::max<size_t>(1, f.Draws.size()) * DrawStride;
        bool rebind = !s.Bound;
        rebind |= Fit(s.Vertices, nv, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        rebind |= Fit(s.Polygons, np, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        rebind |= Fit(s.Draws, nd, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        if (!s.Vertices.Mapped || !s.Polygons.Mapped || !s.Draws.Mapped) return false;
        memcpy(s.Vertices.Mapped, f.Vertices.data(), f.Vertices.size() * sizeof(WideGpuVertex));
        memcpy(s.Polygons.Mapped, f.Polygons.data(), f.Polygons.size() * sizeof(WideGpuPolygon));
        for (size_t i = 0; i < f.Draws.size(); i++)
        {
            const WideGpuDraw& d = f.Draws[i];
            const u32 params[4] = { d.First, d.Pixels, (u32)d.DepthBias, f.WDepth ? 1u : 0u };
            memcpy(static_cast<u8*>(s.Draws.Mapped) + i * DrawStride, params, sizeof params);
        }
        if (!f.TexVersion || f.TexVersion != s.TexVersion)
        {
            memcpy(s.Tex.Mapped, f.TexMem, 512 * 1024);
            memcpy(s.Pal.Mapped, f.PalMem, 128 * 1024);
            s.TexVersion = f.TexVersion;
        }
        if (rebind || s.TargetsSeen != TargetsMade) Bind(s);

        VkCommandBuffer cmd = s.Cmd;
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        // The main pass: the targets cleared, the draws in the game's order.
        const VkPipelineStageFlags anyGraphics = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        Transition(cmd, Color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, anyGraphics,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
        Transition(cmd, Attr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, anyGraphics,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        Transition(cmd, Depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo ca[2] {};
        ca[0].sType = ca[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ca[0].imageView = Color.View;
        ca[1].imageView = Attr.View;
        for (VkRenderingAttachmentInfo& a : ca)
        {
            a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        memcpy(ca[0].clearValue.color.float32, f.ClearColor, sizeof f.ClearColor);
        ca[1].clearValue.color.uint32[0] = f.ClearDepth;
        ca[1].clearValue.color.uint32[1] = f.ClearFog;
        VkRenderingAttachmentInfo da { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        da.imageView = Depth.View;
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        da.clearValue.depthStencil = { f.ClearDepthValue, f.ClearStencil };
        VkRenderingInfo ri { VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea = { { 0, 0 }, { (u32)W, (u32)H } };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 2;
        ri.pColorAttachments = ca;
        ri.pDepthAttachment = &da;
        ri.pStencilAttachment = &da;
        vkCmdBeginRendering(cmd, &ri);
        const VkViewport vp = { 0, 0, (float)W, (float)H, 0, 1 };
        const VkRect2D sc = { { 0, 0 }, { (u32)W, (u32)H } };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdSetDepthTestEnable(cmd, VK_TRUE);
        vkCmdSetStencilTestEnable(cmd, VK_TRUE);
        vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
        VkPipeline bound = VK_NULL_HANDLE;
        for (size_t i = 0; i < f.Draws.size(); i++)
        {
            const WideGpuDraw& d = f.Draws[i];
            const u32 attrMask = (d.WriteDepthAttr ? 1u : 0u) | (d.WriteFogAttr ? 2u : 0u);
            const VkPipeline p = MainPipeline(d.Early, d.Blend, attrMask);
            if (!p) continue;
            if (p != bound)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
                bound = p;
            }
            const u32 offset = (u32)(i * DrawStride);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Layout, 0, 1, &s.Set, 1, &offset);
            vkCmdSetDepthCompareOp(cmd, f.WDepth ? (d.DepthLequal ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_GREATER)
                                                 : (d.DepthLequal ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS));
            vkCmdSetDepthWriteEnable(cmd, d.DepthWrite ? VK_TRUE : VK_FALSE);
            const VkCompareOp op = d.StencilTest == WideGpuStencil::Always ? VK_COMPARE_OP_ALWAYS :
                d.StencilTest == WideGpuStencil::Equal ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_NOT_EQUAL;
            vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE,
                VK_STENCIL_OP_KEEP, op);
            vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.StencilMask);
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.StencilRef);
            vkCmdDraw(cmd, d.Count, 1, 0, 0);
        }
        vkCmdEndRendering(cmd);

        // The final pass (the fog, the edges) into the picture, copied out to the slot.
        Transition(cmd, Color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        Transition(cmd, Attr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        Transition(cmd, Final, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo fa { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        fa.imageView = Final.View;
        fa.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        fa.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        fa.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo fri { VK_STRUCTURE_TYPE_RENDERING_INFO };
        fri.renderArea = ri.renderArea;
        fri.layerCount = 1;
        fri.colorAttachmentCount = 1;
        fri.pColorAttachments = &fa;
        vkCmdBeginRendering(cmd, &fri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, FinalPipeline);
        const u32 zero = 0;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Layout, 0, 1, &s.Set, 1, &zero);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        Transition(cmd, Final, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy {};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { (u32)W, (u32)H, 1 };
        vkCmdCopyImageToBuffer(cmd, Final.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.Out.Handle, 1, &copy);
        VkMemoryBarrier toHost { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, nullptr, 0, nullptr);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkResetFences(C->Device, 1, &s.Fence);
        VkResult r;
        {
            std::lock_guard<std::mutex> lock(C->Lock);
            r = vkQueueSubmit(C->Queue, 1, &si, s.Fence);
        }
        if (r != VK_SUCCESS)
        {
            static int reported = 0;
            if (reported++ < 5) fprintf(stderr, "[widegpu] Vulkan: vkQueueSubmit failed (%d)\n", (int)r);
            return false;
        }
        s.Pending = true;
        s.Started = std::chrono::steady_clock::now();
        return true;
    }

    void Fetch(int slot) override
    {
        SlotData& s = S[slot];
        if (!s.Pending) return;
        Wait(slot);
        WideGpuBusyNs += (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - s.Started).count();
        if (!s.Out.Coherent)
        {
            VkMappedMemoryRange range { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
            range.memory = s.Out.Memory;
            range.size = VK_WHOLE_SIZE;
            vkInvalidateMappedMemoryRanges(C->Device, 1, &range);
        }
    }

private:
    struct SlotData
    {
        VkCommandBuffer Cmd = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
        VkDescriptorSet Set = VK_NULL_HANDLE;
        Buffer FrameUbo, Draws, Vertices, Polygons, Tex, Pal, Out;
        u64 TexVersion = 0;
        bool Pending = false, Bound = false;
        int TargetsSeen = -1;
        std::chrono::steady_clock::time_point Started;
    };

    static bool Fail(std::string& info, const char* what)
    {
        info = std::string("Vulkan: could not make the ") + what;
        return false;
    }

    void Wait(int slot)
    {
        SlotData& s = S[slot];
        if (!s.Pending) return;
        vkWaitForFences(C->Device, 1, &s.Fence, VK_TRUE, UINT64_MAX);
        s.Pending = false;
    }

    // The buffer made larger when the frame's data does not fit (half again, so that it is not
    // made again for every frame that grows a little); true when it was made anew.
    bool Fit(Buffer& b, size_t need, VkBufferUsageFlags usage)
    {
        if (b.Handle && b.Size >= need) return false;
        MakeBuffer(*C, b, need + need / 2, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        return true;
    }

    void Bind(SlotData& s)
    {
        VkDescriptorBufferInfo bufs[6] = {
            { s.FrameUbo.Handle, 0, sizeof(WideGpuFrameConstants) }, { s.Draws.Handle, 0, 16 },
            { s.Vertices.Handle, 0, VK_WHOLE_SIZE }, { s.Polygons.Handle, 0, VK_WHOLE_SIZE },
            { s.Tex.Handle, 0, VK_WHOLE_SIZE }, { s.Pal.Handle, 0, VK_WHOLE_SIZE },
        };
        VkDescriptorImageInfo imgs[2] = {
            { Sampler, Color.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { Sampler, Attr.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        };
        const u32 bindings[8] = { 0, 1, 8, 9, 10, 11, 16, 17 };
        const VkDescriptorType types[8] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
        VkWriteDescriptorSet w[8];
        for (int i = 0; i < 8; i++)
        {
            w[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w[i].dstSet = s.Set;
            w[i].dstBinding = bindings[i];
            w[i].descriptorCount = 1;
            w[i].descriptorType = types[i];
            if (i < 6) w[i].pBufferInfo = &bufs[i];
            else w[i].pImageInfo = &imgs[i - 6];
        }
        vkUpdateDescriptorSets(C->Device, 8, w, 0, nullptr);
        s.Bound = true;
        s.TargetsSeen = TargetsMade;
    }

    void FreeTargets()
    {
        for (Image* i : { &Color, &Attr, &Depth, &Final }) FreeImage(*C, *i);
    }

    bool MakeTargets(int w, int h)
    {
        FreeTargets();
        W = w;
        H = h;
        TargetsMade++;
        const VkImageUsageFlags sampledTarget = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        return MakeImage(*C, Color, w, h, ColorFormat, sampledTarget, VK_IMAGE_ASPECT_COLOR_BIT) &&
            MakeImage(*C, Attr, w, h, AttrFormat, sampledTarget, VK_IMAGE_ASPECT_COLOR_BIT) &&
            MakeImage(*C, Depth, w, h, DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) &&
            MakeImage(*C, Final, w, h, FinalFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // The main pass's pipelines differ in what Vulkan cannot change while drawing: the fragment
    // shader (early tests or not), blending, and which channels of the second target are written.
    VkPipeline MainPipeline(bool early, bool blend, u32 attrMask)
    {
        const u32 key = (early ? 1u : 0u) | (blend ? 2u : 0u) | (attrMask << 2);
        auto it = Pipelines.find(key);
        if (it != Pipelines.end()) return it->second;
        VkPipeline p = MakePipeline(VsMain, early ? FsEarly : FsMain, true, blend, attrMask, false);
        Pipelines[key] = p;
        return p;
    }

    VkPipeline MakePipeline(VkShaderModule vs, VkShaderModule fs, bool main, bool blend, u32 attrMask, bool final)
    {
        VkPipelineShaderStageCreateInfo stages[2] {};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr };
        VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vps { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vps.viewportCount = vps.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        VkPipelineColorBlendAttachmentState att[2] {};
        att[0].colorWriteMask = 0xF;
        if (blend)
        {
            att[0].blendEnable = VK_TRUE;
            att[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            att[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            att[0].colorBlendOp = VK_BLEND_OP_ADD;
            att[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att[0].alphaBlendOp = VK_BLEND_OP_MAX;
        }
        att[1].colorWriteMask = ((attrMask & 1) ? VK_COLOR_COMPONENT_R_BIT : 0) | ((attrMask & 2) ? VK_COLOR_COMPONENT_G_BIT : 0);
        VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = main ? 2 : 1;
        cb.pAttachments = att;
        std::vector<VkDynamicState> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        if (main)
            dyn.insert(dyn.end(), { VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP,
                VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE });
        VkPipelineDynamicStateCreateInfo dy { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dy.dynamicStateCount = (u32)dyn.size();
        dy.pDynamicStates = dyn.data();
        const VkFormat mainFormats[2] = { ColorFormat, AttrFormat };
        const VkFormat finalFormat = FinalFormat;
        VkPipelineRenderingCreateInfo prc { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        prc.colorAttachmentCount = main ? 2 : 1;
        prc.pColorAttachmentFormats = final ? &finalFormat : mainFormats;
        if (main) prc.depthAttachmentFormat = prc.stencilAttachmentFormat = DepthFormat;
        VkGraphicsPipelineCreateInfo gpi { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gpi.pNext = &prc;
        gpi.stageCount = 2;
        gpi.pStages = stages;
        gpi.pVertexInputState = &vi;
        gpi.pInputAssemblyState = &ia;
        gpi.pViewportState = &vps;
        gpi.pRasterizationState = &rs;
        gpi.pMultisampleState = &ms;
        gpi.pDepthStencilState = &ds;
        gpi.pColorBlendState = &cb;
        gpi.pDynamicState = &dy;
        gpi.layout = Layout;
        VkPipeline p = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(C->Device, VK_NULL_HANDLE, 1, &gpi, nullptr, &p) != VK_SUCCESS)
            fprintf(stderr, "[widegpu] Vulkan: a pipeline could not be made\n");
        return p;
    }

    Context* C = nullptr;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkCommandPool CmdPool = VK_NULL_HANDLE;
    VkShaderModule VsMain = VK_NULL_HANDLE, FsMain = VK_NULL_HANDLE, FsEarly = VK_NULL_HANDLE;
    VkShaderModule VsFinal = VK_NULL_HANDLE, FsFinal = VK_NULL_HANDLE;
    VkPipeline FinalPipeline = VK_NULL_HANDLE;
    std::unordered_map<u32, VkPipeline> Pipelines;
    Image Color, Attr, Depth, Final;
    int W = 0, H = 0, TargetsMade = 0;
    u32 DrawStride = 256;
    SlotData S[Slots];
};
}

std::unique_ptr<WideGpuBackend> WideGpuMakeVk(std::string& info)
{
    // (The PS5 title has made the device for its picture already; elsewhere one for drawing
    // off the screen.)
    Context* c = Vk::Current();
    if (!c) c = Vk::Start(info, false);
    if (!c) return nullptr;
    std::unique_ptr<WideGpuVk> r(new WideGpuVk());
    if (!r->Init(*c, info)) return nullptr;
    return r;
}
}
#endif
