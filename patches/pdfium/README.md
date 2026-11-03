# PDFium chromium/8125: supplementary ActualText

`8125-actualtext-non-bmp.patch` removes two BMP-only filters in
`CPDF_TextPage::PreMarkedContent` and `ProcessMarkedContent`. It retains the
existing treatment of BMP controls and U+FFFD/U+FFFE/U+FFFF, and accepts
supplementary Unicode scalars through U+10FFFF. This targets Linux/macOS
with 32-bit wchar_t. Windows remains outside this port.

Source: https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8125/core/fpdftext/cpdf_textpage.cpp

Original file SHA-256:
`c4dc07072e6bb2d2223b58ce94db3d5e5d01514f8c609896dc53c1d0cc1a1c23`.
Apply with `git apply --check /path/to/8125-actualtext-non-bmp.patch`, then
`git apply /path/to/8125-actualtext-non-bmp.patch` in that PDFium checkout.
Build PDFium using its own build instructions and provide the resulting SDK
via `PDFium_ROOT`. RMRender does not modify an installed binary.

Configure RMRender with `-DRMRENDER_REQUIRE_PDFIUM_ACTUALTEXT=ON` and run
`ctest --test-dir build -R pdfium_actualtext --output-on-failure` after building
the test. The test checks Unicode scalars through `FPDFText_GetUnicode` for BMP,
emoji, skin tone, mixed text, variation-selector and ZWJ cases. Do not use the
legacy UCS-2 `FPDFText_GetText` API to represent supplementary characters.

The default capability test reports a CTest skip only for the exact known
BMP-only behavior. Unexpected extraction results fail. Strict mode turns that
skip into a failure, so an unpatched SDK cannot pass as fixed.

## Reproducible SDK recipe

Run `python3 scripts/build-pdfium.py --work /tmp/pdfium-build --output /path/to/sdk`.
The recipe pins PDFium commit `43d55e429cf72c21e9a1e60475b50feefa16fda9`, depot_tools,
upstream SDK packaging patches and their SHA-256 checksums. Chromium's DEPS pins
its dependencies and Clang. The SDK includes public headers, the shared library,
license notices and `rmrender-pdfium.json` with source/patch/library hashes and
GN options. It does not alter an installed SDK or download during CMake configure.
Requires git, Python 3.12+, Ninja and native development tools; Linux ARM64 hosts
need support for executing Chromium's x86-64 compiler. Windows is not supported.
The native system SDK/libc is still a build input; this is a pinned build recipe,
not a promise of byte-identical binaries across different host toolchains.

The rebuilt patched SDK passed the strict capability test on Linux ARM64 and
macOS ARM64. The `Pinned renderer` CI workflow builds this SDK and requires the
test to pass, with no known-bug skip. `fetch-pdfium.py` intentionally continues
to fetch the checksum-pinned **stock** SDK: those binaries remain unpatched.
If replacing an SDK in an existing CMake build, clear cached `PDFium_LIBRARY`
and `PDFium_INCLUDE_DIR` or use a new build directory, then rebuild test targets.
