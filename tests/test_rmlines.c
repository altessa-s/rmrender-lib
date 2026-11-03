/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmrender/rmlines.h>

#include "test.h"
#ifdef RMRENDER_TEST_APPLE
#include <rmrender/apple.h>
#endif

int main(void) {
  rm_stroke_point points[] = {{.x = -10, .y = 10, .width = 40, .pressure = 128},
                              {.x = 10, .y = 20, .width = 40, .pressure = 128}};
  rm_stroke stroke = {.tool = RM_TOOL_MARKER_2,
                      .color = RM_COLOR_RED,
                      .points = points,
                      .point_count = 2,
                      .thickness_scale = 3};
  rmr_stroke_style style;
  OK(rmr_lines_style(&stroke, &style, nullptr));
  CHECK(fabs(style.line_width - 7.4) < 1e-6 && !style.stroked &&
        style.grain == RMR_GRAIN_SOLID);

  rmr_plan* plan = nullptr;
  OK(rmr_lines_plan(&stroke, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &plan,
                    nullptr));
  CHECK(rmr_plan_count(plan) == 1);
  rmr_plan_free(plan);
  plan = nullptr;

  stroke.tool = RM_TOOL_HIGHLIGHTER_1;
  stroke.color = RM_COLOR_GRAY_OVERLAP;
  OK(rmr_lines_style(&stroke, &style, nullptr));
  CHECK(style.stroked && style.color.alpha == 0.25098 && style.color.red == 0 &&
        style.cap == RMR_CAP_BUTT && style.join == RMR_JOIN_MITER);

  stroke.has_argb = true;
  stroke.argb = 0x40211e1c;
  OK(rmr_lines_style(&stroke, &style, nullptr));
  CHECK(style.color.alpha == 64.0 / 255 && style.color.red == 33.0 / 255);

  stroke.tool = RM_TOOL_PENCIL_1;
  OK(rmr_lines_plan(&stroke, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &plan,
                    nullptr));
  CHECK(rmr_plan_draw(plan, 0)->mask.bytes);
  rmr_plan_free(plan);
  plan = nullptr;

  rmr_path p = {0};
  OK(rmr_lines_outline(&stroke, nullptr, &p, nullptr));
  CHECK(p.count > 0);
  rmr_path_free(&p);

  stroke.tool = RM_TOOL_MARKER_2;
  stroke.has_anchor = true;
  CHECK(rmr_lines_plan(&stroke, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &plan,
                       nullptr) == RMR_ERR_UNSUPPORTED);

  stroke.has_anchor = false;
  stroke.tool = RM_TOOL_ERASER;
  OK(rmr_lines_plan(&stroke, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &plan,
                    nullptr));
  CHECK(!rmr_plan_draw(plan, 0)->path.count);
  rmr_plan_free(plan);
  plan = nullptr;

  stroke.tool = RM_TOOL_BALLPOINT_2;
  OK(rmr_lines_plan(&stroke, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &plan,
                    nullptr));
  CHECK(rmr_plan_draw(plan, 0)->mask.bytes &&
        rmr_plan_draw(plan, 0)->mask.width > 0);
  rmr_mask expected = {0};
  OK(rmr_lines_mask(&stroke, nullptr, &expected, nullptr));
  const rmr_mask_view* actual = &rmr_plan_draw(plan, 0)->mask;
  CHECK(actual->stride == expected.stride && actual->height == expected.height);
  CHECK(memcmp(actual->bytes, expected.bytes,
               expected.stride * expected.height) == 0);
  rmr_mask_free(&expected);
  rmr_plan_free(plan);
  plan = nullptr;

  stroke.tool = RM_TOOL_BALLPOINT_1;
  OK(rmr_lines_plan(&stroke, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &plan,
                    nullptr));
  OK(rmr_lines_mask(&stroke, nullptr, &expected, nullptr));
  actual = &rmr_plan_draw(plan, 0)->mask;
  CHECK(actual->stride == expected.stride && actual->height == expected.height);
  CHECK(memcmp(actual->bytes, expected.bytes,
               expected.stride * expected.height) == 0);
  rmr_mask_free(&expected);
  rmr_plan_free(plan);
  plan = nullptr;

  rmr_limits limits = rmr_limits_default();
  limits.maximum_plan_bytes = 256;
  CHECK(rmr_lines_plan(&stroke, 1, (rmr_rect){-32, 0, 64, 64}, &limits, &plan,
                       nullptr) == RMR_ERR_LIMIT &&
        !plan);

#ifdef RMRENDER_TEST_APPLE
  rmr_unit_raster raster = rmr_apple_unit_raster();
  raster.abi_version = 999;
  stroke.tool = RM_TOOL_PENCIL_1;
  stroke.point_count = 1;
  CHECK(rmr_lines_mask_with_raster(&stroke, &raster, nullptr, &expected,
                                   nullptr) == RMR_ERR_INVALID_ARGUMENT &&
        !expected.bytes);

  raster.abi_version = RMR_UNIT_RASTER_ABI;
  stroke.point_count = 2;
  rm_pen_tool tools[] = {RM_TOOL_PENCIL_1, RM_TOOL_PENCIL_2,
                         RM_TOOL_MECHANICAL_PENCIL_2, RM_TOOL_PAINTBRUSH_2};

  for (size_t tool = 0; tool < 4; tool++) {
    stroke.tool = tools[tool];
    OK(rmr_lines_plan(&stroke, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &plan,
                      nullptr));
    rmr_plan_free(plan);
    plan = nullptr;

    OK(rmr_lines_plan_with_raster(&stroke, 1, (rmr_rect){-32, 0, 64, 64},
                                  &raster, nullptr, &plan, nullptr));
    OK(rmr_lines_mask_with_raster(&stroke, &raster, nullptr, &expected,
                                  nullptr));
    actual = &rmr_plan_draw(plan, 0)->mask;
    CHECK(actual->stride == expected.stride &&
          actual->height == expected.height);
    CHECK(memcmp(actual->bytes, expected.bytes,
                 expected.stride * expected.height) == 0);

    rmr_bitmap bitmap = {0};
    OK(rmr_apple_bitmap(plan, 2, (rmr_color){1, 1, 1, 1}, nullptr, nullptr,
                        nullptr, &bitmap, nullptr));
    bool ink = false;
    for (size_t i = 0; i < bitmap.stride * bitmap.height; i += 4)
      if (bitmap.pixels[i] < 255) ink = true;
    CHECK(ink);
    rmr_bitmap_free(&bitmap);

    const rmr_plan* pages[] = {plan};
    rmr_buffer pdf = {0};
    OK(rmr_apple_pdf(pages, 1, RMR_POINTS_PER_UNIT, nullptr, nullptr, nullptr,
                     &pdf, nullptr));
    CHECK(pdf.size > 100);
    rmr_buffer_free(&pdf);

    rmr_mask_free(&expected);
    rmr_plan_free(plan);
    plan = nullptr;
  }
#endif

  puts("RMLines adapter passed");
  return 0;
}
