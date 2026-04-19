// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <glad/glad.h>
#include "video_core/renderer_base.h"
#include "video_core/renderer_software/sw_rasterizer.h"

namespace Core {
class System;
}

namespace SwRenderer {

struct ScreenInfo {
    u32 width;
    u32 height;
    std::vector<u8> pixels;
    // Cheap dirty-check so LoadFBToScreenInfo can bail out when the game
    // hasn't actually touched the framebuffer (title screens, menus, paused
    // games). Hash is computed over the raw source framebuffer bytes.
    u64 last_fb_hash = 0;
    u32 last_fb_addr = 0;
    bool upload_dirty = true;
};

class RendererSoftware : public VideoCore::RendererBase {
public:
    explicit RendererSoftware(Core::System& system, Pica::PicaCore& pica,
                              Frontend::EmuWindow& window);
    ~RendererSoftware() override;

    [[nodiscard]] VideoCore::RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    [[nodiscard]] const ScreenInfo& Screen(VideoCore::ScreenId id) const noexcept {
        return screen_infos[static_cast<u32>(id)];
    }

    void SwapBuffers() override;
    void TryPresent(int timeout_ms, bool is_secondary) override;
    void PrepareVideoDumping() override {}
    void CleanupVideoDumping() override {}

private:
    void PrepareRenderTarget();
    void LoadFBToScreenInfo(int i);
    void InitOpenGLObjects();
    void DrawScreen(const ScreenInfo& info, float x, float y, float w, float h, GLuint tex);

private:
    Memory::MemorySystem& memory;
    Pica::PicaCore& pica;
    RasterizerSoftware rasterizer;
    std::array<ScreenInfo, 3> screen_infos{};

    // OpenGL objects for presenting. One texture per screen so that the dirty
    // gate in DrawScreen doesn't accidentally show the wrong screen's pixels
    // when only one of the two framebuffers changed this frame.
    std::array<GLuint, 3> gl_textures{};
    GLuint gl_program = 0;
    GLuint gl_vao = 0;
    GLuint gl_vbo = 0;
    GLint gl_tex_uniform = -1;
    bool gl_initialized = false;
};

} // namespace SwRenderer
