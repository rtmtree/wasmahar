## Building WASM (Emscripten)

### Prerequisites

1. **Emscripten SDK** — https://emscripten.org/docs/getting_started/
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **CMake** >= 3.15

### Quick Build

```bash
# Build + copy output to web-app/public/wasm/
./build_wasm.sh all
```

### Build Script Commands

| Command | Description |
|---------|-------------|
| `./build_wasm.sh build` | Compile WASM only |
| `./build_wasm.sh copy` | Copy build output to React project |
| `./build_wasm.sh all` | Build + copy (full pipeline) |
| `./build_wasm.sh clean` | Remove build directory |

### Manual Build

```bash
mkdir build_wasm && cd build_wasm
emcmake cmake .. -DENABLE_EMC=ON -DENABLE_QT=OFF -DENABLE_SDL2_FRONTEND=OFF \
  -DENABLE_ROOM=OFF -DENABLE_TESTS=OFF -DENABLE_WEB_SERVICE=OFF \
  -DENABLE_VULKAN=OFF -DENABLE_LTO=OFF
cmake --build . --parallel
```

### Running the Web App

```bash
cd web-app
npm install
npm run dev
```

The dev server automatically sets the required COOP/COEP headers for SharedArrayBuffer support.

Open http://localhost:5173 and drag a decrypted .3ds ROM onto the page.
