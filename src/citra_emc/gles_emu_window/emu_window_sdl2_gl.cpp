// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <string>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include "citra_emc/gles_emu_window/emu_window_sdl2_gl.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

class SDLGLContext : public Frontend::GraphicsContext {
public:
    using SDL_GLContext = void*;

    SDLGLContext() {
        window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                  SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
        context = window ? SDL_GL_CreateContext(window) : nullptr;
    }

    ~SDLGLContext() override {
#ifdef __EMSCRIPTEN__
        // Skip SDL cleanup in Emscripten — browser handles it.
        context = nullptr;
        window = nullptr;
#else
        try {
            if (context) SDL_GL_DeleteContext(context);
            if (window) SDL_DestroyWindow(window);
        } catch (...) {}
#endif
    }

    void MakeCurrent() override {
        if (window && context) SDL_GL_MakeCurrent(window, context);
    }

    void DoneCurrent() override {
        if (window) SDL_GL_MakeCurrent(window, nullptr);
    }

private:
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
};

static SDL_Window* CreateGLWindow(const std::string& window_title, bool gles) {
    if (gles) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    }
    return SDL_CreateWindow(window_title.c_str(),
                            SDL_WINDOWPOS_UNDEFINED, // x position
                            SDL_WINDOWPOS_UNDEFINED, // y position
                            Core::kScreenTopWidth,
                            Core::kScreenTopHeight + Core::kScreenBottomHeight,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
}

EmuWindow_SDL2_GL::EmuWindow_SDL2_GL(Core::System& system_, bool fullscreen, bool is_secondary)
    : EmuWindow_SDL2{system_, is_secondary} {
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    // Enable context sharing for the shared context
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    // Enable vsync
    SDL_GL_SetSwapInterval(1);
    // Enable debug context
    if (Settings::values.renderer_debug) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

    std::string window_title = fmt::format("Azahar {} | {}-{}", Common::g_build_fullname,
                                           Common::g_scm_branch, Common::g_scm_desc);

    // First, try to create a context with the requested type (GLES).
    render_window = CreateGLWindow(window_title, true);
    if (render_window == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create SDL2 window: {}", SDL_GetError());
        throw std::runtime_error("Failed to create SDL2 window: " + std::string(SDL_GetError()));
    }

    strict_context_required = std::strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0;

    dummy_window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                    SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);

    if (fullscreen) {
        Fullscreen();
    }

    window_context = SDL_GL_CreateContext(render_window);
    if (window_context == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create SDL2 GL context: {}", SDL_GetError());
        throw std::runtime_error("Failed to create SDL2 GL context: " + std::string(SDL_GetError()));
    }

    core_context = CreateSharedContext();
    last_saved_context = nullptr;

    if (core_context == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create shared SDL2 GL context: {}", SDL_GetError());
        throw std::runtime_error("Failed to create shared GL context: " + std::string(SDL_GetError()));
    }

    render_window_id = SDL_GetWindowID(render_window);

    int profile_mask = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile_mask);
    auto gl_load_func =
        profile_mask == SDL_GL_CONTEXT_PROFILE_ES ? gladLoadGLES2Loader : gladLoadGLLoader;

    if (!gl_load_func(static_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        LOG_CRITICAL(Frontend, "Failed to initialize GL functions: {}", SDL_GetError());
        throw std::runtime_error("Failed to initialize GL functions: " + std::string(SDL_GetError()));
    }

    OnResize();
    OnMinimalClientAreaChangeRequest(GetActiveConfig().min_client_area_size);
    SDL_PumpEvents();
}

EmuWindow_SDL2_GL::~EmuWindow_SDL2_GL() {
    std::cout << "[Azahar] ~EmuWindow_SDL2_GL: starting cleanup" << std::endl;

    // core_context (shared SDLGLContext) cleanup
    try {
        core_context.reset();
        std::cout << "[Azahar] ~EmuWindow_SDL2_GL: core_context reset done" << std::endl;
    } catch (...) {
        std::cout << "[Azahar] ~EmuWindow_SDL2_GL: core_context reset threw" << std::endl;
    }

    // NOTE: SDL_GL_DeleteContext can abort() in Emscripten if the GL context
    // is in a bad state or SDL has been partially shut down. Skip it —
    // the browser will clean up all resources when the page unloads.
    std::cout << "[Azahar] ~EmuWindow_SDL2_GL: skipping GL context delete (Emscripten)" << std::endl;
    window_context = nullptr;

    // Same for render_window — skip SDL_DestroyWindow to avoid abort in Emscripten.
    std::cout << "[Azahar] ~EmuWindow_SDL2_GL: skipping window destroy (Emscripten)" << std::endl;
    render_window = nullptr;

    std::cout << "[Azahar] ~EmuWindow_SDL2_GL: cleanup done" << std::endl;
}

std::unique_ptr<Frontend::GraphicsContext> EmuWindow_SDL2_GL::CreateSharedContext() const {
    return std::make_unique<SDLGLContext>();
}

void EmuWindow_SDL2_GL::MakeCurrent() {
    core_context->MakeCurrent();
}

void EmuWindow_SDL2_GL::DoneCurrent() {
    core_context->DoneCurrent();
}

void EmuWindow_SDL2_GL::SaveContext() {
    last_saved_context = SDL_GL_GetCurrentContext();
}

void EmuWindow_SDL2_GL::RestoreContext() {
    if (render_window) SDL_GL_MakeCurrent(render_window, last_saved_context);
}

void EmuWindow_SDL2_GL::Present() {
    if (!render_window || !window_context) return;
    SDL_GL_MakeCurrent(render_window, window_context);
    SDL_GL_SetSwapInterval(0);

#ifdef __EMSCRIPTEN__
    // Single-frame present for Emscripten — the main loop calls this each iteration.
    // The renderer must draw while the window GL context is current, not the core
    // (shared) context left current by Acquire().
    system.GPU().Renderer().TryPresent(0, is_secondary);
    SDL_GL_SwapWindow(render_window);
#else
    while (IsOpen()) {
        system.GPU().Renderer().TryPresent(100, is_secondary);
        SDL_GL_SwapWindow(render_window);
    }
    SDL_GL_MakeCurrent(render_window, nullptr);
#endif
}
