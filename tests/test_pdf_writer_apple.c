/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* The Apple PDF writer against CoreGraphics itself: numbers printed as
 * CoreGraphics prints them, and pages it writes painting exactly what
 * CoreGraphics' own pages paint. */

#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple.h>
#include <rmrender/apple_pdf.h>
#include <rmrender/template.h>

#include "../backends/common/rmr_pdf_stream.h"
#include "../src/rmr_pdf_edit.h"
#include "test.h"

static uint64_t state = 0x243f6a8885a308d3u;

static uint64_t next(void) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

static double uniform(double low, double high) {
  return low + (high - low) * (double)(next() >> 11) / 9007199254740992.0;
}

/* The decoded content of page 1 of a PDF made by CoreGraphics. */
static CFDataRef page_content(CFDataRef pdf) {
  CGDataProviderRef provider = CGDataProviderCreateWithCFData(pdf);
  CGPDFDocumentRef document = CGPDFDocumentCreateWithProvider(provider);
  CGDataProviderRelease(provider);
  CHECK(document);
  CGPDFDictionaryRef page =
      CGPDFPageGetDictionary(CGPDFDocumentGetPage(document, 1));
  CGPDFStreamRef stream = nullptr;
  CHECK(CGPDFDictionaryGetStream(page, "Contents", &stream));
  CGPDFDataFormat format;
  CFDataRef data = CGPDFStreamCopyData(stream, &format);
  CHECK(data && format == CGPDFDataFormatRaw);
  CGPDFDocumentRelease(document);
  return data;
}

/* Each value drawn as a path coordinate (v, index + 0.25) by CoreGraphics
 * prints as rmr_pdf_number prints it. */
static void numbers(void) {
  enum { COUNT = 20000 };
  double* values = malloc(COUNT * sizeof *values);
  CHECK(values);
  for (size_t i = 0; i < COUNT; i++) {
    switch (i % 4) {
      case 0:
        values[i] = uniform(-2000, 2000);
        break;
      case 1:
        values[i] = (next() & 1 ? 1 : -1) * pow(10, uniform(-8, 9));
        break;
      case 2:
        values[i] = ((double)(1000000 + next() % 9000000) + 0.5) *
                    pow(10, (double)(int)(next() % 10) - 9);
        break;
      default:
        values[i] = round(uniform(-1e6, 1e6)) / 1e4;
    }
  }

  CFMutableDataRef pdf = CFDataCreateMutable(kCFAllocatorDefault, 0);
  CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData(pdf);
  CGRect box = CGRectMake(0, 0, 612, 792);
  CGContextRef cg = CGPDFContextCreate(consumer, &box, nullptr);
  CGDataConsumerRelease(consumer);
  CGPDFContextBeginPage(cg, nullptr);
  for (size_t i = 0; i < COUNT; i++) {
    CGContextMoveToPoint(cg, values[i], (double)i + 0.25);
    CGContextAddLineToPoint(cg, 1, 2);
    CGContextAddLineToPoint(cg, 3, 1);
    CGContextClosePath(cg);
    CGContextFillPath(cg);
  }
  CGPDFContextEndPage(cg);
  CGPDFContextClose(cg);
  CGContextRelease(cg);

  CFDataRef content = page_content(pdf);
  size_t size = (size_t)CFDataGetLength(content);
  char* text = malloc(size + 1);
  CHECK(text);
  memcpy(text, CFDataGetBytePtr(content), size);
  text[size] = 0;
  for (size_t i = 0; i < size; i++)
    if (text[i] == '\n') text[i] = ' ';

  /* Tokens before "<index>.25 m". */
  size_t checked = 0;
  char* previous = nullptr;
  for (char* token = strtok(text, " "); token; token = strtok(nullptr, " ")) {
    size_t length = strlen(token);
    if (previous && length > 3 && !strcmp(token + length - 3, ".25")) {
      char* end = nullptr;
      unsigned long index = strtoul(token, &end, 10);
      char* after = strtok(nullptr, " ");
      if (end == token + length - 3 && after && !strcmp(after, "m") &&
          index < COUNT) {
        char mine[RMR_PDF_NUMBER_SIZE];
        size_t n = rmr_pdf_number(mine, values[index]);
        if (n != strlen(previous) || memcmp(mine, previous, n)) {
          fprintf(stderr, "%a: CoreGraphics %s, writer %.*s\n", values[index],
                  previous, (int)n, mine);
          CHECK(false);
        }
        checked++;
      }
      previous = after;
      continue;
    }
    previous = token;
  }
  /* CoreGraphics drops degenerate triangles; most values are compared. */
  CHECK(checked > COUNT * 9 / 10);
  free(text);
  CFRelease(content);
  CFRelease(pdf);
  free(values);
}

/* Page number of pdf rasterized at scale onto white. */
static uint8_t* raster_page(rmr_buffer pdf, size_t number, double scale,
                            size_t* width, size_t* height) {
  CFDataRef data =
      CFDataCreate(kCFAllocatorDefault, pdf.bytes, (CFIndex)pdf.size);
  CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
  CGPDFDocumentRef document = CGPDFDocumentCreateWithProvider(provider);
  CGDataProviderRelease(provider);
  CFRelease(data);
  CHECK(document);
  CGPDFPageRef page = CGPDFDocumentGetPage(document, number);
  CHECK(page);
  CGRect box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
  *width = (size_t)ceil(box.size.width * scale);
  *height = (size_t)ceil(box.size.height * scale);
  uint8_t* pixels = calloc(*width * *height, 4);
  CHECK(pixels);
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef cg =
      CGBitmapContextCreate(pixels, *width, *height, 8, *width * 4, space,
                            kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(cg, 1, 1, 1, 1);
  CGContextFillRect(cg, CGRectMake(0, 0, (double)*width, (double)*height));
  CGContextScaleCTM(cg, scale, scale);
  CGContextDrawPDFPage(cg, page);
  CGContextRelease(cg);
  CGPDFDocumentRelease(document);
  return pixels;
}

/* Page 1 of pdf rasterized at scale onto white. */
static uint8_t* raster(rmr_buffer pdf, double scale, size_t* width,
                       size_t* height) {
  return raster_page(pdf, 1, scale, width, height);
}

static bool contains(rmr_buffer b, const char* s) {
  size_t n = strlen(s);
  for (size_t i = 0; i + n <= b.size; i++)
    if (!memcmp(b.bytes + i, s, n)) return true;
  return false;
}

/* A plan of every kind of mark, with text and a group first when mixed. */
static rmr_plan* marks(bool mixed) {
  enum { STROKES = 120 };
  static rmr_element paths[STROKES][12];
  static uint8_t bits[STROKES][64];
  static rmr_element eraser[] = {{.kind = RMR_MOVE, .end = {-200, 300}},
                                 {.kind = RMR_LINE, .end = {200, 300}},
                                 {.kind = RMR_LINE, .end = {200, 360}},
                                 {.kind = RMR_LINE, .end = {-200, 360}},
                                 {.kind = RMR_CLOSE}};
  static rmr_path_view erasers[] = {{eraser, 5}};
  rmr_draw draws[STROKES + 2] = {0};
  size_t count = 0;
  if (mixed) {
    draws[count++] =
        (rmr_draw){.mode = RMR_TEXT,
                   .color = {0.1, 0.2, 0.3, 1},
                   .text = {"Mixed page", 10, 40, {-600, 80}, {0}}};
    draws[count++] = (rmr_draw){.path = {paths[0], 4},
                                .mode = RMR_FILL,
                                .color = {0.9, 0.8, 0.1, 1},
                                .group = 1,
                                .group_alpha = 0.4,
                                .group_blend = RMR_BLEND_DARKEN};
  }
  for (size_t i = 0; i < STROKES; i++) {
    double x = uniform(-650, 550), y = uniform(50, 1750);
    rmr_element* p = paths[i];
    p[0] = (rmr_element){.kind = RMR_MOVE, .end = {x, y}};
    for (int k = 1; k < 10; k++)
      p[k] =
          (rmr_element){.kind = k % 4 ? RMR_LINE : RMR_CURVE,
                        .control1 = {x + uniform(0, 30), y + uniform(-10, 10)},
                        .control2 = {x + uniform(0, 30), y + uniform(-10, 10)},
                        .end = {x + k * uniform(3, 9), y + uniform(-20, 20)}};
    p[10] = (rmr_element){.kind = RMR_CLOSE};
    for (size_t k = 0; k < 64; k++) bits[i][k] = (uint8_t)next();
    if (i % 9 == 1) memcpy(bits[i], bits[i - 1], 64);
    rmr_color color = {uniform(0, 1), uniform(0, 1), uniform(0, 1),
                       i % 5 ? 1 : uniform(0.2, 0.9)};
    rmr_blend blend = (rmr_blend)(i % 3);
    rmr_draw* d = &draws[count++];
    switch (i % 6) {
      case 0:
        *d = (rmr_draw){
            .path = {p, 11}, .mode = RMR_FILL, .color = color, .blend = blend};
        break;
      case 1:
        /* Masks of shared bytes but other dimensions follow one another. */
        *d = (rmr_draw){
            .path = {p, 11},
            .mode = RMR_FILL,
            .color = color,
            .mask = {bits[i], 14 + (i % 4) * 2, 16, 4, {x, y - 20, 60, 40}}};
        break;
      case 2:
        *d = (rmr_draw){.path = {p, 10},
                        .mode = RMR_STROKE,
                        .color = color,
                        .blend = blend,
                        .line_width = uniform(0.5, 9),
                        .cap = (rmr_cap)(i % 3),
                        .join = (rmr_join)(i % 3),
                        .miter_limit = i % 2 ? 2 : 0};
        break;
      case 3:
        *d = (rmr_draw){.path = {p, 11},
                        .mode = RMR_FILL_STROKE,
                        .color = color,
                        .stroke_color = {0.2, 0.1, 0.7, uniform(0.3, 1)},
                        .line_width = 2.5,
                        .join = RMR_JOIN_MITER};
        break;
      case 4:
        *d = (rmr_draw){.path = {p, 10},
                        .mode = RMR_STROKE,
                        .color = color,
                        .line_width = 4,
                        .cap = RMR_CAP_ROUND,
                        .join = RMR_JOIN_ROUND,
                        .erasers = erasers,
                        .eraser_count = 1};
        break;
      default:
        *d = (rmr_draw){.path = {p, 11},
                        .mode = RMR_FILL,
                        .color = color,
                        .erasers = erasers,
                        .eraser_count = 1,
                        .mask = {bits[i], 20, 16, 4, {x, y - 20, 60, 40}}};
    }
  }
  rmr_plan* plan = nullptr;
  OK(rmr_plan_create(draws, count, (rmr_rect){-702, 0, 1404, 1872}, nullptr,
                     &plan, nullptr));
  return plan;
}

static void identity(bool mixed) {
  rmr_plan* plan = marks(mixed);
  rmr_sheet sheet = {445.33216129,
                     593.7762345,
                     {0.317180616, 0, 0, -0.317180616, 222.666, 593.7762345}};
  const rmr_plan* plans[] = {plan};

  /* CoreGraphics alone, as rmr_apple_pdf_sheets draws. */
  rmr_buffer native = {0}, written = {0}, rounded = {0};
  OK(rmr_apple_pdf_sheets(plans, &sheet, 1, nullptr, nullptr, nullptr, &native,
                          nullptr));
  /* The writer, alone or after CoreGraphics' share of a mixed page. */
  rmr_pdf_page page = {.marks = plan, .sheet = sheet};
  OK(rmr_apple_pdf_overlay(nullptr, &page, 1, nullptr, nullptr, nullptr,
                           &written, nullptr, nullptr));
  CHECK(contains(written, "/RMRx") && contains(written, "/RMRg"));

  for (double scale = 1; scale <= 6; scale *= 2.5) {
    size_t w1, h1, w2, h2;
    uint8_t* a = raster(native, scale, &w1, &h1);
    uint8_t* b = raster(written, scale, &w2, &h2);
    CHECK(w1 == w2 && h1 == h2);
    size_t differing = 0;
    for (size_t i = 0; i < w1 * h1 * 4; i += 4)
      differing += memcmp(a + i, b + i, 4) != 0;
    if (differing)
      fprintf(stderr, "mixed %d scale %g: %zu pixels differ\n", mixed, scale,
              differing);
    CHECK(!differing);
    free(a);
    free(b);
  }

  /* Rounding is asked for, never assumed: two decimals are smaller and
   * still a valid page. */
  rmr_pdf_options options = rmr_pdf_options_default();
  options.decimals = 2;
  OK(rmr_apple_pdf_overlay_with_options(nullptr, &page, 1, nullptr, &options,
                                        nullptr, nullptr, &rounded, nullptr,
                                        nullptr));
  CHECK(rounded.size < written.size);
  size_t w, h;
  free(raster(rounded, 1, &w, &h));
  options.decimals = 7;
  rmr_buffer refused = {0};
  CHECK(rmr_apple_pdf_overlay_with_options(
            nullptr, &page, 1, nullptr, &options, nullptr, nullptr, &refused,
            nullptr, nullptr) == RMR_ERR_INVALID_ARGUMENT);

  rmr_buffer_free(&native);
  rmr_buffer_free(&written);
  rmr_buffer_free(&rounded);
  rmr_plan_free(plan);
}

/* Pixels that differ between two rasters of one size. */
static size_t differing(const uint8_t* a, const uint8_t* b, size_t bytes) {
  size_t count = 0;
  for (size_t i = 0; i < bytes; i += 4) count += memcmp(a + i, b + i, 4) != 0;
  return count;
}

/* A plan of a line nothing erases and, unless only_kept, every mark an
 * eraser takes away whole — a grain mask, a fill, a line — and a grain mask
 * of no contour at all; and a grain mask an eraser takes half of. */
static rmr_plan* erased_marks(bool only_kept) {
  static rmr_element witness[] = {{.kind = RMR_MOVE, .end = {-600, 1500}},
                                  {.kind = RMR_LINE, .end = {600, 1500}},
                                  {.kind = RMR_LINE, .end = {600, 1520}},
                                  {.kind = RMR_LINE, .end = {-600, 1520}},
                                  {.kind = RMR_CLOSE}};
  static rmr_element band[] = {{.kind = RMR_MOVE, .end = {-200, 300}},
                               {.kind = RMR_LINE, .end = {200, 300}},
                               {.kind = RMR_LINE, .end = {200, 340}},
                               {.kind = RMR_LINE, .end = {-200, 340}},
                               {.kind = RMR_CLOSE}};
  static rmr_element half[] = {{.kind = RMR_MOVE, .end = {-200, 700}},
                               {.kind = RMR_LINE, .end = {200, 700}},
                               {.kind = RMR_LINE, .end = {200, 740}},
                               {.kind = RMR_LINE, .end = {-200, 740}},
                               {.kind = RMR_CLOSE}};
  static rmr_element whole[] = {{.kind = RMR_MOVE, .end = {-300, 200}},
                                {.kind = RMR_LINE, .end = {300, 200}},
                                {.kind = RMR_LINE, .end = {300, 440}},
                                {.kind = RMR_LINE, .end = {-300, 440}},
                                {.kind = RMR_CLOSE}};
  static rmr_element left[] = {{.kind = RMR_MOVE, .end = {-300, 600}},
                               {.kind = RMR_LINE, .end = {0, 600}},
                               {.kind = RMR_LINE, .end = {0, 840}},
                               {.kind = RMR_LINE, .end = {-300, 840}},
                               {.kind = RMR_CLOSE}};
  static rmr_element west[] = {{.kind = RMR_MOVE, .end = {-300, 200}},
                               {.kind = RMR_LINE, .end = {10, 200}},
                               {.kind = RMR_LINE, .end = {10, 440}},
                               {.kind = RMR_LINE, .end = {-300, 440}},
                               {.kind = RMR_CLOSE}};
  static rmr_element east[] = {{.kind = RMR_MOVE, .end = {-10, 200}},
                               {.kind = RMR_LINE, .end = {300, 200}},
                               {.kind = RMR_LINE, .end = {300, 440}},
                               {.kind = RMR_LINE, .end = {-10, 440}},
                               {.kind = RMR_CLOSE}};
  /* One eraser over it all; two taking it between them; one over half. */
  static rmr_path_view covering[] = {{whole, 5}},
                       halves[] = {{west, 5}, {east, 5}},
                       leftward[] = {{left, 5}};
  static uint8_t bits[4][5 * 40];
  for (size_t k = 0; k < sizeof bits; k++)
    bits[k / sizeof *bits][k % sizeof *bits] = (uint8_t)(k * 37 + 11);

  rmr_draw draws[7] = {0};
  size_t count = 0;
  draws[count++] = (rmr_draw){
      .path = {witness, 5}, .mode = RMR_FILL, .color = {0.1, 0.2, 0.6, 1}};
  if (!only_kept) {
    draws[count++] =
        (rmr_draw){.path = {band, 5},
                   .mode = RMR_FILL,
                   .color = {0, 0, 0, 1},
                   .erasers = covering,
                   .eraser_count = 1,
                   .mask = {bits[0], 40, 40, 5, {-210, 290, 420, 60}}};
    draws[count++] =
        (rmr_draw){.mode = RMR_FILL,
                   .color = {0, 0, 0, 0.6},
                   .mask = {bits[1], 40, 40, 5, {-210, 990, 420, 60}}};
    draws[count++] = (rmr_draw){.path = {band, 5},
                                .mode = RMR_FILL,
                                .color = {0.8, 0.1, 0.1, 1},
                                .blend = RMR_BLEND_DARKEN,
                                .erasers = halves,
                                .eraser_count = 2};
    draws[count++] =
        (rmr_draw){.path = {band, 5},
                   .mode = RMR_FILL,
                   .color = {0.2, 0.2, 0.2, 0.7},
                   .erasers = halves,
                   .eraser_count = 2,
                   .mask = {bits[3], 40, 40, 5, {-210, 290, 420, 60}}};
    draws[count++] = (rmr_draw){.path = {band, 4},
                                .mode = RMR_STROKE,
                                .color = {0.1, 0.6, 0.1, 0.5},
                                .line_width = 6,
                                .cap = RMR_CAP_ROUND,
                                .join = RMR_JOIN_ROUND,
                                .erasers = covering,
                                .eraser_count = 1};
  }
  draws[count++] =
      (rmr_draw){.path = {half, 5},
                 .mode = RMR_FILL,
                 .color = {0, 0, 0, 1},
                 .erasers = leftward,
                 .eraser_count = 1,
                 .mask = {bits[2], 40, 40, 5, {-210, 690, 420, 60}}};
  rmr_plan* plan = nullptr;
  OK(rmr_plan_create(draws, count, (rmr_rect){-702, 0, 1404, 1872}, nullptr,
                     &plan, nullptr));
  return plan;
}

/* A mark an eraser takes away whole, and a grain mask with no contour,
 * paint nothing: in a bitmap, on a page CoreGraphics draws and on a page
 * the writer writes — not the whole rectangle of the grain, which
 * CoreGraphics draws under the clip of an empty path. What an eraser takes
 * only part of still paints the rest, the same in all three. */
static void erased_away(void) {
  rmr_plan* erased = erased_marks(false);
  rmr_plan* kept = erased_marks(true);
  rmr_sheet sheet = {445.33216129,
                     593.7762345,
                     {0.317180616, 0, 0, -0.317180616, 222.666, 593.7762345}};
  /* A bitmap's sheet is whole pixels. */
  rmr_sheet canvas = {900, 1200, {0.64, 0, 0, -0.64, 450, 1200}};

  rmr_bitmap bitmaps[2] = {0};
  rmr_buffer native[2] = {0}, written[2] = {0};
  for (size_t i = 0; i < 2; i++) {
    const rmr_plan* plans[] = {i ? kept : erased};
    OK(rmr_apple_bitmap_on_sheet(plans[0], &canvas, (rmr_color){1, 1, 1, 1},
                                 nullptr, nullptr, nullptr, &bitmaps[i],
                                 nullptr));
    OK(rmr_apple_pdf_sheets(plans, &sheet, 1, nullptr, nullptr, nullptr,
                            &native[i], nullptr));
    rmr_pdf_page page = {.marks = plans[0], .sheet = sheet};
    OK(rmr_apple_pdf_overlay(nullptr, &page, 1, nullptr, nullptr, nullptr,
                             &written[i], nullptr, nullptr));
  }
  CHECK(bitmaps[0].width == bitmaps[1].width &&
        bitmaps[0].height == bitmaps[1].height);
  size_t bitmap_differing = differing(bitmaps[0].pixels, bitmaps[1].pixels,
                                      bitmaps[0].stride * bitmaps[0].height);
  if (bitmap_differing)
    fprintf(stderr, "erased bitmap: %zu pixels differ\n", bitmap_differing);
  CHECK(!bitmap_differing);

  for (double scale = 1; scale <= 6; scale *= 2.5) {
    size_t width[4], height[4];
    uint8_t* pixels[4] = {
        raster(native[0], scale, &width[0], &height[0]),
        raster(written[0], scale, &width[1], &height[1]),
        raster(native[1], scale, &width[2], &height[2]),
        raster(written[1], scale, &width[3], &height[3]),
    };
    for (size_t i = 1; i < 4; i++) {
      CHECK(width[i] == width[0] && height[i] == height[0]);
      size_t count = differing(pixels[0], pixels[i], width[0] * height[0] * 4);
      if (count)
        fprintf(stderr, "erased scale %g, page %zu: %zu pixels differ\n", scale,
                i, count);
      CHECK(!count);
    }
    for (size_t i = 0; i < 4; i++) free(pixels[i]);
  }

  for (size_t i = 0; i < 2; i++) {
    rmr_bitmap_free(&bitmaps[i]);
    rmr_buffer_free(&native[i]);
    rmr_buffer_free(&written[i]);
  }
  rmr_plan_free(erased);
  rmr_plan_free(kept);
}

/* Where s first occurs in b at or after from, SIZE_MAX if nowhere. */
static size_t find(rmr_buffer b, const char* s, size_t from) {
  size_t n = strlen(s);
  if (from > b.size || n > b.size - from) return SIZE_MAX;
  for (size_t i = from; i + n <= b.size; i++)
    if (!memcmp(b.bytes + i, s, n)) return i;
  return SIZE_MAX;
}

/* How many times s occurs in b. */
static size_t occurrences(rmr_buffer b, const char* s) {
  size_t count = 0;
  for (size_t at = find(b, s, 0); at != SIZE_MAX; at = find(b, s, at + 1))
    count++;
  return count;
}

/* Pages number of a and b paint the same pixels at three scales. */
static void same_page(rmr_buffer a, rmr_buffer b, size_t number,
                      const char* label) {
  for (double scale = 1; scale <= 6.25; scale *= 2.5) {
    size_t w1, h1, w2, h2;
    uint8_t* x = raster_page(a, number, scale, &w1, &h1);
    uint8_t* y = raster_page(b, number, scale, &w2, &h2);
    CHECK(w1 == w2 && h1 == h2);
    size_t count = differing(x, y, w1 * h1 * 4);
    if (count)
      fprintf(stderr, "%s page %zu scale %g: %zu pixels differ\n", label,
              number, scale, count);
    CHECK(!count);
    free(x);
    free(y);
  }
}

static const rmr_sheet tablet = {
    612, 816, {0.4358974358974359, 0, 0, -0.4358974358974359, 306, 816}};

/* Transparency groups of every kind of member: strokes of each join, grain
 * masks, erased strokes and masked fills, over ink they blend with; groups
 * one after another, one interrupted and taken up again under the same
 * token, one off the page and one partly off it, one erased away. A group's
 * alpha and blend mode are given. With text, the plan ends in an empty text
 * run, which leaves every draw to CoreGraphics and paints nothing. */
static rmr_plan* grouped(double alpha, rmr_blend blend, bool text) {
  static rmr_element under[] = {{.kind = RMR_MOVE, .end = {-500, 200}},
                                {.kind = RMR_LINE, .end = {400, 200}},
                                {.kind = RMR_LINE, .end = {400, 900}},
                                {.kind = RMR_LINE, .end = {-500, 900}},
                                {.kind = RMR_CLOSE}};
  static rmr_element swipe[] = {{.kind = RMR_MOVE, .end = {-600, 300.25}},
                                {.kind = RMR_CURVE,
                                 .control1 = {-300, 250},
                                 .control2 = {0, 420},
                                 .end = {350.5, 330}},
                                {.kind = RMR_LINE, .end = {200, 520}}};
  static rmr_element corner[] = {{.kind = RMR_MOVE, .end = {-400, 700}},
                                 {.kind = RMR_LINE, .end = {-100, 600}},
                                 {.kind = RMR_LINE, .end = {-380, 560}}};
  static rmr_element box[] = {{.kind = RMR_MOVE, .end = {-100, 750}},
                              {.kind = RMR_CURVE,
                               .control1 = {50, 640},
                               .control2 = {150, 900},
                               .end = {250, 760}},
                              {.kind = RMR_LINE, .end = {250, 980}},
                              {.kind = RMR_LINE, .end = {-100, 980}},
                              {.kind = RMR_CLOSE}};
  static rmr_element cut[] = {{.kind = RMR_MOVE, .end = {-50, 280}},
                              {.kind = RMR_LINE, .end = {60, 280}},
                              {.kind = RMR_LINE, .end = {60, 1000}},
                              {.kind = RMR_LINE, .end = {-50, 1000}},
                              {.kind = RMR_CLOSE}};
  static rmr_element away[] = {{.kind = RMR_MOVE, .end = {-2000, 300}},
                               {.kind = RMR_LINE, .end = {-1500, 320}}};
  static rmr_element edge[] = {{.kind = RMR_MOVE, .end = {500, 1700}},
                               {.kind = RMR_LINE, .end = {900, 1900}}};
  static rmr_element tiny[] = {{.kind = RMR_MOVE, .end = {0, 290}},
                               {.kind = RMR_LINE, .end = {10, 290}}};
  static rmr_path_view erasers[] = {{cut, 5}};
  static uint8_t bits[16 * 40];
  for (size_t k = 0; k < sizeof bits; k++) bits[k] = (uint8_t)(k * 53 + 7);

  rmr_draw base = {
      .color = {1, 1, 1, 1}, .group_alpha = alpha, .group_blend = blend};
  rmr_draw draws[32];
  size_t count = 0;
  draws[count++] = (rmr_draw){
      .path = {under, 5}, .mode = RMR_FILL, .color = {0.2, 0.5, 0.8, 1}};
  draws[count++] = (rmr_draw){.path = {swipe, 3},
                              .mode = RMR_STROKE,
                              .color = {0.1, 0.1, 0.1, 1},
                              .line_width = 3,
                              .cap = RMR_CAP_ROUND,
                              .join = RMR_JOIN_ROUND};
  /* Group 1: strokes of each join, a grain mask and erased members. */
  rmr_draw d = base;
  d.group = 1;
  d.path = (rmr_path_view){swipe, 3};
  d.mode = RMR_STROKE;
  d.color = (rmr_color){0.95, 0.85, 0.1, 1};
  d.line_width = 40;
  d.cap = RMR_CAP_ROUND;
  d.join = RMR_JOIN_ROUND;
  draws[count++] = d;
  d.path = (rmr_path_view){corner, 3};
  d.color = (rmr_color){0.9, 0.3, 0.2, 1};
  d.line_width = 18;
  d.cap = RMR_CAP_BUTT;
  d.join = RMR_JOIN_MITER;
  d.miter_limit = 4;
  draws[count++] = d;
  d.join = RMR_JOIN_BEVEL;
  d.miter_limit = 0;
  d.cap = RMR_CAP_SQUARE;
  d.line_width = 6;
  d.color = (rmr_color){0.3, 0.9, 0.2, 1};
  draws[count++] = d;
  d.path = (rmr_path_view){box, 5};
  d.mode = RMR_FILL;
  d.color = (rmr_color){0.5, 0.1, 0.6, 1};
  d.mask = (rmr_mask_view){bits, 128, 40, 16, {-120, 700, 400, 300}};
  draws[count++] = d;
  d.erasers = erasers;
  d.eraser_count = 1;
  d.color = (rmr_color){0.1, 0.6, 0.6, 1};
  draws[count++] = d;
  d.mask = (rmr_mask_view){0};
  d.path = (rmr_path_view){swipe, 3};
  d.mode = RMR_STROKE;
  d.line_width = 24;
  d.cap = RMR_CAP_ROUND;
  d.join = RMR_JOIN_ROUND;
  d.color = (rmr_color){0.9, 0.5, 0.1, 1};
  draws[count++] = d;
  d.erasers = nullptr;
  d.eraser_count = 0;
  /* Ink between, then the same token again: a group of its own. */
  draws[count++] = (rmr_draw){.path = {corner, 3},
                              .mode = RMR_STROKE,
                              .color = {0, 0, 0, 0.5},
                              .line_width = 2};
  d.path = (rmr_path_view){box, 5};
  d.mode = RMR_FILL;
  d.color = (rmr_color){0.2, 0.8, 0.4, 1};
  draws[count++] = d;
  /* Groups 2 and 3 one after the other, overlapping members inside. */
  d.group = 2;
  d.path = (rmr_path_view){under, 5};
  d.color = (rmr_color){0.8, 0.2, 0.2, 1};
  draws[count++] = d;
  d.path = (rmr_path_view){box, 5};
  d.color = (rmr_color){0.2, 0.2, 0.8, 1};
  draws[count++] = d;
  d.group = 3;
  d.path = (rmr_path_view){swipe, 3};
  d.mode = RMR_STROKE;
  d.line_width = 60;
  d.color = (rmr_color){0.7, 0.7, 0.1, 1};
  draws[count++] = d;
  /* Translucent ink, a group of its alpha and blend mode, then opaque ink:
   * what the group sets must not outlast it. */
  d = base;
  d.path = (rmr_path_view){box, 5};
  d.mode = RMR_FILL;
  d.color = (rmr_color){0.6, 0.3, 0.1, 0.3};
  d.blend = RMR_BLEND_DARKEN;
  draws[count++] = d;
  d.color.alpha = 1;
  d.blend = RMR_BLEND_NORMAL;
  d.group = 7;
  d.group_alpha = 0.3;
  d.group_blend = RMR_BLEND_DARKEN;
  d.path = (rmr_path_view){under, 5};
  draws[count++] = d;
  d.group = 0;
  d.color = (rmr_color){0.3, 0.3, 0.3, 1};
  d.mask = (rmr_mask_view){bits, 128, 40, 16, {-120, 700, 400, 300}};
  draws[count++] = d;
  d.mask = (rmr_mask_view){0};
  d.path = (rmr_path_view){corner, 3};
  d.mode = RMR_STROKE;
  d.line_width = 5;
  draws[count++] = d;
  d = base;
  d.mode = RMR_STROKE;
  d.color = (rmr_color){0.7, 0.7, 0.1, 1};
  d.line_width = 60;
  d.cap = RMR_CAP_ROUND;
  d.join = RMR_JOIN_ROUND;
  /* Off the page, partly off it, erased away. */
  d.group = 4;
  d.path = (rmr_path_view){away, 2};
  draws[count++] = d;
  d.group = 5;
  d.path = (rmr_path_view){edge, 2};
  draws[count++] = d;
  d.group = 6;
  d.path = (rmr_path_view){tiny, 2};
  d.line_width = 4;
  d.erasers = erasers;
  d.eraser_count = 1;
  draws[count++] = d;
  if (text)
    draws[count++] = (rmr_draw){.mode = RMR_TEXT,
                                .color = {0, 0, 0, 1},
                                .text = {nullptr, 0, 20, {0, 100}, {0}}};
  rmr_plan* plan = nullptr;
  OK(rmr_plan_create(draws, count, (rmr_rect){-702, 0, 1404, 1872}, nullptr,
                     &plan, nullptr));
  return plan;
}

/* A source PDF of one blank page of width by height. */
static rmr_buffer blank_page(double width, double height) {
  CFMutableDataRef pdf = CFDataCreateMutable(kCFAllocatorDefault, 0);
  CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData(pdf);
  CGRect box = CGRectMake(0, 0, width, height);
  CGContextRef cg = CGPDFContextCreate(consumer, &box, nullptr);
  CGDataConsumerRelease(consumer);
  CGPDFContextBeginPage(cg, nullptr);
  CGPDFContextEndPage(cg);
  CGPDFContextClose(cg);
  CGContextRelease(cg);
  size_t size = (size_t)CFDataGetLength(pdf);
  rmr_buffer out = {malloc(size), size};
  CHECK(out.bytes);
  memcpy(out.bytes, CFDataGetBytePtr(pdf), size);
  CFRelease(pdf);
  return out;
}

/* Transparency groups written by the writer paint what CoreGraphics'
 * transparency layers paint, for every blend mode and a range of alphas: on
 * a document's page, and on a page of a PDF updated in place, against the
 * same update with CoreGraphics drawing the groups. */
static void groups(void) {
  static const double alphas[] = {0, 0.25, 0.6, 1};
  for (unsigned blend = 0; blend <= RMR_BLEND_MULTIPLY; blend++)
    for (size_t k = 0; k < sizeof alphas / sizeof *alphas; k++) {
      rmr_plan* plan = grouped(alphas[k], (rmr_blend)blend, false);
      const rmr_plan* plans[] = {plan};
      rmr_buffer native = {0}, written = {0};
      OK(rmr_apple_pdf_sheets(plans, &tablet, 1, nullptr, nullptr, nullptr,
                              &native, nullptr));
      rmr_pdf_page page = {.marks = plan, .sheet = tablet};
      OK(rmr_apple_pdf_overlay(nullptr, &page, 1, nullptr, nullptr, nullptr,
                               &written, nullptr, nullptr));
      /* Written, not drawn: no CoreGraphics form. */
      CHECK(contains(written, "/S /Transparency") && !contains(written, "/Fm"));
      char label[64];
      snprintf(label, sizeof label, "groups blend %u alpha %g", blend,
               alphas[k]);
      same_page(native, written, 1, label);
      rmr_buffer_free(&native);
      rmr_buffer_free(&written);
      rmr_plan_free(plan);
    }

  rmr_buffer source = blank_page(tablet.width, tablet.height);
  rmr_apple_pdf_source* src = nullptr;
  OK(rmr_apple_pdf_source_create(source.bytes, source.size, nullptr, &src,
                                 nullptr));
  rmr_buffer updated[2] = {0};
  for (int text = 0; text < 2; text++) {
    rmr_plan* plan = grouped(0.4, RMR_BLEND_DARKEN, text);
    rmr_pdf_page page = {.marks = plan,
                         .sheet = tablet,
                         .source_rect = {-702, 0, 1404, 1872},
                         .has_source = true,
                         .source_visible = true};
    OK(rmr_apple_pdf_incremental(src, &page, 1, nullptr, nullptr, nullptr,
                                 &updated[text], nullptr, nullptr));
    rmr_plan_free(plan);
  }
  CHECK(contains(updated[0], "/S /Transparency") &&
        !contains(updated[0], "/Fm"));
  CHECK(contains(updated[1], "/Fm"));
  same_page(updated[1], updated[0], 1, "updated groups");
  rmr_buffer_free(&updated[0]);
  rmr_buffer_free(&updated[1]);
  rmr_apple_pdf_source_free(src);
  rmr_buffer_free(&source);
}

/* The text of the first /BBox array in b, "none" without one. */
static void first_box(rmr_buffer b, char out[160]) {
  size_t at = find(b, "/BBox", 0);
  size_t end = at == SIZE_MAX ? SIZE_MAX : find(b, "]", at);
  if (at == SIZE_MAX || end == SIZE_MAX || end - at >= 160) {
    snprintf(out, 160, "none");
    return;
  }
  snprintf(out, 160, "%.*s", (int)(end - at + 1), (const char*)b.bytes + at);
}

/* A random path about center: moves, lines and curves, closed if asked. */
static size_t random_path(rmr_element* e, double x, double y, double spread,
                          bool closed) {
  size_t n = 0;
  x += uniform(-spread, spread) / 2;
  y += uniform(-spread, spread) / 2;
  e[n++] = (rmr_element){.kind = RMR_MOVE, .end = {x, y}};
  for (int k = 1 + (int)(next() % 4); k > 0; k--) {
    double nx = x + uniform(-spread, spread) / 2,
           ny = y + uniform(-spread, spread) / 2;
    e[n++] = next() & 1
                 ? (rmr_element){.kind = RMR_LINE, .end = {nx, ny}}
                 : (rmr_element){.kind = RMR_CURVE,
                                 .control1 = {x + uniform(-spread, spread),
                                              y + uniform(-spread, spread)},
                                 .control2 = {nx + uniform(-spread, spread),
                                              ny + uniform(-spread, spread)},
                                 .end = {nx, ny}};
    x = nx;
    y = ny;
  }
  if (closed) e[n++] = (rmr_element){.kind = RMR_CLOSE};
  return n;
}

/* A group's BBox as the writer prints it is CoreGraphics' own, for random
 * groups of strokes of every cap, join and miter limit, fills, grain masks
 * and erased members, at several scales, some partly or wholly off the page
 * (a layer that misses the page is written by neither). */
static void group_boxes(void) {
  static const double scales[] = {0.4358974358974359, 1, 0.75, 2.5,
                                  0.3333333333333333};
  static uint8_t bits[64 * 8];
  for (size_t k = 0; k < sizeof bits; k++) bits[k] = (uint8_t)next();
  for (int c = 0; c < 300; c++) {
    double a = scales[c % 5];
    rmr_sheet sheet = {1404 * a, 1872 * a, {a, 0, 0, -a, 702 * a, 1872 * a}};
    rmr_element paths[4][12], cuts[4][12];
    rmr_path_view erasers[4];
    rmr_draw draws[4];
    size_t members = 1 + next() % 4;
    double x = uniform(-400, 400), y = uniform(300, 1500),
           spread = next() % 5 ? uniform(30, 500) : 2000;
    for (size_t k = 0; k < members; k++) {
      int kind = (int)(next() % 5);
      bool stroke = kind == 0 || kind == 3;
      size_t n = random_path(paths[k], x, y, spread, !stroke);
      draws[k] =
          (rmr_draw){.path = {paths[k], n},
                     .mode = stroke ? RMR_STROKE : RMR_FILL,
                     .color = {uniform(0, 1), uniform(0, 1), uniform(0, 1), 1},
                     .line_width = uniform(0.5, 40),
                     .cap = (rmr_cap)(next() % 3),
                     .join = (rmr_join)(next() % 3),
                     .miter_limit = next() & 1 ? 0 : uniform(1, 6),
                     .group = 9,
                     .group_alpha = 0.5,
                     .group_blend = RMR_BLEND_DARKEN};
      if (kind == 2)
        draws[k].mask =
            (rmr_mask_view){bits,
                            64,
                            64,
                            8,
                            {x + uniform(-spread, spread) * 0.6,
                             y + uniform(-spread, spread) * 0.6,
                             uniform(10, spread), uniform(10, spread)}};
      if (kind >= 3) {
        erasers[k] =
            (rmr_path_view){cuts[k], random_path(cuts[k], x, y, spread, true)};
        draws[k].erasers = &erasers[k];
        draws[k].eraser_count = 1;
      }
    }
    rmr_plan* plan = nullptr;
    OK(rmr_plan_create(draws, members, (rmr_rect){-702, 0, 1404, 1872}, nullptr,
                       &plan, nullptr));
    const rmr_plan* plans[] = {plan};
    rmr_buffer native = {0}, written = {0};
    OK(rmr_apple_pdf_sheets(plans, &sheet, 1, nullptr, nullptr, nullptr,
                            &native, nullptr));
    rmr_pdf_page page = {.marks = plan, .sheet = sheet};
    OK(rmr_apple_pdf_overlay(nullptr, &page, 1, nullptr, nullptr, nullptr,
                             &written, nullptr, nullptr));
    char want[160], got[160];
    first_box(native, want);
    first_box(written, got);
    if (strcmp(want, got))
      fprintf(stderr, "group %d: CoreGraphics %s, writer %s\n", c, want, got);
    CHECK(!strcmp(want, got));
    rmr_buffer_free(&native);
    rmr_buffer_free(&written);
    rmr_plan_free(plan);
  }
}

/* A template of lines, a repeated group of lines and a fill, with a label
 * after its first lines when labelled. */
static rmr_template_document* template_model(bool labelled) {
  char json[1024];
  snprintf(json, sizeof json,
           "{\"name\": \"Test\", \"items\": ["
           "{\"type\": \"path\", \"data\": [\"M\", 0, 120, \"L\", "
           "\"templateWidth\", 120]},"
           "%s"
           "{\"type\": \"group\", \"boundingBox\": {\"x\": 0, \"y\": 200, "
           "\"width\": \"templateWidth\", \"height\": 64}, \"repeat\": "
           "{\"rows\": \"down\"}, \"children\": [{\"type\": \"path\", "
           "\"strokeColor\": \"#8C8C8C\", \"strokeWidth\": 1.5, \"data\": "
           "[\"M\", 40, 0, \"L\", \"templateWidth - 40\", 0]}]},"
           "{\"type\": \"path\", \"fillColor\": \"#262626\", \"data\": [\"M\", "
           "100, 1700, \"L\", 300, 1700, \"L\", 300, 1780, \"Z\"]}]}",
           labelled ? "{\"type\": \"text\", \"text\": \"Label\", "
                      "\"fontSize\": 40, \"position\": {\"x\": 60, "
                      "\"y\": 100}},"
                    : "");
  rmr_template_document* document = nullptr;
  OK(rmr_template_document_decode(json, strlen(json), nullptr, &document,
                                  nullptr));
  return document;
}

/* The page's marks over the template: ink, and typed text's place taken
 * by a text run first when typed. */
static rmr_plan* templated(const rmr_template* model, bool typed) {
  static rmr_element ink[] = {{.kind = RMR_MOVE, .end = {-300, 500}},
                              {.kind = RMR_CURVE,
                               .control1 = {-100, 400},
                               .control2 = {100, 700},
                               .end = {300, 520}}};
  rmr_draw draws[2];
  size_t count = 0;
  if (typed)
    draws[count++] = (rmr_draw){.mode = RMR_TEXT,
                                .color = {0, 0, 0, 1},
                                .text = {"Typed", 5, 30, {-500, 300}, {0}}};
  draws[count++] = (rmr_draw){.path = {ink, 2},
                              .mode = RMR_STROKE,
                              .color = {0.1, 0.2, 0.7, 1},
                              .line_width = 4,
                              .cap = RMR_CAP_ROUND,
                              .join = RMR_JOIN_ROUND};
  rmr_plan *foreground = nullptr, *plan = nullptr;
  OK(rmr_plan_create(draws, count, (rmr_rect){-702, 0, 1404, 1872}, nullptr,
                     &foreground, nullptr));
  rmr_text_provider text = rmr_apple_text_provider();
  OK(rmr_template_background_plan_with_text(
      model, (rmr_point){1404, 1872}, (rmr_point){1404, 1872}, foreground, true,
      &text, nullptr, &plan, nullptr));
  rmr_plan_free(foreground);
  return plan;
}

/* The first content stream of page number of pdf begins with prefix. */
static bool leads_with(rmr_buffer pdf, size_t number, const char* prefix) {
  CFDataRef data =
      CFDataCreate(kCFAllocatorDefault, pdf.bytes, (CFIndex)pdf.size);
  CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
  CGPDFDocumentRef document = CGPDFDocumentCreateWithProvider(provider);
  CGDataProviderRelease(provider);
  CFRelease(data);
  CHECK(document);
  CGPDFDictionaryRef page =
      CGPDFPageGetDictionary(CGPDFDocumentGetPage(document, number));
  CGPDFArrayRef contents = nullptr;
  CGPDFStreamRef first = nullptr;
  bool leads = false;
  if (CGPDFDictionaryGetArray(page, "Contents", &contents) &&
      CGPDFArrayGetStream(contents, 0, &first)) {
    CGPDFDataFormat format;
    CFDataRef bytes = CGPDFStreamCopyData(first, &format);
    size_t n = strlen(prefix);
    leads = bytes && (size_t)CFDataGetLength(bytes) >= n &&
            !memcmp(CFDataGetBytePtr(bytes), prefix, n);
    if (bytes) CFRelease(bytes);
  }
  CGPDFDocumentRelease(document);
  return leads;
}

/* A page's template is a form written once for the pages that draw it the
 * same, under the page's ink or, labelled or under typed text, ahead of what
 * CoreGraphics draws; either way every page paints what CoreGraphics' own
 * page paints. */
static void templates(void) {
  for (int labelled = 0; labelled < 2; labelled++) {
    rmr_template_document* document = template_model(labelled);
    const rmr_template* model = rmr_template_document_model(document);
    enum { PAGES = 4 };
    rmr_plan* plans[PAGES];
    rmr_pdf_page pages[PAGES];
    rmr_sheet wide = {800,
                      1066.6666666666667,
                      {0.5698005698005698, 0, 0, -0.5698005698005698, 400,
                       1066.6666666666667}};
    for (size_t i = 0; i < PAGES; i++) {
      plans[i] = templated(model, i == 1);
      CHECK(rmr_plan_background(plans[i]) > 2);
      pages[i] = (rmr_pdf_page){.marks = plans[i],
                                .sheet = i == PAGES - 1 ? wide : tablet};
    }
    rmr_buffer native = {0}, written = {0};
    rmr_sheet sheets[PAGES];
    for (size_t i = 0; i < PAGES; i++) sheets[i] = pages[i].sheet;
    OK(rmr_apple_pdf_sheets((const rmr_plan* const*)plans, sheets, PAGES,
                            nullptr, nullptr, nullptr, &native, nullptr));
    OK(rmr_apple_pdf_overlay(nullptr, pages, PAGES, nullptr, nullptr, nullptr,
                             &written, nullptr, nullptr));
    /* One form for the three pages of one sheet, one for the other. */
    CHECK(occurrences(written, "/FormType 1 /BBox [-") == 2);
    /* Typed text, and a label, leave the template's lines ahead of it. */
    CHECK(leads_with(written, 2, "/RMRx"));
    CHECK(leads_with(written, 1, "/RMRx") == (bool)labelled);
    CHECK(!leads_with(written, 3, "/RMRx") || labelled);
    for (size_t i = 0; i < PAGES; i++)
      same_page(native, written, i + 1, labelled ? "labelled" : "template");
    rmr_buffer_free(&native);
    rmr_buffer_free(&written);
    for (size_t i = 0; i < PAGES; i++) rmr_plan_free(plans[i]);
    rmr_template_document_free(document);
  }
}

/* a and b are the same file but for the dates and ID CoreGraphics stamps
 * on it. */
static bool same_but_stamps(rmr_buffer a, rmr_buffer b) {
  size_t i = 0, j = 0;
  while (i < a.size && j < b.size) {
    bool date = i + 3 <= a.size && j + 3 <= b.size &&
                !memcmp(a.bytes + i, "(D:", 3) &&
                !memcmp(b.bytes + j, "(D:", 3);
    bool id = i + 5 <= a.size && j + 5 <= b.size &&
              !memcmp(a.bytes + i, "/ID [", 5) &&
              !memcmp(b.bytes + j, "/ID [", 5);
    if (date || id) {
      char close = date ? ')' : ']';
      while (i < a.size && a.bytes[i] != close) i++;
      while (j < b.size && b.bytes[j] != close) j++;
      continue;
    }
    if (a.bytes[i++] != b.bytes[j++]) return false;
  }
  return i == a.size && j == b.size;
}

/* A document of groups and shared templates comes out the same every time. */
static void deterministic(void) {
  rmr_template_document* document = template_model(true);
  const rmr_template* model = rmr_template_document_model(document);
  rmr_plan* plans[] = {grouped(0.5, RMR_BLEND_DARKEN, false),
                       templated(model, false), templated(model, true),
                       grouped(1, RMR_BLEND_MULTIPLY, false)};
  enum { PAGES = sizeof plans / sizeof *plans };
  rmr_pdf_page pages[PAGES];
  for (size_t i = 0; i < PAGES; i++)
    pages[i] = (rmr_pdf_page){.marks = plans[i], .sheet = tablet};
  rmr_buffer runs[2] = {0};
  for (size_t k = 0; k < 2; k++)
    OK(rmr_apple_pdf_overlay(nullptr, pages, PAGES, nullptr, nullptr, nullptr,
                             &runs[k], nullptr, nullptr));
  CHECK(same_but_stamps(runs[0], runs[1]));
  rmr_buffer_free(&runs[0]);
  rmr_buffer_free(&runs[1]);
  for (size_t i = 0; i < PAGES; i++) rmr_plan_free(plans[i]);
  rmr_template_document_free(document);
}

int main(void) {
  numbers();
  identity(false);
  identity(true);
  erased_away();
  groups();
  group_boxes();
  templates();
  deterministic();
  puts("pdf writer ok");
  return 0;
}
