// ssrecomp: Vulkan for the widescreen view's polygons (GPU3D_WideGpu_Vk.cpp) and, on the PS5
// title, for its picture (frontend/vkrender.cpp). Every command is reached through a table of
// pointers read with vkGetInstanceProcAddr, so the same code runs with Vulkan's loader on a PC
// (the headless tests, on Mesa's lavapipe) and with RADV linked into the PS5 title, which has
// no loader. Vulkan 1.3: drawing without render pass objects, depth and stencil state set as
// commands.
#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <mutex>
#include <string>

namespace melonDS
{
namespace Vk
{
#define SSR_VK_INSTANCE_FNS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkGetPhysicalDeviceDisplayPropertiesKHR) X(vkGetDisplayModePropertiesKHR) \
    X(vkCreateDisplayPlaneSurfaceKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkDestroySurfaceKHR)

#define SSR_VK_DEVICE_FNS(X) \
    X(vkGetDeviceQueue) X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkCreateBuffer) X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) \
    X(vkMapMemory) X(vkUnmapMemory) X(vkFlushMappedMemoryRanges) X(vkInvalidateMappedMemoryRanges) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
    X(vkResetDescriptorPool) X(vkCreateCommandPool) X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
    X(vkResetCommandBuffer) X(vkQueueSubmit) X(vkCreateFence) X(vkDestroyFence) \
    X(vkWaitForFences) X(vkResetFences) X(vkGetFenceStatus) X(vkCreateSemaphore) \
    X(vkDestroySemaphore) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImageToBuffer) X(vkCmdCopyBuffer) X(vkCmdBeginRendering) X(vkCmdEndRendering) \
    X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDraw) X(vkCmdSetViewport) \
    X(vkCmdSetScissor) X(vkCmdSetDepthCompareOp) X(vkCmdSetDepthWriteEnable) \
    X(vkCmdSetDepthTestEnable) X(vkCmdSetStencilTestEnable) X(vkCmdSetStencilOp) \
    X(vkCmdSetStencilCompareMask) X(vkCmdSetStencilReference) X(vkCmdSetStencilWriteMask) \
    X(vkCmdPushConstants) X(vkCmdBlitImage) X(vkCmdClearColorImage) X(vkCmdBindVertexBuffers) \
    X(vkFreeDescriptorSets) \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) X(vkQueuePresentKHR)

#define SSR_VK_DECLARE(name) extern PFN_##name name;
SSR_VK_INSTANCE_FNS(SSR_VK_DECLARE)
SSR_VK_DEVICE_FNS(SSR_VK_DECLARE)
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
extern PFN_vkCreateInstance vkCreateInstance;
#undef SSR_VK_DECLARE

// The device everything shares: the PS5 title's picture and the widescreen view's polygons
// draw with the same one, on its one queue, which takes one submission at a time (Lock).
struct Context
{
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice Gpu = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    uint32_t QueueFamily = 0;
    VkPhysicalDeviceMemoryProperties Memory {};
    std::string Name;
    std::mutex Lock;
};

// The shared device, made the first time it is asked for: with the display extensions when
// display is true (the PS5 title's picture, VK_KHR_display), else for drawing off the screen
// only. nullptr (and why, in info) if Vulkan cannot start. getProc: where the commands come
// from (RADV's, linked in, on the PS5); nullptr for Vulkan's loader, opened by name.
Context* Start(std::string& info, bool display, PFN_vkGetInstanceProcAddr getProc = nullptr);
Context* Current();

// A memory type of the device with these properties among those allowed, or -1.
int MemoryType(const Context& c, uint32_t allowed, VkMemoryPropertyFlags want);

// A buffer with its own memory; mapped when the memory is the processor's.
struct Buffer
{
    VkBuffer Handle = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    void* Mapped = nullptr;
    bool Coherent = true;
};
bool MakeBuffer(Context& c, Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags want);
void FreeBuffer(Context& c, Buffer& b);

struct Image
{
    VkImage Handle = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    int W = 0, H = 0;
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
bool MakeImage(Context& c, Image& i, int w, int h, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
void FreeImage(Context& c, Image& i);
// A barrier moving the image to a layout, for the stages and accesses given.
void Transition(VkCommandBuffer cmd, Image& i, VkImageLayout to, VkImageAspectFlags aspect,
    VkPipelineStageFlags fromStage, VkAccessFlags fromAccess, VkPipelineStageFlags toStage, VkAccessFlags toAccess);

VkShaderModule Shader(Context& c, const uint32_t* code, size_t bytes);
}
}
