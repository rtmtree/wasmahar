# Azahar Web

A React + Vite front-end for the Azahar 3DS emulator compiled to WebAssembly. Runs a decrypted `.3ds` ROM in any cross-origin-isolated browser context, with an on-screen controller, canvas touch input, and IndexedDB-backed save persistence.

## Running locally

```bash
# In the repo root — build the WASM core (first time, ~10-20 min depending on machine)
./build_wasm.sh all

# Then serve the React app
cd web-app
npm install
npm run dev
```

Open the URL Vite prints (default `http://localhost:5180/` if using the pinned config in `../.claude/launch.json`, otherwise `http://localhost:5173/`). The dev server sends the `Cross-Origin-Embedder-Policy: require-corp` and `Cross-Origin-Opener-Policy: same-origin` headers required for `SharedArrayBuffer` — without these the emulator cannot start.

By default a sample ROM (`public/ab.3ds`, Angry Birds Star Wars) auto-loads on page open. Drag-and-drop any other decrypted `.3ds` / `.3dsx` / `.cxi` onto the drop zone to switch games.

## Controls

### Virtual on-screen controller

| Button | Mapping | 3DS button |
| --- | --- | --- |
| ▲ ◀ ▶ ▼ | KeyT / F / H / G | D-pad |
| A B X Y | KeyA / S / Z / X | Face buttons |
| L R | KeyQ / W | Shoulder |
| SELECT START | KeyN / M | Select / Start |
| ↑ ← → ↓ | Arrow keys | Circle Pad (analog) |

### Canvas touch

Click or touch anywhere on the emulator canvas — the lower half of the 400×480 area is the 3DS bottom screen. Pointer events are dispatched straight to the 3DS input service via a custom `EmcTouchDown/Move/Up` ccall (bypasses a quirk in Emscripten's SDL2 port that routed mouse-motion events to a hidden window and dropped them).

### Physical keyboard

Any physical key that matches the virtual controller's mapping also works — the virtual buttons just synthesize the same `KeyboardEvent`s.

## Save persistence

Game save data, NAND state and emulator config all live under `/home/web_user/.local/share/azahar-emu/` in the Emscripten virtual filesystem. That path is mounted as [`IDBFS`](https://emscripten.org/docs/api_reference/Filesystem-API.html#filesystem-api-idbfs) so everything is round-tripped to IndexedDB:

- **On boot**: `FS.syncfs(true, …)` pulls any previously-saved files into MEMFS *before* the emulator starts, so games see their saves on startup.
- **Every 10 seconds** while the emulator runs, plus on `beforeunload`, `pagehide`, and tab `visibilitychange: hidden`: `FS.syncfs(false, …)` flushes dirty MEMFS entries back to IndexedDB.

In practice: close the tab mid-level, reopen it, and your game progress is still there. The only loss window is up to 10 s of unflushed writes if the browser force-kills the tab.

## Performance notes

Runs at ~60 FPS (real-time) on lightweight 2D games like Angry Birds Star Wars; ~20-30 FPS on heavier 3D titles. The main speed ceilings are:

1. **ARM_DynCom interpreter** — WASM can't host the Dynarmic JIT backend, so ARM CPU code runs through an interpreter (roughly 10× slower than native JIT).
2. **Software rasterizer** — Azahar's OpenGL rasterizer relies on `GL_TEXTURE_BUFFER` and GLSL ES 3.20+ features not available in WebGL2, so rendering runs on the CPU.

See `../WASM.md` for build flags and tuning knobs, and the root repo for the upstream Azahar project.

## Architecture highlights

A handful of Emscripten-specific adaptations in the C++ core make the web build work:

| Adaptation | Where |
| --- | --- |
| Custom main loop with a per-tick CPU budget (stacks multiple `RunLoop()` slices per `requestAnimationFrame` to offset the interpreter's slowness) | [`src/citra_emc/citra_emc.cpp`](../src/citra_emc/citra_emc.cpp) |
| Software renderer with per-frame FNV-1a dirty-check (skips re-upload + format conversion when the 3DS framebuffer hasn't changed) | [`src/video_core/renderer_software/renderer_software.cpp`](../src/video_core/renderer_software/renderer_software.cpp) |
| HLE audio mixer skipped on Emscripten (keeps per-source `Tick()` so games don't deadlock polling status, drops the heavy mixer path) | [`src/audio_core/hle/hle.cpp`](../src/audio_core/hle/hle.cpp) |
| SDL2 input-event window-ID filter bypass (Emscripten's SDL port emits events with `windowID=0`; the upstream code was dropping them) | [`src/citra_emc/gles_emu_window/emu_window_sdl2.cpp`](../src/citra_emc/gles_emu_window/emu_window_sdl2.cpp) |
| `#canvas` id required by SDL2's event target, plus a force-resize after window creation (SDL momentarily collapses the canvas to 1×1 while probing CSS sizing) | [`src/components/EmulatorCanvas.tsx`](src/components/EmulatorCanvas.tsx), [`src/citra_emc/gles_emu_window/emu_window_sdl2_gl.cpp`](../src/citra_emc/gles_emu_window/emu_window_sdl2_gl.cpp) |
| Direct `EmcTouch*` ccall path for canvas pointer events (SDL's multi-window mouse-focus accounting drops motion events for the render window) | [`src/hooks/useEmulator.ts`](src/hooks/useEmulator.ts), [`src/citra_emc/citra_emc.cpp`](../src/citra_emc/citra_emc.cpp) |
| IDBFS mount before `callMain()` + periodic `syncfs` flush | [`src/hooks/useEmulator.ts`](src/hooks/useEmulator.ts) |

## Browser requirements

- **SharedArrayBuffer + cross-origin isolation.** Without these, WASM memory can't be shared with worker threads and the emulator hangs on boot. The app checks for this up-front and shows a clear error if missing.
- **WebGL2.** Used by the software renderer for the final upload-and-draw.
- **IndexedDB.** Used for save persistence. If unavailable, saves are session-only and a console warning is logged (non-fatal).

The embedded Electron preview in tools like Claude Code has `SharedArrayBuffer` disabled — open `http://localhost:5180/` in a regular Chrome/Firefox window instead.

## License

Same as the upstream Azahar project. See root `../license.txt`.
