#include "render/context.h"

#include "app/log.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace render {
namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL OnValidation(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    app::Log("vulkan: %s", data->pMessage);
    return VK_FALSE;
}

bool HasLayer(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, name) == 0) return true;
    }
    return false;
}

}  // namespace

std::unique_ptr<Context> Context::Create() {
    if (volkInitialize() != VK_SUCCESS) {
        app::Log("vulkan: no loader (vulkan-1.dll)");
        return nullptr;
    }
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3) {
        app::Log("vulkan: loader older than 1.3");
        return nullptr;
    }
    std::unique_ptr<Context> context(new Context());
    if (!context->Init()) return nullptr;
    return context;
}

bool Context::Init() {
    // SUN_PHOTON_VALIDATION=1 turns on the Khronos validation layer when it is installed.
    const bool validate = GetEnvironmentVariableW(L"SUN_PHOTON_VALIDATION", nullptr, 0) > 0 &&
                          HasLayer("VK_LAYER_KHRONOS_validation");

    std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    std::vector<const char*> layers;
    if (validate) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "The Sun: The Life of a Photon";
    app.apiVersion       = VK_API_VERSION_1_3;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo        = &app;
    info.enabledExtensionCount   = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount       = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames     = layers.data();
    if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
        app::Log("vulkan: vkCreateInstance failed");
        return false;
    }
    volkLoadInstanceOnly(instance_);

    if (validate) {
        VkDebugUtilsMessengerCreateInfoEXT m{};
        m.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        m.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        m.pfnUserCallback = OnValidation;
        vkCreateDebugUtilsMessengerEXT(instance_, &m, nullptr, &messenger_);
        app::Log("vulkan: validation on");
    }

    // A device with Vulkan 1.3 and a queue that can both draw and present to a Win32 window.
    // Discrete GPUs first, but an integrated one is what this saver usually runs on.
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceDriverProperties driver{};
        driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &driver;
        vkGetPhysicalDeviceProperties2(candidate, &props2);
        const VkPhysicalDeviceProperties& props = props2.properties;
        app::Log("vulkan: candidate %s, api %u.%u, driver %s %s", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
                 VK_API_VERSION_MINOR(props.apiVersion), driver.driverName, driver.driverInfo);
        if (props.apiVersion < VK_API_VERSION_1_3) continue;

        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (std::uint32_t f = 0; f < familyCount; ++f) {
            if (!(families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (!vkGetPhysicalDeviceWin32PresentationSupportKHR(candidate, f)) continue;
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                              : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                           : 1;
            if (score > bestScore) {
                bestScore     = score;
                physical_     = candidate;
                family_       = f;
                maxPointSize_ = props.limits.pointSizeRange[1];
            }
            break;
        }
    }
    if (!physical_) {
        app::Log("vulkan: no device with 1.3 and Win32 presentation");
        return false;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory_);
    app::Log("vulkan: %s (driver 0x%x), max point size %.0f", props.deviceName, props.driverVersion, maxPointSize_);

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical_, &supported);

    const float             priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = family_;
    queueInfo.queueCount       = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext               = &features13;
    features.features.largePoints = supported.largePoints;
    if (!supported.largePoints) maxPointSize_ = 1.0f;

    const char*        deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext                   = &features;
    deviceInfo.queueCreateInfoCount    = 1;
    deviceInfo.pQueueCreateInfos       = &queueInfo;
    deviceInfo.enabledExtensionCount   = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    if (vkCreateDevice(physical_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) {
        app::Log("vulkan: vkCreateDevice failed");
        return false;
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, family_, 0, &queue_);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family_;
    return vkCreateCommandPool(device_, &poolInfo, nullptr, &pool_) == VK_SUCCESS;
}

Context::~Context() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

std::uint32_t Context::MemoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    for (std::uint32_t i = 0; i < memory_.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (memory_.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    return 0;
}

Buffer Context::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Buffer             b;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size  = size;
    info.usage = usage;
    vkCreateBuffer(device_, &info, nullptr, &b.buffer);

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, b.buffer, &req);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = req.size;
    alloc.memoryTypeIndex = MemoryType(req.memoryTypeBits, hostVisible
                                                               ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                               : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &alloc, nullptr, &b.memory);
    vkBindBufferMemory(device_, b.buffer, b.memory, 0);
    b.size = size;
    if (hostVisible) vkMapMemory(device_, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);
    return b;
}

void Context::Destroy(Buffer& b) {
    if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
    b = Buffer{};
}

Image Context::CreateImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, std::uint32_t mips) {
    Image             img;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType   = VK_IMAGE_TYPE_2D;
    info.format      = format;
    info.extent      = {extent.width, extent.height, 1};
    info.mipLevels   = mips;
    info.arrayLayers = 1;
    info.samples     = VK_SAMPLE_COUNT_1_BIT;
    info.tiling      = VK_IMAGE_TILING_OPTIMAL;
    info.usage       = usage;
    vkCreateImage(device_, &info, nullptr, &img.image);

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, img.image, &req);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = req.size;
    alloc.memoryTypeIndex = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &alloc, nullptr, &img.memory);
    vkBindImageMemory(device_, img.image, img.memory, 0);

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image            = img.image;
    view.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    view.format           = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
    vkCreateImageView(device_, &view, nullptr, &img.view);

    img.format = format;
    img.extent = extent;
    img.mips   = mips;
    return img;
}

void Context::Destroy(Image& img) {
    if (img.view) vkDestroyImageView(device_, img.view, nullptr);
    if (img.image) vkDestroyImage(device_, img.image, nullptr);
    if (img.memory) vkFreeMemory(device_, img.memory, nullptr);
    img = Image{};
}

VkCommandBuffer Context::BeginOneShot() {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = pool_;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd      = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &alloc, &cmd);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    return cmd;
}

void Context::EndOneShot(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, pool_, 1, &cmd);
}

void Context::Upload(const Buffer& dst, const void* data, VkDeviceSize size) {
    Buffer staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, data, size);
    Submit([&](VkCommandBuffer cmd) {
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &region);
    });
    Destroy(staging);
}

void Context::UploadTexture(const Image& img, const void* rgba) {
    const VkDeviceSize size    = VkDeviceSize(img.extent.width) * img.extent.height * 4;
    Buffer             staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, rgba, size);

    Submit([&](VkCommandBuffer cmd) {
        Transition(cmd, img.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent      = {img.extent.width, img.extent.height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        std::int32_t w = static_cast<std::int32_t>(img.extent.width);
        std::int32_t h = static_cast<std::int32_t>(img.extent.height);
        for (std::uint32_t mip = 1; mip < img.mips; ++mip) {
            Transition(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mip - 1, 1);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
            blit.srcOffsets[1]  = {w, h, 1};
            w                   = w > 1 ? w / 2 : 1;
            h                   = h > 1 ? h / 2 : 1;
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            blit.dstOffsets[1]  = {w, h, 1};
            vkCmdBlitImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        }
        if (img.mips > 1) {
            Transition(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, img.mips - 1);
        }
        Transition(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, img.mips - 1, 1);
    });
    Destroy(staging);
}

void Transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, std::uint32_t baseMip,
                std::uint32_t mipCount) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask    = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask    = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout        = from;
    barrier.newLayout        = to;
    barrier.image            = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, 1};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

}  // namespace render
