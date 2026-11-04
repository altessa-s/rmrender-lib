/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Rasterized SVG against the platform's own bitmap for the same plan. Exits
 * 77 (skipped) when no SVG rasterizer is installed. */
#include "pdf_fixture.h"
#include "svg_raster.h"
#include "test.h"
#ifdef RMR_TEST_LINUX
#include <rmrender/linux.h>
#define native(name) rmr_linux_##name
#else
#include <rmrender/apple.h>
#define native(name) rmr_apple_##name
#endif

/* Builds a stroke outline-free plan touching every vector feature, a grain
 * mask, an image, platform TEXT and an embedded PDF page. */
static rmr_plan* scene(const uint8_t* pdf, size_t pdf_size, rmr_buffer image,
                       rmr_mask* grain) {
  static rmr_element box[] = {{.kind = RMR_MOVE, .end = {-180, 20}},
                              {.kind = RMR_LINE, .end = {-60, 20}},
                              {.kind = RMR_LINE, .end = {-60, 120}},
                              {.kind = RMR_LINE, .end = {-180, 120}},
                              {.kind = RMR_CLOSE}};
  static rmr_element wave[] = {{.kind = RMR_MOVE, .end = {-190, 160}},
                               {.kind = RMR_CURVE,
                                .control1 = {-120, 100},
                                .control2 = {-60, 220},
                                .end = {20, 150}},
                               {.kind = RMR_LINE, .end = {60, 200}},
                               {.kind = RMR_LINE, .end = {90, 140}}};
  static rmr_element cut[] = {{.kind = RMR_MOVE, .end = {-140, 0}},
                              {.kind = RMR_LINE, .end = {-120, 0}},
                              {.kind = RMR_LINE, .end = {-100, 300}},
                              {.kind = RMR_LINE, .end = {-120, 300}},
                              {.kind = RMR_CLOSE}};
  static rmr_element right[] = {{.kind = RMR_MOVE, .end = {40, 20}},
                                {.kind = RMR_LINE, .end = {180, 20}},
                                {.kind = RMR_LINE, .end = {180, 120}},
                                {.kind = RMR_LINE, .end = {40, 120}},
                                {.kind = RMR_CLOSE}};
  static rmr_element page[] = {{.kind = RMR_MOVE, .end = {100, 180}},
                               {.kind = RMR_LINE, .end = {190, 180}},
                               {.kind = RMR_LINE, .end = {190, 270}},
                               {.kind = RMR_LINE, .end = {100, 270}},
                               {.kind = RMR_CLOSE}};
  static rmr_path_view erasers[] = {{cut, 5}};

  rmr_grain_point points[] = {
      {-170, 240, 14, 120, 0}, {-60, 250, 14, 200, 0}, {40, 260, 14, 255, 0}};
  rmr_rect area;
  OK(rmr_grain_box(points, 3, nullptr, &area, nullptr));
  OK(rmr_grain_mask(points, 3, RMR_GRAIN_STAMP, area, nullptr, nullptr, grain,
                    nullptr));
  rmr_element mask_area[] = {
      {.kind = RMR_MOVE, .end = {area.x, area.y}},
      {.kind = RMR_LINE, .end = {area.x + area.width, area.y}},
      {.kind = RMR_LINE, .end = {area.x + area.width, area.y + area.height}},
      {.kind = RMR_LINE, .end = {area.x, area.y + area.height}},
      {.kind = RMR_CLOSE}};

  rmr_draw d[] = {
      {.path = {box, 5}, .mode = RMR_FILL, .color = {.9, .2, .1, 1}},
      {.path = {right, 5},
       .mode = RMR_IMAGE,
       .color = {0, 0, 0, .8},
       .image = {image.bytes, image.size, {140, 0, 0, 100, 40, 20}}},
      {.path = {wave, 4},
       .mode = RMR_STROKE,
       .color = {.1, .3, .9, .6},
       .line_width = 12,
       .cap = RMR_CAP_ROUND,
       .join = RMR_JOIN_ROUND},
      {.path = {wave, 4},
       .mode = RMR_STROKE,
       .color = {1, .9, 0, 1},
       .blend = RMR_BLEND_MULTIPLY,
       .line_width = 20,
       .cap = RMR_CAP_SQUARE,
       .join = RMR_JOIN_MITER,
       .erasers = erasers,
       .eraser_count = 1},
      {.path = {box, 5},
       .mode = RMR_FILL_STROKE,
       .color = {.2, .8, .3, .3},
       .stroke_color = {0, 0, 0, .7},
       .line_width = 4,
       .cap = RMR_CAP_BUTT,
       .join = RMR_JOIN_BEVEL},
      {.path = {right, 5},
       .mode = RMR_FILL,
       .color = {.5, 0, .5, 1},
       .group = 3,
       .group_alpha = .4,
       .group_blend = RMR_BLEND_DARKEN},
      {.path = {wave, 4},
       .mode = RMR_STROKE,
       .color = {0, .6, .6, 1},
       .line_width = 8,
       .cap = RMR_CAP_BUTT,
       .join = RMR_JOIN_BEVEL,
       .group = 3,
       .group_alpha = .4,
       .group_blend = RMR_BLEND_DARKEN},
      {.path = {mask_area, 5},
       .mode = RMR_FILL,
       .color = {.1, .1, .1, .9},
       .mask = {grain->bytes, grain->width, grain->height, grain->stride,
                grain->box}},
      {.mode = RMR_TEXT,
       .color = {0, 0, 0, 1},
       .text = {"Typed Helvetica 42", 18, 24, {-190, 290}, {0, -24, 220, 30}}},
      {.path = {page, 5},
       .mode = RMR_PDF,
       .color = {0, 0, 0, .9},
       .pdf_page = 1,
       .image = {pdf, pdf_size, {90, 0, 0, 90, 100, 180}}}};

  rmr_plan* plan = nullptr;
  OK(rmr_plan_create(d, sizeof d / sizeof *d, (rmr_rect){-200, 0, 400, 300},
                     nullptr, &plan, nullptr));
  return plan;
}

int main(void) {
  const char* tool = svg_rasterizer();
  if (!tool) {
    puts("no SVG rasterizer (rsvg-convert or resvg); skipped");
    return 77;
  }

  fixture pdf = source(false);
  rmr_mask grain = {0};
  rmr_buffer image = svg_png_quadrants(64);
  rmr_plan* plan = scene((const uint8_t*)pdf.bytes, pdf.n, image, &grain);
  rmr_sheet sheet = {800, 600, {2, 0, 0, -2, 400, 600}};

  rmr_bitmap reference = {0}, drawn = {0};
  OK(native(bitmap_on_sheet)(plan, &sheet, (rmr_color){1, 1, 1, 1}, nullptr,
                             nullptr, nullptr, &reference, nullptr));

  rmr_svg_pages pages = {0};
  const rmr_plan* plans[] = {plan};
  rmr_error error = {0};
  OK(native(svg)(plans, &sheet, 1, 72, nullptr, nullptr, nullptr, &pages,
                 &error));
  CHECK(
      svg_well_formed((const char*)pages.pages[0].bytes, pages.pages[0].size));
  CHECK(svg_xmllint(pages.pages[0].bytes, pages.pages[0].size));
  CHECK(svg_rasterize(tool, &pages.pages[0], 800, 600, &drawn));

  svg_difference d = svg_compare(&reference, &drawn);
  printf("%s: mean channel difference %.3f, strong pixels %.4f%%\n", tool,
         d.mean, d.strong * 100);
  CHECK(d.mean < 2.5 && d.strong < .01);

  const char* keep = getenv("RMR_SVG_KEEP");
  if (keep && *keep) {
    FILE* f = fopen(keep, "wb");
    CHECK(f && fwrite(pages.pages[0].bytes, 1, pages.pages[0].size, f) ==
                   pages.pages[0].size);
    fclose(f);
  }

  free(drawn.pixels);
  rmr_bitmap_free(&reference);
  rmr_svg_pages_free(&pages);
  rmr_plan_free(plan);
  rmr_mask_free(&grain);
  free(image.bytes);
  return 0;
}
