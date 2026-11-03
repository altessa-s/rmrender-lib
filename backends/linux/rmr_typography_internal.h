/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef RMR_LINUX_TYPOGRAPHY_INTERNAL_H
#define RMR_LINUX_TYPOGRAPHY_INTERNAL_H
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <rmrender/linux_text.h>
#include <stdatomic.h>

#include "rmr_linux_internal.h"

struct rmr_linux_fonts {
  atomic_size_t references;
  FcConfig* config;
  FcCharSet* base_characters[5];
  PangoFontMap* map;
  int fds[14];
#ifdef __APPLE__
  char paths[14][64];
#endif
  bool has_emoji;
};


PangoLayout* rmr_pango_layout(const rmr_linux_fonts* fonts, rm_str text,
                              double size, rmr_type_face face, bool bold,
                              bool italic);

void rmr_pango_attributes(const rmr_linux_fonts* fonts, PangoAttrList* attrs,
                          rm_str text, size_t start, size_t end, double size,
                          rmr_type_face face, bool bold, bool italic);
rmr_status rmr_pango_calibrate(PangoLayout* layout, const rmr_limits* limits);
PangoAttrList* rmr_pango_source_attributes(PangoLayout* layout);
double rmr_pango_cluster_spacing(PangoLayout* layout, size_t from, size_t to);
double rmr_pango_cluster_adjustment(PangoGlyphItem* run, int cluster);
double rmr_pango_caret_adjustment(PangoLayout* layout, size_t index);

rmr_status rmr_pango_show_line(cairo_t* cr, PangoLayoutLine* line,
                               const rmr_limits* limits, size_t* retained);

#endif
