/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmrender/output.h>
#include <stdlib.h>

void rmr_bitmap_free(rmr_bitmap* b) {
  if (b) {
    free(b->pixels);
    *b = (rmr_bitmap){0};
  }
}
