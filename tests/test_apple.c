/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple.h>

static bool cancelled(void *context) {
    (void)context;
    return true;
}

static void masked_output(void) {
    rmr_element shape[] = {{.kind = RMR_MOVE, .end = {0, 0}},
                           {.kind = RMR_LINE, .end = {3, 0}},
                           {.kind = RMR_LINE, .end = {3, 8}},
                           {.kind = RMR_LINE, .end = {0, 8}},
                           {.kind = RMR_CLOSE}};
    uint8_t bits[] = {0x0f, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    rmr_draw d = {
        .path = {shape, 5}, .color = {1, 0, 0, 0.5}, .mask = {bits, 8, 8, 1, {0, 0, 8, 8}}};
    rmr_plan *plan = nullptr;
    OK(rmr_plan_create(&d, 1, (rmr_rect){0, 0, 8, 8}, nullptr, &plan, nullptr));
    /* Caller buffers can disappear immediately after creation. */
    memset(bits, 0xff, sizeof bits);
    rmr_bitmap b = {0};
    OK(rmr_apple_bitmap(plan, 4, (rmr_color){0}, nullptr, nullptr, nullptr, &b, nullptr));
    size_t ink = 4 * b.stride + 4 * 4, clip = 4 * b.stride + 14 * 4, bottom = 24 * b.stride + 4 * 4;
    CHECK(b.pixels[ink + 3] >= 127 && b.pixels[ink + 3] <= 128);
    CHECK(b.pixels[clip + 3] == 0 && b.pixels[bottom + 3] == 0);
    rmr_bitmap_free(&b);
    rmr_buffer pdf = {0};
    const rmr_plan *pages[] = {plan};
    OK(rmr_apple_pdf(pages, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(nullptr, pdf.bytes, pdf.size, nullptr);
    CHECK(provider);
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithProvider(provider);
    CHECK(doc);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CHECK(space);
    uint8_t pixels[32 * 32 * 4] = {0};
    CGContextRef cg = CGBitmapContextCreate(
        pixels, 32, 32, 8, 128, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CHECK(cg);
    CGContextScaleCTM(cg, 4, 4);
    CGContextDrawPDFPage(cg, CGPDFDocumentGetPage(doc, 1));
    CHECK(pixels[ink + 3] >= 127 && pixels[ink + 3] <= 128);
    CHECK(pixels[clip + 3] == 0 && pixels[bottom + 3] == 0);
    CGContextRelease(cg);
    CGColorSpaceRelease(space);
    CGPDFDocumentRelease(doc);
    CGDataProviderRelease(provider);
    rmr_buffer_free(&pdf);
    rmr_plan_free(plan);
}

static void composition_output(void) {
    rmr_plan *base = rectangle((rmr_color){0, 0, 0, 1}, RMR_BLEND_NORMAL);
    rmr_element hole[] = {{.kind = RMR_MOVE, .end = {-12, 0}},
                          {.kind = RMR_LINE, .end = {-8, 0}},
                          {.kind = RMR_LINE, .end = {-8, 64}},
                          {.kind = RMR_LINE, .end = {-12, 64}},
                          {.kind = RMR_CLOSE}};
    rmr_path_view eraser = {hole, 5};
    rmr_draw draws[2] = {*rmr_plan_draw(base, 0), *rmr_plan_draw(base, 0)};
    for (size_t i = 0; i < 2; i++) {
        draws[i].erasers = &eraser;
        draws[i].eraser_count = 1;
        draws[i].group = 1;
        draws[i].group_alpha = .5;
        draws[i].group_blend = RMR_BLEND_NORMAL;
    }
    rmr_plan *plan = nullptr;
    OK(rmr_plan_create(draws, 2, rmr_plan_viewport(base), nullptr, &plan, nullptr));
    rmr_plan_free(base);
    rmr_bitmap bitmap = {0};
    OK(rmr_apple_bitmap(plan, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, nullptr));
    CHECK(bitmap.pixels[10 * bitmap.stride + 22 * 4 + 3] == 0);
    CHECK(bitmap.pixels[10 * bitmap.stride + 15 * 4 + 3] >= 127 &&
          bitmap.pixels[10 * bitmap.stride + 15 * 4 + 3] <= 128);
    rmr_bitmap_free(&bitmap);
    rmr_plan_free(plan);
}

int main(void) {
    composition_output();
    masked_output();
    rmr_plan *plan = rectangle((rmr_color){1, 0, 0, 0.5}, RMR_BLEND_NORMAL);
    rmr_bitmap b = {0};
    OK(rmr_apple_bitmap(plan, 1, (rmr_color){0, 0, 0, 0}, nullptr, nullptr, nullptr, &b, nullptr));
    CHECK(b.width == 64 && b.height == 64 && b.stride == 256);
    size_t at = 10 * b.stride + 15 * 4;
    if (!(b.pixels[at] >= 127 && b.pixels[at] <= 128 && b.pixels[at + 3] >= 127 &&
          b.pixels[at + 3] <= 128))
        fprintf(stderr, "top pixel RGBA: %u %u %u %u\n", b.pixels[at], b.pixels[at + 1],
                b.pixels[at + 2], b.pixels[at + 3]);
    CHECK(b.pixels[at] >= 127 && b.pixels[at] <= 128 && b.pixels[at + 3] >= 127 &&
          b.pixels[at + 3] <= 128);
    CHECK(b.pixels[0] == 0 && b.pixels[50 * b.stride + 15 * 4 + 3] == 0);
    rmr_bitmap_free(&b);
    rmr_limits l = rmr_limits_default();
    l.maximum_bitmap_bytes = 100;
    CHECK(rmr_apple_bitmap(plan, 1, (rmr_color){0}, &l, nullptr, nullptr, &b, nullptr) ==
              RMR_ERR_LIMIT &&
          !b.pixels);
    CHECK(rmr_apple_bitmap(plan, 1, (rmr_color){0}, nullptr, cancelled, nullptr, &b, nullptr) ==
          RMR_ERR_CANCELLED);
    const rmr_plan *pages[] = {plan, plan};
    rmr_buffer pdf = {0};
    OK(rmr_apple_pdf(pages, 2, RMR_POINTS_PER_UNIT, nullptr, nullptr, nullptr, &pdf, nullptr));
    CHECK(pdf.size > 100 && memcmp(pdf.bytes, "%PDF-", 5) == 0);
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(nullptr, pdf.bytes, pdf.size, nullptr);
    CHECK(provider);
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithProvider(provider);
    CHECK(doc);
    CHECK(CGPDFDocumentGetNumberOfPages(doc) == 2);
    CGRect box = CGPDFPageGetBoxRect(CGPDFDocumentGetPage(doc, 1), kCGPDFMediaBox);
    CHECK(fabs(box.size.width - 64 * RMR_POINTS_PER_UNIT) < 0.001);
    CGPDFDocumentRelease(doc);
    CGDataProviderRelease(provider);
    rmr_buffer_free(&pdf);
    l = rmr_limits_default();
    l.maximum_pdf_bytes = 32;
    CHECK(rmr_apple_pdf(pages, 2, 1, &l, nullptr, nullptr, &pdf, nullptr) == RMR_ERR_LIMIT &&
          !pdf.bytes);
    CHECK(rmr_apple_pdf(pages, 2, 1, nullptr, cancelled, nullptr, &pdf, nullptr) ==
              RMR_ERR_CANCELLED &&
          !pdf.bytes);
    rmr_plan_free(plan);
    puts("apple bitmap/PDF passed");
    return 0;
}
