#pragma once

#include <string>
#include <vector>
#include <cstdint>

#ifndef USE_VULKAN
#include <GL/glew.h>
#endif

// Forward declaration. NOTE: IGraphicsBackend is at GLOBAL scope in this
// codebase (see graphics/IGraphicsBackend.hpp) — it is NOT inside a
// `graphics` namespace, so we forward-declare it globally.
class IGraphicsBackend;

/**
 * @brief Subtitle style parameters. All colors are AABBGGRR (ASS convention).
 *
 * ASS color layout is a 32-bit unsigned int where the lowest byte is TRANSPARENCY
 * (TT): 0x00 = opaque, 0xFF = fully transparent. The remaining 3 bytes are BGR.
 * So 0x00FFFFFF = opaque white, 0x00000000 = opaque black.
 */
struct SubtitleStyle {
    std::string fontName = "Microsoft YaHei";
    double fontSize = 48.0;
    uint32_t primaryColor = 0x00FFFFFF;   // AABBGGRR: opaque white
    uint32_t outlineColor = 0x00000000;    // AABBGGRR: opaque black
    double outlineWidth = 2.0;
    uint32_t shadowColor = 0x00000000;
    double shadowDepth = 0.0;              // 0 = no shadow
    int alignment = 2;                     // ASS numpad: 2 = bottom-center
    int marginV = 30;
};

/**
 * @brief One composited subtitle glyph/layer quad to draw on screen.
 *
 * The textureHandle comes from IGraphicsBackend::CreateTexture and points to
 * a premultiplied-alpha RGBA texture. dst_x/dst_y are window-local
 * screen-space coordinates produced by libass.
 */
struct OverlayQuad {
    uint64_t textureHandle;
    int dstX, dstY;     // Screen-space destination position
    int width, height;  // Quad dimensions
};

/**
 * @brief Manages the complete libass subtitle lifecycle.
 *
 * - Owns ASS_Library / ASS_Renderer / ASS_Track (opaque via void* in header
 *   so ass.h stays out of the public include surface).
 * - Builds tracks programmatically (no ass_read_file / ass_read_memory).
 * - Converts ASS_Image linked lists into premultiplied-alpha RGBA textures
 *   via the IGraphicsBackend texture API.
 *
 * Thread-safety: all libass calls MUST happen on the main render thread
 * (same constraint as GL/VK calls). This class is NOT thread-safe.
 *
 * Lifecycle follows the AudioManager pattern: Init()/Uninit()/IsInitialized(),
 * with graceful degradation — Init() returns false if the backend pointer is
 * null or if any libass allocation fails, and all other methods no-op when
 * not initialized.
 */
class SubtitleManager {
public:
    SubtitleManager();
    ~SubtitleManager();

    SubtitleManager(const SubtitleManager&) = delete;
    SubtitleManager& operator=(const SubtitleManager&) = delete;

    /**
     * @brief Initialize libass library, renderer, and an empty track.
     *
     * Graceful degradation: if backend is null or any libass allocation
     * fails, logs and returns false; all later calls become no-ops.
     *
     * @param backend         Graphics backend for texture create/delete.
     * @param windowWidth     Initial frame width in pixels.
     * @param windowHeight    Initial frame height in pixels.
     * @return true on success, false on failure (object stays uninitialized).
     */
    bool Init(IGraphicsBackend* backend, int windowWidth, int windowHeight);

    /**
     * @brief Release all libass state and delete any live GPU textures.
     *
     * Safe to call on an uninitialized instance (no-op).
     */
    void Uninit();

    bool IsInitialized() const { return m_renderer != nullptr; }

    /**
     * @brief Replace the current subtitle event with one styled event.
     *
     * Clears any existing events, ensures style index 0 matches `style`,
     * then allocates a single event spanning [startMs, startMs+durationMs).
     * `text` may contain ASS override tags — libass parses them natively.
     */
    void SetText(const std::string& text, const SubtitleStyle& style,
                 int64_t startMs, int64_t durationMs);

    /**
     * @brief Remove all events so no subtitle is drawn on the next Render.
     */
    void Hide();

    /**
     * @brief Update the libass frame + play resolution. Call on window resize.
     */
    void Resize(int width, int height);

    /**
     * @brief Render the track at the given timestamp.
     *
     * @param nowMs Current playback time in milliseconds.
     * @return true if libass reported a content change (textures were
     *              rebuilt and the caller should redraw); false if cached.
     */
    bool Render(int64_t nowMs);

    /**
     * @brief Overlay quads produced by the most recent Render() call.
     *
     * Valid until the next Render() that returns true (which rebuilds them).
     */
    const std::vector<OverlayQuad>& GetOverlayQuads() const { return m_quads; }

    /**
     * @brief Draw the current overlays (GL path implemented in T7, VK path in T8).
     */
    void DrawOverlays();

private:
    void CleanupTextures();
    void ConvertAssImageToTextures(void* imgs);  // ASS_Image* (kept opaque)

    // libass state — void* keeps ass.h out of this header.
    void* m_library;     // ASS_Library*
    void* m_renderer;    // ASS_Renderer*
    void* m_track;       // ASS_Track*
    int m_detectChange;

    // Graphics backend for texture operations.
    IGraphicsBackend* m_backend;

    // Window dimensions (also the libass frame size).
    int m_windowWidth;
    int m_windowHeight;

    // Current overlay quads (one per ASS_Image node from the last changed frame).
    std::vector<OverlayQuad> m_quads;

#ifndef USE_VULKAN
    bool m_glInitialized;
    GLuint m_shaderProgram;
    GLuint m_vao;
    GLuint m_vbo;

    void InitGlOverlay();
    void DrawGlOverlays();
    void CheckGlError(const char* context);
#endif
};
