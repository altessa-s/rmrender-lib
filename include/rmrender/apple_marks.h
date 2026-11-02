/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef RMR_APPLE_MARKS_H
#define RMR_APPLE_MARKS_H
#include "apple_text.h"
#include <rmmarks/types.h>
typedef struct rmr_page_marks rmr_page_marks;
/* frame is the part of .rm space visible in the final rendered sheet, y down.
 * Resolves anchored handwriting through the same native text layout as drawing.
 * Results own text, identifiers and geometry and can be passed to RMMarks. */
[[nodiscard]] RMR_API rmr_status rmr_apple_page_marks(const rm_page *page, rmr_point canvas,
                                                      rmr_rect frame, rm_str page_id,
                                                      const rmr_apple_fonts *fonts,
                                                      const rmr_limits *limits,
                                                      rmr_page_marks **out, rmr_error *error);
RMR_API const rmm_marked_page *rmr_page_marks_view(const rmr_page_marks *marks);
RMR_API void rmr_page_marks_free(rmr_page_marks *marks);
typedef struct {
    rmr_bitmap bitmap;
    const rmm_mark_strip *strips;
    size_t count;
} rmr_mark_sheet;
/* Stack clipped bitmap blocks with white margins and black separator rules.
 * Source/result use RGBA premultiplied alpha, row zero at top. */
[[nodiscard]] RMR_API rmr_status rmr_apple_mark_sheet(const rmr_bitmap *page,
                                                      const rmm_page_box *blocks, size_t count,
                                                      size_t margin, size_t rule,
                                                      const rmr_limits *limits, rmr_mark_sheet *out,
                                                      rmr_error *error);
RMR_API void rmr_mark_sheet_free(rmr_mark_sheet *sheet);
RMR_API rmm_page_box rmr_mark_around(rmm_page_box box, double margin);
#endif
