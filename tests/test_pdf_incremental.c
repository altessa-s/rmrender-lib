/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <CoreGraphics/CoreGraphics.h>
#include <assert.h>
#include <math.h>
#include <rmrender/apple_pdf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_fixture.h"
#include "pdf_fixture.h"

static CGPDFDocumentRef document(const uint8_t* bytes, size_t n) {
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
  CGPDFDictionaryRef cat = CGPDFDocumentGetCatalog(doc), form, field, sig,
                     names;
  CGPDFArrayRef fields, annotations;
  CGPDFStringRef string;
  assert(CGPDFDictionaryGetDictionary(cat, "AcroForm", &form));
  assert(CGPDFDictionaryGetArray(form, "Fields", &fields) &&
         CGPDFArrayGetCount(fields) == 2);

  assert(CGPDFArrayGetDictionary(fields, 0, &field) &&
         CGPDFDictionaryGetString(field, "V", &string));
  assert(CGPDFStringGetLength(string) == 5 &&
         !memcmp(CGPDFStringGetBytePtr(string), "Alice", 5));

  assert(CGPDFArrayGetDictionary(fields, 1, &field) &&
         CGPDFDictionaryGetDictionary(field, "V", &sig));
  assert(CGPDFDictionaryGetString(sig, "Contents", &string) &&
         CGPDFStringGetLength(string) == 6);

  assert(CGPDFDictionaryGetDictionary(cat, "Names", &names));

  assert(CGPDFDictionaryGetArray(
             CGPDFPageGetDictionary(CGPDFDocumentGetPage(doc, 1)), "Annots",
             &annotations) &&
         CGPDFArrayGetCount(annotations) == 2);

  assert(CGPDFPageGetRotationAngle(CGPDFDocumentGetPage(doc, 2)) == 90);
}

static rmr_plan* plan(void) {
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
  rmr_draw draws[] = {
      {.path = {path, 5}, .mode = RMR_FILL, .color = {1, 0, 0, 1}},
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

  rmr_plan* p = nullptr;
  assert(rmr_plan_create(draws, sizeof draws / sizeof *draws,
                         (rmr_rect){-100, 0, 200, 200}, nullptr, &p,
                         nullptr) == RMR_OK);
  return p;
}

static bool cancelled(void* ctx) {
  (void)ctx;
  return true;
}

static void pixels(CGPDFDocumentRef doc, size_t index) {
  uint8_t bytes[200 * 200 * 4];
  memset(bytes, 0, sizeof bytes);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef cg = CGBitmapContextCreate(bytes, 200, 200, 8, 800, cs,
                                          kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(cs);
  assert(cg);

  CGPDFPageRef page = CGPDFDocumentGetPage(doc, index);
  CGContextConcatCTM(
      cg, CGPDFPageGetDrawingTransform(page, kCGPDFMediaBox,
                                       CGRectMake(0, 0, 200, 200), 0, false));
  CGContextDrawPDFPage(cg, page);
  CGContextRelease(cg);

  size_t center = (100 * 200 + 100) * 4, corner = (100 * 200 + 10) * 4;
  assert(bytes[center] > 240 && bytes[center + 2] < 20);
  assert(bytes[corner + 2] > 240 && bytes[corner] < 20);
}

static void raster(CGPDFDocumentRef doc, size_t page, uint8_t* bytes) {
  memset(bytes, 0, 200 * 200 * 4);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef cg = CGBitmapContextCreate(bytes, 200, 200, 8, 800, cs,
                                          kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(cs);
  assert(cg);

  CGPDFPageRef p = CGPDFDocumentGetPage(doc, page);
  CGContextConcatCTM(
      cg, CGPDFPageGetDrawingTransform(p, kCGPDFMediaBox,
                                       CGRectMake(0, 0, 200, 200), 0, false));
  CGContextDrawPDFPage(cg, p);
  CGContextRelease(cg);
}

static void compare_fresh(const rmr_apple_pdf_source* source,
                          const rmr_pdf_page* pages, CGPDFDocumentRef actual) {
  rmr_buffer pdf = {0};
  assert(rmr_apple_pdf_overlay(source, pages, 2, nullptr, nullptr, nullptr,
                               &pdf, nullptr, nullptr) == RMR_OK);

  CGPDFDocumentRef expected = document(pdf.bytes, pdf.size);
  uint8_t *a = malloc(200 * 200 * 4), *b = malloc(200 * 200 * 4);
  assert(a && b);

  for (size_t page = 1; page <= 2; page++) {
    raster(actual, page, a);
    raster(expected, page, b);
    size_t different = 0;
    for (size_t i = 0; i < 200 * 200 * 4; i++)
      if (abs((int)a[i] - (int)b[i]) > 3) different++;
    if (different)
      fprintf(stderr, "page %zu: %zu raster channels differ\n", page,
              different);
    assert(different <
           100); /* PDF font rasterisation/AA can round at a boundary. */
  }

  free(a);
  free(b);
  CGPDFDocumentRelease(expected);
  rmr_buffer_free(&pdf);
}

/* Page `page` of doc rasterized at scale onto white; malloc'd, *side by *side
 * pixels of four bytes. */
static uint8_t* render(CGPDFDocumentRef doc, size_t page, double scale,
                       size_t* side) {
  *side = (size_t)(200 * scale);
  uint8_t* bytes = calloc(*side * *side, 4);
  assert(bytes);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef cg = CGBitmapContextCreate(bytes, *side, *side, 8, *side * 4, cs,
                                          kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(cs);
  assert(cg);
  CGContextSetRGBFillColor(cg, 1, 1, 1, 1);
  CGContextFillRect(cg, CGRectMake(0, 0, (double)*side, (double)*side));
  CGPDFPageRef p = CGPDFDocumentGetPage(doc, page);
  CGContextConcatCTM(
      cg, CGPDFPageGetDrawingTransform(
              p, kCGPDFMediaBox, CGRectMake(0, 0, (double)*side, (double)*side),
              0, false));
  CGContextDrawPDFPage(cg, p);
  CGContextRelease(cg);
  return bytes;
}

/* Whether page 1 of a and b paint the same pixels, at three scales. */
static bool same_pixels(const rmr_buffer* a, const rmr_buffer* b) {
  CGPDFDocumentRef x = document(a->bytes, a->size),
                   y = document(b->bytes, b->size);
  size_t differing = 0;
  for (double scale = 1; scale <= 7; scale *= 2.5) {
    size_t side = 0, other = 0;
    uint8_t *p = render(x, 1, scale, &side), *q = render(y, 1, scale, &other);
    assert(side == other);
    for (size_t i = 0; i < side * side * 4; i += 4)
      differing += memcmp(p + i, q + i, 4) != 0;
    free(p);
    free(q);
  }
  if (differing) fprintf(stderr, "%zu pixels differ\n", differing);
  CGPDFDocumentRelease(x);
  CGPDFDocumentRelease(y);
  return !differing;
}

static void count_form(const char* key, CGPDFObjectRef value, void* info) {
  (void)value;
  if (!strncmp(key, "RMR", 3)) ++*(size_t*)info;
}

/* The forms an update draws over page 1 of pdf. */
static size_t overlay_forms(const rmr_buffer* pdf) {
  CGPDFDocumentRef doc = document(pdf->bytes, pdf->size);
  CGPDFDictionaryRef resources, objects;
  size_t count = 0;
  if (CGPDFDictionaryGetDictionary(
          CGPDFPageGetDictionary(CGPDFDocumentGetPage(doc, 1)), "Resources",
          &resources) &&
      CGPDFDictionaryGetDictionary(resources, "XObject", &objects))
    CGPDFDictionaryApplyFunction(objects, count_form, &count);
  CGPDFDocumentRelease(doc);
  return count;
}

/* An empty page made by CoreGraphics, as a text layer without glyphs is. */
static rmr_buffer empty_layer(void) {
  CFMutableDataRef data = CFDataCreateMutable(nullptr, 0);
  CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData(data);
  CGRect box = CGRectMake(0, 0, 200, 200);
  CGContextRef cg = CGPDFContextCreate(consumer, &box, nullptr);
  CGDataConsumerRelease(consumer);
  assert(cg);
  CGPDFContextBeginPage(cg, nullptr);
  CGContextSetTextMatrix(cg, CGAffineTransformIdentity);
  CGPDFContextEndPage(cg);
  CGPDFContextClose(cg);
  CGContextRelease(cg);
  rmr_buffer out = {malloc((size_t)CFDataGetLength(data)),
                    (size_t)CFDataGetLength(data)};
  assert(out.bytes);
  memcpy(out.bytes, CFDataGetBytePtr(data), out.size);
  CFRelease(data);
  return out;
}

/* page's plan as an update of fixture f: through rmr_apple_pdf_incremental,
 * and as CoreGraphics alone draws it over page 1, the reference. */
static void update_both(const fixture* f, const rmr_apple_pdf_source* src,
                        const rmr_plan* marks, rmr_buffer* updated,
                        rmr_buffer* reference) {
  rmr_pdf_page pages[2];
  for (size_t i = 0; i < 2; i++)
    pages[i] = (rmr_pdf_page){.marks = marks,
                              .sheet = {200, 200, {1, 0, 0, -1, 100, 200}},
                              .source_rect = {-100, 0, 200, 200},
                              .source_page = i,
                              .has_source = true,
                              .source_visible = true};
  rmr_error error = {0};
  rmr_status s = rmr_apple_pdf_incremental(src, pages, 2, nullptr, nullptr,
                                           nullptr, updated, nullptr, &error);
  if (s != RMR_OK) fprintf(stderr, "%s\n", error.message);
  assert(s == RMR_OK);

  rmr_buffer native = {0};
  const rmr_plan* plans[] = {marks};
  assert(rmr_apple_pdf_sheets(plans, &pages[0].sheet, 1, nullptr, nullptr,
                              nullptr, &native, nullptr) == RMR_OK);
  rmr_pdf_overlay o = {.pdf = native.bytes,
                       .size = native.size,
                       .transform = {1, 0, 0, 1, 0, 0}};
  assert(rmr_pdf_incremental((const uint8_t*)f->bytes, f->n, &o, 1, nullptr,
                             nullptr, nullptr, reference, nullptr,
                             nullptr) == RMR_OK);
  rmr_buffer_free(&native);
}

/* A page whose text and image come before its ink is CoreGraphics' up to its
 * last image and the writer's after, two forms that paint what CoreGraphics'
 * one form of the whole page paints; a page whose only native draw is a text
 * layer without glyphs is the writer's alone, and paints the same too. */
static void mixed_pages(const fixture* f, const rmr_apple_pdf_source* src) {
  rmr_plan* p = plan();
  rmr_buffer updated = {0}, reference = {0};
  update_both(f, src, p, &updated, &reference);
  assert(overlay_forms(&updated) == 2 && overlay_forms(&reference) == 1);
  assert(same_pixels(&updated, &reference));
  rmr_buffer_free(&updated);
  rmr_buffer_free(&reference);
  rmr_plan_free(p);

  rmr_buffer layer = empty_layer();
  rmr_element frame[] = {{.kind = RMR_MOVE, .end = {-100, 0}},
                         {.kind = RMR_LINE, .end = {100, 0}},
                         {.kind = RMR_LINE, .end = {100, 200}},
                         {.kind = RMR_LINE, .end = {-100, 200}},
                         {.kind = RMR_CLOSE}};
  rmr_element ink[] = {{.kind = RMR_MOVE, .end = {-50, 50}},
                       {.kind = RMR_LINE, .end = {50, 60}},
                       {.kind = RMR_LINE, .end = {40, 150}},
                       {.kind = RMR_CLOSE}};
  rmr_draw draws[] = {
      {.path = {frame, 5},
       .mode = RMR_PDF,
       .color = {0, 0, 0, 1},
       .image = {layer.bytes, layer.size, {200, 0, 0, 200, -100, 0}}},
      {.path = {ink, 4}, .mode = RMR_FILL, .color = {0.1, 0.5, 0.2, 0.6}},
      {.path = {ink, 4},
       .mode = RMR_STROKE,
       .color = {0.7, 0.1, 0.1, 1},
       .line_width = 3,
       .cap = RMR_CAP_ROUND,
       .join = RMR_JOIN_ROUND}};
  rmr_plan* empty = nullptr;
  assert(rmr_plan_create(draws, 3, (rmr_rect){-100, 0, 200, 200}, nullptr,
                         &empty, nullptr) == RMR_OK);
  rmr_buffer_free(&layer);
  update_both(f, src, empty, &updated, &reference);
  assert(overlay_forms(&updated) == 1);
  assert(same_pixels(&updated, &reference));
  rmr_buffer_free(&updated);
  rmr_buffer_free(&reference);
  rmr_plan_free(empty);
}

/* A source of count blank pages. */
static rmr_buffer blank_source(size_t count) {
  size_t capacity = 4096 + count * 160, n = 0;
  char* bytes = malloc(capacity);
  size_t* offsets = calloc(count + 3, sizeof *offsets);
  assert(bytes && offsets);
  n += (size_t)snprintf(bytes + n, capacity - n, "%%PDF-1.7\n");
  offsets[1] = n;
  n += (size_t)snprintf(bytes + n, capacity - n,
                        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  offsets[2] = n;
  n += (size_t)snprintf(bytes + n, capacity - n,
                        "2 0 obj\n<< /Type /Pages /Count %zu /Kids [", count);
  for (size_t i = 0; i < count; i++)
    n += (size_t)snprintf(bytes + n, capacity - n, "%zu 0 R ", i + 3);
  n += (size_t)snprintf(bytes + n, capacity - n, "] >>\nendobj\n");
  for (size_t i = 0; i < count; i++) {
    offsets[i + 3] = n;
    n += (size_t)snprintf(bytes + n, capacity - n,
                          "%zu 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox "
                          "[0 0 200 200] >>\nendobj\n",
                          i + 3);
  }
  size_t x = n;
  n += (size_t)snprintf(bytes + n, capacity - n,
                        "xref\n0 %zu\n0000000000 65535 f \n", count + 3);
  for (size_t i = 1; i < count + 3; i++)
    n += (size_t)snprintf(bytes + n, capacity - n, "%010zu 00000 n \n",
                          offsets[i]);
  n += (size_t)snprintf(bytes + n, capacity - n,
                        "trailer\n<< /Size %zu /Root 1 0 R >>\nstartxref\n%zu\n"
                        "%%%%EOF\n",
                        count + 3, x);
  assert(n < capacity);
  free(offsets);
  return (rmr_buffer){(uint8_t*)bytes, n};
}

/* Ink of distinct grain masks, a page's worth of overlay, no mask the same as
 * another page's (identical masks are written once for all pages). */
static rmr_plan* grain_marks(size_t page) {
  enum { MASKS = 300 };
  static uint8_t bits[MASKS][32];
  static rmr_element paths[MASKS][5];
  rmr_draw draws[MASKS];
  for (size_t i = 0; i < MASKS; i++) {
    for (size_t k = 0; k < 32; k++) bits[i][k] = (uint8_t)(i * 31 + k * 7);
    bits[i][0] = (uint8_t)i;
    bits[i][1] = (uint8_t)(i >> 8);
    bits[i][2] = (uint8_t)page;
    double x = -90 + (double)(i % 30) * 6, y = 10 + (double)(i / 30) * 18;
    rmr_element* e = paths[i];
    e[0] = (rmr_element){.kind = RMR_MOVE, .end = {x, y}};
    e[1] = (rmr_element){.kind = RMR_LINE, .end = {x + 5, y}};
    e[2] = (rmr_element){.kind = RMR_LINE, .end = {x + 5, y + 16}};
    e[3] = (rmr_element){.kind = RMR_LINE, .end = {x, y + 16}};
    e[4] = (rmr_element){.kind = RMR_CLOSE};
    draws[i] = (rmr_draw){.path = {e, 5},
                          .mode = RMR_FILL,
                          .color = {0.2, 0.2, 0.6, 1},
                          .mask = {bits[i], 16, 16, 2, {x, y, 5, 16}}};
  }
  rmr_plan* p = nullptr;
  assert(rmr_plan_create(draws, MASKS, (rmr_rect){-100, 0, 200, 200}, nullptr,
                         &p, nullptr) == RMR_OK);
  return p;
}

/* Overlays go into the update as they are made: a document whose overlays
 * together outgrow the plan budget, each well inside it, is updated. */
static void streamed_overlays(void) {
  enum { PAGES = 60 };
  rmr_buffer pdf = blank_source(PAGES);
  rmr_apple_pdf_source* src = nullptr;
  assert(rmr_apple_pdf_source_create(pdf.bytes, pdf.size, nullptr, &src,
                                     nullptr) == RMR_OK);
  rmr_plan* marks[PAGES];
  rmr_pdf_page pages[PAGES];
  for (size_t i = 0; i < PAGES; i++) marks[i] = grain_marks(i);
  for (size_t i = 0; i < PAGES; i++)
    pages[i] = (rmr_pdf_page){.marks = marks[i],
                              .sheet = {200, 200, {1, 0, 0, -1, 100, 200}},
                              .source_rect = {-100, 0, 200, 200},
                              .source_page = i,
                              .has_source = true,
                              .source_visible = true};
  rmr_buffer one = {0}, all = {0};
  rmr_error error = {0};
  assert(rmr_apple_pdf_incremental(src, pages, 1, nullptr, nullptr, nullptr,
                                   &one, nullptr,
                                   &error) == RMR_ERR_UNSUPPORTED);
  rmr_limits limits = rmr_limits_default();
  limits.maximum_plan_bytes = 2 << 20;
  for (int fresh = 0; fresh < 2; fresh++) {
    rmr_status s =
        fresh ? rmr_apple_pdf_recompose(src, pages, PAGES, &limits, nullptr,
                                        nullptr, &all, &error)
              : rmr_apple_pdf_incremental(src, pages, PAGES, &limits, nullptr,
                                          nullptr, &all, nullptr, &error);
    if (s != RMR_OK) fprintf(stderr, "%s\n", error.message);
    assert(s == RMR_OK);
    /* Together the overlays are more than the budget. */
    assert(all.size - pdf.size > limits.maximum_plan_bytes);
    CGPDFDocumentRef doc = document(all.bytes, all.size);
    assert(CGPDFDocumentGetNumberOfPages(doc) == PAGES);
    CGPDFDocumentRelease(doc);
    rmr_buffer_free(&all);
  }

  for (size_t i = 0; i < PAGES; i++) rmr_plan_free(marks[i]);
  rmr_apple_pdf_source_free(src);
  rmr_buffer_free(&pdf);
}

static int update_file(const char* input, const char* output) {
  FILE* f = fopen(input, "rb");
  assert(f);
  assert(fseek(f, 0, SEEK_END) == 0);
  long length = ftell(f);
  assert(length > 0 && length < 10000000);
  rewind(f);
  uint8_t* data = malloc((size_t)length);
  assert(data);
  assert(fread(data, 1, (size_t)length, f) == (size_t)length);
  fclose(f);

  rmr_plan* p = plan();
  rmr_sheet sheet = {200, 200, {1, 0, 0, -1, 100, 200}};
  rmr_buffer overlay_pdf = {0}, out = {0};
  rmr_error error = {0};
  const rmr_plan* plans[] = {p};
  assert(rmr_apple_pdf_sheets(plans, &sheet, 1, nullptr, nullptr, nullptr,
                              &overlay_pdf, &error) == RMR_OK);

  rmr_pdf_overlay o = {.pdf = overlay_pdf.bytes,
                       .size = overlay_pdf.size,
                       .transform = {1, 0, 0, 1, 0, 0}};
  rmr_status status =
      rmr_pdf_incremental(data, (size_t)length, &o, 1, nullptr, nullptr,
                          nullptr, &out, nullptr, &error);
  if (status != RMR_OK) fprintf(stderr, "%s\n", error.message);
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

int main(int argc, char** argv) {
  if (argc == 3) return update_file(argv[1], argv[2]);
  assert(argc == 1);

  fixture f = source(false);
  rmr_plan* p = plan();
  rmr_error error = {0};
  rmr_buffer result = {0};
  rmr_pdf_incremental_report report = {0};
  rmr_apple_pdf_source* src = nullptr;
  assert(rmr_apple_pdf_source_create((uint8_t*)f.bytes, f.n, nullptr, &src,
                                     &error) == RMR_OK);

  rmr_pdf_page pages[2];
  for (size_t i = 0; i < 2; i++)
    pages[i] = (rmr_pdf_page){.marks = p,
                              .sheet = {200, 200, {1, 0, 0, -1, 100, 200}},
                              .source_rect = {-100, 0, 200, 200},
                              .source_page = i,
                              .has_source = true,
                              .source_visible = true};

  rmr_status s = rmr_apple_pdf_incremental(src, pages, 2, nullptr, nullptr,
                                           nullptr, &result, &report, &error);
  if (s != RMR_OK) fprintf(stderr, "%d: %s\n", (int)s, error.message);
  assert(s == RMR_OK);

  assert(result.size > f.n && !memcmp(result.bytes, f.bytes, f.n));
  assert(report.original_bytes == f.n && report.pages_updated == 2 &&
         report.original_revision_preserved);

  CGPDFDocumentRef doc = document(result.bytes, result.size);
  preserved(doc);
  compare_fresh(src, pages, doc);
  mixed_pages(&f, src);
  streamed_overlays();
  pixels(doc, 1);
  pixels(doc, 2);
  CGPDFDocumentRelease(doc);

  rmr_pdf_page selected[] = {pages[1], pages[0], pages[0]};
  rmr_buffer recomposed = {0};
  assert(rmr_apple_pdf_recompose(src, selected, 3, nullptr, nullptr, nullptr,
                                 &recomposed, &error) == RMR_OK);

  doc = document(recomposed.bytes, recomposed.size);
  assert(CGPDFDocumentGetNumberOfPages(doc) == 3);
  assert(CGPDFPageGetRotationAngle(CGPDFDocumentGetPage(doc, 1)) == 90);
  for (size_t i = 2; i <= 3; i++) {
    CGPDFArrayRef annotations;
    assert(CGPDFDictionaryGetArray(
        CGPDFPageGetDictionary(CGPDFDocumentGetPage(doc, i)), "Annots",
        &annotations));
    assert(CGPDFArrayGetCount(annotations) == 2);
    pixels(doc, i);
  }

  CGPDFDictionaryRef form, field;
  CGPDFArrayRef fields, widgets;
  assert(CGPDFDictionaryGetDictionary(CGPDFDocumentGetCatalog(doc), "AcroForm",
                                      &form));
  assert(CGPDFDictionaryGetArray(form, "Fields", &fields));
  assert(CGPDFArrayGetDictionary(fields, 0, &field));
  assert(CGPDFDictionaryGetArray(field, "Kids", &widgets));
  assert(CGPDFArrayGetCount(widgets) == 2);
  CGPDFDocumentRelease(doc);
  rmr_buffer_free(&recomposed);

  rmr_apple_pdf_source* again = nullptr;
  assert(rmr_apple_pdf_source_create(result.bytes, result.size, nullptr, &again,
                                     &error) == RMR_OK);
  rmr_buffer second = {0};
  assert(rmr_apple_pdf_incremental(again, pages, 2, nullptr, nullptr, nullptr,
                                   &second, nullptr, &error) == RMR_OK);
  assert(second.size > result.size &&
         !memcmp(second.bytes, result.bytes, result.size));
  doc = document(second.bytes, second.size);
  preserved(doc);
  pixels(doc, 1);
  CGPDFDocumentRelease(doc);
  rmr_buffer_free(&second);
  rmr_apple_pdf_source_free(again);

  pages[0].sheet.width = 240;
  pages[0].sheet.transform.tx = 120;
  rmr_buffer expanded = {0};
  assert(rmr_apple_pdf_incremental(src, pages, 2, nullptr, nullptr, nullptr,
                                   &expanded, nullptr, &error) == RMR_OK);
  doc = document(expanded.bytes, expanded.size);
  preserved(doc);
  CGRect expanded_box =
      CGPDFPageGetBoxRect(CGPDFDocumentGetPage(doc, 1), kCGPDFMediaBox);
  assert(fabs(expanded_box.origin.x + 20) < 0.001 &&
         fabs(expanded_box.size.width - 240) < 0.001);
  assert(CGPDFPageGetBoxRect(CGPDFDocumentGetPage(doc, 2), kCGPDFMediaBox)
             .size.width == 200);
  CGPDFArrayRef annots, rect;
  CGPDFDictionaryRef note;
  CGPDFReal x0;
  assert(CGPDFDictionaryGetArray(
      CGPDFPageGetDictionary(CGPDFDocumentGetPage(doc, 1)), "Annots", &annots));
  assert(CGPDFArrayGetDictionary(annots, 1, &note) &&
         CGPDFDictionaryGetArray(note, "Rect", &rect));
  assert(CGPDFArrayGetNumber(rect, 0, &x0) && x0 == 10);
  CGPDFDocumentRelease(doc);
  rmr_buffer_free(&expanded);

  pages[0].sheet.width = 200;
  pages[0].sheet.transform.tx = 100;
  rmr_buffer sentinel = {(uint8_t*)(uintptr_t)1, 123};
  rmr_pdf_incremental_report before = {99, 98, false};
  assert(rmr_apple_pdf_incremental(src, pages, 1, nullptr, nullptr, nullptr,
                                   &sentinel, &before,
                                   &error) == RMR_ERR_UNSUPPORTED);

  pages[0].source_visible = false;
  assert(rmr_apple_pdf_incremental(src, pages, 2, nullptr, nullptr, nullptr,
                                   &sentinel, &before,
                                   &error) == RMR_ERR_UNSUPPORTED);

  pages[0].source_visible = true;
  assert(rmr_apple_pdf_incremental(src, pages, 2, nullptr, cancelled, nullptr,
                                   &sentinel, &before,
                                   &error) == RMR_ERR_CANCELLED);

  rmr_limits limits = rmr_limits_default();
  limits.maximum_pdf_bytes = f.n;
  assert(rmr_apple_pdf_incremental(src, pages, 2, &limits, nullptr, nullptr,
                                   &sentinel, &before,
                                   &error) == RMR_ERR_LIMIT);

  assert(sentinel.bytes == (uint8_t*)(uintptr_t)1 && sentinel.size == 123 &&
         before.pages_updated == 99);

  fixture cert = source(true);
  assert(rmr_pdf_incremental((uint8_t*)cert.bytes, cert.n, nullptr, 0, nullptr,
                             nullptr, nullptr, &sentinel, nullptr,
                             &error) == RMR_ERR_UNSUPPORTED);

  rmr_buffer copy = {0};
  assert(rmr_pdf_incremental((uint8_t*)f.bytes, f.n, nullptr, 0, nullptr,
                             nullptr, nullptr, &copy, nullptr,
                             &error) == RMR_OK);
  assert(copy.size == f.n && !memcmp(copy.bytes, f.bytes, f.n));
  rmr_buffer_free(&copy);

  /* Every truncation must fail transactionally; mutation smoke under ASan. */
  for (size_t i = 1; i < f.n; i += 17) {
    rmr_status st =
        rmr_pdf_incremental((uint8_t*)f.bytes, i, nullptr, 0, nullptr, nullptr,
                            nullptr, &copy, nullptr, &error);
    if (st == RMR_OK) rmr_buffer_free(&copy);
  }

  for (size_t i = 0; i < f.n; i += 31) {
    char old = f.bytes[i];
    f.bytes[i] = '[';
    rmr_status st =
        rmr_pdf_incremental((uint8_t*)f.bytes, f.n, nullptr, 0, nullptr,
                            nullptr, nullptr, &copy, nullptr, &error);
    if (st == RMR_OK) rmr_buffer_free(&copy);
    f.bytes[i] = old;
  }

  rmr_buffer_free(&result);
  rmr_apple_pdf_source_free(src);
  rmr_plan_free(p);
  puts("incremental PDF preservation passed");
  return 0;
}
