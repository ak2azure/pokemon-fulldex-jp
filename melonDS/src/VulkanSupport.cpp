// ssrecomp: see VulkanSupport.h.
#include "VulkanSupport.h"

#include <cstdio>
#include <cstring>
#include <vector>
#if !defined(__PROSPERO__) && !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace melonDS
{
namespace Vk
{
#define SSR_VK_DEFINE(name) PFN_##name name = nullptr;
SSR_VK_INSTANCE_FNS(SSR_VK_DEFINE)
SSR_VK_DEVICE_FNS(SSR_VK_DEFINE)
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
PFN_vkCreateInstance vkCreateInstance = nullptr;
#undef SSR_VK_DEFINE

namespace
{
Context* Shared = nullptr;

bool Fail(std::string& info, const char* what, VkResult r)
{
    char line[160];
    snprintf(line, sizeof line, "%s failed (%d)", what, (int)r);
    info = line;
    return false;
}

bool Make(Context& c, std::string& info, bool display, PFN_vkGetInstanceProcAddr getProc)
{
    vkGetInstanceProcAddr = getProc;
#if !defined(__PROSPERO__) && !defined(_WIN32)
    if (!vkGetInstanceProcAddr)
    {
        void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (lib) vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    }
#endif
    if (!vkGetInstanceProcAddr)
    {
        info = "no Vulkan (its loader or driver is not there)";
        return false;
    }
    vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (!vkCreateInstance)
    {
        info = "no vkCreateInstance";
        return false;
    }
    VkApplicationInfo app { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "Pokemon Fulldex";
    app.apiVersion = VK_API_VERSION_1_3;
    const char* instExt[] = { "VK_KHR_surface", "VK_KHR_display" };
    VkInstanceCreateInfo ici { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = display ? 2 : 0;
    ici.ppEnabledExtensionNames = instExt;
    VkResult r = vkCreateInstance(&ici, nullptr, &c.Instance);
    if (r != VK_SUCCESS) return Fail(info, "vkCreateInstance", r);
#define SSR_VK_LOAD(name) name = (PFN_##name)vkGetInstanceProcAddr(c.Instance, #name);
    SSR_VK_INSTANCE_FNS(SSR_VK_LOAD)
#undef SSR_VK_LOAD

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(c.Instance, &n, nullptr);
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(c.Instance, &n, gpus.data());
    for (VkPhysicalDevice g : gpus)
    {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(g, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, qs.data());
        for (uint32_t i = 0; i < qn; i++)
            if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                c.Gpu = g;
                c.QueueFamily = i;
                c.Name = p.deviceName;
                break;
            }
        if (c.Gpu) break;
    }
    if (!c.Gpu)
    {
        info = "no Vulkan 1.3 device that draws";
        return false;
    }
    vkGetPhysicalDeviceMemoryProperties(c.Gpu, &c.Memory);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = c.QueueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features v13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    v13.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceFeatures2 features { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features.pNext = &v13;
    const char* devExt[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext = &features;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = display ? 1 : 0;
    dci.ppEnabledExtensionNames = devExt;
    r = vkCreateDevice(c.Gpu, &dci, nullptr, &c.Device);
    if (r != VK_SUCCESS) return Fail(info, "vkCreateDevice", r);
#define SSR_VK_LOAD(name) name = (PFN_##name)vkGetDeviceProcAddr(c.Device, #name);
    SSR_VK_DEVICE_FNS(SSR_VK_LOAD)
#undef SSR_VK_LOAD
    vkGetDeviceQueue(c.Device, c.QueueFamily, 0, &c.Queue);
    fprintf(stderr, "[vulkan] %s\n", c.Name.c_str());
    return true;
}
}

Context* Start(std::string& info, bool display, PFN_vkGetInstanceProcAddr getProc)
{
    if (Shared) return Shared;
    Context* c = new Context();
    if (!Make(*c, info, display, getProc))
    {
        // (What was made is kept: a Vulkan that fails part way is not tried again.)
        fprintf(stderr, "[vulkan] cannot start: %s\n", info.c_str());
        return nullptr;
    }
    Shared = c;
    return c;
}

Context* Current() { return Shared; }

int MemoryType(const Context& c, uint32_t allowed, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < c.Memory.memoryTypeCount; i++)
        if ((allowed & (1u << i)) && (c.Memory.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}

bool MakeBuffer(Context& c, Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags want)
{
    FreeBuffer(c, b);
    VkBufferCreateInfo bci { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    if (vkCreateBuffer(c.Device, &bci, nullptr, &b.Handle) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.Device, b.Handle, &req);
    int type = MemoryType(c, req.memoryTypeBits, want);
    // (Memory the processor reads is best cached; where there is none such, any it can map.)
    if (type < 0 && (want & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
        type = MemoryType(c, req.memoryTypeBits, want & ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (type < 0) return false;
    VkMemoryAllocateInfo mai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = (uint32_t)type;
    if (vkAllocateMemory(c.Device, &mai, nullptr, &b.Memory) != VK_SUCCESS) return false;
    vkBindBufferMemory(c.Device, b.Handle, b.Memory, 0);
    b.Size = size;
    const VkMemoryPropertyFlags got = c.Memory.memoryTypes[type].propertyFlags;
    b.Coherent = (got & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    if (got & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) vkMapMemory(c.Device, b.Memory, 0, VK_WHOLE_SIZE, 0, &b.Mapped);
    return true;
}

void FreeBuffer(Context& c, Buffer& b)
{
    if (b.Handle) vkDestroyBuffer(c.Device, b.Handle, nullptr);
    if (b.Memory) vkFreeMemory(c.Device, b.Memory, nullptr);
    b = Buffer();
}

bool MakeImage(Context& c, Image& i, int w, int h, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
    FreeImage(c, i);
    VkImageCreateInfo ici { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(c.Device, &ici, nullptr, &i.Handle) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(c.Device, i.Handle, &req);
    const int type = MemoryType(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type < 0) return false;
    VkMemoryAllocateInfo mai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = (uint32_t)type;
    if (vkAllocateMemory(c.Device, &mai, nullptr, &i.Memory) != VK_SUCCESS) return false;
    vkBindImageMemory(c.Device, i.Handle, i.Memory, 0);
    VkImageViewCreateInfo vci { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = i.Handle;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange = { aspect, 0, 1, 0, 1 };
    if (vkCreateImageView(c.Device, &vci, nullptr, &i.View) != VK_SUCCESS) return false;
    i.Format = format;
    i.W = w;
    i.H = h;
    i.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void FreeImage(Context& c, Image& i)
{
    if (i.View) vkDestroyImageView(c.Device, i.View, nullptr);
    if (i.Handle) vkDestroyImage(c.Device, i.Handle, nullptr);
    if (i.Memory) vkFreeMemory(c.Device, i.Memory, nullptr);
    i = Image();
}

void Transition(VkCommandBuffer cmd, Image& i, VkImageLayout to, VkImageAspectFlags aspect,
    VkPipelineStageFlags fromStage, VkAccessFlags fromAccess, VkPipelineStageFlags toStage, VkAccessFlags toAccess)
{
    VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = fromAccess;
    b.dstAccessMask = toAccess;
    b.oldLayout = i.Layout;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = i.Handle;
    b.subresourceRange = { aspect, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, fromStage, toStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    i.Layout = to;
}

VkShaderModule Shader(Context& c, const uint32_t* code, size_t bytes)
{
    VkShaderModuleCreateInfo sci { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    sci.codeSize = bytes;
    sci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(c.Device, &sci, nullptr, &m);
    return m;
}
}
}
