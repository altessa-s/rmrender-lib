/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef RMR_HVAR_H
#define RMR_HVAR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded OpenType HVAR reader. Keep interpolation fractional until scaling;
 * HarfBuzz's public advance API rounds variation deltas in design units.
 * https://learn.microsoft.com/en-us/typography/opentype/spec/otvarcommonformats
 */
typedef struct {
  const uint8_t* data;
  size_t size;
} rmr_ot_bytes;

bool rmr_ot_has(rmr_ot_bytes b, size_t at, size_t n);

uint32_t rmr_ot_uint(rmr_ot_bytes b, size_t at, unsigned n);

int32_t rmr_ot_int(rmr_ot_bytes b, size_t at, unsigned n);

rmr_ot_bytes rmr_ot_tail(rmr_ot_bytes b, size_t at);

bool rmr_hvar_delta(rmr_ot_bytes b, uint32_t glyph, const int* coords,
                    unsigned count, double* result);

#endif
