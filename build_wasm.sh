#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# WASM Build Script for Azahar (3DS Emulator)
# Builds the Emscripten (EMC) frontend targeting WebAssembly + WebGL2
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_wasm"
OUTPUT_DIR="${SCRIPT_DIR}/web-app/public/wasm"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

log_info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_step()  { echo -e "${CYAN}[STEP]${NC}  $*"; }

# ---------------------------------------------------------------------------
# Check prerequisites
# ---------------------------------------------------------------------------
check_emscripten() {
    if ! command -v emcc &>/dev/null; then
        log_error "Emscripten SDK (emcc) not found in PATH."
        log_error "Install it from https://emscripten.org/docs/getting_started/"
        log_error ""
        log_error "  git clone https://github.com/emscripten-core/emsdk.git"
        log_error "  cd emsdk"
        log_error "  ./emsdk install latest"
        log_error "  ./emsdk activate latest"
        log_error "  source ./emsdk_env.sh"
        exit 1
    fi

    EMCC_VERSION=$(emcc --version | head -1)
    log_info "Using ${EMCC_VERSION}"
}

check_cmake() {
    if ! command -v cmake &>/dev/null; then
        log_error "CMake not found. Install it first."
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Git submodules
# ---------------------------------------------------------------------------
check_submodules() {
    log_step "Checking git submodules..."

    if [ ! -d "${SCRIPT_DIR}/.git" ]; then
        log_warn "Not a git repository. Skipping submodule check."
        return 0
    fi

    # Check if any submodule is missing (status lines starting with '-')
    local MISSING
    MISSING=$(cd "${SCRIPT_DIR}" && git submodule status 2>/dev/null | grep -c '^-') || true

    if [ "${MISSING}" -gt 0 ]; then
        log_info "${MISSING} submodule(s) not initialized. Fetching..."
        cd "${SCRIPT_DIR}"
        git submodule update --init --recursive
        log_info "Submodules initialized."
    else
        log_info "All submodules present."
    fi
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
build() {
    local BUILD_TYPE="${1:-Release}"

    log_info "Build type: ${BUILD_TYPE}"
    log_info "Build directory: ${BUILD_DIR}"

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # Configure
    log_step "Running CMake configure..."
    emcmake cmake "${SCRIPT_DIR}" \
        -DENABLE_EMC=ON \
        -DENABLE_QT=OFF \
        -DENABLE_SDL2_FRONTEND=OFF \
        -DENABLE_ROOM=OFF \
        -DENABLE_TESTS=OFF \
        -DENABLE_WEB_SERVICE=OFF \
        -DENABLE_SCRIPTING=OFF \
        -DENABLE_CUBEB=OFF \
        -DENABLE_OPENAL=ON \
        -DENABLE_VULKAN=OFF \
        -DENABLE_OPENGL=ON \
        -DENABLE_SOFTWARE_RENDERER=ON \
        -DENABLE_MICROPROFILE=OFF \
        -DENABLE_LTO=ON \
        -DUSE_SYSTEM_SDL2=OFF \
        -DUSE_SYSTEM_BOOST=OFF \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

    # Build
    log_step "Compiling..."
    local JOBS
    JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    log_info "Using ${JOBS} parallel jobs"
    cmake --build . --parallel "${JOBS}"
}

# ---------------------------------------------------------------------------
# Copy output to React project's public directory
# ---------------------------------------------------------------------------
copy_output() {
    local BIN_DIR="${BUILD_DIR}/bin"

    if [ ! -d "${BIN_DIR}" ]; then
        log_error "Build output not found at ${BIN_DIR}"
        log_error "Did the build complete successfully?"
        exit 1
    fi

    log_step "Copying WASM output to ${OUTPUT_DIR}..."
    mkdir -p "${OUTPUT_DIR}"

    # Emscripten produces: .html, .js, .wasm, and potentially .data (preload)
    local COPIED=0
    for ext in html js wasm data; do
        for f in "${BIN_DIR}"/*/*."${ext}" "${BIN_DIR}"/*."${ext}" "${BUILD_DIR}"/*."${ext}"; do
            if [ -f "$f" ]; then
                cp "$f" "${OUTPUT_DIR}/"
                COPIED=$((COPIED + 1))
                log_info "  Copied $(basename "$f")"
            fi
        done
    done

    # Also grab worker files (.worker.js)
    for f in "${BIN_DIR}"/*/*.worker.js "${BIN_DIR}"/*.worker.js "${BUILD_DIR}"/*.worker.js; do
        if [ -f "$f" ]; then
            cp "$f" "${OUTPUT_DIR}/"
            COPIED=$((COPIED + 1))
            log_info "  Copied $(basename "$f")"
        fi
    done

    if [ "$COPIED" -eq 0 ]; then
        log_warn "No WASM output files found. Check the build completed successfully."
    else
        log_info "Copied ${COPIED} file(s) to ${OUTPUT_DIR}"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
usage() {
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  build    Build WASM (default)"
    echo "  copy     Copy build output to React project"
    echo "  all      Full pipeline: submodules + build + copy"
    echo "  clean    Remove build directory"
    echo "  help     Show this help"
    echo ""
    echo "Environment:"
    echo "  BUILD_TYPE  Release (default) or Debug"
}

COMMAND="${1:-build}"

case "${COMMAND}" in
    build)
        check_emscripten
        check_cmake
        check_submodules
        build "${BUILD_TYPE:-Release}"
        ;;
    copy)
        copy_output
        ;;
    all)
        check_emscripten
        check_cmake
        check_submodules
        build "${BUILD_TYPE:-Release}"
        copy_output
        ;;
    clean)
        log_info "Cleaning ${BUILD_DIR}..."
        rm -rf "${BUILD_DIR}"
        log_info "Done."
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        log_error "Unknown command: ${COMMAND}"
        usage
        exit 1
        ;;
esac
