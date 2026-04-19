# Building the Azahar WASM / Web target

Compiles the EMC frontend (see `src/citra_emc/`) to WebAssembly with the Emscripten toolchain and drops the output into `web-app/public/wasm/`.

## Prerequisites

1. **Emscripten SDK** — https://emscripten.org/docs/getting_started/
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **CMake** ≥ 3.15, **Node.js** ≥ 20 (for the web-app), **Python** 3 (for Emscripten tooling).

## Quick build

```bash
# Build + copy output to web-app/public/wasm/
./build_wasm.sh all
```

First build is ~10-20 minutes (emscripten compiles SDL2, OpenAL, cryptopp, libressl etc. as system libraries). Incremental rebuilds touching only the Azahar core are under a minute.

## Build script commands

| Command | Description |
|---------|-------------|
| `./build_wasm.sh build` | Configure + compile WASM |
| `./build_wasm.sh copy` | Copy `bin/Release/azahar.{js,wasm}` to `web-app/public/wasm/` |
| `./build_wasm.sh all` | Build + copy (full pipeline) |
| `./build_wasm.sh clean` | Remove `build_wasm/` |

## Current Emscripten link flags

Set in `CMakeLists.txt` under `if (EMSCRIPTEN)`. Key choices:

| Flag | Why |
| --- | --- |
| `-O3 -flto` (link) + `-fexperimental-library -fwasm-exceptions` | Binaryen gets to run full post-link optimizations. `-fwasm-exceptions` uses the native WASM exception-handling proposal (faster than the JS-based fallback). |
| `-pthread -sPTHREAD_POOL_SIZE=15` | Web Worker-backed pthreads. Pool is sized so the software rasterizer's scanline workers, logging thread, async tasks and audio DSP thread all have pre-spun workers. |
| `-sINITIAL_MEMORY=1024MB -sALLOW_MEMORY_GROWTH=0` | Fixed 1 GiB heap. Growable memory with pthreads forces a `growMemViews()` wrapper around every HEAP access (~30 % slowdown on pthread builds) — we pre-allocate instead. Covers the 256 MB ROM + FCRAM + VRAM + OS overhead. |
| `-msimd128`, `-mbulk-memory`, `-mnontrapping-fptoint` (compile) | Modern WASM instruction-set extensions. `-msimd128` lets clang auto-vectorize the software renderer's format-conversion loop. |
| `-sFORCE_FILESYSTEM=1 -lidbfs.js` | Pulls the full FS API (`FS.mount` / `FS.syncfs` / `FS.filesystems`) into the module so the React front-end can mount IndexedDB-backed save storage. |
| `-sUSE_WEBGL2=1 -sMAX_WEBGL_VERSION=2` | WebGL2 context (required by our `glTexImage2D` path for the software-renderer blit). |
| `-sMODULARIZE=1 -sEXPORT_NAME=createAzaharModule -sINVOKE_RUN=0` | Clean factory API — the app awaits the module, writes the ROM into the virtual FS, and explicitly calls `callMain()`. |
| `-sEXPORTED_FUNCTIONS=[_main,_SetEmcRomPath,_EmcTouchDown,_EmcTouchMove,_EmcTouchUp,_EmcKeyDown,_EmcKeyUp]` | C functions the React app calls via `ccall`. Touch handlers are wired directly to `EmuWindow::TouchPressed/Moved/Released` to dodge an SDL2 mouse-focus bug in Emscripten's multi-window setup. `EmcKey{Down,Up}(scancode)` feed `InputCommon::GetKeyboard()` directly so the on-screen virtual controller bypasses SDL2's brittle synthetic-event routing. |

Closure compiler (`--closure 1`) is deliberately **off** — it renames the `FS.mount` / `FS.syncfs` / `FS.filesystems` symbols used by the save-persistence path, and they can't be trivially whitelisted without listing every internal FS member.

Debug info (`-g`) is also deliberately off in Release — having it suppresses Binaryen's post-link optimizations (look for the `running limited binaryen optimizations because DWARF info requested` warning if you see it, it means something pulled DWARF back in).

## Manual build

```bash
mkdir build_wasm && cd build_wasm
emcmake cmake .. \
  -DENABLE_EMC=ON -DENABLE_QT=OFF -DENABLE_SDL2_FRONTEND=OFF \
  -DENABLE_ROOM=OFF -DENABLE_TESTS=OFF -DENABLE_WEB_SERVICE=OFF \
  -DENABLE_SCRIPTING=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=ON \
  -DENABLE_VULKAN=OFF -DENABLE_OPENGL=ON -DENABLE_SOFTWARE_RENDERER=ON \
  -DENABLE_MICROPROFILE=OFF -DENABLE_LTO=ON \
  -DUSE_SYSTEM_SDL2=OFF -DUSE_SYSTEM_BOOST=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build . --target citra_meta --parallel
```

Output is at `build_wasm/bin/Release/azahar.{js,wasm}`. Copy both to `web-app/public/wasm/`.

## Running the web app

```bash
cd web-app
npm install
npm run dev
```

See [`web-app/README.md`](web-app/README.md) for the front-end details, controls, and how save persistence works.

The dev server automatically emits the COOP/COEP headers required for `SharedArrayBuffer`. In production you need these on whatever serves the app — the emulator will refuse to start and show a clear message otherwise.
