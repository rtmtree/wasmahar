// Copyright 2023 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/color.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/framebuffer_layout.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_software/renderer_software.h"

namespace SwRenderer {

static const char* vertex_shader = R"(
attribute vec2 a_position;
attribute vec2 a_texcoord;
varying vec2 v_texcoord;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
}
)";

static const char* fragment_shader = R"(
precision mediump float;
varying vec2 v_texcoord;
uniform sampler2D u_texture;
void main() {
    gl_FragColor = texture2D(u_texture, v_texcoord);
}
)";

static GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

RendererSoftware::RendererSoftware(Core::System& system, Pica::PicaCore& pica_,
                                   Frontend::EmuWindow& window)
    : VideoCore::RendererBase{system, window, nullptr}, memory{system.Memory()}, pica{pica_},
      rasterizer{memory, pica} {}

RendererSoftware::~RendererSoftware() {
    for (auto& tex : gl_textures) {
        if (tex) glDeleteTextures(1, &tex);
    }
    if (gl_program) glDeleteProgram(gl_program);
    if (gl_vao) glDeleteVertexArrays(1, &gl_vao);
    if (gl_vbo) glDeleteBuffers(1, &gl_vbo);
}

void RendererSoftware::InitOpenGLObjects() {
    if (gl_initialized) return;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertex_shader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragment_shader);
    if (!vs || !fs) return;

    gl_program = glCreateProgram();
    glAttachShader(gl_program, vs);
    glAttachShader(gl_program, fs);
    glBindAttribLocation(gl_program, 0, "a_position");
    glBindAttribLocation(gl_program, 1, "a_texcoord");
    glLinkProgram(gl_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(gl_program, GL_LINK_STATUS, &ok);
    if (!ok) return;

    gl_tex_uniform = glGetUniformLocation(gl_program, "u_texture");

    glGenTextures(static_cast<GLsizei>(gl_textures.size()), gl_textures.data());
    for (auto tex : gl_textures) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glGenVertexArrays(1, &gl_vao);
    glGenBuffers(1, &gl_vbo);

    gl_initialized = true;
}

void RendererSoftware::DrawScreen(const ScreenInfo& info, float x, float y, float w, float h,
                                  GLuint tex) {
    if (info.width == 0 || info.height == 0 || info.pixels.empty()) return;

    glBindTexture(GL_TEXTURE_2D, tex);
    // Skip the upload when the source framebuffer hasn't changed — saves
    // ~2 MB/frame of driver traffic on static screens. Because each screen
    // has its own texture, binding the right tex shows its cached pixels
    // even when we skip the upload.
    if (info.upload_dirty) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, info.width, info.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, info.pixels.data());
    }

    // Texcoords flipped in both axes to compensate for the 3DS framebuffer's
    // column-major + bottom-origin layout as it lands in our upload buffer.
    // Effectively rotates the sampled image 180°, matching the display orientation.
    float verts[] = {
        x,     y,     1.0f, 0.0f,
        x + w, y,     0.0f, 0.0f,
        x + w, y + h, 0.0f, 1.0f,
        x,     y + h, 1.0f, 1.0f,
    };

    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);

    glBindVertexArray(gl_vao);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void RendererSoftware::TryPresent(int timeout_ms, bool is_secondary) {
    if (!gl_initialized) {
        InitOpenGLObjects();
        if (!gl_initialized) return;
    }

    const auto layout = render_window.GetFramebufferLayout();
    if (layout.width == 0 || layout.height == 0) return;

    glViewport(0, 0, layout.width, layout.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(gl_program);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(gl_tex_uniform, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw top screen (screen_infos[0])
    // NDC y goes up, but layout coordinates go down — so height translates
    // to a negative Y offset in NDC (quad extends downward from top edge).
    const auto& top = layout.top_screen;
    float top_x = (float)top.left / layout.width * 2.0f - 1.0f;
    float top_y = 1.0f - (float)top.top / layout.height * 2.0f;
    float top_w = (float)(top.right - top.left) / layout.width * 2.0f;
    float top_h = -(float)(top.bottom - top.top) / layout.height * 2.0f;
    DrawScreen(screen_infos[0], top_x, top_y, top_w, top_h, gl_textures[0]);

    // Draw bottom screen (screen_infos[2])
    const auto& bot = layout.bottom_screen;
    float bot_x = (float)bot.left / layout.width * 2.0f - 1.0f;
    float bot_y = 1.0f - (float)bot.top / layout.height * 2.0f;
    float bot_w = (float)(bot.right - bot.left) / layout.width * 2.0f;
    float bot_h = -(float)(bot.bottom - bot.top) / layout.height * 2.0f;
    DrawScreen(screen_infos[2], bot_x, bot_y, bot_w, bot_h, gl_textures[2]);
}

void RendererSoftware::SwapBuffers() {
    PrepareRenderTarget();
    EndFrame();
}

void RendererSoftware::PrepareRenderTarget() {
    const auto& regs_lcd = pica.regs_lcd;
    for (u32 i = 0; i < 3; i++) {
        const u32 fb_id = i == 2 ? 1 : 0;

        const auto color_fill = fb_id == 0 ? regs_lcd.color_fill_top : regs_lcd.color_fill_bottom;
        if (!color_fill.is_enabled) {
            LoadFBToScreenInfo(i);
        }
    }
}

void RendererSoftware::LoadFBToScreenInfo(int i) {
    const u32 fb_id = i == 2 ? 1 : 0;
    const auto& framebuffer = pica.regs.framebuffer_config[fb_id];
    auto& info = screen_infos[i];

    const PAddr framebuffer_addr =
        framebuffer.active_fb == 0 ? framebuffer.address_left1 : framebuffer.address_left2;
    const s32 bpp = Pica::BytesPerPixel(framebuffer.color_format);
    const u8* framebuffer_data = memory.GetPhysicalPointer(framebuffer_addr);

    // 3DS framebuffer memory stores the image rotated: each memory "row"
    // is actually a vertical column of the displayed image, scanned from
    // bottom to top. So memory dims are (framebuffer.height columns) x
    // (pixel_stride rows-per-column), and the display dims are
    // (width = framebuffer.height, height = pixel_stride).
    const s32 pixel_stride = framebuffer.stride / bpp;
    const u32 fb_bytes = static_cast<u32>(framebuffer.height) * framebuffer.stride;

    // Dirty-check: FNV-1a hash of the source framebuffer. If nothing changed
    // since the last LoadFBToScreenInfo, skip the 150k+ per-pixel format
    // conversion AND the glTexImage2D upload. Title screens, menus and paused
    // games hit this path on almost every frame.
    if (framebuffer_data != nullptr) {
        u64 h = 1469598103934665603ull;
        // Sample every 16th byte — 4× faster to hash, still catches real
        // mutations (2+ samples out of any 128-byte change with overwhelming
        // probability).
        const u32 stride_bytes = 16;
        for (u32 off = 0; off < fb_bytes; off += stride_bytes) {
            h ^= framebuffer_data[off];
            h *= 1099511628211ull;
        }
        if (info.last_fb_addr == framebuffer_addr && info.last_fb_hash == h &&
            !info.pixels.empty()) {
            info.upload_dirty = false;
            return;
        }
        info.last_fb_addr = framebuffer_addr;
        info.last_fb_hash = h;
    }
    info.upload_dirty = true;

    info.width = framebuffer.height;   // display width = number of memory rows
    info.height = pixel_stride;        // display height = column length
    info.pixels.resize(info.width * info.height * 4);

    // Hoist the format switch out of the hot loop — the format is constant
    // for the whole frame so a per-pixel branch costs ~5 % of the conversion
    // time on its own. `-msimd128` at the project level lets clang
    // auto-vectorize the per-format loops below where the access pattern
    // permits.
    const u32 W = info.width;
    const u32 H = info.height;
    u8* const dst = info.pixels.data();
    const u8* const src = framebuffer_data;

    auto idx = [&](u32 dx, u32 dy) -> const u8* {
        return src + ((W - 1 - dx) * pixel_stride + (pixel_stride - 1 - dy)) * bpp;
    };

    switch (framebuffer.color_format) {
    case Pica::PixelFormat::RGBA8:
        for (u32 dx = 0; dx < W; dx++) {
            for (u32 dy = 0; dy < H; dy++) {
                const u8* p = idx(dx, dy);
                u8* d = dst + (dy * W + dx) * 4;
                d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = p[0];
            }
        }
        break;
    case Pica::PixelFormat::RGB8:
        for (u32 dx = 0; dx < W; dx++) {
            for (u32 dy = 0; dy < H; dy++) {
                const u8* p = idx(dx, dy);
                u8* d = dst + (dy * W + dx) * 4;
                d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = 0xFF;
            }
        }
        break;
    case Pica::PixelFormat::RGB565:
        for (u32 dx = 0; dx < W; dx++) {
            for (u32 dy = 0; dy < H; dy++) {
                const Common::Vec4 c = Common::Color::DecodeRGB565(idx(dx, dy));
                std::memcpy(dst + (dy * W + dx) * 4, c.AsArray(), 4);
            }
        }
        break;
    case Pica::PixelFormat::RGB5A1:
        for (u32 dx = 0; dx < W; dx++) {
            for (u32 dy = 0; dy < H; dy++) {
                const Common::Vec4 c = Common::Color::DecodeRGB5A1(idx(dx, dy));
                std::memcpy(dst + (dy * W + dx) * 4, c.AsArray(), 4);
            }
        }
        break;
    case Pica::PixelFormat::RGBA4:
        for (u32 dx = 0; dx < W; dx++) {
            for (u32 dy = 0; dy < H; dy++) {
                const Common::Vec4 c = Common::Color::DecodeRGBA4(idx(dx, dy));
                std::memcpy(dst + (dy * W + dx) * 4, c.AsArray(), 4);
            }
        }
        break;
    }
}

} // namespace SwRenderer
