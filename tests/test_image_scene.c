/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "image_fixture.h"
#include "test.h"
#include <rmrender/rmlines.h>
#ifdef RMRENDER_TEST_APPLE
#include "image_jpeg_fixture.h"
#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple.h>
#include <rmrender/apple_context.h>

static rmr_bitmap render(rmr_plan *p) {
    rmr_bitmap b = {0};
    OK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b, nullptr));
    return b;
}

static void pixel(rmr_bitmap b, size_t x, size_t y, unsigned r, unsigned g, unsigned blue,
                  unsigned a) {
    const uint8_t *p = b.pixels + y * b.stride + x * 4;
    CHECK(abs((int)p[0] - (int)r) <= 1 && abs((int)p[1] - (int)g) <= 1 &&
          abs((int)p[2] - (int)blue) <= 1 && abs((int)p[3] - (int)a) <= 1);
}

static void pdf_check(rmr_plan *p, bool release_before_close) {
    const rmr_plan *pages[] = {p};
    rmr_buffer pdf = {0};
    if (release_before_close) {
        rmr_plan *copy = nullptr;
        OK(rmr_plan_concat(pages, 1, rmr_plan_viewport(p), nullptr, &copy, nullptr));
        CFMutableDataRef data = CFDataCreateMutable(kCFAllocatorDefault, 0);
        CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData(data);
        CGRect box = CGRectMake(0, 0, 64, 64);
        CGContextRef output = CGPDFContextCreate(consumer, &box, nullptr);
        CHECK(output);
        CGDataConsumerRelease(consumer);
        CGPDFContextBeginPage(output, nullptr);
        rmr_sheet sheet = {64, 64, {1, 0, 0, -1, 32, 64}};
        OK(rmr_apple_draw_context(output, copy, &sheet, nullptr, nullptr, nullptr, nullptr));
        rmr_plan_free(copy); /* Deferred native decoding must not borrow the plan. */
        CGPDFContextEndPage(output);
        CGPDFContextClose(output);
        CGContextRelease(output);
        pdf.size = (size_t)CFDataGetLength(data);
        pdf.bytes = malloc(pdf.size);
        CHECK(pdf.bytes);
        memcpy(pdf.bytes, CFDataGetBytePtr(data), pdf.size);
        CFRelease(data);
    } else
        OK(rmr_apple_pdf(pages, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(nullptr, pdf.bytes, pdf.size, nullptr);
    CHECK(provider);
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithProvider(provider);
    CHECK(doc);
    uint8_t pixels[64 * 64 * 4] = {0};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CHECK(space);
    CGContextRef cg = CGBitmapContextCreate(
        pixels, 64, 64, 8, 256, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CHECK(cg);
    CGContextDrawPDFPage(cg, CGPDFDocumentGetPage(doc, 1));
    rmr_bitmap bitmap = {pixels, 64, 64, 256};
    pixel(bitmap, 16, 16, 255, 0, 0, 255);
    pixel(bitmap, 48, 16, 0, 255, 0, 255);
    pixel(bitmap, 16, 48, 0, 0, 255, 255);
    pixel(bitmap, 48, 48, 128, 128, 0, 128);
    CGContextRelease(cg);
    CGColorSpaceRelease(space);
    CGPDFDocumentRelease(doc);
    CGDataProviderRelease(provider);
    rmr_buffer_free(&pdf);
}
#endif
static rmr_status resolve(void *context, rm_str file, rmr_asset_view *out) {
    (void)file;
    *out = context ? *(rmr_asset_view *)context : (rmr_asset_view){image_png, sizeof image_png};
    return RMR_OK;
}

static rmr_status refused(void *c, rm_str f, rmr_asset_view *out) {
    (void)c;
    (void)f;
    (void)out;
    return RMR_ERR_CANCELLED;
}

static rmr_plan *plan(rm_page *page, void *context) {
    rmr_plan *p = nullptr;
    OK(rmr_lines_scene_plan(page, (rmr_rect){-32, 0, 64, 64}, resolve, context, nullptr, nullptr,
                            &p, nullptr));
    return p;
}

int main(void) {
    rm_page_image image = {
        .vertices = {{-32, 0, 0, 0}, {32, 0, 1, 0}, {32, 64, 1, 1}, {-32, 64, 0, 1}}};
    rm_page page = {.images = &image, .image_count = 1};
    rmr_plan *p = plan(&page, nullptr);
    CHECK(rmr_plan_count(p) == 1 && rmr_plan_draw(p, 0)->mode == RMR_IMAGE);
#ifdef RMRENDER_TEST_APPLE
    rmr_bitmap b = render(p);
    pixel(b, 16, 16, 255, 0, 0, 255);
    pixel(b, 48, 16, 0, 255, 0, 255);
    pixel(b, 16, 48, 0, 0, 255, 255);
    pixel(b, 48, 48, 128, 128, 0, 128);
    rmr_bitmap_free(&b);
    pdf_check(p, false);
    pdf_check(p, true);
    rmr_limits limit = rmr_limits_default();
    limit.maximum_bitmap_side = 4;
    rmr_bitmap untouched = {0};
    /* Small output passes its own limit, decoded 8x8 attachment does not. */
    CHECK(rmr_apple_bitmap(p, .0625, (rmr_color){0}, &limit, nullptr, nullptr, &untouched,
                           nullptr) == RMR_ERR_LIMIT &&
          !untouched.pixels);
    const rmr_plan *pages[] = {p};
    rmr_buffer pdf = {0};
    CHECK(rmr_apple_pdf(pages, 1, 1, &limit, nullptr, nullptr, &pdf, nullptr) == RMR_ERR_LIMIT &&
          !pdf.bytes);
#endif
    rmr_plan_free(p);
#ifdef RMRENDER_TEST_APPLE
    rmr_asset_view jpeg = {image_jpeg, sizeof image_jpeg};
    p = plan(&page, &jpeg);
    b = render(p);
    CHECK(b.pixels[16 * b.stride + 16 * 4] >= 250 && b.pixels[16 * b.stride + 16 * 4 + 1] <= 5);
    rmr_bitmap_free(&b);
    rmr_plan_free(p);
#endif
    /* Crop left half: full texture mapping remains unchanged, quad clips. */
    image.vertices[1].x = image.vertices[2].x = 0;
    image.vertices[1].u = image.vertices[2].u = .5;
    p = plan(&page, nullptr);
#ifdef RMRENDER_TEST_APPLE
    b = render(p);
    pixel(b, 16, 16, 255, 0, 0, 255);
    pixel(b, 48, 16, 0, 0, 0, 0);
    rmr_bitmap_free(&b);
#endif
    rmr_plan_free(p);
    image.vertices[1].x = image.vertices[2].x = 32;
    image.vertices[1].u = image.vertices[2].u = 1;
    rm_stroke_point points[] = {{.x = -30, .y = 16, .width = 40}, {.x = 30, .y = 16, .width = 40}};
    rm_stroke ink = {.tool = RM_TOOL_FINELINER_2, .points = points, .point_count = 2};
    rm_stroke_point cuts[] = {{.x = 0, .y = 0, .width = 32}, {.x = 0, .y = 64, .width = 32}};
    rm_stroke strokes[] = {ink, {.tool = RM_TOOL_ERASER, .points = cuts, .point_count = 2}, ink};
    page.strokes = strokes;
    page.stroke_count = 2;
    image.strokes_before = 1;
    p = plan(&page, nullptr);
    CHECK(rmr_plan_draw(p, 0)->eraser_count == 1 && rmr_plan_draw(p, 1)->mode == RMR_IMAGE &&
          !rmr_plan_draw(p, 1)->eraser_count);
#ifdef RMRENDER_TEST_APPLE
    b = render(p);
    pixel(b, 16, 16, 255, 0, 0, 255);
    pixel(b, 48, 16, 0, 255, 0, 255);
    rmr_bitmap_free(&b);
#endif
    rmr_plan_free(p);
    page.stroke_count = 3;
    p = plan(&page, nullptr);
#ifdef RMRENDER_TEST_APPLE
    b = render(p);
    pixel(b, 16, 16, 0, 0, 0, 255);
    rmr_bitmap_free(&b);
#endif
    rmr_plan_free(p);
    rm_stroke shader = ink;
    shader.tool = RM_TOOL_SHADER;
    shader.has_source_id = true;
    shader.source_id = (rm_item_id){.author = 1, .counter = 42};
    strokes[0] = strokes[1] = strokes[2] = shader;
    strokes[1].has_argb = true;
    strokes[1].argb = 0x800000ff;
    rmr_asset_view missing = {0};
    p = plan(&page, &missing);
    CHECK(rmr_plan_draw(p, 1)->mode == RMR_FILL && !rmr_plan_draw(p, 1)->path.count);
    CHECK(rmr_plan_draw(p, 0)->group_alpha == .25098);
    CHECK(fabs(rmr_plan_draw(p, 2)->group_alpha - 128.0 / 255) < 1e-12);
    CHECK(rmr_plan_draw(p, 3)->group_alpha == rmr_plan_draw(p, 2)->group_alpha);
#ifdef RMRENDER_TEST_APPLE
    b = render(p);
    CHECK(b.pixels[16 * b.stride + 16 * 4 + 3] >= 159 &&
          b.pixels[16 * b.stride + 16 * 4 + 3] <= 161);
    rmr_bitmap_free(&b);
#endif
    rmr_plan_free(p);
    p = nullptr;
    CHECK(rmr_lines_scene_plan(&page, (rmr_rect){-32, 0, 64, 64}, refused, nullptr, nullptr,
                               nullptr, &p, nullptr) == RMR_ERR_CANCELLED &&
          !p);
    image.has_anchor = true;
    CHECK(rmr_lines_scene_plan(&page, (rmr_rect){-32, 0, 64, 64}, resolve, nullptr, nullptr,
                               nullptr, &p, nullptr) == RMR_ERR_UNSUPPORTED);
    image.has_anchor = false;
    page.has_text = true;
    CHECK(rmr_lines_scene_plan(&page, (rmr_rect){-32, 0, 64, 64}, resolve, nullptr, nullptr,
                               nullptr, &p, nullptr) == RMR_ERR_UNSUPPORTED);
    page.has_text = false;
    rm_page_image unordered[] = {image, image};
    unordered[0].strokes_before = 2;
    page.images = unordered;
    page.image_count = 2;
    CHECK(rmr_lines_scene_plan(&page, (rmr_rect){-32, 0, 64, 64}, resolve, nullptr, nullptr,
                               nullptr, &p, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    page.images = &image;
    page.image_count = 1;
    image.strokes_before = SIZE_MAX;
    p = plan(&page, nullptr);
    CHECK(rmr_plan_draw(p, 3)->mode == RMR_IMAGE);
    rmr_plan_free(p);
    for (size_t k = 0; k < 4; k++)
        image.vertices[k].u = 0;
    p = plan(&page, nullptr);
    CHECK(rmr_plan_draw(p, 3)->mode == RMR_FILL);
    rmr_plan_free(p);
#ifdef RMRENDER_TEST_APPLE
    image.vertices[1].u = image.vertices[2].u = 1;
    rmr_asset_view bad = {(const uint8_t *)"bad", 3};
    p = plan(&page, &bad);
    CHECK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &untouched, nullptr) ==
              RMR_ERR_BACKEND &&
          !untouched.pixels);
    rmr_plan_free(p);
#endif
    page.stroke_count = 0;
    page.images = unordered;
    page.image_count = 2;
    unordered[0].strokes_before = unordered[1].strokes_before = 0;
    unordered[1].vertices[0].x += 1;
    p = plan(&page, nullptr);
    CHECK(rmr_plan_count(p) == 2);
    CHECK(rmr_plan_draw(p, 0)->path.elements[0].end.x + 1 ==
          rmr_plan_draw(p, 1)->path.elements[0].end.x);
    rmr_limits budget = rmr_limits_default();
    budget.maximum_plan_bytes = rmr_plan_bytes(p);
    rmr_plan *unchanged = p;
    CHECK(rmr_lines_scene_plan(&page, (rmr_rect){-32, 0, 64, 64}, resolve, nullptr, nullptr,
                               &budget, &p, nullptr) == RMR_ERR_LIMIT &&
          p == unchanged);
    rmr_plan_free(p);
    puts("image ordering, placement, barriers and failures passed");
}
