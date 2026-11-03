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
/* Each entry is one extended grapheme, with its independent UTF-16 length. */
static const struct {
  const char* text;
  size_t units;
  uint32_t scalar_boundaries;
} clusters[] = {{"é", 2, 7},
                {"ä́", 3, 15},
                {"कि", 2, 7},
                {"שָׁ", 3, 15},
                {"😀", 2, 5},
                {"👍🏽", 4, 21},
                {"🇫🇷", 4, 21},
                {"👩‍💻", 5, 45},
                {"👨‍👩‍👧‍👦", 11, 2925},
                {"❤️", 2, 7},
                {"1️⃣", 3, 15}};

static void invalid(const rmr_caret_provider* provider, size_t paragraph,
                    size_t line, size_t index) {
  rmr_caret_offsets out = {123, 456};
  CHECK(provider->measure(provider->context, paragraph, line, index, &out) ==
        RMR_ERR_INVALID_ARGUMENT);
  CHECK(out.at_index == 123 && out.at_start == 456);
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  F* fonts = nullptr;
  OK(create(argv[1], nullptr, &fonts, nullptr));
  const double columns[] = {1, 20, 100, 500};

  for (size_t sample = 0; sample < sizeof clusters / sizeof *clusters; sample++)
    for (size_t column = 0; column < sizeof columns / sizeof *columns;
         column++) {
      char text[256];
      int n = snprintf(text, sizeof text, "%s %s", clusters[sample].text,
                       clusters[sample].text);
      CHECK(n > 0 && (size_t)n < sizeof text);
      size_t u = clusters[sample].units, total = 2 * u + 1;
      rm_paragraph p = {.text = {text, (size_t)n}};
      rm_text_block block = {.paragraphs = &p,
                             .paragraph_count = 1,
                             .has_column_width = true,
                             .column_width = columns[column]};
      L* layout = nullptr;
      OK(layout_create(&block, columns[column], fonts, nullptr, &layout,
                       nullptr));
      const rmr_anchor_layout* a = anchors(layout);
      CHECK(a->count == 1 && a->paragraphs[0].line_count);

      size_t end = 0;
      for (size_t line = 0; line < a->paragraphs[0].line_count; line++) {
        rmr_text_line visual;
        OK(line_get(layout, 0, line, &visual, nullptr));
        if (!(visual.to == u || visual.to == u + 1 || visual.to == total))
          fprintf(stderr,
                  "split cluster sample %zu column %.0f range %zu..%zu\n",
                  sample, columns[column], visual.from, visual.to);
        CHECK(visual.from == end && visual.to > visual.from);
        CHECK(visual.to == u || visual.to == u + 1 || visual.to == total);
        CHECK(isfinite(visual.start) && isfinite(visual.end) &&
              isfinite(visual.baseline));
        invalid(a->caret, 0, line, visual.to + 1);
        invalid(a->caret, 0, line, SIZE_MAX);
        if (visual.from) invalid(a->caret, 0, line, visual.from - 1);
        end = visual.to;
        for (size_t index = visual.from; index <= visual.to; index++) {
          rmr_caret_offsets c = {123, 456};
          rmr_status status =
              a->caret->measure(a->caret->context, 0, line, index, &c);
          size_t position = index <= u ? index : index - u - 1;
          bool scalar = (clusters[sample].scalar_boundaries &
                         (UINT32_C(1) << position)) != 0;
          CHECK(status == (scalar ? RMR_OK : RMR_ERR_INVALID_ARGUMENT));
          if (scalar)
            CHECK(isfinite(c.at_index) && isfinite(c.at_start));
          else
            CHECK(c.at_index == 123 && c.at_start == 456);
        }
      }

      CHECK(end == total);
      invalid(a->caret, 1, 0, 0);
      invalid(a->caret, 0, a->paragraphs[0].line_count, 0);
      CHECK(a->caret->measure(a->caret->context, 0, 0, 0, nullptr) ==
            RMR_ERR_INVALID_ARGUMENT);
      rmr_caret_offsets untouched = {123, 456};
      CHECK(a->caret->measure(nullptr, 0, 0, 0, &untouched) ==
            RMR_ERR_INVALID_ARGUMENT);
      CHECK(untouched.at_index == 123 && untouched.at_start == 456);
      layout_free(layout);
    }

  release(fonts);
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
  FcFini();
#endif
  return 0;
}
