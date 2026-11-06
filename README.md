# RMRender (C23)

Rendering for reMarkable pages and documents: handwriting with its pen
textures, typed text, highlights, images and page templates, drawn to a bitmap,
a vector PDF or SVG. A C23 library with a portable core, a CoreGraphics/CoreText
backend for Apple platforms and a Cairo/Pango/PDFium backend for Linux, macOS
and Windows, plus adapters for the sibling C packages that read `.rm` pages,
`.rmdoc` archives and highlighted text.

The C libraries need no Swift runtime. The build and ownership conventions
follow [rmlines-lib](https://github.com/altessa-s/rmlines-lib): CMake, static or
shared libraries, an explicitly exported C API, status codes and owned results
with one release function. The same renderer is available for Swift as
[rmrender-swift](https://github.com/altessa-s/rmrender-swift).

Part of **[Slate Drive](https://slatedrive.app)**
([repository](https://github.com/altessa-s/slatedrive)), the macOS app that
shows a reMarkable library in Finder.

The API and ABI stay compatible within a major version; the shared-library
ABI name is the major version. The version is declared once in
`include/rmrender/rmrender.h`; the release workflow requires the pushed tag to
match it.

## What it renders

| Component | CMake target | Needs | Provides |
|---|---|---|---|
| Core | `RMRender::core` | zlib | Stroke outlines, grain masks, owning render plans, the backend ABI, a portable SVG writer, paper and sheet sizing, template expressions and JSON templates, `.content` page order, PDF overlay import and structure-preserving PDF updates |
| Apple | `RMRender::apple` | CoreGraphics, CoreText | RGBA bitmaps, multipage vector PDF, SVG, sheet output, PDF source pages with remapped links and outlines |
| Cairo | `RMRender::linux` | Cairo, Pango, GdkPixbuf, PDFium | RGBA bitmaps, vector PDF, SVG and source-page composition |
| Cairo text | `RMRender::linux_text` | Cairo + RMLines + Fontconfig | Pango paragraph layout with the bundled open fonts, anchors and complete notebook pages |
| Cairo document | `RMRender::linux_document` | Cairo text + RMDoc | A whole document to PDF, fresh, preserving or recomposed, or to SVG pages |
| RMLines | `RMRender::rmlines` | [rmlines-lib](https://github.com/altessa-s/rmlines-lib) | Tool calibration and palettes, stroke/scene/page plans, page growth, anchors, highlights, template store |
| Apple text | `RMRender::apple_text` | Apple + RMLines | CoreText paragraph layout with the bundled open fonts, complete notebook pages |
| RMDoc | `RMRender::rmdoc` | RMLines + [rmdoc-lib](https://github.com/altessa-s/rmdoc-lib) | Pages, templates, tombstones and source-page mapping from an unpacked archive |
| Apple document | `RMRender::apple_document` | Apple text + RMDoc | A whole document to PDF, fresh or as an incremental update of its source PDF, or to SVG pages |
| Cairo marks | `RMRender::linux_marks` | Cairo text + [rmmarks-lib](https://github.com/altessa-s/rmmarks-lib) | Highlight extraction and stacked OCR sheets |
| Apple marks | `RMRender::apple_marks` | Apple text + [rmmarks-lib](https://github.com/altessa-s/rmmarks-lib) | Highlighted text on a rendered page and stacked mark sheets for OCR |

Pen textures are sampled in portable C for every grain family (the pencils,
the paintbrush and both ballpoints): pressure maps along each segment,
SplitMix64 lattice hashes, and 16 procedural 64×64 tiles embedded as packed
bits. Nothing is loaded at run time and no platform rasterizer is
involved. Apple and Cairo backends draw output; the core can also be built
without either backend.

## Build and install

CMake 3.21 or newer and a C23 compiler are required. The headers use
`constexpr`, fixed-underlying-type enums, `nullptr` and `[[nodiscard]]`; use a
recent Apple Clang, clang 19+ or GCC 15+. The core links zlib. Template
expressions use a private C locale (POSIX `newlocale`/`strtod_l`, or Windows
`_create_locale`/`_strtod_l`) and never change the process locale.

```sh
cmake -S . -B build -G Ninja -DRMRENDER_WERROR=ON
cmake --build build
cmake --build build --target check
cmake --install build --prefix "$PWD/stage"
```

| Option | Default | Purpose |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build shared libraries |
| `RMRENDER_BUILD_TESTS` | on for a top-level build | Build tests and the `check` target |
| `RMRENDER_WERROR` | `OFF` | Treat compiler warnings as errors |
| `RMRENDER_LTO` | `ON` | Link-time optimization for optimized shared builds |
| `RMRENDER_BUILD_APPLE` | on with an Apple SDK | Build the CoreGraphics/CoreText backend |
| `RMRENDER_BUILD_CAIRO` | `OFF` | Build the Cairo/Pango/PDFium backend on Linux, macOS or Windows (needs `RMRENDER_BUILD_APPLE=OFF`) |
| `RMRENDER_BUILD_LINUX` | `OFF` | Same as `RMRENDER_BUILD_CAIRO` |
| `RMRENDER_BUILD_RMLINES` | `OFF` | Build the RMLines adapter |
| `RMRENDER_BUILD_RMDOC` | `OFF` | Build the RMDoc adapter (needs RMLines) |
| `RMRENDER_BUILD_RMMARKS` | `OFF` | Build the RMMarks adapter (needs RMLines and a backend) |
| `RMRENDER_BUILD_EXAMPLES` | `OFF` | Build `rmrender_grain`, an Apple example that draws each grain |

The adapters find installed `RMLines`, `RMDoc` and `RMMarks` CMake packages;
pass their prefixes through `CMAKE_PREFIX_PATH`:

```sh
cmake -S . -B build -G Ninja -DRMRENDER_WERROR=ON \
    -DRMRENDER_BUILD_RMLINES=ON -DRMRENDER_BUILD_RMDOC=ON -DRMRENDER_BUILD_RMMARKS=ON \
    -DCMAKE_PREFIX_PATH=/path/to/dependencies
```

Installation includes the headers, libraries, a relocatable CMake package,
pkg-config metadata, and `share/rmrender` with the fonts, their licenses, the
grain tiles and the page templates. For CMake consumers:

```cmake
find_package(RMRender 1 REQUIRED)
target_link_libraries(your_target PRIVATE RMRender::apple_text)
```

pkg-config names use `rmrender-` and hyphens (`rmrender-core`,
`rmrender-apple-text`, …); `rmrender-apple-full` brings in the document and
marks components. `add_subdirectory` is supported; tests default to off when
embedded. Public names use `rmr_`/`RMR_` and coexist with RMLines' `rm_` names.

The examples below use Apple entry points. The Cairo backend uses the matching
`linux*.h` headers and the `rmr_linux_` prefix.

## Cairo backend

The Cairo backend builds on Linux, macOS and Windows, one backend per build.
It needs Cairo 1.16+, Pango 1.50+, Fontconfig, HarfBuzz 3.3+, GdkPixbuf, zlib
and PDFium; only PDFium's C API is used. CMake never downloads anything;
`scripts/fetch-pdfium.py` fetches a pinned, checksum-verified PDFium build
(`linux-x64`, `linux-arm64`, `mac-arm64`, `mac-x64`, `win-x64`, `win-arm64`). On
Debian 13:

```sh
apt-get install clang cmake ninja-build pkg-config python3 libcairo2-dev libpango1.0-dev \
    libgdk-pixbuf-2.0-dev libfontconfig1-dev libharfbuzz-dev zlib1g-dev
python3 scripts/fetch-pdfium.py --platform linux-x64 --output /path/to/pdfium
cmake -S . -B build -G Ninja -DRMRENDER_BUILD_CAIRO=ON -DRMRENDER_BUILD_APPLE=OFF \
    -DRMRENDER_BUILD_RMLINES=ON -DRMRENDER_BUILD_RMDOC=ON -DRMRENDER_BUILD_RMMARKS=ON \
    -DPDFium_ROOT=/path/to/pdfium -DCMAKE_PREFIX_PATH=/path/to/dependencies
```

The targets are `RMRender::linux`, `linux_text`, `linux_document` and
`linux_marks`, with `RMRender::cairo*` aliases; pkg-config names are
`rmrender-linux`, `rmrender-linux-text`, `rmrender-linux-document` and
`rmrender-linux-marks`. The installed package finds PDFium through
`PDFium_ROOT`. RMRender does not bundle PDFium or the graphics libraries;
ship them and their licenses with the application.

Text always uses the bundled fonts through a private Fontconfig/Pango font
map, never system font registration. Pango still loads Fontconfig's default
configuration; where Fontconfig finds no `fonts.conf` of its own, as in
minimal Linux containers, `FONTCONFIG_FILE` is pointed at an empty
configuration in `$XDG_CACHE_HOME/rmrender` (Windows: see below) unless
`FONTCONFIG_FILE`, `FONTCONFIG_PATH` or `FONTCONFIG_SYSROOT` is set, so
nothing is printed on stderr. PDFium is process-global and not thread
safe: RMRender initializes it once, serializes its own PDFium calls and keeps
it alive for the life of the process, so applications must not call
`FPDF_DestroyLibrary` and must coordinate their own PDFium use. A font
provider and its layouts serve one thread at a time.

`rmr_pdf_rewrite` writes a document's current object graph as a new
single-revision PDF, and `rmr_pdf_select_pages` reorders, omits or repeats
pages along the way. The `pdf_recompose` and `document_pdf_recomposed` entry
points combine page selection, overlays and rewriting. A rewrite invalidates
existing signatures and is not redaction; use the incremental routes to keep a
signed revision.

### Windows

Windows x64 and ARM64 are supported with Clang and the UCRT: build in an MSYS2
CLANG64 shell with the `mingw-w64-clang-x86_64-*` packages (x64), or in a
CLANGARM64 shell with the `mingw-w64-clang-aarch64-*` packages (ARM64), for
clang, cmake, ninja, pkgconf, python, cairo, pango, fontconfig, harfbuzz,
gdk-pixbuf2 and zlib, and build rmlines-lib, rmdoc-lib and rmmarks-lib with the
same toolchain; do not mix CLANG64 or CLANGARM64 with MINGW64 or UCRT64
packages, or one architecture with the other. MSVC and clang-cl are not
supported.

```sh
# --platform win-arm64 in a CLANGARM64 shell
python scripts/fetch-pdfium.py --platform win-x64 --output /path/to/pdfium
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DBUILD_SHARED_LIBS=ON \
    -DRMRENDER_BUILD_CAIRO=ON -DRMRENDER_BUILD_APPLE=OFF \
    -DRMRENDER_BUILD_RMLINES=ON -DRMRENDER_BUILD_RMDOC=ON -DRMRENDER_BUILD_RMMARKS=ON \
    -DPDFium_ROOT=/path/to/pdfium -DCMAKE_PREFIX_PATH=/path/to/dependencies
```

The PDFium SDK must hold both `pdfium.dll` and its import library. Shared
builds install `librmrender-1.dll` and its siblings to `bin`; the DLL name
carries the ABI major version, the import library does not. At run time
`pdfium.dll`, the RMRender DLLs and the CLANG64 or CLANGARM64 runtime DLLs
(Cairo, Pango, GLib, FreeType, Fontconfig, HarfBuzz and their dependencies)
must sit next to the executable or on `PATH`; a static RMRender build still
needs them; all of them must match the executable's architecture. An
application deploys these transitive DLLs together with their license notices.
Resource paths are UTF-8. The bundled fonts are written once to
`%LOCALAPPDATA%\rmrender\fonts`, named by content and shared by every process;
Fontconfig opens them through ANSI APIs, so a non-ASCII profile path is
reached through its 8.3 alias, or `XDG_DATA_HOME` can point to an ASCII
directory. Where Fontconfig finds no `fonts.conf` of its own, as when only the
DLLs ship, `FONTCONFIG_FILE` is pointed at an empty configuration in that
directory unless `FONTCONFIG_FILE`, `FONTCONFIG_PATH` or `FONTCONFIG_SYSROOT`
is set; rendering never uses Fontconfig's default configuration.

## Draw a plan

A plan is an immutable list of draw commands in `.rm` page coordinates: x from
the page centre, y down. Every component above ends in one.

```c
#include <rmrender/apple.h>

rmr_sample samples[] = {{{-200, 300}, 4}, {{0, 360}, 9}, {{200, 300}, 4}};
rmr_path path = {0};
rmr_plan *plan = nullptr;
rmr_buffer pdf = {0};
rmr_error error;
rmr_status status =
    rmr_outline(samples, 3, RMR_CAP_ROUND, RMR_JOIN_ROUND, nullptr, &path, &error);
if (status == RMR_OK) {
    rmr_draw draw = {.path = {path.elements, path.count}, .color = {0, 0, 0, 1}};
    status = rmr_plan_create(&draw, 1, (rmr_rect){-702, 0, 1404, 1872}, nullptr, &plan,
                             &error);
    rmr_path_free(&path); /* the plan owns a copy */
}
if (status == RMR_OK) {
    const rmr_plan *pages[] = {plan};
    status = rmr_apple_pdf(pages, 1, RMR_POINTS_PER_UNIT, nullptr, nullptr, nullptr, &pdf,
                           &error);
}
/* On success pdf.bytes holds pdf.size bytes of PDF. */
rmr_buffer_free(&pdf);
rmr_plan_free(plan);
```

`rmr_outline` takes calibrated widths; `rmr_grain_mask` turns the same points
into a one-bit texture mask for a fill command. Draw commands can also carry
ordered eraser contours, isolated transparency groups, encoded PNG/JPEG
images, a PDF page, or a line of text. A backend declares its capabilities and
plans needing others are refused before drawing begins; `backend.h` defines
the ABI for other backends.

## Render a notebook page

With the RMLines and Apple text components, a parsed `.rm` page becomes a plan
with its template, ink, images, typed text and highlights:

```c
#include <rmlines/parser.h>
#include <rmrender/apple_text.h>

/* fonts_dir and templates_dir: resources/fonts and resources/templates, or the
 * same directories under the installed share/rmrender. */
rmr_status page_pdf(const uint8_t *bytes, size_t size, const char *fonts_dir,
                    const char *templates_dir, rmr_buffer *out, rmr_error *error) {
    rm_page *page = nullptr;
    rm_error parse_error;
    if (rm_parse_page(bytes, size, &page, &parse_error) != RM_OK)
        return RMR_ERR_INVALID_ARGUMENT;
    rmr_apple_fonts *fonts = nullptr;
    rmr_template_store *templates = nullptr;
    rmr_plan *plan = nullptr;
    rmr_status status = rmr_apple_fonts_create_directory(fonts_dir, nullptr, &fonts, error);
    if (status == RMR_OK)
        status = rmr_template_store_create(templates_dir, nullptr, &templates, error);
    rmr_point canvas = {0};
    const rm_page *order[] = {page};
    if (status == RMR_OK)
        status = rmr_lines_document_canvas(order, 1, (rmr_point){0}, false, nullptr, &canvas,
                                           error);
    if (status == RMR_OK) {
        rmr_page_resources resources = {.template = rmr_template_store_resolve,
                                        .template_context = templates};
        status = rmr_apple_page_plan(page, canvas, RMR_EDGE_RIBBON, 0, &resources, fonts,
                                     nullptr, &plan, error);
    }
    rmr_sheet sheet;
    if (status == RMR_OK)
        status = rmr_paper_sheet(RMR_PAPER_A4, canvas.x, canvas.y, rmr_plan_viewport(plan),
                                 &sheet, error);
    if (status == RMR_OK) {
        const rmr_plan *pages[] = {plan};
        status = rmr_apple_pdf_sheets(pages, &sheet, 1, nullptr, nullptr, nullptr, out, error);
    }
    rmr_plan_free(plan);
    rmr_template_store_free(templates);
    rmr_apple_fonts_free(fonts);
    rm_page_free(page);
    return status;
}
```

- **Page growth.** The plan's viewport grows with ink beyond the canvas.
  `RMR_EDGE_RIBBON` follows notebook export, `RMR_EDGE_UNCUT` keeps overhanging
  width, and `RMR_EDGE_CENTRELINE` follows the centreline with a margin.
- **Sheets.** `rmr_paper_sheet` picks A4, US Letter or tablet size and keeps
  one scale per canvas, so long pages grow instead of shrinking.
  `rmr_sheet_create` sizes bitmaps; `rmr_apple_bitmap_on_sheet` renders one.
- **Resources are explicit.** Fonts come from a directory or a callback and
  are never registered process-wide; a missing font is an error, not a silent
  substitution. Images come from a resolver callback (`rmr_page_resources.image`);
  templates come from `rmr_template_store_create_layered` when custom
  directories should take precedence over the bundled ones. The adapters do no
  I/O of their own beyond these stores.
- **Handwriting only.** `rmr_apple_page_ink_plan` draws ink alone, for OCR, and
  `rmr_apple_page_marks` / `rmr_linux_page_marks` return the highlighted text for
  [rmmarks-lib](https://github.com/altessa-s/rmmarks-lib).

## Documents and PDFs

`rmr_archive_document_create` borrows an unpacked RMDoc archive and resolves
its page order, templates and source-PDF pages; `rmr_apple_document_pdf`
renders the whole document. Parsing errors fail the call; recoverable losses
are counted in `rmr_document_report`.

There are two ways to put marks on an imported PDF:

- **A fresh PDF** (`rmr_apple_pdf_overlay`): source pages are imported as
  vectors, may be reordered or hidden, and keep URI/local links and remapped
  outlines. Forms, signatures and other annotations are not carried over.
- **An incremental update** (`rmr_pdf_incremental`, `rmr_apple_pdf_incremental`,
  `rmr_apple_document_pdf_preserving`): the source bytes are copied unchanged
  and new page objects, overlay Form XObjects and an xref revision are
  appended. Annotations, AcroForm fields, attachments, metadata, outlines and
  signature dictionaries keep their objects. Classic xref tables, xref streams,
  hybrid files, `/Prev` chains, object streams, Flate and byte predictors are
  supported. Encryption, other filters, non-unit `/UserUnit`, certified
  (`/Perms`) files and any change of source page order, count or visibility are
  refused with an error; there is no silent fallback to a redraw.

An incremental update leaves existing signed byte ranges intact, so an approval
signature can still verify for its own revision. The appended revision is
**not signed**, and the library does not validate certificates, signature
policies or permissions.

## SVG output

Version 1.2.0 adds SVG output. Each page becomes one standalone SVG 1.1
document in an owned buffer, sized to its `rmr_sheet` like the PDF and bitmap
paths: the sheet's units are PDF points, so `width`/`height` are written in
`pt` and the `viewBox` spans the sheet, which clips the page. The writer is in
the portable core and executes plans through the ordinary backend ABI; the
same plan gives the same bytes on macOS and Linux, except where a platform
supplies text outlines or rasters.

- Paths stay vector: fills, strokes with their width, caps, joins and miter
  limit, combined fill/stroke, colors with alpha, isolated groups with group
  opacity (`isolation:isolate`), multiply and darken (`mix-blend-mode`), and
  erasers as luminance masks that remove their contours before the command is
  composited, as the native backends do.
- Grain textures (pencils, brushes, ballpoints) are masks holding their
  one-bit raster as an embedded PNG, sampled without smoothing.
- Images keep their PNG/JPEG bytes as `data:` URIs, placed by the plan's
  transform and clipped to the command's path; they are not decoded.
- Text has no font dependency: glyphs become filled outline paths. The core
  asks a platform provider (`rmr_svg_provider`) for the outlines of TEXT
  commands; `rmr_apple_page_svg` and `rmr_linux_page_svg` lay typed text out
  with CoreText or Pango exactly as the PDF output does and draw its glyph
  outlines instead of embedding a text PDF. The original string of a TEXT
  command is kept in `aria-label`; text is not selectable.
- Imported PDF pages cannot stay vector in SVG. They are rasterized through
  the platform renderer (CoreGraphics, or PDFium on Cairo) at `raster_dpi`
  pixels per inch of sheet (zero selects 150) and embedded as PNG under the
  vector marks. Crop boxes, links and outlines of a source PDF are not
  represented.

```c
#include <rmrender/apple_document.h>

rmr_svg_pages pages = {0};
rmr_error error = {0};
if (rmr_apple_document_svg(document, nullptr, fonts, RMR_PAPER_A4, 0, 0,
                           nullptr, nullptr, nullptr, &pages, nullptr,
                           &error) == RMR_OK) {
  for (size_t i = 0; i < pages.count; i++)
    fwrite(pages.pages[i].bytes, 1, pages.pages[i].size, files[i]);
  rmr_svg_pages_free(&pages);
}
```

| Entry point | Input |
|---|---|
| `rmr_svg_sheets` | Plans and sheets, with an optional `rmr_svg_provider` for TEXT outlines and PDF rasters |
| `rmr_apple_svg`, `rmr_linux_svg` | Plans and sheets, with the platform's text outlines and PDF rasterizer |
| `rmr_apple_page_svg`, `rmr_linux_page_svg` | A notebook page on its paper sheet, typed text as outlines |
| `rmr_apple_document_svg`, `rmr_linux_document_svg` | Every page of an archive document, on the sheets of the document PDF |

Output follows the PDF contract: `maximum_pdf_bytes` bounds the total SVG
bytes, `maximum_draws` the page count, rasters the bitmap limits; cancellation
is checked between draws and nothing is returned on failure. Output is
deterministic, escapes attribute text and refers to nothing outside the
document. Browsers, librsvg and resvg render it; renderers without
`mix-blend-mode` draw multiply and darken highlights as normal paint.

## Ownership and failures

All inputs are borrowed for the duration of a call. Plans deep-copy their
commands, paths, masks, images and text, so inputs can be freed afterwards.
Release each result once with its `rmr_*_free` function; each accepts `nullptr`.
Owning results must not be copied. Views returned by accessors such as
`rmr_plan_draw` are borrowed and live as long as their owner.

On failure every output argument is unchanged. `rmr_error` is optional and is
left unchanged on success; on failure it carries the status, an index into the
input where one applies and a message.

| Status | Meaning |
|---|---|
| `RMR_OK` | Success |
| `RMR_ERR_INVALID_ARGUMENT` | Invalid pointer/count, non-finite geometry or malformed input |
| `RMR_ERR_OUT_OF_MEMORY` | An allocation failed |
| `RMR_ERR_LIMIT` | A configured limit would be exceeded |
| `RMR_ERR_UNSUPPORTED` | The input needs something this API or backend does not provide |
| `RMR_ERR_BACKEND` | The native backend failed, for example on an undecodable image |
| `RMR_ERR_CANCELLED` | The cancel callback asked to stop |

`rmr_limits` (default from `rmr_limits_default`) bounds points, elements,
draws, plan bytes, bitmap and PDF sizes, coordinates and widths. The limits
cover library-owned memory; allocations inside CoreGraphics/CoreText and
caller callbacks are outside them. Cancellation is cooperative between draw
operations and cannot interrupt a native call.

Immutable plans can be shared between threads; each render call uses its own
backend context. The library emits no logs and keeps no global caches; the one
process-wide state is the Cairo backend's PDFium engine, described above. A
template store serves one synchronous render at a time.

## Validation

With every component enabled, `check` runs 37 suites with the Apple backend
and 41 with the Cairo backend: geometry, the backend lifecycle, invalid inputs,
grain masks, allocation failure injected at each allocation, templates and JSON
decoding, hostile and incremental PDFs, page selection and rewriting, bitmap,
PDF, SVG and text output, Unicode layout, page composition, archives and full
pages. The core alone builds and passes its suites on Linux with GCC. SVG
output is checked for well-formed XML (with `xmllint` when installed) and, when
`rsvg-convert` or `resvg` is installed, rasterized and compared with the
backend's own bitmap of the same plan; without a rasterizer that suite is
skipped.

The Windows workflow builds static and DLL variants on x64 (CLANG64) and ARM64
(CLANGARM64), checks that font files do not accumulate across runs, repeats the
suites with non-ASCII temporary and cache directories, checks that only the
shipped DLLs suffice for Fontconfig and checks an installed SDK consumer. It
records the MSYS2 dependency versions; Windows pixel parity with the pinned Linux/macOS stack is
not yet a release gate.

CI builds the portable core, the Cairo backend on Linux x64 and arm64 and on
macOS in static and shared configurations, and the full Apple package, with
ASan/UBSan. A second workflow builds a pinned Cairo/Pango stack and patched
PDFium on Linux and macOS and requires their rasters to match pixel for pixel.
CodeQL analyses the portable core, and a format check holds the sources to
`.clang-format`. Generated files are reproducible:

```sh
python3 scripts/generate-grain.py --check       # grain tile PNGs
python3 scripts/embed-marker.py --check         # embedded marker tiles
python3 scripts/embed-pencil.py --check         # embedded pencil tiles
python3 scripts/generate-templates.py --check   # page templates
swift scripts/generate-unicode.swift | cmp - backends/apple/rmr_alphabetic.c
```

`tests/verify_pdf_signature.py --writer build/test_pdf_incremental` signs a
generated PDF locally, applies two incremental updates and verifies the
original signed bytes with OpenSSL.

## Bindings

- **[rmrender-swift](https://github.com/altessa-s/rmrender-swift)** — the same
  renderer as a Swift package for Apple platforms.
- **[rmrender-go](https://github.com/altessa-s/rmrender-go)** — Go bindings over
  this library.

## See also

- **[rmlines-lib](https://github.com/altessa-s/rmlines-lib)** — the `.rm` v6
  page format: pages, strokes, typed text and highlights, read and written.
- **[rmdoc-lib](https://github.com/altessa-s/rmdoc-lib)** — the `.rmdoc`
  container a document and its pages travel in.
- **[rmmarks-lib](https://github.com/altessa-s/rmmarks-lib)** — which words a
  highlight covers, and how OCR from cropped strips returns to the page.

## Acknowledgements

reMarkable does not publish its file formats. This library is built on openly
available information and on the research of third-party developers who
documented them.

## License

MIT, as in the sibling packages; see [LICENSE](LICENSE). The bundled Noto and
EB Garamond fonts carry the SIL Open Font License (`resources/fonts/*-OFL.txt`);
generated Unicode tables carry the
[Unicode data license](resources/UNICODE-LICENSE.txt).

reMarkable is a trademark of reMarkable AS. This project is not affiliated
with, endorsed by or sponsored by reMarkable AS.
