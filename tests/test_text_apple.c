/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple.h>

static void text_operator(CGPDFScannerRef scanner, void *info) {
    (void)scanner;
    ++*(size_t *)info;
}

int main(void) {
    rmr_text_provider provider = rmr_apple_text_provider();
    const char *labels[] = {"Sunday", "Линия", "مرحبا", "שלום", "日本語", "📓", "e\xcc\x81", ""};

    for (size_t i = 0; i < sizeof labels / sizeof *labels; i++) {
        rmr_template_item item = {.kind = RMR_TEMPLATE_TEXT,
                                  .text = {.utf8 = labels[i],
                                           .size = strlen(labels[i]),
                                           .has_font_size = true,
                                           .font_size = {.number = 16},
                                           .position = {.x = {.number = 16}, .y = {.number = 32}}}};
        rmr_template model = {.items = &item, .count = 1};
        rmr_plan *p = nullptr;
        OK(rmr_template_plan_with_text(&model, (rmr_point){128, 64}, (rmr_point){128, 64},
                                       (rmr_rect){-64, 0, 128, 64}, &provider, nullptr, &p,
                                       nullptr));

        rmr_bitmap b = {0};
        OK(rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b, nullptr));
        rmr_rect box = rmr_plan_bounds(p);
        size_t ink = 0;
        for (size_t y = 0; y < b.height; y++)
            for (size_t x = 0; x < b.width; x++)
                if (b.pixels[y * b.stride + x * 4 + 3]) {
                    ink++;
                    CHECK((double)x - 64 >= floor(box.x) - 1 &&
                          (double)x - 64 <= ceil(box.x + box.width) + 1);
                    CHECK((double)y >= floor(box.y) - 1 &&
                          (double)y <= ceil(box.y + box.height) + 1);
                }
        CHECK(*labels[i] ? ink > 0 : ink == 0);

        if (i == 0) {
            const rmr_plan *pages[] = {p};
            rmr_buffer pdf = {0};
            OK(rmr_apple_pdf(pages, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
            CGDataProviderRef data =
                CGDataProviderCreateWithData(nullptr, pdf.bytes, pdf.size, nullptr);
            CHECK(data);
            CGPDFDocumentRef doc = CGPDFDocumentCreateWithProvider(data);
            CHECK(doc);
            CGPDFPageRef page = CGPDFDocumentGetPage(doc, 1);
            CHECK(page);
            CGPDFContentStreamRef content = CGPDFContentStreamCreateWithPage(page);
            CHECK(content);
            CGPDFOperatorTableRef table = CGPDFOperatorTableCreate();
            CHECK(table);
            CGPDFOperatorTableSetCallback(table, "Tj", text_operator);
            CGPDFOperatorTableSetCallback(table, "TJ", text_operator);
            size_t operations = 0;
            CGPDFScannerRef scanner = CGPDFScannerCreate(content, table, &operations);
            CHECK(scanner);
            CHECK(CGPDFScannerScan(scanner));
            CHECK(operations > 0);
            CGPDFScannerRelease(scanner);
            CGPDFOperatorTableRelease(table);
            CGPDFContentStreamRelease(content);

            uint8_t pixels[128 * 64 * 4] = {0};
            CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            CHECK(space);
            CGContextRef cg =
                CGBitmapContextCreate(pixels, 128, 64, 8, 128 * 4, space,
                                      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
            CHECK(cg);
            CGContextDrawPDFPage(cg, page);
            size_t pdf_ink = 0;
            for (size_t j = 3; j < sizeof pixels; j += 4)
                pdf_ink += pixels[j] != 0;
            CHECK(pdf_ink > 100);
            CGContextRelease(cg);
            CGColorSpaceRelease(space);
            CGPDFDocumentRelease(doc);
            CGDataProviderRelease(data);
            rmr_buffer_free(&pdf);
        }

        rmr_bitmap_free(&b);
        rmr_plan_free(p);
    }

    rmr_text_metrics sentinel = {.width = 42};
    CHECK(provider.measure(provider.context, "\xc0\xaf", 2, 16, &sentinel) ==
              RMR_ERR_INVALID_ARGUMENT &&
          sentinel.width == 42);

    CHECK(provider.measure(provider.context, "x", 1, 0, &sentinel) == RMR_ERR_INVALID_ARGUMENT &&
          sentinel.width == 42);
    return 0;
}
