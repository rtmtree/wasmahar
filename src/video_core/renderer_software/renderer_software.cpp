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
    if (gl_texture) glDeleteTextures(1, &gl_texture);
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

    glGenTextures(1, &gl_texture);
    glBindTexture(GL_TEXTURE_2D, gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenVertexArrays(1, &gl_vao);
    glGenBuffers(1, &gl_vbo);

    gl_initialized = true;
}

void RendererSoftware::DrawScreen(const ScreenInfo& info, float x, float y, float w, float h) {
    if (info.width == 0 || info.height == 0 || info.pixels.empty()) return;

    glBindTexture(GL_TEXTURE_2D, gl_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, info.width, info.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, info.pixels.data());

    float verts[] = {
        x,     y,     0.0f, 1.0f,
        x + w, y,     1.0f, 1.0f,
        x + w, y + h, 1.0f, 0.0f,
        x,     y + h, 0.0f, 0.0f,
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
    DrawScreen(screen_infos[0], top_x, top_y, top_w, top_h);

    // Draw bottom screen (screen_infos[2])
    const auto& bot = layout.bottom_screen;
    float bot_x = (float)bot.left / layout.width * 2.0f - 1.0f;
    float bot_y = 1.0f - (float)bot.top / layout.height * 2.0f;
    float bot_w = (float)(bot.right - bot.left) / layout.width * 2.0f;
    float bot_h = -(float)(bot.bottom - bot.top) / layout.height * 2.0f;
    DrawScreen(screen_infos[2], bot_x, bot_y, bot_w, bot_h);
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
    info.width = framebuffer.height;   // display width = number of memory rows
    info.height = pixel_stride;        // display height = column length
    info.pixels.resize(info.width * info.height * 4);

    for (u32 display_x = 0; display_x < info.width; display_x++) {
        for (u32 display_y = 0; display_y < info.height; display_y++) {
            // Memory is column-major, but column 0 in memory corresponds to
            // the rightmost column on the display (the 3DS scans columns
            // right-to-left). Each column is stored bottom-to-top, so:
            //   fb[(width-1-display_x) * pixel_stride + (pixel_stride-1-display_y)]
            const u8* pixel = framebuffer_data +
                ((info.width - 1 - display_x) * pixel_stride +
                 (pixel_stride - 1 - display_y)) * bpp;
            const Common::Vec4 color = [&] {
                switch (framebuffer.color_format) {
                case Pica::PixelFormat::RGBA8:
                    return Common::Color::DecodeRGBA8(pixel);
                case Pica::PixelFormat::RGB8:
                    return Common::Color::DecodeRGB8(pixel);
                case Pica::PixelFormat::RGB565:
                    return Common::Color::DecodeRGB565(pixel);
                case Pica::PixelFormat::RGB5A1:
                    return Common::Color::DecodeRGB5A1(pixel);
                case Pica::PixelFormat::RGBA4:
                    return Common::Color::DecodeRGBA4(pixel);
                }
                UNREACHABLE();
            }();
            // Row-major destination: (display_y, display_x) in a WxH image.
            u8* dest = info.pixels.data() + (display_y * info.width + display_x) * 4;
            std::memcpy(dest, color.AsArray(), sizeof(color));
        }
    }
}

} // namespace SwRenderer
