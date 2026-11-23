/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmlines/parser.h>
#include <rmrender/rmlines.h>

#include "test.h"
#ifdef RMRENDER_TEST_APPLE
#include <rmrender/apple.h>
#endif

int main(int argc, char** argv) {
  CHECK(argc == 2);
  if (!rm_pen_tool_is_eraser((rm_pen_tool)22)) {
    puts("Skipping masked eraser fixture: linked RMLines lacks tool 22");
    return 77;
  }
  FILE* file = fopen(argv[1], "rb");
  CHECK(file);
  uint8_t bytes[4096];
  size_t size = fread(bytes, 1, sizeof bytes, file);
  CHECK(feof(file) && !ferror(file));
  CHECK(fclose(file) == 0);

  rm_page* page = nullptr;
  CHECK(rm_parse_page(bytes, size, &page, nullptr) == RM_OK);
  CHECK(page->stroke_count == 2);
  CHECK(page->strokes[0].color == (rm_pen_color)16);
  CHECK(page->strokes[1].tool == (rm_pen_tool)22);

  rmr_plan* plan = nullptr;
  OK(rmr_lines_page_plan(page, 100, 100, RMR_EDGE_CENTRELINE, 0, nullptr,
                         nullptr, nullptr, nullptr, &plan, nullptr));
  CHECK(rmr_plan_viewport(plan).width == 100);
  const rmr_draw* ink = rmr_plan_draw(plan, 0);
  CHECK(ink->color.red == 199.0 / 255 && ink->color.green == 199.0 / 255 &&
        ink->color.blue == 198.0 / 255);
  CHECK(ink->eraser_count == 1);

#ifdef RMRENDER_TEST_APPLE
  rmr_bitmap bitmap = {0};
  OK(rmr_apple_bitmap(plan, 1, (rmr_color){0}, nullptr, nullptr, nullptr,
                      &bitmap, nullptr));
  /* Ink at scene x=-15 survives. The masked eraser crosses the ink at x=10. */
  CHECK(bitmap.pixels[20 * bitmap.stride + 35 * 4 + 3] > 0);
  CHECK(bitmap.pixels[20 * bitmap.stride + 60 * 4 + 3] == 0);
  rmr_bitmap_free(&bitmap);
#endif

  rmr_plan_free(plan);
  rm_page_free(page);
}
