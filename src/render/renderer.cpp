#include "render/renderer.h"

#include "app/log.h"
#include "render/context.h"
#include "render/image_io.h"
#include "render/overlay.h"
#include "render/shaders_embedded.h"
#include "sim/system.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace render {
namespace {

constexpr std::uint32_t kFramesInFlight  = 2;
constexpr VkFormat      kHdrFormat       = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr std::uint32_t kBloomMips       = 7;
constexpr std::uint32_t kOverlayVertices = 32768;  // the photon's path at its longest uses ~6,000
constexpr std::uint32_t kParticles       = 790000;  // must match the ranges in shaders/particles.vert

// GPU timestamps around each pass of the first window, averaged into the log: the profile that
// decides between code paths. Stamps: start, scene, particles, bloom, tonemap and overlay.
constexpr std::uint32_t kStamps = 5;
const char* const       kPassNames[kStamps - 1] = {"scene", "particles", "bloom", "tonemap"};

// Must match `Frame` in shaders/common.glsl (std140: only vec4 and mat4 members).
struct FrameUniforms {
    float viewProj[16];
    float camPos[4];
    float camFwd[4];
    float camRight[4];
    float camUp[4];
    float viewport[4];
    float render[4];
    float sun[4];
    float sun2[4];
    float planets[sim::kPlanetCount][4];
    float planetSpin[sim::kPlanetCount][4];
    float eruptions[sim::kEruptionCount][4];
    float eruptInfo[sim::kEruptionCount][4];
    float moons[sim::kMoonCount][4];
    float moonOrbit[sim::kMoonCount][4];
    float moonShape[sim::kMoonCount][4];
    float moonInfo[sim::kMoonCount][4];
};

enum class Blend { None, Additive, Alpha };

struct Target {
    HWND           hwnd    = nullptr;
    VkSurfaceKHR   surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat       format  = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent{};
    bool           canCapture = false;
    bool           stale      = true;
    // No presentable surface (or SUN_PHOTON_OFFSCREEN): draw into `output` instead and never
    // present. Captures and benchmarks still work, and the loop runs unthrottled.
    bool           offscreen  = false;
    Image          output;

    std::vector<VkImage>     images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> rendered;  // one per swapchain image, signalled when its frame is drawn
    VkSemaphore              acquired[kFramesInFlight]{};

    Image       hdr;                       // scene radiance, with a mip chain for the bloom
    VkImageView hdrTarget = VK_NULL_HANDLE;  // mip 0 alone, for rendering into

    VkPipeline      tonemap       = VK_NULL_HANDLE;
    VkFormat        tonemapFormat = VK_FORMAT_UNDEFINED;
    Buffer          uniforms[kFramesInFlight];
    Buffer          overlay[kFramesInFlight];  // this frame's overlay quads
    std::uint32_t   overlayCount = 0;
    VkPipeline      overlayPipeline = VK_NULL_HANDLE;
    VkDescriptorSet sets[kFramesInFlight]{};

    std::uint32_t imageIndex = 0;
    bool          drawing    = false;  // acquired an image this frame

    std::wstring captureTo;
    Buffer       readback;
};

class VulkanRenderer final : public Renderer {
public:
    explicit VulkanRenderer(std::unique_ptr<Context> context) : ctx_(std::move(context)) {}
    ~VulkanRenderer() override;

    bool Init();

    bool Attach(HWND hwnd) override;
    void Detach(HWND hwnd) override;
    void Resized(HWND hwnd) override;
    void RenderFrame(double seconds) override;
    void RequestCapture(HWND hwnd, const std::wstring& path) override;
    bool CapturePending() const override;
    void WaitIdle() override { vkDeviceWaitIdle(ctx_->device()); }

private:
    VkShaderModule Module(const char* name);
    VkPipeline     MakePipeline(const char* vert, const char* frag, VkFormat format, Blend blend,
                                VkPrimitiveTopology topology);
    bool           BuildSwapchain(Target& t);
    bool           BuildOffscreen(Target& t);
    void           BuildFrameResources(Target& t);
    void           DestroySwapchain(Target& t);
    void           DestroyTarget(Target& t);
    void           FillUniforms(const Target& t, FrameUniforms& u, OverlayView& o) const;
    void           Record(VkCommandBuffer cmd, Target& t, bool first);
    void           FinishCaptures();
    void           BuildOverlay(Target& t, const OverlayView& view);
    void           Stamp(VkCommandBuffer cmd, bool first, std::uint32_t index);
    void           CollectStamps();
    Image          LoadTexture(int resourceId);

    std::unique_ptr<Context> ctx_;

    VkDescriptorSetLayout setLayout_  = VK_NULL_HANDLE;
    VkPipelineLayout      layout_     = VK_NULL_HANDLE;
    VkDescriptorPool      pool_       = VK_NULL_HANDLE;
    VkSampler             clamp_      = VK_NULL_HANDLE;
    VkSampler             wrap_       = VK_NULL_HANDLE;
    VkPipeline            scene_      = VK_NULL_HANDLE;
    VkPipeline            particles_  = VK_NULL_HANDLE;
    std::vector<std::pair<std::string, VkShaderModule>> modules_;

    Image                      textAtlas_;
    Image                      maps_[MAP_COUNT];
    std::unique_ptr<Overlay>   overlays_;
    std::vector<OverlayVertex> overlayScratch_;
    sim::Moment                moment_;  // this frame's
    float                      particleStrength_ = 0.0f;

    VkCommandPool   framePool_ = VK_NULL_HANDLE;
    VkCommandBuffer commands_[kFramesInFlight]{};
    VkFence         fences_[kFramesInFlight]{};
    VkQueryPool     queries_ = VK_NULL_HANDLE;
    bool            stamped_[kFramesInFlight]{};
    double          passMs_[kStamps - 1]{};
    std::uint32_t   passFrames_ = 0;
    float           tickNs_     = 1.0f;
    std::uint32_t   frame_      = 0;
    std::uint64_t   frameCount_ = 0;

    std::vector<std::unique_ptr<Target>> targets_;
};

// ---- setup ---------------------------------------------------------------------------------

VkShaderModule VulkanRenderer::Module(const char* name) {
    for (const auto& [cached, module] : modules_) {
        if (cached == name) return module;
    }
    const ShaderBlob* blob = shader_blob(name);
    if (!blob) {
        app::Log("shader %s missing", name);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = blob->size;
    info.pCode    = reinterpret_cast<const std::uint32_t*>(blob->data);
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(ctx_->device(), &info, nullptr, &module);
    modules_.emplace_back(name, module);
    return module;
}

VkPipeline VulkanRenderer::MakePipeline(const char* vert, const char* frag, VkFormat format, Blend blend,
                                        VkPrimitiveTopology topology) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = Module(vert);
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = Module(frag);
    stages[1].pName  = "main";
    if (!stages[0].module || !stages[1].module) return VK_NULL_HANDLE;

    VkPipelineVertexInputStateCreateInfo   vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = topology;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.lineWidth   = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;
    if (blend != Blend::None) {
        attachment.blendEnable  = VK_TRUE;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        if (blend == Blend::Alpha) {  // the overlays, over the finished image
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &attachment;

    const VkDynamicState             dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount    = 1;
    rendering.pColorAttachmentFormats = &format;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext               = &rendering;
    info.stageCount          = 2;
    info.pStages             = stages;
    info.pVertexInputState   = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState      = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState   = &multisample;
    info.pDepthStencilState  = &depth;
    info.pColorBlendState    = &colorBlend;
    info.pDynamicState       = &dynamic;
    info.layout              = layout_;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx_->device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        app::Log("pipeline %s + %s failed", vert, frag);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

Image VulkanRenderer::LoadTexture(int resourceId) {
    Pixels pixels = LoadResourceImage(resourceId);
    if (pixels.rgba.empty()) {
        pixels.width  = 1;
        pixels.height = 1;
        pixels.rgba.assign(4, 0);
    }
    std::uint32_t mips = 1;
    while ((std::max(pixels.width, pixels.height) >> mips) > 0) ++mips;
    Image image = ctx_->CreateImage({pixels.width, pixels.height}, VK_FORMAT_R8G8B8A8_SRGB,
                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    mips);
    ctx_->UploadTexture(image, pixels.rgba.data());
    app::Log("map %d: %ux%u, %u mips", resourceId, pixels.width, pixels.height, mips);
    return image;
}

bool VulkanRenderer::Init() {
    VkDevice device = ctx_->device();

    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // the HDR scene
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},            // overlay quads
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // the text atlas
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAP_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // planet maps
    };
    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = 5;
    setInfo.pBindings    = bindings;
    vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout_);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &setLayout_;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout_);

    constexpr std::uint32_t    kMaxSets = 64;  // windows x frames in flight; plenty for any desk
    const VkDescriptorPoolSize sizes[]  = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxSets},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxSets},
                                           {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxSets * (2 + MAP_COUNT)}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets       = kMaxSets;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes    = sizes;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_);

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter    = VK_FILTER_LINEAR;
    sampler.minFilter    = VK_FILTER_LINEAR;
    sampler.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod       = VK_LOD_CLAMP_NONE;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &sampler, nullptr, &clamp_);
    // Planet maps wrap in longitude, clamp at the poles; anisotropy is off so the limb softens a little.
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(device, &sampler, nullptr, &wrap_);

    scene_     = MakePipeline("fullscreen.vert", "scene.frag", kHdrFormat, Blend::None, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    particles_ = MakePipeline("particles.vert", "particles.frag", kHdrFormat, Blend::Additive,
                              VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    if (!scene_ || !particles_) return false;

    // Overlay text, set once in Michroma. Without it (no font, GDI trouble) the overlays are off,
    // but a 1x1 texture still fills the binding.
    // The planet maps, decoded from the .scr. A map that fails to load is a 1x1 black texture, and
    // the shader falls back to the procedural surface where a map is black.
    for (int i = 0; i < MAP_COUNT; ++i) maps_[i] = LoadTexture(IDR_MAP_FIRST + i);

    TextAtlas atlas = BuildTextAtlas();
    const bool haveText = !atlas.rgba.empty();
    if (!haveText) {
        atlas.width = atlas.height = 1;
        atlas.rgba.assign(4, 0);
    }
    std::uint32_t atlasMips = 1;
    while ((std::max(atlas.width, atlas.height) >> atlasMips) > 0) ++atlasMips;
    textAtlas_ = ctx_->CreateImage({atlas.width, atlas.height}, VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   atlasMips);
    ctx_->UploadTexture(textAtlas_, atlas.rgba.data());
    if (haveText) overlays_ = std::make_unique<Overlay>(atlas);
    app::Log("particles: %u, placed on the GPU from the loop time", kParticles);

    VkCommandPoolCreateInfo cmdPool{};
    cmdPool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPool.queueFamilyIndex = ctx_->queueFamily();
    vkCreateCommandPool(device, &cmdPool, nullptr, &framePool_);
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = framePool_;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = kFramesInFlight;
    vkAllocateCommandBuffers(device, &alloc, commands_);
    for (auto& fence : fences_) {
        VkFenceCreateInfo f{};
        f.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        f.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &f, nullptr, &fence);
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx_->physical(), &props);
    if (props.limits.timestampComputeAndGraphics) {
        tickNs_ = props.limits.timestampPeriod;
        VkQueryPoolCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        q.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        q.queryCount = kStamps * kFramesInFlight;
        vkCreateQueryPool(device, &q, nullptr, &queries_);
    }
    return true;
}

void VulkanRenderer::Stamp(VkCommandBuffer cmd, bool first, std::uint32_t index) {
    if (!first || !queries_) return;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queries_, frame_ * kStamps + index);
}

// Called once this frame slot's fence has signalled, so its stamps are ready.
void VulkanRenderer::CollectStamps() {
    if (!queries_ || !stamped_[frame_]) return;
    std::uint64_t ticks[kStamps]{};
    if (vkGetQueryPoolResults(ctx_->device(), queries_, frame_ * kStamps, kStamps, sizeof(ticks), ticks,
                              sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
        return;
    }
    for (std::uint32_t i = 0; i + 1 < kStamps; ++i) passMs_[i] += double(ticks[i + 1] - ticks[i]) * tickNs_ * 1e-6;
    if (++passFrames_ == 300) {
        char line[256];
        int  n = 0;
        double total = 0.0;
        for (std::uint32_t i = 0; i + 1 < kStamps; ++i) {
            n += std::snprintf(line + n, sizeof(line) - n, " %s %.2f", kPassNames[i], passMs_[i] / passFrames_);
            total += passMs_[i] / passFrames_;
        }
        app::Log("gpu ms (%ux%u):%s, total %.2f", targets_.empty() ? 0 : targets_.front()->extent.width,
                 targets_.empty() ? 0 : targets_.front()->extent.height, line, total);
        for (double& ms : passMs_) ms = 0.0;
        passFrames_ = 0;
    }
}

VulkanRenderer::~VulkanRenderer() {
    VkDevice device = ctx_->device();
    vkDeviceWaitIdle(device);
    for (auto& t : targets_) DestroyTarget(*t);
    targets_.clear();
    for (auto fence : fences_) vkDestroyFence(device, fence, nullptr);
    if (queries_) vkDestroyQueryPool(device, queries_, nullptr);
    if (framePool_) vkDestroyCommandPool(device, framePool_, nullptr);
    ctx_->Destroy(textAtlas_);
    for (Image& map : maps_) ctx_->Destroy(map);
    for (VkPipeline p : {scene_, particles_}) {
        if (p) vkDestroyPipeline(device, p, nullptr);
    }
    for (auto& [name, m] : modules_) vkDestroyShaderModule(device, m, nullptr);
    vkDestroySampler(device, clamp_, nullptr);
    vkDestroySampler(device, wrap_, nullptr);
    vkDestroyDescriptorPool(device, pool_, nullptr);
    vkDestroyPipelineLayout(device, layout_, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
}

// ---- windows -------------------------------------------------------------------------------

bool VulkanRenderer::Attach(HWND hwnd) {
    auto t  = std::make_unique<Target>();
    t->hwnd = hwnd;

    t->offscreen = GetEnvironmentVariableW(L"SUN_PHOTON_OFFSCREEN", nullptr, 0) > 0;
    if (!t->offscreen) {
        VkWin32SurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hinstance = GetModuleHandleW(nullptr);
        surfaceInfo.hwnd      = hwnd;
        VkBool32 supported    = VK_FALSE;
        if (vkCreateWin32SurfaceKHR(ctx_->instance(), &surfaceInfo, nullptr, &t->surface) == VK_SUCCESS) {
            vkGetPhysicalDeviceSurfaceSupportKHR(ctx_->physical(), ctx_->queueFamily(), t->surface, &supported);
        }
        if (!supported) t->offscreen = true;
    }

    VkDevice device = ctx_->device();
    for (std::uint32_t f = 0; f < kFramesInFlight; ++f) {
        VkSemaphoreCreateInfo s{};
        s.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(device, &s, nullptr, &t->acquired[f]);
        t->uniforms[f] = ctx_->CreateBuffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        t->overlay[f]  = ctx_->CreateBuffer(kOverlayVertices * sizeof(OverlayVertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool     = pool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts        = &setLayout_;
        if (vkAllocateDescriptorSets(device, &alloc, &t->sets[f]) != VK_SUCCESS) {
            DestroyTarget(*t);
            return false;
        }
    }
    if (!BuildSwapchain(*t)) app::Log("window %p: no swapchain yet", static_cast<void*>(hwnd));
    targets_.push_back(std::move(t));
    return true;
}

void VulkanRenderer::Detach(HWND hwnd) {
    for (auto it = targets_.begin(); it != targets_.end(); ++it) {
        if ((*it)->hwnd == hwnd) {
            vkDeviceWaitIdle(ctx_->device());
            DestroyTarget(**it);
            targets_.erase(it);
            return;
        }
    }
}

void VulkanRenderer::Resized(HWND hwnd) {
    for (auto& t : targets_) {
        if (t->hwnd == hwnd) t->stale = true;
    }
}

bool VulkanRenderer::BuildSwapchain(Target& t) {
    VkDevice device = ctx_->device();
    vkDeviceWaitIdle(device);

    if (t.offscreen) return BuildOffscreen(t);

    VkSurfaceCapabilitiesKHR caps{};
    if (const VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_->physical(), t.surface, &caps); r != VK_SUCCESS) {
        // Seen from some non-interactive sessions: the surface exists but can't be queried.
        app::Log("window %p: surface capabilities failed (%d), drawing offscreen", static_cast<void*>(t.hwnd), r);
        t.offscreen = true;
        return BuildOffscreen(t);
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        RECT rc{};
        GetClientRect(t.hwnd, &rc);
        extent = {static_cast<std::uint32_t>(rc.right), static_cast<std::uint32_t>(rc.bottom)};
    }
    if (extent.width == 0 || extent.height == 0) {
        RECT rc{};
        GetClientRect(t.hwnd, &rc);
        app::Log("window %p: zero surface extent (client %ldx%ld, iconic %d, visible %d)", static_cast<void*>(t.hwnd),
                 rc.right, rc.bottom, IsIconic(t.hwnd), IsWindowVisible(t.hwnd));
        return false;  // minimised; try again later
    }

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_->physical(), t.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_->physical(), t.surface, &formatCount, formats.data());
    // A UNORM format, so the tonemap's own sRGB encoding is what reaches the screen and the capture.
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }

    t.canCapture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface          = t.surface;
    info.minImageCount    = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount) info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
    info.imageFormat      = chosen.format;
    info.imageColorSpace  = chosen.colorSpace;
    info.imageExtent      = extent;
    info.imageArrayLayers = 1;
    info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (t.canCapture ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform     = caps.currentTransform;
    info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode      = VK_PRESENT_MODE_FIFO_KHR;  // vsync: smooth, and it paces the loop
    info.clipped          = VK_TRUE;
    info.oldSwapchain     = t.swapchain;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if (const VkResult r = vkCreateSwapchainKHR(device, &info, nullptr, &swapchain); r != VK_SUCCESS) {
        app::Log("window %p: vkCreateSwapchainKHR %ux%u format %d failed: %d", static_cast<void*>(t.hwnd), extent.width,
                 extent.height, chosen.format, r);
        return false;
    }
    DestroySwapchain(t);
    t.swapchain = swapchain;
    t.format    = chosen.format;
    t.extent    = extent;

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    t.images.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, t.images.data());
    for (VkImage image : t.images) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image            = image;
        view.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        view.format           = t.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v         = VK_NULL_HANDLE;
        vkCreateImageView(device, &view, nullptr, &v);
        t.views.push_back(v);
        VkSemaphoreCreateInfo s{};
        s.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore           sem = VK_NULL_HANDLE;
        vkCreateSemaphore(device, &s, nullptr, &sem);
        t.rendered.push_back(sem);
    }

    BuildFrameResources(t);
    app::Log("window %p: swapchain %ux%u, format %d, %u images", static_cast<void*>(t.hwnd), extent.width,
             extent.height, t.format, count);
    return true;
}

bool VulkanRenderer::BuildOffscreen(Target& t) {
    vkDeviceWaitIdle(ctx_->device());
    RECT rc{};
    GetClientRect(t.hwnd, &rc);
    VkExtent2D extent{static_cast<std::uint32_t>(rc.right), static_cast<std::uint32_t>(rc.bottom)};
    if (extent.width == 0 || extent.height == 0) extent = {1600, 900};

    DestroySwapchain(t);
    t.output = ctx_->CreateImage(extent, VK_FORMAT_B8G8R8A8_UNORM,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    t.format     = VK_FORMAT_B8G8R8A8_UNORM;
    t.extent     = extent;
    t.canCapture = true;
    t.images     = {t.output.image};
    t.views      = {t.output.view};
    BuildFrameResources(t);
    app::Log("window %p: offscreen %ux%u", static_cast<void*>(t.hwnd), extent.width, extent.height);
    return true;
}

// What every target needs whatever it draws into: the HDR scene, its tonemap and descriptors.
void VulkanRenderer::BuildFrameResources(Target& t) {
    VkDevice         device = ctx_->device();
    const VkExtent2D extent = t.extent;

    // The bloom samples mips 1..6 of the scene; small windows get as many as fit.
    std::uint32_t mips = 1;
    while (mips < kBloomMips && (std::min(extent.width, extent.height) >> mips) >= 4) ++mips;
    t.hdr = ctx_->CreateImage(extent, kHdrFormat,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              mips);
    VkImageViewCreateInfo target{};
    target.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    target.image            = t.hdr.image;
    target.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    target.format           = kHdrFormat;
    target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device, &target, nullptr, &t.hdrTarget);

    if (t.tonemapFormat != t.format) {
        if (t.tonemap) vkDestroyPipeline(device, t.tonemap, nullptr);
        t.tonemap       = MakePipeline("fullscreen.vert", "tonemap.frag", t.format, Blend::None,
                                       VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        if (t.overlayPipeline) vkDestroyPipeline(device, t.overlayPipeline, nullptr);
        t.overlayPipeline = MakePipeline("overlay.vert", "overlay.frag", t.format, Blend::Alpha,
                                         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        t.tonemapFormat = t.format;
    }

    for (std::uint32_t f = 0; f < kFramesInFlight; ++f) {
        VkDescriptorBufferInfo uniforms{t.uniforms[f].buffer, 0, sizeof(FrameUniforms)};
        VkDescriptorImageInfo  hdr{clamp_, t.hdr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo overlay{t.overlay[f].buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo  text{clamp_, textAtlas_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkWriteDescriptorSet writes[5]{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = t.sets[f];
            writes[i].dstBinding      = i;
            writes[i].descriptorCount = 1;
        }
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo    = &uniforms;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo     = &hdr;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo    = &overlay;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].pImageInfo     = &text;
        VkDescriptorImageInfo maps[MAP_COUNT];
        for (int i = 0; i < MAP_COUNT; ++i) maps[i] = {wrap_, maps_[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet mapWrite{};
        mapWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        mapWrite.dstSet          = t.sets[f];
        mapWrite.dstBinding      = 4;
        mapWrite.descriptorCount = MAP_COUNT;
        mapWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        mapWrite.pImageInfo      = maps;
        writes[4] = mapWrite;
        vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
    }

    t.stale = false;
}

void VulkanRenderer::DestroySwapchain(Target& t) {
    VkDevice device = ctx_->device();
    if (t.output.image) {
        ctx_->Destroy(t.output);  // owns the one view in `views`
    } else {
        for (VkImageView v : t.views) vkDestroyImageView(device, v, nullptr);
    }
    for (VkSemaphore s : t.rendered) vkDestroySemaphore(device, s, nullptr);
    t.views.clear();
    t.rendered.clear();
    t.images.clear();
    if (t.hdrTarget) vkDestroyImageView(device, t.hdrTarget, nullptr);
    t.hdrTarget = VK_NULL_HANDLE;
    ctx_->Destroy(t.hdr);
    if (t.swapchain) vkDestroySwapchainKHR(device, t.swapchain, nullptr);
    t.swapchain = VK_NULL_HANDLE;
}

void VulkanRenderer::DestroyTarget(Target& t) {
    VkDevice device = ctx_->device();
    DestroySwapchain(t);
    if (t.tonemap) vkDestroyPipeline(device, t.tonemap, nullptr);
    if (t.overlayPipeline) vkDestroyPipeline(device, t.overlayPipeline, nullptr);
    for (std::uint32_t f = 0; f < kFramesInFlight; ++f) {
        if (t.acquired[f]) vkDestroySemaphore(device, t.acquired[f], nullptr);
        ctx_->Destroy(t.uniforms[f]);
        ctx_->Destroy(t.overlay[f]);
        if (t.sets[f]) vkFreeDescriptorSets(device, pool_, 1, &t.sets[f]);
    }
    ctx_->Destroy(t.readback);
    if (t.surface) vkDestroySurfaceKHR(ctx_->instance(), t.surface, nullptr);
}

// ---- per frame -----------------------------------------------------------------------------

void VulkanRenderer::FillUniforms(const Target& t, FrameUniforms& u, OverlayView& o) const {
    const sim::Moment&    m      = moment_;
    const sim::CameraPose& pose  = m.camera;
    const float           aspect = float(t.extent.width) / float(t.extent.height);
    const sim::Mat4       view   = sim::LookAt(pose);
    const sim::Mat4       proj   = sim::Perspective(sim::kFovY, aspect, 0.002f, 2000.0f);
    const sim::Mat4       vp     = sim::Multiply(proj, view);
    std::memcpy(u.viewProj, vp.m, sizeof(u.viewProj));

    // The camera's own axes, for the scene shader's rays: exact at any distance, where unprojecting
    // through a matrix with a 1:10^6 depth range would not be.
    const sim::Vec3 f     = sim::Normalize(pose.forward);
    const sim::Vec3 right = sim::Normalize(sim::Cross(f, pose.up));
    const sim::Vec3 up    = sim::Cross(right, f);
    const float     tanH  = std::tan(0.5f * sim::kFovY);
    auto put = [](float* dst, sim::Vec3 v, float w) {
        dst[0] = v.x;
        dst[1] = v.y;
        dst[2] = v.z;
        dst[3] = w;
    };
    put(u.camPos, pose.eye, m.t);
    put(u.camFwd, f, tanH);
    put(u.camRight, right, aspect);
    put(u.camUp, up, 0.0f);

    u.viewport[0] = float(t.extent.width);
    u.viewport[1] = float(t.extent.height);
    u.viewport[2] = 1.0f / float(t.extent.width);
    u.viewport[3] = 1.0f / float(t.extent.height);

    const float focal = 0.5f * float(t.extent.height) / tanH;
    u.render[0] = 1.0f;  // exposure
    u.render[1] = focal;
    u.render[2] = static_cast<float>(frameCount_ % 100000);
    u.render[3] = ctx_->maxPointSize();

    // The Sun's radius on screen, in pixels.
    const float dist  = std::max(sim::Length(pose.eye), 1.001f);
    const float sunPx = focal * std::tan(std::asin(1.0f / dist));
    u.sun[0]  = m.sunAngle;
    u.sun[1]  = m.wedge.halfAngle;
    u.sun[2]  = m.wedge.centre;
    u.sun[3]  = m.wedge.open;
    u.sun2[0] = particleStrength_;
    u.sun2[1] = static_cast<float>(m.stage);
    u.sun2[2] = sunPx;
    u.sun2[3] = m.sunRate;

    const sim::Planet* planets = sim::Planets();
    for (int i = 0; i < sim::kPlanetCount; ++i) {
        const sim::Planet& p = planets[i];
        put(u.planets[i], p.position, p.radius);
        u.planetSpin[i][0] = p.tiltDeg * sim::kPi / 180.0f;
        u.planetSpin[i][1] = sim::kTwoPi * m.t / p.dayLoopSeconds;
        u.planetSpin[i][2] = p.ringInner;
        u.planetSpin[i][3] = p.ringOuter;
    }
    for (int i = 0; i < sim::kEruptionCount; ++i) {
        const sim::Eruption& e = m.eruptions[i];
        put(u.eruptions[i], e.dir, e.age);
        u.eruptInfo[i][0] = e.seed;
        u.eruptInfo[i][1] = e.scale;
        u.eruptInfo[i][2] = sim::Timing().eruptionLength;
        u.eruptInfo[i][3] = 0.0f;
    }
    sim::MoonState moons[sim::kMoonCount];
    sim::Moons(m.t, moons);
    for (int k = 0; k < sim::kMoonCount; ++k) {
        const sim::MoonState& s = moons[k];
        put(u.moons[k], s.position, s.radius);
        put(u.moonOrbit[k], s.normal, static_cast<float>(s.parent));
        put(u.moonShape[k], s.axes, static_cast<float>(static_cast<int>(s.look)));
        u.moonInfo[k][0] = s.brightness;
        u.moonInfo[k][1] = u.moonInfo[k][2] = u.moonInfo[k][3] = 0.0f;
    }

    o.viewProj = vp;
    o.width    = float(t.extent.width);
    o.height   = float(t.extent.height);
    o.focal    = focal;
}

void VulkanRenderer::Record(VkCommandBuffer cmd, Target& t, bool first) {
    const VkViewport viewport{0, 0, float(t.extent.width), float(t.extent.height), 0, 1};
    const VkRect2D   scissor{{0, 0}, t.extent};

    if (first && queries_) vkCmdResetQueryPool(cmd, queries_, frame_ * kStamps, kStamps);
    Stamp(cmd, first, 0);

    // 1. The scene, in linear HDR: the ray-traced sky, planets and Sun, then the particles.
    Transition(cmd, t.hdr.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 1);
    VkRenderingAttachmentInfo scene{};
    scene.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    scene.imageView   = t.hdrTarget;
    scene.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    scene.loadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // the scene shader covers every pixel
    scene.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    pass.renderArea           = scissor;
    pass.layerCount           = 1;
    pass.colorAttachmentCount = 1;
    pass.pColorAttachments    = &scene;

    vkCmdBeginRendering(cmd, &pass);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &t.sets[frame_], 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    Stamp(cmd, first, 1);
    // Far off, the Sun is a few pixels and its particles would only pile up into a hot speck.
    if (particleStrength_ > 0.0f) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particles_);
        vkCmdDraw(cmd, kParticles, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);
    Stamp(cmd, first, 2);

    // 3. A mip chain of the scene for the bloom, by successive linear blits.
    Transition(cmd, t.hdr.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 1);
    std::int32_t w = std::int32_t(t.extent.width), h = std::int32_t(t.extent.height);
    for (std::uint32_t mip = 1; mip < t.hdr.mips; ++mip) {
        Transition(cmd, t.hdr.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
        blit.srcOffsets[1]  = {w, h, 1};
        w                   = std::max(w / 2, 1);
        h                   = std::max(h / 2, 1);
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
        blit.dstOffsets[1]  = {w, h, 1};
        vkCmdBlitImage(cmd, t.hdr.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.hdr.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        Transition(cmd, t.hdr.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mip, 1);
    }
    Transition(cmd, t.hdr.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Stamp(cmd, first, 3);

    // 4. Tonemap into the swapchain image.
    VkImage image = t.images[t.imageIndex];
    Transition(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo out{};
    out.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    out.imageView   = t.views[t.imageIndex];
    out.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    out.loadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    out.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    pass.pColorAttachments = &out;
    vkCmdBeginRendering(cmd, &pass);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, t.tonemap);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    if (t.overlayCount && t.overlayPipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, t.overlayPipeline);
        vkCmdDraw(cmd, t.overlayCount, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);
    Stamp(cmd, first, 4);

    // 5. A capture copies the finished image out before it is presented.
    const VkImageLayout finalLayout =
        t.offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if (!t.captureTo.empty() && t.canCapture) {
        const VkDeviceSize bytes = VkDeviceSize(t.extent.width) * t.extent.height * 4;
        if (t.readback.size < bytes) {
            ctx_->Destroy(t.readback);
            t.readback = ctx_->CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        }
        Transition(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent      = {t.extent.width, t.extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.readback.buffer, 1, &copy);
        if (!t.offscreen) Transition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, finalLayout);
    } else {
        Transition(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, finalLayout);
    }
}

void VulkanRenderer::RenderFrame(double seconds) {
    VkDevice device = ctx_->device();
    vkWaitForFences(device, 1, &fences_[frame_], VK_TRUE, UINT64_MAX);
    CollectStamps();
    stamped_[frame_] = false;

    std::vector<VkSemaphore>          waits;
    std::vector<VkPipelineStageFlags> waitStages;
    std::vector<VkSemaphore>          signals;
    std::vector<VkSwapchainKHR>       swapchains;
    std::vector<std::uint32_t>        indices;

    bool offscreen = false;
    // One moment for every window: the same instant on each monitor.
    moment_ = sim::At(static_cast<float>(seconds));
    {
        const float dist  = std::max(sim::Length(moment_.camera.eye), 1.001f);
        const float angle = std::asin(1.0f / dist);  // the Sun's angular radius
        // Particles fade in as the Sun grows past Mercury, from 6% to 20% of the screen's height in radius.
        particleStrength_ = sim::SmoothStep(0.06f, 0.20f, angle / sim::kFovY);
    }
    OverlayView view;
    for (auto& t : targets_) {
        t->drawing = false;
        if (t->stale && !BuildSwapchain(*t)) continue;
        if (t->offscreen) {
            t->imageIndex = 0;
            t->drawing    = true;
            offscreen     = true;
            FrameUniforms uniforms{};
            FillUniforms(*t, uniforms, view);
            std::memcpy(t->uniforms[frame_].mapped, &uniforms, sizeof(uniforms));
            BuildOverlay(*t, view);
            continue;
        }
        const VkResult r = vkAcquireNextImageKHR(device, t->swapchain, UINT64_MAX, t->acquired[frame_],
                                                 VK_NULL_HANDLE, &t->imageIndex);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            t->stale = true;
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) continue;
        t->drawing = true;

        FrameUniforms uniforms{};
        FillUniforms(*t, uniforms, view);
        std::memcpy(t->uniforms[frame_].mapped, &uniforms, sizeof(uniforms));
        BuildOverlay(*t, view);

        waits.push_back(t->acquired[frame_]);
        waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        signals.push_back(t->rendered[t->imageIndex]);
        swapchains.push_back(t->swapchain);
        indices.push_back(t->imageIndex);
    }

    if (swapchains.empty() && !offscreen) {
        Sleep(16);  // every window minimised or gone: idle at about the frame rate
        return;
    }

    vkResetFences(device, 1, &fences_[frame_]);
    VkCommandBuffer cmd = commands_[frame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    bool first = true;
    for (auto& t : targets_) {
        if (!t->drawing) continue;
        Record(cmd, *t, first);
        stamped_[frame_] |= first && queries_;
        first = false;
    }
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = static_cast<std::uint32_t>(waits.size());
    submit.pWaitSemaphores      = waits.data();
    submit.pWaitDstStageMask    = waitStages.data();
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = static_cast<std::uint32_t>(signals.size());
    submit.pSignalSemaphores    = signals.data();
    vkQueueSubmit(ctx_->queue(), 1, &submit, fences_[frame_]);

    if (swapchains.empty()) {  // offscreen only: nothing to present
        FinishCaptures();
        frame_ = (frame_ + 1) % kFramesInFlight;
        ++frameCount_;
        return;
    }

    // All windows present together, so several monitors stay in step.
    std::vector<VkResult> results(swapchains.size());
    VkPresentInfoKHR      present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = static_cast<std::uint32_t>(signals.size());
    present.pWaitSemaphores    = signals.data();
    present.swapchainCount     = static_cast<std::uint32_t>(swapchains.size());
    present.pSwapchains        = swapchains.data();
    present.pImageIndices      = indices.data();
    present.pResults           = results.data();
    vkQueuePresentKHR(ctx_->queue(), &present);
    std::size_t k = 0;
    for (auto& t : targets_) {
        if (!t->drawing || t->offscreen) continue;
        if (results[k] == VK_ERROR_OUT_OF_DATE_KHR || results[k] == VK_SUBOPTIMAL_KHR) t->stale = true;
        ++k;
    }

    FinishCaptures();
    frame_ = (frame_ + 1) % kFramesInFlight;
    ++frameCount_;
}

void VulkanRenderer::BuildOverlay(Target& t, const OverlayView& view) {
    t.overlayCount = 0;
    if (!overlays_) return;
    overlayScratch_.clear();
    overlays_->Emit(moment_, view, overlayScratch_);
    t.overlayCount = static_cast<std::uint32_t>(std::min<std::size_t>(overlayScratch_.size(), kOverlayVertices));
    std::memcpy(t.overlay[frame_].mapped, overlayScratch_.data(), t.overlayCount * sizeof(OverlayVertex));
}

void VulkanRenderer::FinishCaptures() {
    bool any = false;
    for (auto& t : targets_) any |= t->drawing && !t->captureTo.empty();
    if (!any) return;
    vkWaitForFences(ctx_->device(), 1, &fences_[frame_], VK_TRUE, UINT64_MAX);
    for (auto& t : targets_) {
        if (!t->drawing || t->captureTo.empty()) continue;
        bool ok = false;
        if (t->canCapture) {
            // Swapchain bytes are BGRA or RGBA; the encoder wants BGRA.
            auto* pixels = static_cast<std::uint8_t*>(t->readback.mapped);
            if (t->format == VK_FORMAT_R8G8B8A8_UNORM) {
                for (std::size_t i = 0; i < std::size_t(t->extent.width) * t->extent.height; ++i) {
                    std::swap(pixels[i * 4], pixels[i * 4 + 2]);
                }
            }
            ok = SaveImage(t->captureTo, t->extent.width, t->extent.height, pixels, t->extent.width * 4);
        }
        app::Log("capture %s: %ls", ok ? "written" : "FAILED", t->captureTo.c_str());
        t->captureTo.clear();
    }
}

void VulkanRenderer::RequestCapture(HWND hwnd, const std::wstring& path) {
    for (auto& t : targets_) {
        if (t->hwnd == hwnd) t->captureTo = path;
    }
}

bool VulkanRenderer::CapturePending() const {
    for (const auto& t : targets_) {
        if (!t->captureTo.empty()) return true;
    }
    return false;
}

}  // namespace

std::unique_ptr<Renderer> Renderer::Create() {
    std::unique_ptr<Context> context = Context::Create();
    if (!context) return nullptr;
    auto renderer = std::make_unique<VulkanRenderer>(std::move(context));
    if (!renderer->Init()) {
        app::Log("renderer: initialisation failed");
        return nullptr;
    }
    return renderer;
}

}  // namespace render
