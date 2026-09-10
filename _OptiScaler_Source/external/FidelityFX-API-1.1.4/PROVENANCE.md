# FidelityFX API headers for this OptiScaler fork

Unmodified API headers and LICENSE.txt from AMD's official FidelityFX SDK:

- Repository: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- Tag: v1.1.4
- Commit: c6efa6bf7f2027b3ec94f28578bb5965eabb9e55
- Paths: `ffx-api/include/**` and `LICENSE.txt`.

This fork uses the legacy combined DX12/Vulkan API headers. The separately
vendored SDK in `../FidelityFX-SDK` has the newer Kits layout and lacks the
legacy Vulkan API header. Mixing those headers also redefines frame-generation
structures provided locally by the fork. Keep the two include roots separate.

These files supply build declarations only, not runtime DLLs or neural models.
