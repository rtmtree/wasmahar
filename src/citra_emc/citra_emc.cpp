#include <algorithm>
#include <iostream>
#include <string>
#include <atomic>
#include <stdexcept>

#include "common/detached_tasks.h"
#include "common/settings.h"
#include "common/file_util.h"

#include "core/core.h"
#include "core/frontend/applets/default_applets.h"
#include "core/frontend/framebuffer_layout.h"
#include "input_common/main.h"
#include "network/network.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

#include "citra_emc/gles_emu_window/emu_window_sdl2.h"
#include "citra_emc/gles_emu_window/emu_window_sdl2_gl.h"
#include "citra_emc/citra_emc.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// ---------------------------------------------------------------------------
// Global state shared with JavaScript
// ---------------------------------------------------------------------------
static std::string g_rom_filepath;
static std::atomic_bool g_rom_path_set{false};
static std::atomic_bool g_emulator_running{false};

static Core::System* g_system = nullptr;
static EmuWindow_SDL2* g_emu_window = nullptr;

extern "C" void SetEmcRomPath(const char* path) {
    if (path && path[0] != '\0') {
        g_rom_filepath = path;
        g_rom_path_set = true;
    }
}

// ---------------------------------------------------------------------------
// Direct touch input from JS. Emscripten's SDL2 port routes mouse/touch
// events through multiple SDL_Windows (main + hidden dummies for shared
// GL contexts), and the mouse-focus accounting drops motion events for
// the render window. Calling the EmuWindow Touch API directly avoids all
// of that indirection — JS passes canvas-local pixel coords.
// ---------------------------------------------------------------------------
extern "C" void EmcTouchDown(int x, int y) {
    if (g_emu_window) {
        g_emu_window->TouchPressed(static_cast<unsigned>(std::max(x, 0)),
                                   static_cast<unsigned>(std::max(y, 0)));
    }
}

extern "C" void EmcTouchMove(int x, int y) {
    if (g_emu_window) {
        g_emu_window->TouchMoved(static_cast<unsigned>(std::max(x, 0)),
                                 static_cast<unsigned>(std::max(y, 0)));
    }
}

extern "C" void EmcTouchUp() {
    if (g_emu_window) {
        g_emu_window->TouchReleased();
    }
}

// ---------------------------------------------------------------------------
// Error-safe initialization — returns false instead of calling exit()
// ---------------------------------------------------------------------------
static bool InitEmulator() {
    try {
        std::cout << "[Azahar] Initializing emulator..." << std::endl;

        Common::DetachedTasks detached_tasks;

        Common::Log::Initialize();
        Common::Log::Start();

        std::cout << "[Azahar] Logging initialized" << std::endl;

        // Wait for JS to provide a ROM path
        if (!g_rom_path_set) {
            std::cout << "[Azahar] No ROM path set. Call SetEmcRomPath() first." << std::endl;
            return false;
        }

        std::cout << "[Azahar] ROM path: " << g_rom_filepath << std::endl;

        // Ensure required user data directories exist
        const auto user_path = FileUtil::GetUserPath(FileUtil::UserPath::UserDir);
        std::cout << "[Azahar] User data dir: " << user_path << std::endl;
        FileUtil::CreateFullPath(user_path + "sysdata/");
        FileUtil::CreateFullPath(user_path + "nand/data/00000000000000000000000000000000/extdata/");
        FileUtil::CreateFullPath(user_path + "sdmc/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/extdata/");
        FileUtil::CreateFullPath(user_path + "config/");
        FileUtil::CreateFullPath(user_path + "cache/");
        std::cout << "[Azahar] User directories created" << std::endl;

        auto& system = Core::System::GetInstance();
        g_system = &system;

        Frontend::RegisterDefaultApplets(system);

        std::cout << "[Azahar] Created system and registered default applets" << std::endl;

        EmuWindow_SDL2::InitializeSDL2();

        std::cout << "[Azahar] SDL Initialized" << std::endl;
        std::cout << "[Azahar] Creating emu window..." << std::endl;

        auto emu_window_ptr = std::make_unique<EmuWindow_SDL2_GL>(system, true, false);
        g_emu_window = emu_window_ptr.get();

        std::cout << "[Azahar] Created window" << std::endl;

        const auto scope = emu_window_ptr->Acquire();

        std::cout << "[Azahar] Acquired window scope, loading ROM..." << std::endl;

#ifdef __EMSCRIPTEN__
        // Force software renderer — WebGL2 lacks GL_TEXTURE_BUFFER needed by OpenGL renderer
        Settings::values.graphics_api.SetValue(Settings::GraphicsAPI::Software);
        std::cout << "[Azahar] Using software renderer (WebGL2)" << std::endl;
#endif

        const Core::System::ResultStatus load_result{system.Load(*emu_window_ptr, g_rom_filepath, nullptr)};

        std::cout << "[Azahar] Load result: " << static_cast<int>(load_result) << std::endl;

        switch (load_result) {
        case Core::System::ResultStatus::Success:
            std::cout << "[Azahar] ROM loaded successfully!" << std::endl;
            break;
        case Core::System::ResultStatus::ErrorGetLoader:
            std::cerr << "[Azahar] Failed to obtain loader for " << g_rom_filepath << std::endl;
            return false;
        case Core::System::ResultStatus::ErrorLoader:
            std::cerr << "[Azahar] Failed to load ROM!" << std::endl;
            return false;
        case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted:
            std::cerr << "[Azahar] ROM is encrypted. Must be decrypted first." << std::endl;
            return false;
        case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
            std::cerr << "[Azahar] ROM format not supported." << std::endl;
            return false;
        case Core::System::ResultStatus::ErrorNotInitialized:
            std::cerr << "[Azahar] CPU core not initialized." << std::endl;
            return false;
        case Core::System::ResultStatus::ErrorSystemMode:
            std::cerr << "[Azahar] Failed to determine system mode." << std::endl;
            return false;
        default:
            std::cerr << "[Azahar] Error loading ROM: " << system.GetStatusDetails() << std::endl;
            return false;
        }

        // Load disk resources
        std::atomic_bool stop_run{false};
        system.GPU().Renderer().Rasterizer()->LoadDefaultDiskResources(
            stop_run, [](VideoCore::LoadCallbackStage stage, std::size_t value, std::size_t total) {
                std::cout << "[Azahar] Loading resources: stage=" << static_cast<u32>(stage)
                          << " progress=" << value << "/" << total << std::endl;
            });

        std::cout << "[Azahar] Disk resources loaded. Starting emulation loop." << std::endl;

        // Keep the window and scope alive — they must outlive the main loop.
        // In the emscripten path, emscripten_set_main_loop does not return,
        // so we intentionally leak these — they get cleaned up on page unload.
        emu_window_ptr.release();
        new decltype(scope)(std::move(scope));

        g_emulator_running = true;
        return true;
    } catch (const std::exception& e) {
        std::cout << "[Azahar] Exception during init: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cout << "[Azahar] Unknown exception during init" << std::endl;
        return false;
    }
}

// ---------------------------------------------------------------------------
// Single iteration of the emulation main loop
// ---------------------------------------------------------------------------
static void MainLoopIter() {
    if (!g_emulator_running || !g_system || !g_emu_window) {
        return;
    }

    // Run multiple CPU slices per rAF tick, bounded by a real-time budget.
    // The ARM_DynCom interpreter is ~10× slower than the native Dynarmic JIT
    // (Dynarmic can't emit code in WASM), so a single slice per rAF only runs
    // the game at ~10-15 % speed. Burn up to 12 ms of wall-time per tick
    // (out of a 16.6 ms frame budget) stacking back-to-back slices — this
    // lifts the game to ~90 % of real-time speed when CPU-bound, without
    // starving the browser event loop.
    constexpr double kBudgetMs = 12.0;
    const double start_ms = emscripten_get_now();
    Core::System::ResultStatus result = Core::System::ResultStatus::Success;
    do {
        result = g_system->RunLoop();
        if (result != Core::System::ResultStatus::Success) break;
    } while (emscripten_get_now() - start_ms < kBudgetMs);

    if (result != Core::System::ResultStatus::Success &&
        result != Core::System::ResultStatus::ShutdownRequested) {
        std::cerr << "[Azahar] RunLoop error: " << static_cast<int>(result) << std::endl;
    }

    switch (result) {
    case Core::System::ResultStatus::ShutdownRequested:
        std::cout << "[Azahar] Shutdown requested." << std::endl;
        g_emu_window->RequestClose();
        g_emulator_running = false;
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        break;
    case Core::System::ResultStatus::Success:
        break;
    default:
        std::cerr << "[Azahar] Error in run loop: " << static_cast<int>(result) << std::endl;
        break;
    }

#ifdef __EMSCRIPTEN__
    // Present the frame to the canvas after each emulation step.
    // In Emscripten, there is no separate Present thread — we do it here.
    // Present() will make the window GL context current, call TryPresent,
    // then SDL_GL_SwapWindow.
    if (g_system && g_emu_window) {
        g_emu_window->Present();
    }
#endif
}

extern "C" void LaunchEmcFrontend(int argc, char** argv) {
    std::cout << "[Azahar] Welcome to Azahar WASM" << std::endl;

    if (!InitEmulator()) {
        std::cerr << "[Azahar] Initialization failed." << std::endl;
        return;
    }

#ifdef __EMSCRIPTEN__
    // Use emscripten's main loop instead of std::thread + while loop.
    // This is the browser-compatible way to run a persistent loop.
    emscripten_set_main_loop(MainLoopIter, 0, 1);
#else
    // Native fallback — use a regular while loop
    while (g_emulator_running && g_emu_window && g_emu_window->IsOpen()) {
        MainLoopIter();
    }
#endif

    // Cleanup (only reached in native mode)
    if (g_system) {
        Network::Shutdown();
        InputCommon::Shutdown();
        g_system->Shutdown();
    }

    std::cout << "[Azahar] Bye!" << std::endl;
}
