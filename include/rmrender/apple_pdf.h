/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef RMR_APPLE_PDF_H
#define RMR_APPLE_PDF_H
#include "apple.h"
#include "pdf.h"
typedef struct rmr_apple_pdf_source rmr_apple_pdf_source;
[[nodiscard]] RMR_API rmr_status rmr_apple_pdf_source_create(const uint8_t *bytes, size_t size,
                                                             const rmr_limits *limits,
                                                             rmr_apple_pdf_source **out,
                                                             rmr_error *error);
RMR_API void rmr_apple_pdf_source_free(rmr_apple_pdf_source *source);
RMR_API size_t rmr_apple_pdf_source_count(const rmr_apple_pdf_source *source);
/* Displayed media-box size in PDF points, after source rotation. */
[[nodiscard]] RMR_API rmr_status rmr_apple_pdf_source_size(const rmr_apple_pdf_source *source,
                                                           size_t page, rmr_point *out,
                                                           rmr_error *error);
/* Rebuild an ordered PDF with vector source pages and mark plans. Retains URI
 * and supported local links plus remapped outlines. This is a fresh PDF, not
 * incremental editing: forms, signatures and arbitrary annotations are not
 * retained. Link refusals are counted. No file I/O; transactional bounded output.
 * Annotations and intentional source crops follow source transforms, including
 * rotation and reordered pages. Hidden source pages do not impose their crop. */
[[nodiscard]] RMR_API rmr_status rmr_apple_pdf_overlay(const rmr_apple_pdf_source *source,
                                                       const rmr_pdf_page *pages, size_t count,
                                                       const rmr_limits *limits,
                                                       rmr_cancel_fn cancel, void *context,
                                                       rmr_buffer *out, rmr_pdf_report *report,
                                                       rmr_error *error);
/* Strict append-only plan overlay. Exactly one visible ordered source page per
 * input page; insertion, removal and reorder return UNSUPPORTED. Rotation is
 * handled without moving original objects. Boxes expand only for overflowing
 * marks. An intentional source CropBox remains even when MediaBox expands. See pdf.h for
 * supported structures and signature semantics. No silent redraw fallback. */
[[nodiscard]] RMR_API rmr_status rmr_apple_pdf_incremental(
    const rmr_apple_pdf_source *source, const rmr_pdf_page *pages, size_t count,
    const rmr_limits *limits, rmr_cancel_fn cancel, void *context, rmr_buffer *out,
    rmr_pdf_incremental_report *report, rmr_error *error);
#endif
