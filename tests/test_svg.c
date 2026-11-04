/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmrender/svg.h>

#include "image_fixture.h"
#include "rmr_internal.h"
#include "svg_check.h"
#include "test.h"

typedef struct {
  size_t texts, rasters;
  rmr_sheet sheet;
  rmr_status fail;
} services;

/* Test text outline: a unit-high box per byte along the baseline. */
static rmr_status box_text(void* context, const rmr_text_view* text,
                           const rmr_limits* limits, rmr_path* out) {
  (void)limits;
  services* s = context;
  s->texts++;
  if (s->fail != RMR_OK) return s->fail;

  rmr_element* e = calloc(5, sizeof *e);
  if (!e) return RMR_ERR_OUT_OF_MEMORY;
  rmr_point o = text->origin;
  double w = (double)text->size * text->font_size / 2;
  e[0] = (rmr_element){.kind = RMR_MOVE, .end = {o.x, o.y}};
  e[1] = (rmr_element){.kind = RMR_LINE, .end = {o.x + w, o.y}};
  e[2] = (rmr_element){.kind = RMR_LINE, .end = {o.x + w, o.y - 4}};
  e[3] = (rmr_element){.kind = RMR_LINE, .end = {o.x, o.y - 4}};
  e[4] = (rmr_element){.kind = RMR_CLOSE};
  *out = (rmr_path){e, 5};
  return RMR_OK;
}

/* Test raster: opaque half-gray pixels of the requested sheet size. */
static rmr_status gray_raster(void* context, const rmr_plan* plan,
                              const rmr_sheet* sheet, const rmr_limits* limits,
                              rmr_bitmap* out) {
  (void)limits;
  services* s = context;
  s->rasters++;
  s->sheet = *sheet;
  CHECK(rmr_plan_count(plan) == 1 && rmr_plan_draw(plan, 0)->mode == RMR_PDF &&
        rmr_plan_draw(plan, 0)->color.alpha == 1);

  size_t w = (size_t)sheet->width, h = (size_t)sheet->height;
  uint8_t* pixels = malloc(w * h * 4);
  if (!pixels) return RMR_ERR_OUT_OF_MEMORY;
  for (size_t i = 0; i < w * h * 4; i++) pixels[i] = (i % 4 == 3) ? 255 : 128;
  *out = (rmr_bitmap){pixels, w, h, w * 4};
  return RMR_OK;
}

/* Builds a plan exercising every draw kind the writer handles. */
static rmr_plan* every_kind(void) {
  static rmr_element square[] = {{.kind = RMR_MOVE, .end = {-20, 5}},
                                 {.kind = RMR_LINE, .end = {0, 5}},
                                 {.kind = RMR_LINE, .end = {0, 25}},
                                 {.kind = RMR_LINE, .end = {-20, 25}},
                                 {.kind = RMR_CLOSE}};
  static rmr_element wave[] = {{.kind = RMR_MOVE, .end = {-25, 40}},
                               {.kind = RMR_CURVE,
                                .control1 = {-15, 30},
                                .control2 = {-5, 50},
                                .end = {5.25, 40.125}},
                               {.kind = RMR_LINE, .end = {20, 45}}};
  static rmr_element cut[] = {{.kind = RMR_MOVE, .end = {-10, 0}},
                              {.kind = RMR_LINE, .end = {-5, 0}},
                              {.kind = RMR_LINE, .end = {-5, 60}},
                              {.kind = RMR_LINE, .end = {-10, 60}},
                              {.kind = RMR_CLOSE}};
  static rmr_path_view erasers[] = {{cut, 5}};
  static const uint8_t bits[] = {0x0f, 0xc0, 0xaa, 0x40,
                                 0xff, 0xc0, 0x00, 0x00};
  static const char words[] = "A&<\"b\x01";

  rmr_draw d[] = {
      {.path = {square, 5}, .mode = RMR_FILL, .color = {1, 0, 0, 1}},
      {.path = {wave, 3},
       .mode = RMR_STROKE,
       .color = {0, 0, 1, .5},
       .blend = RMR_BLEND_MULTIPLY,
       .line_width = 3,
       .cap = RMR_CAP_ROUND,
       .join = RMR_JOIN_ROUND},
      {.path = {wave, 3},
       .mode = RMR_STROKE,
       .color = {0, .5, 0, 1},
       .line_width = 2,
       .cap = RMR_CAP_BUTT,
       .join = RMR_JOIN_MITER},
      {.path = {square, 5},
       .mode = RMR_FILL_STROKE,
       .color = {1, 1, 0, .25},
       .stroke_color = {0, 0, 0, 1},
       .line_width = 1.5,
       .cap = RMR_CAP_SQUARE,
       .join = RMR_JOIN_BEVEL},
      {.path = {square, 5},
       .mode = RMR_FILL,
       .color = {0, 1, 0, 1},
       .group = 7,
       .group_alpha = .5,
       .group_blend = RMR_BLEND_DARKEN},
      {.path = {wave, 3},
       .mode = RMR_STROKE,
       .color = {0, 1, 1, 1},
       .line_width = 4,
       .group = 7,
       .group_alpha = .5,
       .group_blend = RMR_BLEND_DARKEN},
      {.path = {square, 5},
       .mode = RMR_FILL,
       .color = {.5, 0, .5, 1},
       .erasers = erasers,
       .eraser_count = 1},
      {.path = {wave, 3},
       .mode = RMR_STROKE,
       .color = {.2, .2, .2, .8},
       .blend = RMR_BLEND_DARKEN,
       .line_width = 5,
       .erasers = erasers,
       .eraser_count = 1},
      {.path = {square, 5},
       .mode = RMR_FILL,
       .color = {0, 0, 0, .9},
       .mask = {bits, 10, 4, 2, {-20, 5, 5, 2}}},
      {.path = {square, 5},
       .mode = RMR_IMAGE,
       .color = {0, 0, 0, .75},
       .image = {image_png, sizeof image_png, {20, 0, 0, 20, -20, 5}}},
      {.mode = RMR_TEXT,
       .color = {0, 0, 0, 1},
       .text = {words, sizeof words - 1, 10, {-30, 60}, {0, -8, 30, 10}}},
      {.path = {square, 5},
       .mode = RMR_PDF,
       .color = {0, 0, 0, .5},
       .blend = RMR_BLEND_MULTIPLY,
       .image = {
           (const uint8_t*)"%PDF-1.4 stand-in", 17, {20, 0, 0, 20, -20, 5}}}};

  rmr_plan* plan = nullptr;
  OK(rmr_plan_create(d, sizeof d / sizeof *d, (rmr_rect){-32, 0, 64, 64},
                     nullptr, &plan, nullptr));
  return plan;
}

/* Whether the page contains needle. */
static bool has(const rmr_buffer* page, const char* needle) {
  size_t n = strlen(needle);
  for (size_t i = 0; i + n <= page->size; i++)
    if (!memcmp(page->bytes + i, needle, n)) return true;
  return false;
}

/* Counts occurrences of needle in the page. */
static size_t count(const rmr_buffer* page, const char* needle) {
  size_t n = strlen(needle), found = 0;
  for (size_t i = 0; i + n <= page->size; i++)
    if (!memcmp(page->bytes + i, needle, n)) found++;
  return found;
}

static size_t cancel_after;

/* Cancels once cancel_after earlier checks have passed. */
static bool cancel_later(void* context) {
  (void)context;
  if (!cancel_after) return true;
  cancel_after--;
  return false;
}

static void structure(void) {
  rmr_plan* plan = every_kind();
  rmr_sheet sheet = {200, 200, {2, 0, 0, -2, 64, 190}};
  services s = {0};
  rmr_svg_provider provider = {RMR_SVG_PROVIDER_ABI, sizeof provider, &s,
                               box_text, gray_raster};
  const rmr_plan* plans[] = {plan, plan};
  rmr_sheet sheets[] = {sheet, sheet};
  rmr_svg_pages pages = {0};
  rmr_error error = {0};
  OK(rmr_svg_sheets(plans, sheets, 2, &provider, 72, nullptr, nullptr, nullptr,
                    &pages, &error));
  CHECK(pages.count == 2 && s.texts == 2 && s.rasters == 2);
  CHECK(
      pages.pages[0].size == pages.pages[1].size &&
      !memcmp(pages.pages[0].bytes, pages.pages[1].bytes, pages.pages[0].size));

  const rmr_buffer* p = &pages.pages[0];
  CHECK(svg_well_formed((const char*)p->bytes, p->size));
  CHECK(svg_xmllint(p->bytes, p->size));

  CHECK(has(p, "width=\"200pt\" height=\"200pt\" viewBox=\"0 0 200 200\""));
  CHECK(has(p, "<g transform=\"matrix(2 0 0 2 64 10)\">"));
  CHECK(has(p, "d=\"M-20 5 L0 5 L0 25 L-20 25 Z\" fill=\"#ff0000\"/>"));
  CHECK(has(p, "C-15 30 -5 50 5.25 40.125 L20 45"));
  CHECK(has(p,
            "fill=\"none\" stroke=\"#0000ff\" stroke-opacity=\"0.5\" "
            "stroke-width=\"3\" stroke-linecap=\"round\" "
            "stroke-linejoin=\"round\" style=\"mix-blend-mode:multiply\""));
  CHECK(has(p,
            "stroke-linecap=\"butt\" stroke-linejoin=\"miter\" "
            "stroke-miterlimit=\"10\""));
  CHECK(has(p,
            "fill=\"#ffff00\" fill-opacity=\"0.25\" stroke=\"#000000\" "
            "stroke-width=\"1.5\" stroke-linecap=\"square\" "
            "stroke-linejoin=\"bevel\""));
  CHECK(count(p,
              "<g opacity=\"0.5\" "
              "style=\"isolation:isolate;mix-blend-mode:darken\">") == 1);
  CHECK(count(p, "<mask id=") == 3 && count(p, "fill=\"#000\"/></mask>") == 2);
  CHECK(has(p, "<g mask=\"url(#r1)\">"));
  CHECK(has(p, "<g mask=\"url(#r2)\" style=\"mix-blend-mode:darken\">"));
  CHECK(has(p, "image-rendering=\"optimizeSpeed\""));
  CHECK(has(p, "fill-opacity=\"0.9\" mask=\"url(#r3)\""));
  CHECK(has(p, "<clipPath id=\"r4\">") && has(p, "<g clip-path=\"url(#r4)\">"));
  CHECK(has(p,
            "opacity=\"0.75\" transform=\"matrix(20 0 0 20 -20 5)\" "
            "xlink:href=\"data:image/png;base64,iVBORw0KGgo"));
  CHECK(has(p, "aria-label=\"A&amp;&lt;&quot;b \"/>"));
  CHECK(has(p,
            "opacity=\"0.5\" style=\"mix-blend-mode:multiply\" "
            "xlink:href=\"data:image/png;base64,"));
  CHECK(count(p, "data:image/png;base64,") == 3);
  CHECK(!has(p, "-0 ") && !has(p, "e+") && !has(p, "nan"));

  /* The PDF raster covers the clip's sheet pixels at 72 dpi. */
  CHECK(s.sheet.width == 40 && s.sheet.height == 40);
  CHECK(s.sheet.transform.a == 2 && s.sheet.transform.d == -2);
  CHECK(s.sheet.transform.tx == 64 - 24 && s.sheet.transform.ty == 50);
  CHECK(has(p, "<image x=\"-20\" y=\"5\" width=\"20\" height=\"20\""));

  rmr_svg_pages_free(&pages);
  CHECK(!pages.pages && !pages.count);
  rmr_svg_pages_free(nullptr);

  /* Nonzero dpi scales the raster; the default is 150. */
  OK(rmr_svg_sheets(plans, sheets, 1, &provider, 0, nullptr, nullptr, nullptr,
                    &pages, nullptr));
  CHECK(s.sheet.width == 84 && s.sheet.height == 84);
  rmr_svg_pages_free(&pages);

  rmr_plan_free(plan);
}

static void contract(void) {
  rmr_plan* plan = every_kind();
  rmr_plan* simple = rectangle((rmr_color){0, 0, 0, 1}, RMR_BLEND_NORMAL);
  rmr_sheet sheet = {64, 64, {1, 0, 0, -1, 32, 64}};
  services s = {0};
  rmr_svg_provider provider = {RMR_SVG_PROVIDER_ABI, sizeof provider, &s,
                               box_text, gray_raster};
  const rmr_plan* plans[] = {plan};
  rmr_svg_pages pages = {0}, kept = {(rmr_buffer*)1, 9};
  rmr_error error = {0};

  /* Plans needing services fail before output without them. */
  CHECK(rmr_svg_sheets(plans, &sheet, 1, nullptr, 0, nullptr, nullptr, nullptr,
                       &kept, &error) == RMR_ERR_UNSUPPORTED);
  rmr_svg_provider half = provider;
  half.raster = nullptr;
  CHECK(rmr_svg_sheets(plans, &sheet, 1, &half, 0, nullptr, nullptr, nullptr,
                       &kept, &error) == RMR_ERR_UNSUPPORTED);
  const rmr_plan* plain[] = {simple};
  OK(rmr_svg_sheets(plain, &sheet, 1, nullptr, 0, nullptr, nullptr, nullptr,
                    &pages, &error));
  CHECK(
      svg_well_formed((const char*)pages.pages[0].bytes, pages.pages[0].size));
  rmr_svg_pages_free(&pages);

  /* Arguments and sheets are validated before output. */
  rmr_sheet skewed = sheet;
  skewed.transform.b = 1;
  CHECK(rmr_svg_sheets(plans, &skewed, 1, &provider, 0, nullptr, nullptr,
                       nullptr, &kept, &error) == RMR_ERR_INVALID_ARGUMENT);
  CHECK(rmr_svg_sheets(plans, &sheet, 1, &provider, -1, nullptr, nullptr,
                       nullptr, &kept, &error) == RMR_ERR_INVALID_ARGUMENT);
  rmr_svg_provider wrong = provider;
  wrong.abi_version = 99;
  CHECK(rmr_svg_sheets(plans, &sheet, 1, &wrong, 0, nullptr, nullptr, nullptr,
                       &kept, &error) == RMR_ERR_INVALID_ARGUMENT);
  CHECK(rmr_svg_sheets(plans, &sheet, 0, &provider, 0, nullptr, nullptr,
                       nullptr, &kept, &error) == RMR_ERR_INVALID_ARGUMENT);
  const rmr_plan* missing[] = {nullptr};
  CHECK(rmr_svg_sheets(missing, &sheet, 1, &provider, 0, nullptr, nullptr,
                       nullptr, &kept, &error) == RMR_ERR_INVALID_ARGUMENT);

  /* Output and page-count limits. */
  rmr_limits limits = rmr_limits_default();
  limits.maximum_pdf_bytes = 600;
  CHECK(rmr_svg_sheets(plans, &sheet, 1, &provider, 0, &limits, nullptr,
                       nullptr, &kept, &error) == RMR_ERR_LIMIT);
  limits = rmr_limits_default();
  limits.maximum_draws = 1;
  const rmr_plan* two[] = {simple, simple};
  rmr_sheet sheets[] = {sheet, sheet};
  CHECK(rmr_svg_sheets(two, sheets, 2, nullptr, 0, &limits, nullptr, nullptr,
                       &kept, &error) == RMR_ERR_LIMIT);
  limits = rmr_limits_default();
  OK(rmr_svg_sheets(two, sheets, 2, nullptr, 0, &limits, nullptr, nullptr,
                    &pages, &error));
  limits.maximum_pdf_bytes = pages.pages[0].size * 2 - 1;
  rmr_svg_pages_free(&pages);
  CHECK(rmr_svg_sheets(two, sheets, 2, nullptr, 0, &limits, nullptr, nullptr,
                       &kept, &error) == RMR_ERR_LIMIT);
  limits.maximum_bitmap_side = 4;
  CHECK(rmr_svg_sheets(plans, &sheet, 1, &provider, 0, &limits, nullptr,
                       nullptr, &kept, &error) == RMR_ERR_LIMIT);

  /* Provider failures and malformed outlines propagate. */
  s.fail = RMR_ERR_BACKEND;
  CHECK(rmr_svg_sheets(plans, &sheet, 1, &provider, 0, nullptr, nullptr,
                       nullptr, &kept, &error) == RMR_ERR_BACKEND);
  CHECK(error.index == 10);
  s.fail = RMR_OK;

  /* Cancellation before output and between draws. */
  for (size_t after = 0; after < 15; after++) {
    cancel_after = after;
    rmr_status status = rmr_svg_sheets(plans, &sheet, 1, &provider, 0, nullptr,
                                       cancel_later, nullptr, &kept, &error);
    CHECK(status == RMR_ERR_CANCELLED);
  }
  CHECK(kept.pages == (rmr_buffer*)1 && kept.count == 9);

  /* Allocation failures leave the output unchanged until success. */
  size_t failure = 0;
  for (;; failure++) {
    rmr_test_fail_after(failure);
    rmr_status status = rmr_svg_sheets(plans, &sheet, 1, &provider, 0, nullptr,
                                       nullptr, nullptr, &kept, &error);
    rmr_test_fail_after(SIZE_MAX);
    if (status == RMR_OK) break;
    CHECK(status == RMR_ERR_OUT_OF_MEMORY);
    CHECK(kept.pages == (rmr_buffer*)1 && kept.count == 9);
  }
  CHECK(failure > 5);
  CHECK(kept.count == 1);
  rmr_svg_pages_free(&kept);

  rmr_plan_free(simple);
  rmr_plan_free(plan);
}

int main(void) {
  structure();
  contract();
  return 0;
}
