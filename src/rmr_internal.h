/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef RMR_INTERNAL_H
#define RMR_INTERNAL_H
#include <math.h>
#include <rmrender/backend.h>
#include <rmrender/grain.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rmr_plan {
  rmr_draw* draws;
  rmr_element* elements;
  uint8_t* masks;
  rmr_path_view* erasers;
  size_t count, bytes;
  rmr_rect viewport, bounds;
  uint64_t caps;
};

rmr_status rmr_grain_validate(const rmr_grain_point* points, size_t count,
                              rmr_limits limits, rmr_error* error);
rmr_status rmr_mask_layout(rmr_rect box, rmr_limits limits, rmr_mask* out,
                           rmr_error* error);
struct rmr_expression;
size_t rmr_expression_bytes(const struct rmr_expression* expression);
size_t rmr_expression_cost(const struct rmr_expression* expression);
size_t rmr_expression_stack_bytes(const struct rmr_expression* expression);
void* rmr_alloc(size_t size);
void* rmr_calloc(size_t count, size_t size);
void* rmr_realloc(void* p, size_t size);
#ifdef RMRENDER_TESTING
void rmr_test_fail_after(size_t count);
#endif
rmr_status rmr_fail(rmr_error* e, rmr_status code, size_t index,
                    const char* message);
bool rmr_utf8_valid(const char* text, size_t size);
bool rmr_point_valid(rmr_point p, double bound);
bool rmr_color_valid(rmr_color color);

static inline double rmr_clamp(double x, double low, double high) {
  return fmin(fmax(x, low), high);
}

#endif
