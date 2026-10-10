// The Vulkan instance and device, and the few resource helpers the renderer needs.
//
// Everything comes through volk, so a machine without a Vulkan driver gets a null Context rather
// than a missing-DLL dialog, and the saver shows black.
#pragma once

#include <volk.h>

#include <cstdint>
#include <memory>

namespace render {

struct Buffer {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
    void*          mapped = nullptr;  // host-visible buffers stay mapped
};

struct Image {
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
    VkFormat       format = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent{};
    std::uint32_t  mips = 1;
};

class Context {
public:
    // Null when there is no usable Vulkan 1.3 device; the reason goes to the log.
    static std::unique_ptr<Context> Create();
    ~Context();

    Context(const Context&)            = delete;
    Context& operator=(const Context&) = delete;

    VkInstance       instance() const { return instance_; }
    VkPhysicalDevice physical() const { return physical_; }
    VkDevice         device() const { return device_; }
    VkQueue          queue() const { return queue_; }
    std::uint32_t    queueFamily() const { return family_; }
    float            maxPointSize() const { return maxPointSize_; }

    Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void   Destroy(Buffer& buffer);

    Image CreateImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, std::uint32_t mips = 1);
    void  Destroy(Image& image);

    // Copies into a device-local buffer through a staging buffer, and waits.
    void Upload(const Buffer& dst, const void* data, VkDeviceSize size);

    // Uploads RGBA8 pixels into mip 0 and generates the rest; leaves the image shader-readable.
    void UploadTexture(const Image& image, const void* rgba);

    // Records with `record` into a one-off command buffer, submits it and waits.
    template <typename F>
    void Submit(F&& record) {
        VkCommandBuffer cmd = BeginOneShot();
        record(cmd);
        EndOneShot(cmd);
    }

private:
    Context() = default;
    bool Init();

    VkCommandBuffer BeginOneShot();
    void            EndOneShot(VkCommandBuffer cmd);
    std::uint32_t   MemoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const;

    VkInstance               instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_ = VK_NULL_HANDLE;
    VkDevice                 device_   = VK_NULL_HANDLE;
    VkQueue                  queue_    = VK_NULL_HANDLE;
    std::uint32_t            family_   = 0;
    VkCommandPool            pool_     = VK_NULL_HANDLE;
    float                    maxPointSize_ = 1.0f;
    VkPhysicalDeviceMemoryProperties memory_{};
};

// Image layout transition with synchronization2; the masks are deliberately broad because these
// run a handful of times per frame.
void Transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                std::uint32_t baseMip = 0, std::uint32_t mipCount = VK_REMAINING_MIP_LEVELS);

}  // namespace render
