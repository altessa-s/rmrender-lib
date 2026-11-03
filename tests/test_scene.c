/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <rmrender/rmlines.h>
#ifdef RMRENDER_TEST_APPLE
#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple.h>

static rmr_bitmap render(rmr_plan *p, rmr_color background) {
    rmr_bitmap b = {0};
    OK(rmr_apple_bitmap(p, 1, background, nullptr, nullptr, nullptr, &b, nullptr));
    return b;
}

static unsigned int alpha(rmr_bitmap b, size_t x, size_t y) {
    return b.pixels[y * b.stride + x * 4 + 3];
}

static unsigned int pdf_alpha(rmr_plan *p) {
    const rmr_plan *pages[] = {p};
    rmr_buffer pdf = {0};
    OK(rmr_apple_pdf(pages, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(nullptr, pdf.bytes, pdf.size, nullptr);
    CHECK(provider);
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithProvider(provider);
    CHECK(doc);
    uint8_t bytes[64 * 64 * 4] = {0};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CHECK(space);
    CGContextRef cg = CGBitmapContextCreate(
        bytes, 64, 64, 8, 256, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CHECK(cg);
    CGContextDrawPDFPage(cg, CGPDFDocumentGetPage(doc, 1));
    unsigned int value = bytes[20 * 256 + 32 * 4 + 3];
    CGContextRelease(cg);
    CGColorSpaceRelease(space);
    CGPDFDocumentRelease(doc);
    CGDataProviderRelease(provider);
    rmr_buffer_free(&pdf);
    return value;
}

static bool cancel_group(void *context) { return ++*(unsigned int *)context == 3; }
#endif
static rmr_plan *plan_for(rm_stroke *strokes, size_t count) {
    rmr_plan *p = nullptr;
    OK(rmr_lines_plan(strokes, count, (rmr_rect){-32, 0, 64, 64}, nullptr, &p, nullptr));
    return p;
}

int main(void) {
    rm_stroke_point horizontal[] = {{.x = -20, .y = 20, .width = 40, .pressure = 128},
                                    {.x = 20, .y = 20, .width = 40, .pressure = 128}};
    rm_stroke_point vertical[] = {{.x = 0, .y = 0, .width = 32}, {.x = 0, .y = 40, .width = 32}};
    rm_stroke ink = {.tool = RM_TOOL_FINELINER_2,
                     .color = RM_COLOR_BLACK,
                     .points = horizontal,
                     .point_count = 2};
    rm_stroke eraser = {
        .tool = RM_TOOL_ERASER, .color = RM_COLOR_BLACK, .points = vertical, .point_count = 2};
    rm_stroke strokes[] = {ink, eraser, ink};
    rmr_plan *p = plan_for(strokes, 3);
    CHECK(rmr_plan_draw(p, 0)->eraser_count == 1 && !rmr_plan_draw(p, 1)->path.count &&
          !rmr_plan_draw(p, 2)->eraser_count);
    rmr_plan_free(p);
    p = plan_for(strokes, 2);
#ifdef RMRENDER_TEST_APPLE
    rmr_bitmap b = render(p, (rmr_color){.red = .2, .green = .4, .blue = .6, .alpha = 1});
    CHECK(b.pixels[20 * b.stride + 32 * 4] >= 50 && b.pixels[20 * b.stride + 32 * 4] <= 52);
    CHECK(b.pixels[20 * b.stride + 20 * 4] == 0);
    rmr_bitmap_free(&b);
    CHECK(pdf_alpha(p) == 0);
#endif
    rmr_plan_free(p);
    p = plan_for(strokes, 3);
#ifdef RMRENDER_TEST_APPLE
    b = render(p, (rmr_color){0});
    CHECK(alpha(b, 32, 20) == 255);
    rmr_bitmap_free(&b);
    CHECK(pdf_alpha(p) == 255);
#endif
    rmr_plan_free(p);
    vertical[0].width = vertical[1].width = 400;
    p = plan_for(strokes, 2);
#ifdef RMRENDER_TEST_APPLE
    b = render(p, (rmr_color){0});
    CHECK(alpha(b, 32, 20) == 0 && alpha(b, 20, 20) == 0);
    rmr_bitmap_free(&b);
    CHECK(pdf_alpha(p) == 0);
#endif
    rmr_plan_free(p);
    vertical[0].width = vertical[1].width = 32;
    rm_stroke doubled[] = {ink, eraser, eraser};
    p = plan_for(doubled, 3);
    CHECK(rmr_plan_draw(p, 0)->eraser_count == 2);
#ifdef RMRENDER_TEST_APPLE
    b = render(p, (rmr_color){0});
    CHECK(alpha(b, 32, 20) == 0 && alpha(b, 20, 20) == 255);
    rmr_bitmap_free(&b);
#endif
    rmr_plan_free(p);
    strokes[1].tool = RM_TOOL_ERASER_AREA;
    p = plan_for(strokes, 2);
    CHECK(!rmr_plan_draw(p, 0)->eraser_count && !rmr_plan_draw(p, 1)->path.count);
    rmr_plan_free(p);
    strokes[1] = eraser;
    vertical[0].x = vertical[1].x = 1000;
    p = plan_for(strokes, 2);
    CHECK(!rmr_plan_draw(p, 0)->eraser_count && rmr_plan_draw(p, 0)->mode == RMR_STROKE);
    rmr_plan_free(p);
    vertical[0].x = vertical[1].x = 0;
    rm_stroke shader = ink;
    shader.tool = RM_TOOL_SHADER;
    shader.has_source_id = true;
    shader.source_id = (rm_item_id){.author = 1, .counter = 42};
    rm_stroke fragments[] = {shader, shader};
    p = plan_for(fragments, 2);
    CHECK(rmr_plan_draw(p, 0)->group && rmr_plan_draw(p, 0)->group == rmr_plan_draw(p, 1)->group);
    CHECK(rmr_plan_draw(p, 0)->color.alpha == 1 && rmr_plan_draw(p, 0)->group_alpha == .25098);
#ifdef RMRENDER_TEST_APPLE
    b = render(p, (rmr_color){0});
    CHECK(alpha(b, 32, 20) >= 63 && alpha(b, 32, 20) <= 65);
    rmr_bitmap_free(&b);
    CHECK(pdf_alpha(p) >= 63 && pdf_alpha(p) <= 65);
    unsigned int calls = 0;
    rmr_bitmap cancelled = {0};
    CHECK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, cancel_group, &calls, &cancelled,
                           nullptr) == RMR_ERR_CANCELLED &&
          !cancelled.pixels);
#endif
    rmr_plan_free(p);
    fragments[1].source_id.counter = 43;
    p = plan_for(fragments, 2);
    CHECK(rmr_plan_draw(p, 0)->group != rmr_plan_draw(p, 1)->group);
#ifdef RMRENDER_TEST_APPLE
    b = render(p, (rmr_color){0});
    CHECK(alpha(b, 32, 20) >= 111 && alpha(b, 32, 20) <= 113);
    rmr_bitmap_free(&b);
    CHECK(pdf_alpha(p) >= 111 && pdf_alpha(p) <= 113);
#endif
    rmr_plan_free(p);
    rm_stroke interrupted[] = {shader, {.tool = RM_TOOL_ERASER_AREA}, shader};
    p = plan_for(interrupted, 3);
    CHECK(rmr_plan_draw(p, 0)->group != rmr_plan_draw(p, 2)->group && !rmr_plan_draw(p, 1)->group);
#ifdef RMRENDER_TEST_APPLE
    b = render(p, (rmr_color){0});
    CHECK(alpha(b, 32, 20) >= 111 && alpha(b, 32, 20) <= 113);
    rmr_bitmap_free(&b);
#endif
    rmr_plan_free(p);
    fragments[1] = shader;
    fragments[1].has_argb = true;
    fragments[1].argb = 0x800000ff;
    p = plan_for(fragments, 2);
    CHECK(rmr_plan_draw(p, 1)->group_alpha == .25098 && rmr_plan_draw(p, 1)->color.blue == 1);
    rmr_plan_free(p);
    rm_stroke crowded[20];
    rm_stroke_point far[] = {{.x = 1000, .y = 0, .width = 4}, {.x = 1000, .y = 40, .width = 4}};
    for (size_t i = 0; i < 10; i++)
        crowded[i] = ink;
    for (size_t i = 10; i < 20; i++) {
        crowded[i] = eraser;
        crowded[i].points = far;
    }
    rmr_limits limited = rmr_limits_default();
    limited.maximum_elements = 70;
    p = nullptr;
    CHECK(rmr_lines_plan(crowded, 20, (rmr_rect){-32, 0, 64, 64}, &limited, &p, nullptr) ==
              RMR_ERR_LIMIT &&
          !p);
    puts("stroke order, erasers and fragment groups passed");
    return 0;
}
