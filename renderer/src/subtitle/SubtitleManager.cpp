#include "SubtitleManager.hpp"

// libass public header. T4 added `${libass_src_SOURCE_DIR}/libass` to the
// renderer's include dirs, so the bare filename resolves directly.
#include "ass.h"

#include "LAppPal.hpp"
#include "graphics/IGraphicsBackend.hpp"

#include <cstdlib>
#include <cstring>

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

    LAppPal::PrintLogLn("[SubtitleManager] Initialized (%dx%d)", windowWidth, windowHeight);
    return true;
}

void SubtitleManager::Uninit()
{
    // Textures first — they depend on m_backend, not on libass.
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
    st->FontSize       = style.fontSize;
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

    ass_set_frame_size(static_cast<ASS_Renderer*>(m_renderer), width, height);

    auto* track = static_cast<ASS_Track*>(m_track);
    track->PlayResX = width;
    track->PlayResY = height;

    m_windowWidth = width;
    m_windowHeight = height;

    LAppPal::PrintLogLn("[SubtitleManager] Resized to %dx%d", width, height);
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
// DrawOverlays — stub (real GL/VK drawing lands in T7/T8)
// ---------------------------------------------------------------------------

void SubtitleManager::DrawOverlays()
{
    // Intentional stub: actual drawing is implemented in T7 (OpenGL overlay)
    // and T8 (Vulkan overlay). Logged at info level so callers can confirm
    // wiring without spamming — only logs when there's something to draw.
    if (!m_quads.empty()) {
        LAppPal::PrintLogLn("[SubtitleManager] DrawOverlays: %zu quad(s)",
                            m_quads.size());
    }
}
