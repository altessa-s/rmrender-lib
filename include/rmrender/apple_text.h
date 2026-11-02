/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef RMR_APPLE_TEXT_H
#define RMR_APPLE_TEXT_H
#include "apple.h"
#include "typography.h"
/* Resolve a bundled open font by resource name, without the .ttf extension.
 * Bytes are borrowed for the callback's caller. No process-wide registration.
 * A missing font returns UNSUPPORTED; the library never substitutes silently. */
typedef rmr_status (*rmr_font_resolve_fn)(void *context, const char *resource, rmr_asset_view *out);
typedef struct rmr_apple_fonts rmr_apple_fonts;
[[nodiscard]] RMR_API rmr_status rmr_apple_fonts_create(rmr_font_resolve_fn resolve, void *context,
                                                        const rmr_limits *limits,
                                                        rmr_apple_fonts **out, rmr_error *error);
/* Explicit directory containing the packaged open .ttf files. */
[[nodiscard]] RMR_API rmr_status rmr_apple_fonts_create_directory(const char *directory,
                                                                  const rmr_limits *limits,
                                                                  rmr_apple_fonts **out,
                                                                  rmr_error *error);
RMR_API void rmr_apple_fonts_free(rmr_apple_fonts *fonts);
typedef struct rmr_apple_layout rmr_apple_layout;
/* Snapshot text and build immutable paragraph/line geometry with CoreText.
 * The layout retains everything needed after fonts/input are freed. Native
 * allocations are excluded from maximum_plan_bytes; input, line count and
 * explicit owned buffers remain bounded. */
[[nodiscard]] RMR_API rmr_status rmr_apple_layout_create(const rm_text_block *text,
                                                         double page_width,
                                                         const rmr_apple_fonts *fonts,
                                                         const rmr_limits *limits,
                                                         rmr_apple_layout **out, rmr_error *error);
RMR_API size_t rmr_apple_layout_bytes(const rmr_apple_layout *layout);
RMR_API void rmr_apple_layout_free(rmr_apple_layout *layout);
RMR_API const rmr_anchor_layout *rmr_apple_layout_anchors(const rmr_apple_layout *layout);
RMR_API bool rmr_apple_layout_bottom(const rmr_apple_layout *layout, double *out);
/* Vector PDF commands keep the same shaped lines used for anchor measurement.
 * Output owns its bytes and outlives layout. Viewport uses page coordinates. */
[[nodiscard]] RMR_API rmr_status rmr_apple_layout_plan(const rmr_apple_layout *layout,
                                                       rmr_rect viewport, const rmr_limits *limits,
                                                       rmr_plan **out, rmr_error *error);
/* Complete notebook page foreground/template composition. Native fonts are
 * required only when typed text is present. Anchors without text fail explicitly.
 * Source PDF backgrounds belong to the document/PDF layer. */
[[nodiscard]] RMR_API rmr_status rmr_apple_page_plan(const rm_page *page, rmr_point canvas,
                                                     rmr_ink_edge edge, double margin,
                                                     const rmr_page_resources *resources,
                                                     const rmr_apple_fonts *fonts,
                                                     const rmr_limits *limits, rmr_plan **out,
                                                     rmr_error *error);
/* Handwriting only, for OCR. Text still positions anchors and contributes to
 * bounds; typed glyphs, highlights, images and templates are not drawn. */
[[nodiscard]] RMR_API rmr_status rmr_apple_page_ink_plan(const rm_page *page, rmr_point canvas,
                                                         rmr_ink_edge edge, double margin,
                                                         const rmr_apple_fonts *fonts,
                                                         const rmr_limits *limits, rmr_plan **out,
                                                         rmr_error *error);
/* Native text + placed geometry measurement without constructing draw plans. */
[[nodiscard]] RMR_API rmr_status rmr_apple_page_bounds(const rm_page *page, rmr_point canvas,
                                                       rmr_ink_edge edge, double margin,
                                                       const rmr_apple_fonts *fonts,
                                                       const rmr_limits *limits, rmr_rect *out,
                                                       rmr_error *error);
typedef struct {
    size_t paragraph, from, to; /* UTF-16 code-unit range */
    double baseline, start, end;
} rmr_text_line;
[[nodiscard]] RMR_API rmr_status rmr_apple_layout_line(const rmr_apple_layout *layout,
                                                       size_t paragraph, size_t line,
                                                       rmr_text_line *out, rmr_error *error);
[[nodiscard]] RMR_API rmr_status rmr_apple_text_width(rm_str text, double size, rmr_type_face face,
                                                      bool bold, bool italic, bool less_rightmost,
                                                      const rmr_apple_fonts *fonts,
                                                      const rmr_limits *limits, double *out,
                                                      rmr_error *error);
#endif
