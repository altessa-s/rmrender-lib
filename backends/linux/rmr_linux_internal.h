/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef RMR_LINUX_INTERNAL_H
#define RMR_LINUX_INTERNAL_H
#include <cairo-pdf.h>
#include <cairo.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>
#include <math.h>
#include <rmrender/linux.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    rmr_buffer data;
    size_t maximum;
    rmr_status status;
} rmr_sink;
RMR_API rmr_status rmr_linux_error(rmr_error *e, rmr_status s, const char *message);
RMR_API cairo_status_t rmr_linux_write(void *ctx, const unsigned char *data, unsigned int size);
rmr_transform rmr_pdfium_raw_transform(FPDF_PAGE page, rmr_transform t);
void rmr_pdfium_lock(void);
void rmr_pdfium_unlock(void);
rmr_status rmr_pdfium_save(FPDF_DOCUMENT doc, const rmr_limits *limits, rmr_buffer *out);
rmr_status rmr_pdfium_form(FPDF_DOCUMENT dest, FPDF_PAGE page, FPDF_DOCUMENT source, size_t index,
                           rmr_transform transform);
rmr_status rmr_linux_render_pdf(const rmr_plan *const *plans, const rmr_sheet *sheets, size_t count,
                                const rmr_limits *limits, rmr_cancel_fn cancel, void *context,
                                rmr_buffer *out, rmr_error *e);
#endif
