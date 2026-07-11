#include "SubtitleManager.hpp"

// libass public header. T4 added `${libass_src_SOURCE_DIR}/libass` to the
// renderer's include dirs, so the bare filename resolves directly.
#include "ass.h"

#include "LAppPal.hpp"
#include "graphics/IGraphicsBackend.hpp"

#ifdef USE_VULKAN
#include "graphics/VulkanBackend.hpp"
#include "SubtitleShadersVK.hpp"
#endif

#include <cstdlib>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SubtitleManager::SubtitleManager()
    : m_library(nullptr)
    , m_renderer(nullptr)
    , m_track(nullptr)
    , m_detectChange(0)
    , m_backend(nullptr)
    , m_windowWidth(0)
    , m_windowHeight(0)
#ifndef USE_VULKAN
    , m_glInitialized(false)
    , m_shaderProgram(0)
    , m_vao(0)
    , m_vbo(0)
#endif
#ifdef USE_VULKAN
    , m_vkInitialized(false)
    , m_descriptorSetLayout(VK_NULL_HANDLE)
    , m_pipelineLayout(VK_NULL_HANDLE)
    , m_graphicsPipeline(VK_NULL_HANDLE)
    , m_descriptorPool(VK_NULL_HANDLE)
    , m_vertexBuffer(VK_NULL_HANDLE)
    , m_vertexBufferMemory(VK_NULL_HANDLE)
    , m_sampler(VK_NULL_HANDLE)
#endif
{
}

SubtitleManager::~SubtitleManager()
{
    Uninit();
}

// ---------------------------------------------------------------------------
// Init / Uninit
// ---------------------------------------------------------------------------

bool SubtitleManager::Init(IGraphicsBackend* backend, int windowWidth, int windowHeight)
{
    // Already initialized — idempotent (mirrors AudioManager::Init).
    if (m_renderer != nullptr) {
        return true;
    }

    // Graceful degradation: no backend means we cannot create textures, so
    // there is no point bringing libass up. Log and bail.
    if (backend == nullptr) {
        LAppPal::PrintLogLn("[SubtitleManager] Init: backend is null, subtitles disabled");
        return false;
    }

    auto* lib = ass_library_init();
    if (lib == nullptr) {
        LAppPal::PrintLogLn("[SubtitleManager] ass_library_init() failed");
        return false;
    }
    m_library = lib;

    auto* rnd = ass_renderer_init(lib);
    if (rnd == nullptr) {
        LAppPal::PrintLogLn("[SubtitleManager] ass_renderer_init() failed");
        ass_library_done(lib);
        m_library = nullptr;
        return false;
    }
    m_renderer = rnd;

    ass_set_frame_size(rnd, windowWidth, windowHeight);
    // default_font=nullptr, default_family="Microsoft YaHei",
    // provider=AUTODETECT, config=nullptr, update=1.
    ass_set_fonts(rnd, nullptr, "Microsoft YaHei",
                  ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);

    auto* trk = ass_new_track(lib);
    if (trk == nullptr) {
        LAppPal::PrintLogLn("[SubtitleManager] ass_new_track() failed");
        ass_renderer_done(rnd);
        m_renderer = nullptr;
        ass_library_done(lib);
        m_library = nullptr;
        return false;
    }
    m_track = trk;

    // PlayRes matches the window so 1:1 pixel mapping with no scaling.
    trk->PlayResX = windowWidth;
    trk->PlayResY = windowHeight;

    m_backend = backend;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
    m_detectChange = 0;
    m_defaultStyle = SubtitleStyle();
    m_fontSize = m_defaultStyle.fontSize;

    LAppPal::PrintLogLn("[SubtitleManager] Initialized (%dx%d)", windowWidth, windowHeight);
    return true;
}

void SubtitleManager::Uninit()
{
#ifndef USE_VULKAN
    if (m_shaderProgram != 0) {
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_glInitialized = false;
#endif
#ifdef USE_VULKAN
    CleanupVkOverlay();
#endif

    // Textures first — they depend on m_backend, not on libass.
    if (m_borderTexture != 0 && m_backend != nullptr) {
        m_backend->DeleteTexture(m_borderTexture);
        m_borderTexture = 0;
    }
    CleanupTextures();

    if (m_track != nullptr) {
        ass_free_track(static_cast<ASS_Track*>(m_track));
        m_track = nullptr;
    }
    if (m_renderer != nullptr) {
        ass_renderer_done(static_cast<ASS_Renderer*>(m_renderer));
        m_renderer = nullptr;
    }
    if (m_library != nullptr) {
        ass_library_done(static_cast<ASS_Library*>(m_library));
        m_library = nullptr;
    }

    m_backend = nullptr;
    m_windowWidth = 0;
    m_windowHeight = 0;
    m_detectChange = 0;

    // Only log when we actually tore something down — Uninit() is called from
    // the destructor unconditionally, so silence the no-op path.
}

// ---------------------------------------------------------------------------
// SetText / Hide / Resize
// ---------------------------------------------------------------------------

void SubtitleManager::SetText(const std::string& text, const SubtitleStyle& style,
                              int64_t startMs, int64_t durationMs)
{
    if (m_track == nullptr) {
        return;
    }

    auto* track = static_cast<ASS_Track*>(m_track);

    // Replace any prior events (flush + re-add pattern — events must not be
    // mutated after first render).
    ass_flush_events(track);

    // Ensure style index 0 exists and matches the requested style.
    // ass_alloc_style returns the new slot index (int), not a pointer.
    ASS_Style* st = nullptr;
    if (track->n_styles == 0) {
        const int styleIdx = ass_alloc_style(track);
        (void)styleIdx;
        st = &track->styles[0];
    } else {
        st = &track->styles[0];
        // libass zero-inits new slots, but on reuse the previous FontName was
        // heap-allocated (by us, below). Free before overwriting to avoid
        // leaking on every SetText call.
        if (st->FontName != nullptr) {
            free(st->FontName);
            st->FontName = nullptr;
        }
    }

    st->FontName       = strdup(style.fontName.c_str());
    st->FontSize       = m_fontSize;
    st->PrimaryColour  = style.primaryColor;   // already AABBGGRR
    st->OutlineColour  = style.outlineColor;   // already AABBGGRR
    st->BackColour     = style.shadowColor;     // shadow color (AABBGGRR)
    st->BorderStyle    = 1;                     // 1 = outline + shadow (3 = opaque box)
    st->Outline        = style.outlineWidth;
    st->Shadow         = style.shadowDepth;
    st->Alignment      = style.alignment;       // ASS numpad layout
    st->MarginV        = style.marginV;

    // Single event spanning [startMs, startMs+durationMs), styled by index 0.
    // ass_alloc_event returns the new event index (int), not a pointer.
    const int evIdx = ass_alloc_event(track);
    ASS_Event* ev = &track->events[evIdx];
    ev->Start    = startMs;
    ev->Duration = durationMs;
    ev->Style    = 0;
    // strdup: libass takes ownership and frees on ass_free_track.
    ev->Text     = strdup(text.c_str());

    m_defaultStyle = style;
    m_defaultStyle.fontSize = m_fontSize;

    LAppPal::PrintLogLn("[SubtitleManager] SetText: '%s' @%lldms +%lldms",
                        text.c_str(),
                        static_cast<long long>(startMs),
                        static_cast<long long>(durationMs));
}

void SubtitleManager::Hide()
{
    if (m_track == nullptr) {
        return;
    }
    // Flushing all events means the next ass_render_frame produces no bitmaps.
    ass_flush_events(static_cast<ASS_Track*>(m_track));
}

void SubtitleManager::Resize(int width, int height)
{
    if (m_renderer == nullptr || m_track == nullptr) {
        return;
    }

    m_windowWidth = width;
    m_windowHeight = height;

    const int fw = m_areaWidth > 0 ? m_areaWidth : width;
    const int fh = m_areaHeight > 0 ? m_areaHeight : height;
    ass_set_frame_size(static_cast<ASS_Renderer*>(m_renderer), fw, fh);

    auto* track = static_cast<ASS_Track*>(m_track);
    track->PlayResX = fw;
    track->PlayResY = fh;

    LAppPal::PrintLogLn("[SubtitleManager] Resized to %dx%d (frame %dx%d)", width, height, fw, fh);
}

void SubtitleManager::SetSubtitleLayout(float offsetX, float offsetY,
                                        int areaWidth, int areaHeight,
                                        double fontSize)
{
    m_offsetX = offsetX;
    m_offsetY = offsetY;
    m_areaWidth = areaWidth;
    m_areaHeight = areaHeight;
    m_fontSize = fontSize;

    if (m_renderer != nullptr) {
        const int fw = areaWidth > 0 ? areaWidth : m_windowWidth;
        const int fh = areaHeight > 0 ? areaHeight : m_windowHeight;
        ass_set_frame_size(static_cast<ASS_Renderer*>(m_renderer), fw, fh);
    }

    if (m_track != nullptr) {
        auto* track = static_cast<ASS_Track*>(m_track);
        if (track->n_styles > 0) {
            track->styles[0].FontSize = fontSize;
        }
    }

    LAppPal::PrintLogLn("[SubtitleManager] SetLayout: offset=(%.0f,%.0f) area=%dx%d font=%.1f",
                        offsetX, offsetY, areaWidth, areaHeight, fontSize);
}

void SubtitleManager::AdjustSubtitleOffset(float dxNdc, float dyNdc)
{
    const float halfH = m_windowHeight > 0 ? (m_windowHeight / 2.0f) : 0.0f;
    m_offsetX += dxNdc * halfH;
    m_offsetY += -dyNdc * halfH;
}

void SubtitleManager::AdjustSubtitleFontSize(double factor)
{
    m_fontSize = std::clamp(m_fontSize * factor, 8.0, 200.0);
    m_defaultStyle.fontSize = m_fontSize;

    if (m_track != nullptr) {
        auto* track = static_cast<ASS_Track*>(m_track);
        if (track->n_styles > 0) {
            track->styles[0].FontSize = m_fontSize;
        }
    }

    LAppPal::PrintLogLn("[SubtitleManager] FontSize adjusted to %.1f", m_fontSize);
}

void SubtitleManager::AdjustSubtitleArea(int deltaWidth, int deltaHeight)
{
    m_areaWidth += deltaWidth;
    m_areaHeight += deltaHeight;
    if (m_areaWidth < 50) m_areaWidth = 50;
    if (m_areaHeight < 30) m_areaHeight = 30;
    if (m_renderer) {
        int fw = m_areaWidth > 0 ? m_areaWidth : m_windowWidth;
        int fh = m_areaHeight > 0 ? m_areaHeight : m_windowHeight;
        ass_set_frame_size(static_cast<ASS_Renderer*>(m_renderer), fw, fh);
    }
}

void SubtitleManager::SetAdjustMode(bool enabled)
{
    m_adjustMode = enabled;

    if (enabled) {
        SetText("字幕预览\\NSubtitle Preview", m_defaultStyle, 0, INT64_MAX);
    } else {
        Hide();
    }

    LAppPal::PrintLogLn("[SubtitleManager] AdjustMode %s", enabled ? "ON" : "OFF");
}

void SubtitleManager::SetDefaultStyle(const SubtitleStyle& style)
{
    m_defaultStyle = style;

    if (m_track == nullptr) return;

    auto* track = static_cast<ASS_Track*>(m_track);
    if (track->n_styles == 0) return;

    ASS_Style* st = &track->styles[0];
    if (st->FontName != nullptr) {
        free(st->FontName);
        st->FontName = nullptr;
    }
    st->FontName       = strdup(style.fontName.c_str());
    m_fontSize = style.fontSize;
    st->FontSize       = m_fontSize;
    st->PrimaryColour  = style.primaryColor;
    st->OutlineColour  = style.outlineColor;
    st->BackColour     = style.shadowColor;
    st->Outline        = style.outlineWidth;
    st->Shadow         = style.shadowDepth;
    st->Alignment      = style.alignment;
    st->MarginV        = style.marginV;
}

const SubtitleStyle& SubtitleManager::GetDefaultStyle() const
{
    return m_defaultStyle;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

bool SubtitleManager::Render(int64_t nowMs)
{
    if (m_renderer == nullptr || m_track == nullptr) {
        return false;
    }

    ASS_Image* imgs = ass_render_frame(static_cast<ASS_Renderer*>(m_renderer),
                                       static_cast<ASS_Track*>(m_track),
                                       nowMs, &m_detectChange);

    if (m_detectChange != 0) {
        // Content changed since last frame: rebuild GPU textures.
        CleanupTextures();
        ConvertAssImageToTextures(imgs);
        return true;
    }
    // Cached: previously built quads remain valid.
    return false;
}

// ---------------------------------------------------------------------------
// ASS_Image → RGBA texture conversion
// ---------------------------------------------------------------------------

void SubtitleManager::ConvertAssImageToTextures(void* imgsPtr)
{
    // ASS_Image linked list: one node per glyph/outline/shadow layer.
    // Each node's `bitmap` is a single-channel alpha buffer (0..255), and
    // `color` is AABBGGRR where the low byte is TRANSPARENCY (0x00 = opaque).
    auto* img = static_cast<ASS_Image*>(imgsPtr);

    for (; img != nullptr; img = img->next) {
        if (img->w <= 0 || img->h <= 0 || img->bitmap == nullptr) {
            continue;
        }

        const int w = img->w;
        const int h = img->h;
        const int stride = img->stride;
        // NOTE: ASS_Image documents that the last row may be unpadded —
        // index using stride for row offset, but `w` for in-row column offset
        // (bytes past `w` per line may be uninitialized). Handled below by
        // using `x` (never `stride`) for the column index.
        const unsigned char* bitmap = img->bitmap;

        // Extract the per-style color components from ASS AABBGGRR.
        // Low byte = transparency (TT), then BB, GG, RR going up.
        const uint32_t color = img->color;
        const int styleA = 0xFF - static_cast<int>(color & 0xFF); // TT → opacity
        const int cR = static_cast<int>((color >> 24) & 0xFF);
        const int cG = static_cast<int>((color >> 16) & 0xFF);
        const int cB = static_cast<int>((color >> 8)  & 0xFF);

        // Premultiplied-alpha RGBA buffer. premult: RGB *= a/255.
        const size_t bufSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        uint8_t* rgba = new uint8_t[bufSize];

        for (int y = 0; y < h; ++y) {
            const unsigned char* row = bitmap + (static_cast<ptrdiff_t>(y) * stride);
            uint8_t* dstRow = rgba + (static_cast<ptrdiff_t>(y) * w * 4);
            for (int x = 0; x < w; ++x) {
                const int glyphA = row[x];
                const int finalA = (styleA * glyphA) / 255;
                // Premultiplied RGB scaled by alpha.
                dstRow[x * 4 + 0] = static_cast<uint8_t>((cR * finalA) / 255);
                dstRow[x * 4 + 1] = static_cast<uint8_t>((cG * finalA) / 255);
                dstRow[x * 4 + 2] = static_cast<uint8_t>((cB * finalA) / 255);
                dstRow[x * 4 + 3] = static_cast<uint8_t>(finalA);
            }
        }

        const uint64_t handle = m_backend->CreateTexture(rgba, w, h, 4);
        delete[] rgba;

        if (handle == 0) {
            LAppPal::PrintLogLn("[SubtitleManager] CreateTexture failed for %dx%d quad",
                                w, h);
            continue;
        }

        m_quads.push_back(OverlayQuad{
            handle,
            img->dst_x, img->dst_y,
            w, h
        });
    }
}

void SubtitleManager::CleanupTextures()
{
    if (m_backend == nullptr) {
        m_quads.clear();
        return;
    }
    for (const auto& q : m_quads) {
        if (q.textureHandle != 0) {
            m_backend->DeleteTexture(q.textureHandle);
        }
    }
    m_quads.clear();
}

// ---------------------------------------------------------------------------
// DrawOverlays — dispatches to backend-specific path
// ---------------------------------------------------------------------------

void SubtitleManager::DrawOverlays()
{
#ifndef USE_VULKAN
    DrawGlOverlays();
#else
    DrawVkOverlays();
#endif
}

#ifdef USE_VULKAN

// ---------------------------------------------------------------------------
// Vulkan overlay path (T8)
// ---------------------------------------------------------------------------

uint32_t SubtitleManager::FindVkMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props)
{
    auto* vkBackend = static_cast<VulkanBackend*>(m_backend);
    if (vkBackend == nullptr) return UINT32_MAX;

    VkPhysicalDevice physDev = vkBackend->GetPhysicalDevice();
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

void SubtitleManager::InitVkOverlay()
{
    if (m_vkInitialized) return;
    if (m_backend == nullptr) return;

    auto* vkBackend = static_cast<VulkanBackend*>(m_backend);
    VkDevice device = vkBackend->GetDevice();

    // --- Descriptor set layout: 1 combined image sampler, binding 0, fragment ---
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dsLayoutInfo{};
    dsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsLayoutInfo.bindingCount = 1;
    dsLayoutInfo.pBindings = &layoutBinding;

    if (vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr,
                                    &m_descriptorSetLayout) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkCreateDescriptorSetLayout failed");
        m_vkInitialized = true; // prevent retry loop
        return;
    }

    // --- Pipeline layout: descriptor set + 8-byte push constant (vertex stage) ---
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = 8; // sizeof(vec2) screenSize

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &m_pipelineLayout) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkCreatePipelineLayout failed");
        m_vkInitialized = true;
        return;
    }

    // --- Graphics pipeline (dynamic rendering, no RenderPass) ---
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo vertInfo{};
    vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertInfo.codeSize = kSubtitleVertSpvWordCount * sizeof(uint32_t);
    vertInfo.pCode = kSubtitleVertSpv;
    if (vkCreateShaderModule(device, &vertInfo, nullptr, &vertModule) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkCreateShaderModule (vert) failed");
        m_vkInitialized = true;
        return;
    }

    VkShaderModuleCreateInfo fragInfo{};
    fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragInfo.codeSize = kSubtitleFragSpvWordCount * sizeof(uint32_t);
    fragInfo.pCode = kSubtitleFragSpv;
    if (vkCreateShaderModule(device, &fragInfo, nullptr, &fragModule) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkCreateShaderModule (frag) failed");
        vkDestroyShaderModule(device, vertModule, nullptr);
        m_vkInitialized = true;
        return;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = 4 * sizeof(float);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDescs[2] = {};
    attrDescs[0].location = 0;
    attrDescs[0].binding = 0;
    attrDescs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[0].offset = 0;
    attrDescs[1].location = 1;
    attrDescs[1].binding = 0;
    attrDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[1].offset = 2 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Premultiplied alpha: src=ONE, dst=ONE_MINUS_SRC_ALPHA
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    VkFormat colorFormat = vkBackend->GetImageFormat();
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &m_graphicsPipeline) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkCreateGraphicsPipelines failed");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        m_vkInitialized = true;
        return;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    // --- Descriptor pool: 64 combined-image-sampler sets ---
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 64;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkCreateDescriptorPool failed");
        m_vkInitialized = true;
        return;
    }

    // --- Vertex buffer: host-visible, 6 verts x 4 floats = 96 bytes ---
    VkBufferCreateInfo vbInfo{};
    vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbInfo.size = 6 * 4 * sizeof(float);
    vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &vbInfo, nullptr, &m_vertexBuffer) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkCreateBuffer (vertex) failed");
        m_vkInitialized = true;
        return;
    }

    VkMemoryRequirements vbMemReq;
    vkGetBufferMemoryRequirements(device, m_vertexBuffer, &vbMemReq);

    uint32_t vbMemType = FindVkMemoryType(vbMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vbMemType == UINT32_MAX)
    {
        LAppPal::PrintLogLn("[SubtitleManager] FindVkMemoryType (vertex buffer) failed");
        m_vkInitialized = true;
        return;
    }

    VkMemoryAllocateInfo vbAllocInfo{};
    vbAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vbAllocInfo.allocationSize = vbMemReq.size;
    vbAllocInfo.memoryTypeIndex = vbMemType;

    if (vkAllocateMemory(device, &vbAllocInfo, nullptr, &m_vertexBufferMemory) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkAllocateMemory (vertex buffer) failed");
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
        m_vkInitialized = true;
        return;
    }
    vkBindBufferMemory(device, m_vertexBuffer, m_vertexBufferMemory, 0);

    // --- Sampler: linear filtering, clamp-to-edge, no mipmaps ---
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[SubtitleManager] vkCreateSampler failed");
        m_vkInitialized = true;
        return;
    }

    m_vkInitialized = true;
    LAppPal::PrintLogLn("[SubtitleManager] VK overlay pipeline initialized");
}

void SubtitleManager::DrawVkOverlays()
{
    if (m_quads.empty() && !m_adjustMode) return;
    if (!m_vkInitialized) InitVkOverlay();
    if (m_graphicsPipeline == VK_NULL_HANDLE) return;
    if (m_backend == nullptr) return;

    auto* vkBackend = static_cast<VulkanBackend*>(m_backend);
    VkDevice device = vkBackend->GetDevice();

    // Reset descriptor pool to reclaim sets from prior frames.
    vkResetDescriptorPool(device, m_descriptorPool, 0);

    // Begin our own command buffer + dynamic rendering pass (loadOp=LOAD
    // preserves the model render). After Step 2 (Cubism OnUpdate) the GPU
    // is idle because SubmitCommand calls vkQueueWaitIdle.
    VkCommandBuffer cmdBuf = vkBackend->BeginSingleTimeCommands();

    VkRenderingAttachmentInfoKHR colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    colorAttachment.imageView = vkBackend->GetSwapchainImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfoKHR renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = vkBackend->GetSwapchainExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmdBuf, &renderingInfo);

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    struct { float w, h; } pushConstants = {
        static_cast<float>(m_windowWidth),
        static_cast<float>(m_windowHeight)
    };
    vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(pushConstants), &pushConstants);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_windowWidth);
    viewport.height = static_cast<float>(m_windowHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent.width = static_cast<uint32_t>(m_windowWidth);
    scissor.extent.height = static_cast<uint32_t>(m_windowHeight);
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_vertexBuffer, offsets);

    uint32_t setIndex = 0;
    for (const auto& quad : m_quads)
    {
        if (quad.textureHandle == 0) continue;

        VkImageView imageView = vkBackend->GetTextureImageView(quad.textureHandle);
        if (imageView == VK_NULL_HANDLE) continue;

        VkDescriptorSetAllocateInfo dsAllocInfo{};
        dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAllocInfo.descriptorPool = m_descriptorPool;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &m_descriptorSetLayout;

        VkDescriptorSet descSet = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device, &dsAllocInfo, &descSet) != VK_SUCCESS)
        {
            LAppPal::PrintLogLn("[SubtitleManager] vkAllocateDescriptorSets failed (set %u)", setIndex);
            break;
        }        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_sampler;
        imageInfo.imageView = imageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1, &descSet, 0, nullptr);

        const float halfW = (m_areaWidth > 0 ? static_cast<float>(m_areaWidth) : static_cast<float>(m_windowWidth)) * 0.5f;
        const float halfH = (m_areaHeight > 0 ? static_cast<float>(m_areaHeight) : static_cast<float>(m_windowHeight)) * 0.5f;
        const float ox = m_offsetX - halfW;
        const float oy = m_offsetY - halfH;
        const float x0 = static_cast<float>(quad.dstX) + ox;
        const float y0 = static_cast<float>(quad.dstY) + oy;
        const float x1 = static_cast<float>(quad.dstX + quad.width) + ox;
        const float y1 = static_cast<float>(quad.dstY + quad.height) + oy;

        const float vertices[] = {
            x0, y0, 0.0f, 0.0f,
            x1, y0, 1.0f, 0.0f,
            x0, y1, 0.0f, 1.0f,
            x1, y0, 1.0f, 0.0f,
            x1, y1, 1.0f, 1.0f,
            x0, y1, 0.0f, 1.0f,
        };

        void* data = nullptr;
        vkMapMemory(device, m_vertexBufferMemory, 0, sizeof(vertices), 0, &data);
        std::memcpy(data, vertices, sizeof(vertices));
        vkUnmapMemory(device, m_vertexBufferMemory);

        vkCmdDraw(cmdBuf, 6, 1, 0, 0);
        setIndex++;
    }

    if (m_adjustMode) {
        if (m_borderTexture == 0 && m_backend != nullptr) {
            uint8_t px[4] = { 204, 204, 0, 204 };
            m_borderTexture = m_backend->CreateTexture(px, 1, 1, 4);
        }
        if (m_borderTexture != 0) {
            VkImageView borderView = vkBackend->GetTextureImageView(m_borderTexture);
            if (borderView != VK_NULL_HANDLE && setIndex < 64) {
                VkDescriptorSetAllocateInfo bdsInfo{};
                bdsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                bdsInfo.descriptorPool = m_descriptorPool;
                bdsInfo.descriptorSetCount = 1;
                bdsInfo.pSetLayouts = &m_descriptorSetLayout;

                VkDescriptorSet borderSet = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(device, &bdsInfo, &borderSet) == VK_SUCCESS) {
                    VkDescriptorImageInfo bimgInfo{};
                    bimgInfo.sampler = m_sampler;
                    bimgInfo.imageView = borderView;
                    bimgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    VkWriteDescriptorSet bwrite{};
                    bwrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    bwrite.dstSet = borderSet;
                    bwrite.dstBinding = 0;
                    bwrite.dstArrayElement = 0;
                    bwrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    bwrite.descriptorCount = 1;
                    bwrite.pImageInfo = &bimgInfo;

                    vkUpdateDescriptorSets(device, 1, &bwrite, 0, nullptr);
                    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_pipelineLayout, 0, 1, &borderSet, 0, nullptr);

                    const int bw = m_areaWidth > 0 ? m_areaWidth : m_windowWidth;
                    const int bh = m_areaHeight > 0 ? m_areaHeight : m_windowHeight;
                    const float bx = m_offsetX - static_cast<float>(bw) * 0.5f;
                    const float by = m_offsetY - static_cast<float>(bh) * 0.5f;
                    const float bt = 2.0f;

                    auto drawEdge = [&](float ex0, float ey0, float ex1, float ey1) {
                        const float verts[] = {
                            ex0, ey0, 0.0f, 0.0f,
                            ex1, ey0, 1.0f, 0.0f,
                            ex0, ey1, 0.0f, 1.0f,
                            ex1, ey0, 1.0f, 0.0f,
                            ex1, ey1, 1.0f, 1.0f,
                            ex0, ey1, 0.0f, 1.0f,
                        };
                        void* d = nullptr;
                        vkMapMemory(device, m_vertexBufferMemory, 0, sizeof(verts), 0, &d);
                        std::memcpy(d, verts, sizeof(verts));
                        vkUnmapMemory(device, m_vertexBufferMemory);
                        vkCmdDraw(cmdBuf, 6, 1, 0, 0);
                    };

                    drawEdge(bx, by, bx + static_cast<float>(bw), by + bt);
                    drawEdge(bx, by + static_cast<float>(bh) - bt, bx + static_cast<float>(bw), by + static_cast<float>(bh));
                    drawEdge(bx, by, bx + bt, by + static_cast<float>(bh));
                    drawEdge(bx + static_cast<float>(bw) - bt, by, bx + static_cast<float>(bw), by + static_cast<float>(bh));
                }
            }
        }
    }

    vkCmdEndRendering(cmdBuf);
    vkBackend->SubmitCommand(cmdBuf);
}

void SubtitleManager::CleanupVkOverlay()
{
    if (m_backend == nullptr) {
        m_vkInitialized = false;
        return;
    }

    auto* vkBackend = static_cast<VulkanBackend*>(m_backend);
    VkDevice device = vkBackend->GetDevice();
    if (device == VK_NULL_HANDLE) {
        m_vkInitialized = false;
        return;
    }

    vkDeviceWaitIdle(device);

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_vertexBufferMemory, nullptr);
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    m_vkInitialized = false;
}

#else

// ---------------------------------------------------------------------------
// OpenGL overlay path (T7)
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kVertSrc = R"GLSL(#version 330 core
in vec2 aPos;
in vec2 aUV;
out vec2 vUV;
uniform vec2 uScreenSize;
void main() {
    vec2 ndc = vec2(
        (aPos.x / uScreenSize.x) * 2.0 - 1.0,
        1.0 - (aPos.y / uScreenSize.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
}
)GLSL";

constexpr const char* kFragSrc = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    FragColor = texture(uTex, vUV);
}
)GLSL";

GLuint CompileShader(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint logLen = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(std::max(logLen, 1)));
        glGetShaderInfoLog(sh, logLen, nullptr, log.data());
        LAppPal::PrintLogLn("[SubtitleManager] shader compile failed: %s", log.data());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

} // namespace

void SubtitleManager::InitGlOverlay()
{
    if (m_glInitialized) return;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertSrc);
    if (vs == 0) { m_glInitialized = true; return; }
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
    if (fs == 0) { glDeleteShader(vs); m_glInitialized = true; return; }

    GLuint prog = glCreateProgram();
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUV");
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(std::max(logLen, 1)));
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        LAppPal::PrintLogLn("[SubtitleManager] program link failed: %s", log.data());
        glDeleteProgram(prog);
        m_glInitialized = true;
        return;
    }
    m_shaderProgram = prog;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // 6 vertices * 4 floats (x, y, u, v) = 96 bytes, rewritten each draw.
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);

    GLsizei stride = 4 * static_cast<GLsizei>(sizeof(float));
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(sizeof(float) * 2));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_glInitialized = true;
    LAppPal::PrintLogLn("[SubtitleManager] GL overlay shader+VAO initialized");
    CheckGlError("InitGlOverlay");
}

void SubtitleManager::DrawGlOverlays()
{
    if (m_quads.empty() && !m_adjustMode) return;
    if (!m_glInitialized) InitGlOverlay();
    if (m_shaderProgram == 0) return;

    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);

    glUseProgram(m_shaderProgram);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    // Premultiplied alpha: textures already have RGB scaled by A.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    GLint screenSizeLoc = glGetUniformLocation(m_shaderProgram, "uScreenSize");
    glUniform2f(screenSizeLoc,
                static_cast<float>(m_windowWidth),
                static_cast<float>(m_windowHeight));

    GLint texLoc = glGetUniformLocation(m_shaderProgram, "uTex");
    glUniform1i(texLoc, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(m_vao);

    for (const auto& quad : m_quads) {
        if (quad.textureHandle == 0) continue;

        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(quad.textureHandle));

        const float halfW = (m_areaWidth > 0 ? static_cast<float>(m_areaWidth) : static_cast<float>(m_windowWidth)) * 0.5f;
        const float halfH = (m_areaHeight > 0 ? static_cast<float>(m_areaHeight) : static_cast<float>(m_windowHeight)) * 0.5f;
        const float ox = m_offsetX - halfW;
        const float oy = m_offsetY - halfH;
        const float x0 = static_cast<float>(quad.dstX) + ox;
        const float y0 = static_cast<float>(quad.dstY) + oy;
        const float x1 = static_cast<float>(quad.dstX + quad.width) + ox;
        const float y1 = static_cast<float>(quad.dstY + quad.height) + oy;

        const float vertices[] = {
            x0, y0, 0.0f, 0.0f,
            x1, y0, 1.0f, 0.0f,
            x0, y1, 0.0f, 1.0f,
            x1, y0, 1.0f, 0.0f,
            x1, y1, 1.0f, 1.0f,
            x0, y1, 0.0f, 1.0f,
        };

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    if (m_adjustMode) {
        if (m_borderTexture == 0 && m_backend != nullptr) {
            uint8_t px[4] = { 204, 204, 0, 204 };
            m_borderTexture = m_backend->CreateTexture(px, 1, 1, 4);
        }
        if (m_borderTexture != 0) {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_borderTexture));

            const int bw = m_areaWidth > 0 ? m_areaWidth : m_windowWidth;
            const int bh = m_areaHeight > 0 ? m_areaHeight : m_windowHeight;
            const float bx0 = m_offsetX - static_cast<float>(bw) * 0.5f;
            const float by0 = m_offsetY - static_cast<float>(bh) * 0.5f;
            const float bx1 = bx0 + static_cast<float>(bw);
            const float by1 = by0 + static_cast<float>(bh);

            const float borderVerts[] = {
                bx0, by0, 0.0f, 0.0f,
                bx1, by0, 0.0f, 0.0f,
                bx1, by1, 0.0f, 0.0f,
                bx0, by1, 0.0f, 0.0f,
            };

            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(borderVerts), borderVerts);
            glDrawArrays(GL_LINE_LOOP, 0, 4);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);

    if (!prevBlend) glDisable(GL_BLEND);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST);

    CheckGlError("DrawGlOverlays");
}

void SubtitleManager::CheckGlError(const char* context)
{
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LAppPal::PrintLogLn("[SubtitleManager] GL error after %s: 0x%04X",
                            context, static_cast<unsigned int>(err));
    }
}

#endif // USE_VULKAN / else
