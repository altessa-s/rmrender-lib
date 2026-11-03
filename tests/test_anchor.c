/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmrender/rmlines.h>

#include "test.h"

typedef struct {
  size_t calls, paragraph, line, index;
  rmr_caret_offsets offsets;
  rmr_status status;
} state;

static rmr_status measure(void* context, size_t paragraph, size_t line,
                          size_t index, rmr_caret_offsets* out) {
  state* s = context;
  s->calls++;
  s->paragraph = paragraph;
  s->line = line;
  s->index = index;
  *out = s->offsets;
  return s->status;
}

int main(void) {
  state s = {.offsets = {7, 20}};
  rmr_caret_provider caret = {RMR_CARET_PROVIDER_ABI, sizeof caret, &s,
                              measure};
  /* a + astral emoji + combining mark + b: 4 scalars, 5 UTF-16 units. */
  const char text[] =
      "a\xf0\x9f\x93\x93\xcc\x81"
      "b";
  rmr_anchor_line lines[] = {{0, 3, 30}, {3, 2, 50}};
  rmr_anchor_paragraph paragraphs[2] = {
      {.text = {text, sizeof text - 1},
       .lines = lines,
       .line_count = 2,
       .indent = 4,
       .baseline = 30,
       .has_typesetter = true},
      {.indent = 8, .baseline = 100, .right_to_left = true}};
  rmr_anchor_layout layout = {.left = -40,
                              .column = 80,
                              .paragraphs = paragraphs,
                              .count = 2,
                              .caret = &caret};
  rm_text_anchor anchor = {.paragraph = -1, .offset = 1};
  rmr_point out = {0};

  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(out.x == -29 && out.y == 30 && s.index == 0 && s.paragraph == 0);

  anchor.offset = 2;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(s.index == 1 && s.line == 0);

  anchor.offset = 3;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(s.index == 3 && s.line == 1 && out.y == 50);

  anchor.offset = 4;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(s.index == 4 && s.line == 1);

  anchor.offset = INT32_MAX;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(s.index == 5 && out.y == 50);

  anchor.offset = INT32_MIN;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(s.index == 0);

  paragraphs[0].right_to_left = true;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(out.x == 23);

  anchor.has_origin_x = true;
  anchor.origin_x = 123;
  anchor.offset = 3;
  s.calls = 0;
  layout.caret = nullptr;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(out.x == 123 && out.y == 50 && !s.calls);

  anchor.has_origin_x = false;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
        RMR_ERR_UNSUPPORTED);

  anchor.paragraph = INT32_MAX;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(out.x == 32 && out.y == 100);

  layout.count = 0;
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr));
  CHECK(out.x == -40 && out.y == 0);

  layout.count = 2;
  anchor.paragraph = 0;
  layout.caret = &caret;
  caret.abi_version = 0;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);

  caret.abi_version = RMR_CARET_PROVIDER_ABI;
  s.status = RMR_ERR_CANCELLED;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
        RMR_ERR_CANCELLED);

  s.status = RMR_OK;
  s.offsets.at_start = NAN;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
        RMR_ERR_BACKEND);

  s.offsets.at_start = 20;
  lines[0].length = 2;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);

  lines[0].length = 3;
  lines[1].location = 2;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);

  lines[1].location = 3;
  lines[1].length = SIZE_MAX;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);

  lines[1].length = 3;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);

  lines[1].length = 2;
  const char* bad[] = {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80",
                       "\xf0\x9f", "\xff"};
  for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
    paragraphs[0].text = (rm_str){bad[i], strlen(bad[i])};
    CHECK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);
  }

  paragraphs[0].text = (rm_str){text, sizeof text - 1};
  rmr_limits limits = rmr_limits_default();
  limits.maximum_elements = 11; /* needs 2+2+8 = 12 */
  out = (rmr_point){777, 888};
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, &limits, &out, nullptr) ==
            RMR_ERR_LIMIT &&
        out.x == 777);

  limits = rmr_limits_default();
  limits.maximum_coordinate = 40;
  CHECK(rmr_lines_anchor_pen(&layout, &anchor, &limits, &out, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);

  /* The adapter directly feeds the owned placement layer. */
  rmr_anchor_context context = {&layout, nullptr};
  rmr_anchor_provider provider = rmr_lines_layout_anchor_provider(&context);
  rm_stroke_point point = {.x = 1, .y = 2, .width = 8};
  rm_stroke stroke = {
      .points = &point, .point_count = 1, .has_anchor = true, .anchor = anchor};
  rmr_placed_scene* scene = nullptr;
  OK(rmr_lines_place_scene(&stroke, 1, nullptr, 0, &provider, nullptr, &scene,
                           nullptr));
  CHECK(rmr_placed_scene_strokes(scene)[0].points[0].x == 24 &&
        rmr_placed_scene_strokes(scene)[0].points[0].y == 52);
  rmr_placed_scene_free(scene);

  rmr_error error = {.code = RMR_ERR_BACKEND, .index = 77};
  OK(rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, &error));
  CHECK(error.index == 77 && error.code == RMR_ERR_BACKEND);
  return 0;
}
