/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Platform SVG entry points: a notebook page with typed text and imported PDF
 * document pages. The visual comparison runs when a rasterizer exists. */
#include <rmlines/parser.h>

#include "pdf_fixture.h"
#include "svg_raster.h"
#include "test.h"
#ifdef RMR_TEST_LINUX
#include <fontconfig/fontconfig.h>
#include <rmrender/linux_document.h>
#define native(name) rmr_linux_##name
#else
#include <rmrender/apple_document.h>
#define native(name) rmr_apple_##name
#endif
#define DOC "11111111-1111-1111-1111-111111111111"

/* Whether the buffer contains needle. */
static bool contains(const rmr_buffer* b, const char* needle) {
  size_t n = strlen(needle);
  for (size_t i = 0; i + n <= b->size; i++)
    if (!memcmp(b->bytes + i, needle, n)) return true;
  return false;
}

/* Typed text, handwriting and a highlight as SVG; the typed glyphs must be
 * vector outlines matching the native bitmap of the PDF-text page plan. */
static void notebook(const native(fonts) * fonts, const char* tool) {
  const char* words = "Typed notes, مرحبا and more";
  rm_face_run run = {0, 5, true, false};
  rm_paragraph paragraphs[] = {{.text = {words, strlen(words)},
                                .has_format = true,
                                .format = {.raw = 10},
                                .faces = &run,
                                .face_count = 1},
                               {.text = {"second line", 11}}};
  rm_text_block text = {.origin_x = -250,
                        .origin_y = 100,
                        .paragraphs = paragraphs,
                        .paragraph_count = 2,
                        .has_column_width = true,
                        .column_width = 500};
  rm_stroke_point points[] = {{.x = -200, .y = 300, .width = 6, .pressure = 1},
                              {.x = 150, .y = 340, .width = 6, .pressure = 1}};
  rm_stroke stroke = {.tool = RM_TOOL_FINELINER_2,
                      .color = RM_COLOR_BLUE,
                      .points = points,
                      .point_count = 2,
                      .id = {1, 2}};
  rm_rect rect = {-240, 120, 200, 40};
  rm_highlight highlight = {.rectangles = &rect,
                            .rectangle_count = 1,
                            .color = RM_COLOR_YELLOW,
                            .id = {2, 3}};
  rm_page page = {.has_text = true,
                  .text = text,
                  .strokes = &stroke,
                  .stroke_count = 1,
                  .highlights = &highlight,
                  .highlight_count = 1};
  rmr_point canvas = {600, 800};

  rmr_buffer svg = {0};
  rmr_error error = {0};
  OK(native(page_svg)(&page, canvas, RMR_EDGE_RIBBON, 0, nullptr, fonts,
                      RMR_PAPER_A4, 0, nullptr, nullptr, nullptr, &svg,
                      &error));
  CHECK(svg_well_formed((const char*)svg.bytes, svg.size));
  CHECK(svg_xmllint(svg.bytes, svg.size));
  CHECK(contains(&svg, "pt\" viewBox=\"0 0 "));
  CHECK(!contains(&svg, "<image"));
  CHECK(contains(&svg, " fill=\"#000000\"/>"));

  rmr_buffer again = {0};
  OK(native(page_svg)(&page, canvas, RMR_EDGE_RIBBON, 0, nullptr, fonts,
                      RMR_PAPER_A4, 0, nullptr, nullptr, nullptr, &again,
                      &error));
  CHECK(again.size == svg.size && !memcmp(again.bytes, svg.bytes, svg.size));
  rmr_buffer_free(&again);

  rmr_buffer kept = {(uint8_t*)1, 7};
  rmr_limits small = rmr_limits_default();
  small.maximum_pdf_bytes = svg.size - 1;
  CHECK(native(page_svg)(&page, canvas, RMR_EDGE_RIBBON, 0, nullptr, fonts,
                         RMR_PAPER_A4, 0, &small, nullptr, nullptr, &kept,
                         &error) == RMR_ERR_LIMIT);
  CHECK(kept.bytes == (uint8_t*)1 && kept.size == 7);
  CHECK(native(page_svg)(&page, canvas, RMR_EDGE_RIBBON, 0, nullptr, nullptr,
                         RMR_PAPER_A4, 0, nullptr, nullptr, nullptr, &kept,
                         &error) == RMR_ERR_UNSUPPORTED);

  const char* keep = getenv("RMR_SVG_KEEP_PAGE");
  if (keep && *keep) {
    FILE* f = fopen(keep, "wb");
    CHECK(f && fwrite(svg.bytes, 1, svg.size, f) == svg.size);
    fclose(f);
  }

  if (tool) {
    rmr_plan* plan = nullptr;
    OK(native(page_plan)(&page, canvas, RMR_EDGE_RIBBON, 0, nullptr, fonts,
                         nullptr, &plan, &error));
    rmr_sheet sheet;
    OK(rmr_paper_sheet(RMR_PAPER_A4, canvas.x, canvas.y,
                       rmr_plan_viewport(plan), &sheet, &error));
    CHECK(floor(sheet.width) == sheet.width &&
          floor(sheet.height) == sheet.height);

    /* Twice the point size, so glyph edges are a small share of pixels. */
    rmr_sheet doubled = {sheet.width * 2,
                         sheet.height * 2,
                         {sheet.transform.a * 2, 0, 0, sheet.transform.d * 2,
                          sheet.transform.tx * 2, sheet.transform.ty * 2}};
    rmr_bitmap reference = {0}, drawn = {0};
    OK(native(bitmap_on_sheet)(plan, &doubled, (rmr_color){1, 1, 1, 1}, nullptr,
                               nullptr, nullptr, &reference, &error));
    CHECK(svg_rasterize(tool, &svg, (size_t)doubled.width,
                        (size_t)doubled.height, &drawn));
    svg_difference d = svg_compare(&reference, &drawn);
    printf("notebook page with typed text, %s: mean %.3f, strong %.4f%%\n",
           tool, d.mean, d.strong * 100);
    CHECK(d.mean < 1.5 && d.strong < .005);

    free(drawn.pixels);
    rmr_bitmap_free(&reference);
    rmr_plan_free(plan);
  }

  rmr_buffer_free(&svg);
}

/* An imported PDF's pages render as embedded rasters under the marks. */
static void imported(const char* tool) {
  fixture original = source(false);
  const char name[] = DOC ".pdf";
  rmd_file file = {{name, sizeof name - 1},
                   {(const uint8_t*)original.bytes, original.n}};
  rmd_archive archive = {.files = &file, .count = 1};
  rmr_archive_document* document = nullptr;
  rmr_error error = {0};
  OK(rmr_archive_document_create(&archive, DOC, true, nullptr, &document,
                                 &error));

  rmr_svg_pages pages = {0};
  rmr_document_report report = {0};
  OK(native(document_svg)(document, nullptr, nullptr, RMR_PAPER_A4, 0, 96,
                          nullptr, nullptr, nullptr, &pages, &report, &error));
  CHECK(pages.count == 2 && report.document_pages == 2 &&
        report.rendered_pages == 2);
  for (size_t i = 0; i < pages.count; i++) {
    CHECK(svg_well_formed((const char*)pages.pages[i].bytes,
                          pages.pages[i].size));
    CHECK(svg_xmllint(pages.pages[i].bytes, pages.pages[i].size));
    CHECK(contains(&pages.pages[i], "data:image/png;base64,"));
    CHECK(contains(&pages.pages[i], "width=\"200pt\" height=\"200pt\""));
  }

  if (tool) {
    /* The rotated second page is a solid blue square. */
    rmr_bitmap drawn = {0};
    CHECK(svg_rasterize(tool, &pages.pages[1], 100, 100, &drawn));
    size_t blue = 0;
    for (size_t i = 0; i < 100 * 100; i++) {
      const uint8_t* p = drawn.pixels + i * 4;
      if (p[0] < 30 && p[1] < 30 && p[2] > 220) blue++;
    }
    printf("imported PDF page: %.2f%% blue\n", (double)blue / 100);
    CHECK(blue > 9500);
    free(drawn.pixels);
  }

  rmr_svg_pages one = {0}, kept = {(rmr_buffer*)1, 5};
  OK(native(document_svg)(document, nullptr, nullptr, RMR_PAPER_A4, 1, 0,
                          nullptr, nullptr, nullptr, &one, nullptr, &error));
  CHECK(one.count == 1 && one.pages[0].size > pages.pages[0].size);
  rmr_limits small = rmr_limits_default();
  small.maximum_pdf_bytes = pages.pages[0].size + pages.pages[1].size - 1;
  CHECK(native(document_svg)(document, nullptr, nullptr, RMR_PAPER_A4, 0, 96,
                             &small, nullptr, nullptr, &kept, nullptr,
                             &error) == RMR_ERR_LIMIT);
  CHECK(kept.pages == (rmr_buffer*)1 && kept.count == 5);
  CHECK(native(document_svg)(nullptr, nullptr, nullptr, RMR_PAPER_A4, 0, 0,
                             nullptr, nullptr, nullptr, &kept, nullptr,
                             &error) == RMR_ERR_INVALID_ARGUMENT);

  rmr_svg_pages_free(&one);
  rmr_svg_pages_free(&pages);
  rmr_archive_document_free(document);
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const char* tool = svg_rasterizer();
  if (!tool) puts("no SVG rasterizer; visual comparisons skipped");

  native(fonts)* fonts = nullptr;
  rmr_error error = {0};
  OK(native(fonts_create_directory)(argv[1], nullptr, &fonts, &error));
  notebook(fonts, tool);
  native(fonts_free)(fonts);
  imported(tool);

#ifdef RMR_TEST_LINUX
  FcFini();
#endif
  return 0;
}
