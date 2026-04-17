#pragma once

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// Called from main() when ENABLE_EMC is on.
void LaunchEmcFrontend(int argc, char** argv);

// Called from JavaScript to set the ROM file path before launching.
// The ROM must already be written to the Emscripten virtual filesystem
// (e.g. via FS.writeFile) before calling this.
void SetEmcRomPath(const char* path);

#ifdef __cplusplus
}
#endif
