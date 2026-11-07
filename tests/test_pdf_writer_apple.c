/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* The Apple PDF writer against CoreGraphics itself: numbers printed as
 * CoreGraphics prints them, and pages it writes painting exactly what
 * CoreGraphics' own pages paint. */

#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple.h>
#include <rmrender/apple_pdf.h>

#include "../backends/common/rmr_pdf_stream.h"
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

/* Page 1 of pdf rasterized at scale onto white. */
static uint8_t* raster(rmr_buffer pdf, double scale, size_t* width,
                       size_t* height) {
  CFDataRef data =
      CFDataCreate(kCFAllocatorDefault, pdf.bytes, (CFIndex)pdf.size);
  CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
  CGPDFDocumentRef document = CGPDFDocumentCreateWithProvider(provider);
  CGDataProviderRelease(provider);
  CFRelease(data);
  CHECK(document);
  CGPDFPageRef page = CGPDFDocumentGetPage(document, 1);
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

int main(void) {
  numbers();
  identity(false);
  identity(true);
  erased_away();
  puts("pdf writer ok");
  return 0;
}
