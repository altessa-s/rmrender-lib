/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef RMR_APPLE_TYPOGRAPHY_INTERNAL_H
#define RMR_APPLE_TYPOGRAPHY_INTERNAL_H
#include <CoreText/CoreText.h>
#include <math.h>
#include <rmrender/apple_text.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline rmr_status text_error(rmr_error* e, rmr_status s, const char* m) {
  if (e) {
    *e = (rmr_error){.code = s};
    snprintf(e->message, sizeof e->message, "%s", m);
  }
  return s;
}

CTFontRef rmr_ct_font(const rmr_apple_fonts* fonts, double size,
                      rmr_type_face face, bool bold, bool italic, int script);
double rmr_ct_kern(UniChar c, rmr_type_face face, bool bold, bool italic,
                   int script);

#endif
