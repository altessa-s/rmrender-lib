/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <CoreGraphics/CoreGraphics.h>
#include <rmrender/apple_pdf.h>

int main(void) {
    CFMutableDataRef bytes = CFDataCreateMutable(kCFAllocatorDefault, 0);
    CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData(bytes);
    CGRect box = CGRectMake(0, 0, 100, 200);
    CGContextRef cg = CGPDFContextCreate(consumer, &box, nullptr);
    CGDataConsumerRelease(consumer);
    CHECK(cg);

    CGPDFContextBeginPage(cg, nullptr);
    CGContextSetRGBFillColor(cg, 1, 0, 0, 1);
    CGContextFillRect(cg, box);
    CFURLRef url = CFURLCreateWithString(kCFAllocatorDefault,
                                         CFSTR("https://example.test/own-fixture"), nullptr);
    CGPDFContextSetURLForRect(cg, url, CGRectMake(10, 20, 30, 40));
    CFRelease(url);
    CGPDFContextSetDestinationForRect(cg, CFSTR("second"), CGRectMake(20, 50, 40, 30));
    CGPDFContextEndPage(cg);

    CGPDFContextBeginPage(cg, nullptr);
    CGContextSetRGBFillColor(cg, 0, 0, 1, 1);
    CGContextFillRect(cg, box);
    CGPDFContextAddDestinationAtPoint(cg, CFSTR("second"), CGPointMake(0, 200));
    CGPDFContextEndPage(cg);

    int destination = 2;
    CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &destination);
    const void *keys[] = {kCGPDFOutlineTitle, kCGPDFOutlineDestination},
               *values[] = {CFSTR("Own second page"), n};
    CFDictionaryRef item =
        CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);
    CFRelease(n);
    const void *items[] = {item};
    CFArrayRef children = CFArrayCreate(kCFAllocatorDefault, items, 1, &kCFTypeArrayCallBacks);
    CFRelease(item);

    const void *root_keys[] = {kCGPDFOutlineChildren}, *root_values[] = {children};
    CFDictionaryRef root =
        CFDictionaryCreate(kCFAllocatorDefault, root_keys, root_values, 1,
                           &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(children);
    CGPDFContextSetOutline(cg, root);
    CFRelease(root);
    CGPDFContextClose(cg);
    CGContextRelease(cg);

    rmr_apple_pdf_source *source = nullptr;
    OK(rmr_apple_pdf_source_create(CFDataGetBytePtr(bytes), (size_t)CFDataGetLength(bytes), nullptr,
                                   &source, nullptr));
    CFRelease(bytes);
    CHECK(rmr_apple_pdf_source_count(source) == 2);
    rmr_point size;
    OK(rmr_apple_pdf_source_size(source, 0, &size, nullptr));
    CHECK(size.x == 100 && size.y == 200);

    rmr_plan *empty = nullptr;
    OK(rmr_plan_create(nullptr, 0, (rmr_rect){0, 0, 100, 200}, nullptr, &empty, nullptr));
    rmr_pdf_page pages[2];
    for (size_t i = 0; i < 2; i++)
        pages[i] = (rmr_pdf_page){.marks = empty,
                                  .sheet = {100, 200, {1, 0, 0, -1, 0, 200}},
                                  .source_rect = {0, 0, 100, 200},
                                  .source_page = 1 - i,
                                  .has_source = true,
                                  .source_visible = true};

    rmr_buffer result = {0};
    rmr_pdf_report report = {0};
    rmr_error e = {0};
    OK(rmr_apple_pdf_overlay(source, pages, 2, nullptr, nullptr, nullptr, &result, &report, &e));
    CHECK(report.links_preserved == 2 && report.links_dropped == 0);

    CGDataProviderRef provider =
        CGDataProviderCreateWithData(nullptr, result.bytes, result.size, nullptr);
    CGPDFDocumentRef document = CGPDFDocumentCreateWithProvider(provider);
    CGDataProviderRelease(provider);
    CHECK(document && CGPDFDocumentGetNumberOfPages(document) == 2);

    root = CGPDFDocumentGetOutline(document);
    CHECK(root);
    children = CFDictionaryGetValue(root, kCGPDFOutlineChildren);
    CHECK(children && CFArrayGetCount(children) == 1);
    item = CFArrayGetValueAtIndex(children, 0);
    n = CFDictionaryGetValue(item, kCGPDFOutlineDestination);
    CHECK(CFNumberGetValue(n, kCFNumberIntType, &destination) && destination == 1);

    CGPDFArrayRef annotations = nullptr;
    CHECK(CGPDFDictionaryGetArray(CGPDFPageGetDictionary(CGPDFDocumentGetPage(document, 2)),
                                  "Annots", &annotations));
    CHECK(CGPDFArrayGetCount(annotations) == 2);

    CGPDFDocumentRelease(document);
    rmr_buffer_free(&result);

    rmr_limits limits = rmr_limits_default();
    limits.maximum_pdf_bytes = 20;
    CHECK(rmr_apple_pdf_overlay(source, pages, 2, &limits, nullptr, nullptr, &result, nullptr,
                                &e) == RMR_ERR_LIMIT);
    CHECK(result.bytes == nullptr);

    rmr_apple_pdf_source_free(source);
    rmr_plan_free(empty);
    return 0;
}
