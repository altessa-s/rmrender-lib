/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "image_fixture.h"
#include "pdf_fixture.h"
#include <CoreGraphics/CoreGraphics.h>
#include <assert.h>
#include <math.h>
#include <rmrender/apple_pdf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CGPDFDocumentRef document(const uint8_t *bytes, size_t n) {
    CFDataRef data = CFDataCreate(nullptr, bytes, (CFIndex)n);
    assert(data);
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    assert(provider);
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithProvider(provider);
    CGDataProviderRelease(provider);
    CFRelease(data);
    assert(doc);
    return doc;
}

static void preserved(CGPDFDocumentRef doc) {
    assert(CGPDFDocumentGetNumberOfPages(doc) == 2);
    CGPDFDictionaryRef cat = CGPDFDocumentGetCatalog(doc), form, field, sig, names;
    CGPDFArrayRef fields, annotations;
    CGPDFStringRef string;
    assert(CGPDFDictionaryGetDictionary(cat, "AcroForm", &form));
    assert(CGPDFDictionaryGetArray(form, "Fields", &fields) && CGPDFArrayGetCount(fields) == 2);
    assert(CGPDFArrayGetDictionary(fields, 0, &field) &&
           CGPDFDictionaryGetString(field, "V", &string));
    assert(CGPDFStringGetLength(string) == 5 && !memcmp(CGPDFStringGetBytePtr(string), "Alice", 5));
    assert(CGPDFArrayGetDictionary(fields, 1, &field) &&
           CGPDFDictionaryGetDictionary(field, "V", &sig));
    assert(CGPDFDictionaryGetString(sig, "Contents", &string) && CGPDFStringGetLength(string) == 6);
    assert(CGPDFDictionaryGetDictionary(cat, "Names", &names));
    assert(CGPDFDictionaryGetArray(CGPDFPageGetDictionary(CGPDFDocumentGetPage(doc, 1)), "Annots",
                                   &annotations) &&
           CGPDFArrayGetCount(annotations) == 2);
    assert(CGPDFPageGetRotationAngle(CGPDFDocumentGetPage(doc, 2)) == 90);
}

static rmr_plan *plan(void) {
    rmr_element path[] = {{.kind = RMR_MOVE, .end = {-50, 50}},
                          {.kind = RMR_LINE, .end = {50, 50}},
                          {.kind = RMR_LINE, .end = {50, 150}},
                          {.kind = RMR_LINE, .end = {-50, 150}},
                          {.kind = RMR_CLOSE}};
    rmr_element accent[] = {{.kind = RMR_MOVE, .end = {-90, 15}},
                            {.kind = RMR_LINE, .end = {-65, 15}},
                            {.kind = RMR_LINE, .end = {-90, 35}},
                            {.kind = RMR_CLOSE}};
    rmr_element image_clip[] = {{.kind = RMR_MOVE, .end = {60, 10}},
                                {.kind = RMR_LINE, .end = {90, 10}},
                                {.kind = RMR_LINE, .end = {90, 40}},
                                {.kind = RMR_LINE, .end = {60, 40}},
                                {.kind = RMR_CLOSE}};
    uint8_t bits[] = {0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa};
    rmr_draw draws[] = {{.path = {path, 5}, .mode = RMR_FILL, .color = {1, 0, 0, 1}},
                        {.path = {accent, 4},
                         .mode = RMR_FILL,
                         .color = {0, 1, 0, 0.7},
                         .blend = RMR_BLEND_MULTIPLY},
                        {.mode = RMR_TEXT,
                         .color = {1, 1, 1, 1},
                         .text = {.utf8 = "Overlay",
                                  .size = 7,
                                  .font_size = 12,
                                  .origin = {-80, 185},
                                  .bounds = {0, -14, 80, 20}}},
                        {.path = {image_clip, 5},
                         .mode = RMR_IMAGE,
                         .color = {1, 1, 1, 1},
                         .image = {image_png, sizeof image_png, {30, 0, 0, 30, 60, 10}}},
                        {.path = {accent, 4},
                         .mode = RMR_FILL,
                         .color = {1, 1, 0, 1},
                         .mask = {bits, 8, 8, 1, {-90, 15, 25, 20}}}};
    rmr_plan *p = nullptr;
    assert(rmr_plan_create(draws, sizeof draws / sizeof *draws, (rmr_rect){-100, 0, 200, 200},
                           nullptr, &p, nullptr) == RMR_OK);
    return p;
}

static bool cancelled(void *ctx) {
    (void)ctx;
    return true;
}

static void pixels(CGPDFDocumentRef doc, size_t index) {
    uint8_t bytes[200 * 200 * 4];
    memset(bytes, 0, sizeof bytes);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef cg =
        CGBitmapContextCreate(bytes, 200, 200, 8, 800, cs, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    assert(cg);
    CGPDFPageRef page = CGPDFDocumentGetPage(doc, index);
    CGContextConcatCTM(cg, CGPDFPageGetDrawingTransform(page, kCGPDFMediaBox,
                                                        CGRectMake(0, 0, 200, 200), 0, false));
    CGContextDrawPDFPage(cg, page);
    CGContextRelease(cg);
    size_t center = (100 * 200 + 100) * 4, corner = (100 * 200 + 10) * 4;
    assert(bytes[center] > 240 && bytes[center + 2] < 20);
    assert(bytes[corner + 2] > 240 && bytes[corner] < 20);
}

static void raster(CGPDFDocumentRef doc, size_t page, uint8_t *bytes) {
    memset(bytes, 0, 200 * 200 * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef cg =
        CGBitmapContextCreate(bytes, 200, 200, 8, 800, cs, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    assert(cg);
    CGPDFPageRef p = CGPDFDocumentGetPage(doc, page);
    CGContextConcatCTM(
        cg, CGPDFPageGetDrawingTransform(p, kCGPDFMediaBox, CGRectMake(0, 0, 200, 200), 0, false));
    CGContextDrawPDFPage(cg, p);
    CGContextRelease(cg);
}

static void compare_fresh(const rmr_apple_pdf_source *source, const rmr_pdf_page *pages,
                          CGPDFDocumentRef actual) {
    rmr_buffer pdf = {0};
    assert(rmr_apple_pdf_overlay(source, pages, 2, nullptr, nullptr, nullptr, &pdf, nullptr,
                                 nullptr) == RMR_OK);
    CGPDFDocumentRef expected = document(pdf.bytes, pdf.size);
    uint8_t *a = malloc(200 * 200 * 4), *b = malloc(200 * 200 * 4);
    assert(a && b);
    for (size_t page = 1; page <= 2; page++) {
        raster(actual, page, a);
        raster(expected, page, b);
        size_t different = 0;
        for (size_t i = 0; i < 200 * 200 * 4; i++)
            if (abs((int)a[i] - (int)b[i]) > 3)
                different++;
        if (different)
            fprintf(stderr, "page %zu: %zu raster channels differ\n", page, different);
        assert(different < 100); /* PDF font rasterisation/AA can round at a boundary. */
    }
    free(a);
    free(b);
    CGPDFDocumentRelease(expected);
    rmr_buffer_free(&pdf);
}

static int update_file(const char *input, const char *output) {
    FILE *f = fopen(input, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long length = ftell(f);
    assert(length > 0 && length < 10000000);
    rewind(f);
    uint8_t *data = malloc((size_t)length);
    assert(data);
    assert(fread(data, 1, (size_t)length, f) == (size_t)length);
    fclose(f);
    rmr_plan *p = plan();
    rmr_sheet sheet = {200, 200, {1, 0, 0, -1, 100, 200}};
    rmr_buffer overlay_pdf = {0}, out = {0};
    rmr_error error = {0};
    const rmr_plan *plans[] = {p};
    assert(rmr_apple_pdf_sheets(plans, &sheet, 1, nullptr, nullptr, nullptr, &overlay_pdf,
                                &error) == RMR_OK);
    rmr_pdf_overlay o = {
        .pdf = overlay_pdf.bytes, .size = overlay_pdf.size, .transform = {1, 0, 0, 1, 0, 0}};
    rmr_status status = rmr_pdf_incremental(data, (size_t)length, &o, 1, nullptr, nullptr, nullptr,
                                            &out, nullptr, &error);
    if (status != RMR_OK)
        fprintf(stderr, "%s\n", error.message);
    assert(status == RMR_OK);
    f = fopen(output, "wb");
    assert(f);
    assert(fwrite(out.bytes, 1, out.size, f) == out.size);
    fclose(f);
    CGPDFDocumentRef doc = document(out.bytes, out.size);
    pixels(doc, 1);
    CGPDFDocumentRelease(doc);
    free(data);
    rmr_buffer_free(&out);
    rmr_buffer_free(&overlay_pdf);
    rmr_plan_free(p);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3)
        return update_file(argv[1], argv[2]);
    assert(argc == 1);
    fixture f = source(false);
    rmr_plan *p = plan();
    rmr_error error = {0};
    rmr_buffer result = {0};
    rmr_pdf_incremental_report report = {0};
    rmr_apple_pdf_source *src = nullptr;
    assert(rmr_apple_pdf_source_create((uint8_t *)f.bytes, f.n, nullptr, &src, &error) == RMR_OK);
    rmr_pdf_page pages[2];
    for (size_t i = 0; i < 2; i++)
        pages[i] = (rmr_pdf_page){.marks = p,
                                  .sheet = {200, 200, {1, 0, 0, -1, 100, 200}},
                                  .source_rect = {-100, 0, 200, 200},
                                  .source_page = i,
                                  .has_source = true,
                                  .source_visible = true};
    rmr_status s = rmr_apple_pdf_incremental(src, pages, 2, nullptr, nullptr, nullptr, &result,
                                             &report, &error);
    if (s != RMR_OK)
        fprintf(stderr, "%d: %s\n", (int)s, error.message);
    assert(s == RMR_OK);
    assert(result.size > f.n && !memcmp(result.bytes, f.bytes, f.n));
    assert(report.original_bytes == f.n && report.pages_updated == 2 &&
           report.original_revision_preserved);
    CGPDFDocumentRef doc = document(result.bytes, result.size);
    preserved(doc);
    compare_fresh(src, pages, doc);
    pixels(doc, 1);
    pixels(doc, 2);
    CGPDFDocumentRelease(doc);
    rmr_pdf_page selected[] = {pages[1], pages[0], pages[0]};
    rmr_buffer recomposed = {0};
    assert(rmr_apple_pdf_recompose(src, selected, 3, nullptr, nullptr, nullptr, &recomposed,
                                   &error) == RMR_OK);
    doc = document(recomposed.bytes, recomposed.size);
    assert(CGPDFDocumentGetNumberOfPages(doc) == 3);
    assert(CGPDFPageGetRotationAngle(CGPDFDocumentGetPage(doc, 1)) == 90);
    for (size_t i = 2; i <= 3; i++) {
        CGPDFArrayRef annotations;
        assert(CGPDFDictionaryGetArray(CGPDFPageGetDictionary(CGPDFDocumentGetPage(doc, i)),
                                       "Annots", &annotations));
        assert(CGPDFArrayGetCount(annotations) == 2);
        pixels(doc, i);
    }
    CGPDFDictionaryRef form, field;
    CGPDFArrayRef fields, widgets;
    assert(CGPDFDictionaryGetDictionary(CGPDFDocumentGetCatalog(doc), "AcroForm", &form));
    assert(CGPDFDictionaryGetArray(form, "Fields", &fields));
    assert(CGPDFArrayGetDictionary(fields, 0, &field));
    assert(CGPDFDictionaryGetArray(field, "Kids", &widgets));
    assert(CGPDFArrayGetCount(widgets) == 2);
    CGPDFDocumentRelease(doc);
    rmr_buffer_free(&recomposed);
    rmr_apple_pdf_source *again = nullptr;
    assert(rmr_apple_pdf_source_create(result.bytes, result.size, nullptr, &again, &error) ==
           RMR_OK);
    rmr_buffer second = {0};
    assert(rmr_apple_pdf_incremental(again, pages, 2, nullptr, nullptr, nullptr, &second, nullptr,
                                     &error) == RMR_OK);
    assert(second.size > result.size && !memcmp(second.bytes, result.bytes, result.size));
    doc = document(second.bytes, second.size);
    preserved(doc);
    pixels(doc, 1);
    CGPDFDocumentRelease(doc);
    rmr_buffer_free(&second);
    rmr_apple_pdf_source_free(again);
    pages[0].sheet.width = 240;
    pages[0].sheet.transform.tx = 120;
    rmr_buffer expanded = {0};
    assert(rmr_apple_pdf_incremental(src, pages, 2, nullptr, nullptr, nullptr, &expanded, nullptr,
                                     &error) == RMR_OK);
    doc = document(expanded.bytes, expanded.size);
    preserved(doc);
    CGRect expanded_box = CGPDFPageGetBoxRect(CGPDFDocumentGetPage(doc, 1), kCGPDFMediaBox);
    assert(fabs(expanded_box.origin.x + 20) < 0.001 && fabs(expanded_box.size.width - 240) < 0.001);
    assert(CGPDFPageGetBoxRect(CGPDFDocumentGetPage(doc, 2), kCGPDFMediaBox).size.width == 200);
    CGPDFArrayRef annots, rect;
    CGPDFDictionaryRef note;
    CGPDFReal x0;
    assert(CGPDFDictionaryGetArray(CGPDFPageGetDictionary(CGPDFDocumentGetPage(doc, 1)), "Annots",
                                   &annots));
    assert(CGPDFArrayGetDictionary(annots, 1, &note) &&
           CGPDFDictionaryGetArray(note, "Rect", &rect));
    assert(CGPDFArrayGetNumber(rect, 0, &x0) && x0 == 10);
    CGPDFDocumentRelease(doc);
    rmr_buffer_free(&expanded);
    pages[0].sheet.width = 200;
    pages[0].sheet.transform.tx = 100;
    rmr_buffer sentinel = {(uint8_t *)(uintptr_t)1, 123};
    rmr_pdf_incremental_report before = {99, 98, false};
    assert(rmr_apple_pdf_incremental(src, pages, 1, nullptr, nullptr, nullptr, &sentinel, &before,
                                     &error) == RMR_ERR_UNSUPPORTED);
    pages[0].source_visible = false;
    assert(rmr_apple_pdf_incremental(src, pages, 2, nullptr, nullptr, nullptr, &sentinel, &before,
                                     &error) == RMR_ERR_UNSUPPORTED);
    pages[0].source_visible = true;
    assert(rmr_apple_pdf_incremental(src, pages, 2, nullptr, cancelled, nullptr, &sentinel, &before,
                                     &error) == RMR_ERR_CANCELLED);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_pdf_bytes = f.n;
    assert(rmr_apple_pdf_incremental(src, pages, 2, &limits, nullptr, nullptr, &sentinel, &before,
                                     &error) == RMR_ERR_LIMIT);
    assert(sentinel.bytes == (uint8_t *)(uintptr_t)1 && sentinel.size == 123 &&
           before.pages_updated == 99);
    fixture cert = source(true);
    assert(rmr_pdf_incremental((uint8_t *)cert.bytes, cert.n, nullptr, 0, nullptr, nullptr, nullptr,
                               &sentinel, nullptr, &error) == RMR_ERR_UNSUPPORTED);
    rmr_buffer copy = {0};
    assert(rmr_pdf_incremental((uint8_t *)f.bytes, f.n, nullptr, 0, nullptr, nullptr, nullptr,
                               &copy, nullptr, &error) == RMR_OK);
    assert(copy.size == f.n && !memcmp(copy.bytes, f.bytes, f.n));
    rmr_buffer_free(&copy);
    /* Every truncation must fail transactionally; mutation smoke under ASan. */
    for (size_t i = 1; i < f.n; i += 17) {
        rmr_status st = rmr_pdf_incremental((uint8_t *)f.bytes, i, nullptr, 0, nullptr, nullptr,
                                            nullptr, &copy, nullptr, &error);
        if (st == RMR_OK)
            rmr_buffer_free(&copy);
    }
    for (size_t i = 0; i < f.n; i += 31) {
        char old = f.bytes[i];
        f.bytes[i] = '[';
        rmr_status st = rmr_pdf_incremental((uint8_t *)f.bytes, f.n, nullptr, 0, nullptr, nullptr,
                                            nullptr, &copy, nullptr, &error);
        if (st == RMR_OK)
            rmr_buffer_free(&copy);
        f.bytes[i] = old;
    }
    rmr_buffer_free(&result);
    rmr_apple_pdf_source_free(src);
    rmr_plan_free(p);
    puts("incremental PDF preservation passed");
    return 0;
}
