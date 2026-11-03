/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple.h>
#include <rmrender/template.h>

int main(void) {
    rmr_element ink[] = {{.kind = RMR_MOVE, .end = {-20, 20}},
                         {.kind = RMR_LINE, .end = {20, 20}},
                         {.kind = RMR_LINE, .end = {20, 40}},
                         {.kind = RMR_LINE, .end = {-20, 40}},
                         {.kind = RMR_CLOSE}};
    rmr_element cut[] = {{.kind = RMR_MOVE, .end = {-2, 0}},
                         {.kind = RMR_LINE, .end = {2, 0}},
                         {.kind = RMR_LINE, .end = {2, 64}},
                         {.kind = RMR_LINE, .end = {-2, 64}},
                         {.kind = RMR_CLOSE}};
    rmr_path_view eraser = {cut, 5};
    rmr_draw d = {.path = {ink, 5}, .color = {0, 0, 0, 1}, .erasers = &eraser, .eraser_count = 1};
    rmr_plan *foreground = nullptr;
    OK(rmr_plan_create(&d, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &foreground, nullptr));
    rmr_template_element box[] = {{.kind = RMR_MOVE},
                                  {.kind = RMR_LINE, .x = {.number = 64}},
                                  {.kind = RMR_LINE, .x = {.number = 64}, .y = {.number = 64}},
                                  {.kind = RMR_LINE, .y = {.number = 64}},
                                  {.kind = RMR_CLOSE}};
    rmr_template_item item = {
        .kind = RMR_TEMPLATE_PATH,
        .path = {.elements = box, .count = 5, .has_fill = true, .fill = {0, 1, 0, 1}}};
    rmr_template t = {.items = &item, .count = 1};
    rmr_plan *p = nullptr;
    OK(rmr_template_background_plan(&t, (rmr_point){64, 64}, (rmr_point){64, 64}, foreground, true,
                                    nullptr, &p, nullptr));
    rmr_bitmap b = {0};
    OK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b, nullptr));
    CHECK(b.pixels[30 * b.stride + 32 * 4 + 1] == 255 &&
          b.pixels[30 * b.stride + 32 * 4 + 3] == 255);
    CHECK(b.pixels[30 * b.stride + 20 * 4 + 1] == 0 && b.pixels[30 * b.stride + 20 * 4 + 3] == 255);
    rmr_bitmap_free(&b);
    const rmr_plan *pages[] = {p};
    rmr_buffer pdf = {0};
    OK(rmr_apple_pdf(pages, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    CHECK(pdf.size > 100);
    rmr_buffer_free(&pdf);
    rmr_plan_free(p);
    OK(rmr_template_background_plan(nullptr, (rmr_point){0}, (rmr_point){0}, foreground, false,
                                    nullptr, &p, nullptr));
    OK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b, nullptr));
    CHECK(b.pixels[30 * b.stride + 32 * 4 + 3] == 0);
    rmr_bitmap_free(&b);
    rmr_plan_free(p);
    rmr_plan_free(foreground);
    /* Native combined fill/stroke, different colors and alpha, miter corners. */
    box[0].x.number = box[3].x.number = 10;
    box[1].x.number = box[2].x.number = 54;
    box[0].y.number = box[1].y.number = 10;
    box[2].y.number = box[3].y.number = 54;
    item.path.fill = (rmr_color){1, 0, 0, .5};
    item.path.has_stroke_width = true;
    item.path.stroke_width.number = 8;
    item.path.has_stroke_color = true;
    item.path.stroke = (rmr_color){0, 0, 1, .25};
    OK(rmr_template_plan(&t, (rmr_point){64, 64}, (rmr_point){64, 64}, (rmr_rect){-32, 0, 64, 64},
                         nullptr, &p, nullptr));
    OK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b, nullptr));
    uint8_t expected[64 * 64 * 4] = {0};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CHECK(space);
    CGContextRef cg = CGBitmapContextCreate(
        expected, 64, 64, 8, 256, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CHECK(cg);
    CGContextTranslateCTM(cg, 0, 64);
    CGContextScaleCTM(cg, 1, -1);
    CGContextSetRGBFillColor(cg, 1, 0, 0, .5);
    CGContextSetRGBStrokeColor(cg, 0, 0, 1, .25);
    CGContextSetLineWidth(cg, 8);
    CGContextSetLineCap(cg, kCGLineCapSquare);
    CGContextAddRect(cg, CGRectMake(10, 10, 44, 44));
    CGContextDrawPath(cg, kCGPathFillStroke);
    CHECK(memcmp(expected, b.pixels, sizeof expected) == 0);
    CGContextRelease(cg);
    pages[0] = p;
    OK(rmr_apple_pdf(pages, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(nullptr, pdf.bytes, pdf.size, nullptr);
    CHECK(provider);
    CGPDFDocumentRef document = CGPDFDocumentCreateWithProvider(provider);
    CHECK(document);
    memset(expected, 0, sizeof expected);
    cg = CGBitmapContextCreate(expected, 64, 64, 8, 256, space,
                               kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CHECK(cg);
    CGContextDrawPDFPage(cg, CGPDFDocumentGetPage(document, 1));
    CHECK(expected[32 * 256 + 32 * 4] >= 127 && expected[32 * 256 + 32 * 4] <= 129);
    CHECK(expected[32 * 256 + 8 * 4 + 2] >= 63 && expected[32 * 256 + 8 * 4 + 2] <= 65);
    CGContextRelease(cg);
    CGPDFDocumentRelease(document);
    CGDataProviderRelease(provider);
    rmr_buffer_free(&pdf);
    CGColorSpaceRelease(space);
    rmr_bitmap_free(&b);
    rmr_plan_free(p);
    puts("template background visibility/erasers and native combined paint passed");
}
