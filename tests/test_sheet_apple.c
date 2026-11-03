/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple.h>

#include "test.h"

static bool cancelled(void* context) {
  (void)context;
  return true;
}

int main(void) {
  rmr_plan* p = rectangle((rmr_color){1, 0, 0, 1}, RMR_BLEND_NORMAL);
  rmr_sheet sheets[2];
  OK(rmr_sheet_create(rmr_plan_viewport(p), 100, 80, 1, &sheets[0], nullptr));
  OK(rmr_sheet_create(rmr_plan_viewport(p), 200, 160, 2, &sheets[1], nullptr));

  rmr_bitmap b = {0};
  OK(rmr_apple_bitmap_on_sheet(p, &sheets[0], (rmr_color){0}, nullptr, nullptr,
                               nullptr, &b, nullptr));
  CHECK(b.width == 100 && b.height == 80);
  CHECK(b.pixels[10 * b.stride + 35 * 4] == 255 &&
        b.pixels[10 * b.stride + 35 * 4 + 3] == 255);
  CHECK(b.pixels[40 * b.stride + 35 * 4 + 3] == 0);
  rmr_bitmap_free(&b);

  const rmr_plan* plans[] = {p, p};
  rmr_buffer pdf = {0};
  OK(rmr_apple_pdf_sheets(plans, sheets, 2, nullptr, nullptr, nullptr, &pdf,
                          nullptr));
  CGDataProviderRef provider =
      CGDataProviderCreateWithData(nullptr, pdf.bytes, pdf.size, nullptr);
  CHECK(provider);
  CGPDFDocumentRef doc = CGPDFDocumentCreateWithProvider(provider);
  CHECK(doc);
  CHECK(CGPDFDocumentGetNumberOfPages(doc) == 2);
  CGRect box =
      CGPDFPageGetBoxRect(CGPDFDocumentGetPage(doc, 1), kCGPDFMediaBox);
  CHECK(box.size.width == 100 && box.size.height == 80);
  box = CGPDFPageGetBoxRect(CGPDFDocumentGetPage(doc, 2), kCGPDFMediaBox);
  CHECK(box.size.width == 200 && box.size.height == 160);

  uint8_t pixels[100 * 80 * 4] = {0};
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CHECK(space);
  CGContextRef cg = CGBitmapContextCreate(
      pixels, 100, 80, 8, 400, space,
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CHECK(cg);
  CGContextDrawPDFPage(cg, CGPDFDocumentGetPage(doc, 1));
  CHECK(pixels[10 * 400 + 35 * 4] == 255 &&
        pixels[10 * 400 + 35 * 4 + 3] == 255);
  CHECK(pixels[40 * 400 + 35 * 4 + 3] == 0);
  CGContextRelease(cg);
  CGColorSpaceRelease(space);
  CGPDFDocumentRelease(doc);
  CGDataProviderRelease(provider);
  rmr_buffer_free(&pdf);

  sheets[1].transform.d = 1;
  CHECK(rmr_apple_pdf_sheets(plans, sheets, 2, nullptr, nullptr, nullptr, &pdf,
                             nullptr) == RMR_ERR_INVALID_ARGUMENT &&
        !pdf.bytes);

  CHECK(rmr_apple_bitmap_on_sheet(p, &sheets[0], (rmr_color){0}, nullptr,
                                  cancelled, nullptr, &b,
                                  nullptr) == RMR_ERR_CANCELLED &&
        !b.pixels);

  CHECK(rmr_apple_pdf_sheets(plans, sheets, 1, nullptr, cancelled, nullptr,
                             &pdf, nullptr) == RMR_ERR_CANCELLED &&
        !pdf.bytes);

  sheets[0].width = 100.5;
  CHECK(rmr_apple_bitmap_on_sheet(p, &sheets[0], (rmr_color){0}, nullptr,
                                  nullptr, nullptr, &b,
                                  nullptr) == RMR_ERR_INVALID_ARGUMENT &&
        !b.pixels);

  sheets[0].width = 100;
  rmr_limits limits = rmr_limits_default();
  limits.maximum_bitmap_side = 50;
  CHECK(rmr_apple_bitmap_on_sheet(p, &sheets[0], (rmr_color){0}, &limits,
                                  nullptr, nullptr, &b,
                                  nullptr) == RMR_ERR_LIMIT &&
        !b.pixels);
  rmr_plan_free(p);

  /* Explicit sheet output clips the physical sheet, not the measurement
   * viewport. */
  rmr_element elements[] = {{.kind = RMR_MOVE, .end = {-45, 5}},
                            {.kind = RMR_LINE, .end = {-35, 5}},
                            {.kind = RMR_LINE, .end = {-35, 25}},
                            {.kind = RMR_LINE, .end = {-45, 25}},
                            {.kind = RMR_CLOSE}};
  rmr_draw d = {.path = {elements, 5}, .color = {0, 0, 0, 1}};
  OK(rmr_plan_create(&d, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &p, nullptr));

  OK(rmr_apple_bitmap_on_sheet(p, &sheets[0], (rmr_color){0}, nullptr, nullptr,
                               nullptr, &b, nullptr));
  CHECK(b.pixels[10 * b.stride + 10 * 4 + 3] == 255);
  rmr_bitmap_free(&b);

  OK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b,
                      nullptr));
  for (size_t i = 3; i < b.stride * b.height; i += 4) CHECK(b.pixels[i] == 0);
  rmr_bitmap_free(&b);

  rmr_plan_free(p);
  puts(
      "sheet bitmap/PDF placement, physical clipping, validation and "
      "cancellation passed");
}
