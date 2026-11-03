/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <stdio.h>
#include <string.h>

#include "test.h"
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
#include <rmrender/apple_text.h>
#define F rmr_apple_fonts
#define create rmr_apple_fonts_create_directory
#define release rmr_apple_fonts_free
#define L rmr_apple_layout
#define layout_create rmr_apple_layout_create
#define anchors rmr_apple_layout_anchors
#define line_get rmr_apple_layout_line
#define layout_free rmr_apple_layout_free
#else
#include <fontconfig/fontconfig.h>
#include <rmrender/linux_text.h>
#define F rmr_linux_fonts
#define create rmr_linux_fonts_create_directory
#define release rmr_linux_fonts_free
#define L rmr_linux_layout
#define layout_create rmr_linux_layout_create
#define anchors rmr_linux_layout_anchors
#define line_get rmr_linux_layout_line
#define layout_free rmr_linux_layout_free
#endif
static const char* samples[] = {"office ffi affine",
                                "é ä ô é ä",
                                "مرحبا بالعالم مرحبا بالعالم",
                                "مَرْحَبًا مَرْحَبًا",
                                "नमस्ते दुनिया नमस्ते दुनिया",
                                "के कि कु कं कँ",
                                "שָׁלוֹם שָׁלוֹם",
                                "abc שלום 123 world",
                                "one\ntwo\nthree",
                                "one   two  three",
                                "日本語 日本語 日本語"};
static const double columns[] = {40, 100, 200, 500};

typedef struct {
  size_t from, to;
  double start, end, baseline;
  rmr_caret_offsets begin, finish;
} line_reference;

typedef struct {
  size_t count;
  line_reference lines[32];
} layout_reference;

#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
#include "text_layout_reference.h"
#endif
int main(int argc, char** argv) {
  CHECK(argc == 2 || (argc == 3 && strcmp(argv[2], "--dump") == 0));
  bool dump = argc == 3;
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
  dump = true;
#else
  CHECK(sizeof apple_layouts / sizeof *apple_layouts == 44);
#endif

  F* fonts = nullptr;
  OK(create(argv[1], nullptr, &fonts, nullptr));

  for (size_t sample = 0; sample < 11; sample++)
    for (size_t column = 0; column < 4; column++) {
      rm_paragraph p = {.text = {samples[sample], strlen(samples[sample])}};
      rm_text_block block = {.paragraphs = &p,
                             .paragraph_count = 1,
                             .has_column_width = true,
                             .column_width = columns[column]};
      L* layout = nullptr;
      OK(layout_create(&block, columns[column], fonts, nullptr, &layout,
                       nullptr));
      const rmr_anchor_layout* a = anchors(layout);
      size_t count = a->paragraphs[0].line_count;

      CHECK(count <= 32);
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
      const layout_reference* expected = &apple_layouts[sample * 4 + column];
      CHECK(count == expected->count);
#endif
      if (dump) printf("{%zu, {\n", count);

      for (size_t i = 0; i < count; i++) {
        rmr_text_line line;
        OK(line_get(layout, 0, i, &line, nullptr));
        rmr_caret_offsets begin, finish;
        OK(a->caret->measure(a->caret->context, 0, i, line.from, &begin));
        OK(a->caret->measure(a->caret->context, 0, i, line.to, &finish));
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
        line_reference ref = expected->lines[i];
        CHECK(line.from == ref.from && line.to == ref.to);
        double got[] = {line.start,     line.end,       line.baseline,
                        begin.at_index, begin.at_start, finish.at_index,
                        finish.at_start};
        double want[] = {ref.start,          ref.end,
                         ref.baseline,       ref.begin.at_index,
                         ref.begin.at_start, ref.finish.at_index,
                         ref.finish.at_start};
        for (size_t field = 0; field < 7; field++) {
          if (fabs(got[field] - want[field]) >= .02)
            fprintf(
                stderr,
                "layout %zu/%zu line %zu field %zu: Apple %.9f Linux %.9f\n",
                sample, column, i, field, want[field], got[field]);
          CHECK(fabs(got[field] - want[field]) < .02);
        }
#endif
        if (dump)
          printf("{%zu, %zu, %.9f, %.9f, %.9f, {%.9f, %.9f}, {%.9f, %.9f}},\n",
                 line.from, line.to, line.start, line.end, line.baseline,
                 begin.at_index, begin.at_start, finish.at_index,
                 finish.at_start);
      }

      if (dump) printf("}},\n");
      layout_free(layout);
    }

  release(fonts);
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
  FcFini();
#endif
  return 0;
}
