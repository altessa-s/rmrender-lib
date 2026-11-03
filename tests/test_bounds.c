/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <rmrender/rmlines.h>
#ifdef RMRENDER_TEST_APPLE
#include <rmrender/apple.h>
#endif
static rmr_rect bounds(rm_page *p, rmr_ink_edge edge, double margin) {
    rmr_rect b;
    OK(rmr_lines_content_bounds(p, 100, 200, edge, margin, nullptr, &b, nullptr));
    return b;
}

static void equal(rmr_rect b, double x, double y, double w, double h) {
    CHECK(fabs(b.x - x) < 1e-8 && fabs(b.y - y) < 1e-8 && fabs(b.width - w) < 1e-8 &&
          fabs(b.height - h) < 1e-8);
}

int main(void) {
    rm_page page = {0};
    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -50, 0, 100, 200);

    rm_stroke_point point = {.x = 0, .y = 240, .width = 40};
    rm_stroke stroke = {.tool = RM_TOOL_FINELINER_2, .points = &point, .point_count = 1};
    page.strokes = &stroke;
    page.stroke_count = 1;
    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -50, 0, 100, 200);
    equal(bounds(&page, RMR_EDGE_UNCUT, 0), -50, 0, 100, 292);

    point.y = 247;
    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -50, 0, 100, 200);

    point.y = 248;
    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -50, 0, 100, 300);

    point.x = 80;
    point.y = 100;
    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -100, 0, 200, 200);

    point.x = 2000;
    equal(bounds(&page, RMR_EDGE_UNCUT, 0), -100, 0, 200, 200);

    point.x = 50;
    point.y = 200;
    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -50, 0, 100, 200);
    equal(bounds(&page, RMR_EDGE_UNCUT, 0), -50, 0, 105, 205);

    point.x = 80;
    point.y = 240;
    equal(bounds(&page, RMR_EDGE_CENTRELINE, 20), -50, 0, 150, 260);

    point.x = 0;
    point.y = -10;
    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -50, -15, 100, 215);

    stroke.tool = RM_TOOL_ERASER;
    point.x = 10000;
    point.y = 10000;
    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -50, 0, 100, 200);

    stroke.tool = RM_TOOL_ERASER_AREA;
    equal(bounds(&page, RMR_EDGE_UNCUT, 0), -50, 0, 100, 200);

    page.stroke_count = 0;
    rm_page_image image = {
        .vertices = {{-80, 0, 0, 0}, {80, 0, 1, 0}, {80, 300, 1, 1}, {-80, 300, 0, 1}}};
    page.images = &image;
    page.image_count = 1;

    equal(bounds(&page, RMR_EDGE_RIBBON, 0), -100, 0, 200, 347);
    equal(bounds(&page, RMR_EDGE_CENTRELINE, 10), -90, 0, 180, 310);

    rmr_limits l = rmr_limits_default();
    l.maximum_points = 3;
    rmr_rect out = {1, 2, 3, 4};
    CHECK(rmr_lines_content_bounds(&page, 100, 200, RMR_EDGE_RIBBON, 0, &l, &out, nullptr) ==
          RMR_ERR_LIMIT);
    equal(out, 1, 2, 3, 4);

    image.has_anchor = true;
    CHECK(rmr_lines_content_bounds(&page, 100, 200, RMR_EDGE_RIBBON, 0, nullptr, &out, nullptr) ==
          RMR_ERR_UNSUPPORTED);

    image.has_anchor = false;
    page.has_text = true;
    CHECK(rmr_lines_content_bounds(&page, 100, 200, RMR_EDGE_RIBBON, 0, nullptr, &out, nullptr) ==
          RMR_ERR_UNSUPPORTED);

    page.has_text = false;
    CHECK(rmr_lines_content_bounds(&page, 100, 200, RMR_EDGE_RIBBON, 1, nullptr, &out, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);

    l = rmr_limits_default();
    l.maximum_coordinate = 320;
    CHECK(rmr_lines_content_bounds(&page, 100, 200, RMR_EDGE_RIBBON, 0, &l, &out, nullptr) ==
          RMR_ERR_LIMIT);

    page.image_count = 0;
    page.stroke_count = 1;
    stroke.tool = RM_TOOL_FINELINER_2;
    point.x = 0;
    point.y = 300;
    rmr_plan *p = nullptr;
    OK(rmr_lines_page_plan(&page, 100, 200, RMR_EDGE_UNCUT, 0, nullptr, nullptr, nullptr, nullptr,
                           &p, nullptr));
    equal(rmr_plan_viewport(p), -50, 0, 100, 352);
    rmr_plan_free(p);

    stroke.has_anchor = true;
    CHECK(rmr_lines_content_bounds(&page, 100, 200, RMR_EDGE_RIBBON, 0, nullptr, &out, nullptr) ==
          RMR_ERR_UNSUPPORTED);

    stroke.has_anchor = false;
    point.x = NAN;
    CHECK(rmr_lines_content_bounds(&page, 100, 200, RMR_EDGE_RIBBON, 0, nullptr, &out, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);

#ifdef RMRENDER_TEST_APPLE
    rm_stroke_point lower[] = {{.x = -10, .y = 240, .width = 40}, {.x = 10, .y = 240, .width = 40}};
    stroke.points = lower;
    stroke.point_count = 2;
    OK(rmr_lines_page_plan(&page, 100, 200, RMR_EDGE_UNCUT, 0, nullptr, nullptr, nullptr, nullptr,
                           &p, nullptr));
    rmr_bitmap bitmap = {0};
    OK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, nullptr));
    CHECK(bitmap.width == 100 && bitmap.height == 292);
    CHECK(bitmap.pixels[240 * bitmap.stride + 50 * 4 + 3] == 255);
    CHECK(bitmap.pixels[280 * bitmap.stride + 50 * 4 + 3] == 0);
    rmr_bitmap_free(&bitmap);

    rmr_plan_free(p);
    OK(rmr_lines_page_plan(&page, 100, 200, RMR_EDGE_RIBBON, 0, nullptr, nullptr, nullptr, nullptr,
                           &p, nullptr));
    OK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, nullptr));
    CHECK(bitmap.width == 100 && bitmap.height == 200);
    for (size_t y = 0; y < bitmap.height; y++)
        for (size_t x = 0; x < bitmap.width; x++)
            CHECK(bitmap.pixels[y * bitmap.stride + x * 4 + 3] == 0);
    rmr_bitmap_free(&bitmap);
    rmr_plan_free(p);
#endif

    puts("page growth modes, thresholds, limits and plan integration passed");
}
