# AMD FidelityFX API (FSR 3.1) for KytyPS5

From the AMD FidelityFX SDK **v1.1.4** (MIT, `LICENSE.txt`):
https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4

- `include/ffx_api/` — the FidelityFX API headers (`ffx-api/include/ffx_api`), the DX12 headers left out.
  Local change: `FFX_API_ENTRY` is `__declspec(dllexport)` on Windows only, so the headers compile on Linux too.
- `bin/amd_fidelityfx_vk.dll` — AMD's signed Vulkan runtime (`PrebuiltSignedDLL`), SHA-256
  `a1624cc4238fef046f30c4d80ce3f47be63fc5f5373f49e3ee9edb9960f54c78`. Installed next to `kyty_emulator.exe`
  and loaded at run time (`src/local/fsr.cpp`); without it the emulator runs, FSR off.

v1.1.4 is the last SDK release with a Vulkan backend: FSR 3.1.4 upscaling and frame generation. FSR 4 ships
for DirectX 12 only (SDK 2.x), so a Vulkan emulator cannot use it directly.
