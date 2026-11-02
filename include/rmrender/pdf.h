/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef RMR_PDF_H
#define RMR_PDF_H
#include "backend.h"
/* An independently generated single-page PDF, imported as an isolated Form.
 * transform maps its unrotated PDF coordinates into the original page's PDF
 * coordinates, before /Rotate. Existing page coordinates never move. */
typedef struct {
    size_t page;
    const uint8_t *pdf;
    size_t size;
    rmr_transform transform;
    bool replace_box;
    rmr_rect box; /* New MediaBox; original MediaBox becomes ArtBox. An intentional
                   * CropBox distinct from MediaBox is retained, including inherited
                   * crops. Otherwise CropBox grows with the new MediaBox. */
} rmr_pdf_overlay;
typedef struct {
    size_t pages_updated;
    size_t original_bytes;
    /* Signed revisions remain byte-for-byte intact; this is NOT validation
     * of cryptographic signatures or permission to change certified files. */
    bool original_revision_preserved;
} rmr_pdf_incremental_report;
/* Append-only, transactional memory output. Preserves original objects including
 * annotations, AcroForm, metadata, attachments, outlines and signature bytes.
 * One overlay per page; no page reorder/removal. Classic tables, xref streams and hybrid tables,
 * including /Prev chains, nonzero generations and packed objects. In a hybrid section the
 * classic table is authoritative; its /XRefStm stream supplies only objects the table omits
 * or marks free. Flate and 8-bit TIFF/PNG predictors are supported, with a 64 MiB cap per
 * decoded stream and 256 MiB of decoded data per file. Refuses encryption,
 * catalog /Perms (certification/usage rights), malformed/cyclic structures and
 * non-unit /UserUnit. Never silently redraws. New revisions are not signed.
 * Input and result each bounded by maximum_pdf_bytes; parser/import scratch by
 * maximum_plan_bytes, object/work count by maximum_elements, pages by maximum_draws.
 * Inputs are borrowed only for this call. Zero overlays returns an exact copy.
 * Overlay PDFs need a single unrotated page with one content stream (an indirect
 * /Length is supported); objects packed in object streams are imported. Overlay PDFs must be
 * trusted renderer output, not arbitrary third-party files; their object graph is imported,
 * including actions if present. */
[[nodiscard]] RMR_API rmr_status rmr_pdf_incremental(const uint8_t *source, size_t size,
                                                     const rmr_pdf_overlay *overlays, size_t count,
                                                     const rmr_limits *limits, rmr_cancel_fn cancel,
                                                     void *context, rmr_buffer *out,
                                                     rmr_pdf_incremental_report *report,
                                                     rmr_error *error);
/* A source page as rmr_pdf_incremental resolves it: inherited MediaBox
 * (normalised to positive size) and /Rotate (0, 90, 180 or 270). */
typedef struct {
    rmr_rect media;
    bool has_media;
    int32_t rotate;
} rmr_pdf_page_geometry;
/* Reports the page count and per-page geometry of the source page tree that
 * rmr_pdf_incremental updates, so a caller can refuse a file when another
 * reader (for example CoreGraphics) sees a different page tree. count always
 * receives the full page count; at most capacity entries are written. */
[[nodiscard]] RMR_API rmr_status rmr_pdf_pages(const uint8_t *source, size_t size,
                                               const rmr_limits *limits,
                                               rmr_pdf_page_geometry *pages, size_t capacity,
                                               size_t *count, rmr_error *error);
#endif
