/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* CoreGraphics supplies page sizes and transforms while the portable writer
 * edits the page tree it parses: both must agree, or the update is refused. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <CoreGraphics/CoreGraphics.h>
#include <assert.h>
#include <rmrender/apple_pdf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char b[16384];
    size_t n, off[16];
} buf;

static void add(buf *f, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(f->b + f->n, sizeof f->b - f->n, fmt, a);
    va_end(a);
    assert(n >= 0 && (size_t)n < sizeof f->b - f->n);
    f->n += (size_t)n;
}

static void raw(buf *f, const void *p, size_t n) {
    assert(f->n + n < sizeof f->b);
    memcpy(f->b + f->n, p, n);
    f->n += n;
}

static void obj(buf *f, unsigned id, const char *body) {
    f->off[id] = f->n;
    add(f, "%u 0 obj\n%s\nendobj\n", id, body);
}

static void xref(buf *f, unsigned count, const char *extra) {
    size_t x = f->n;
    add(f, "xref\n0 %u\n0000000000 65535 f \n", count);
    for (unsigned i = 1; i < count; i++)
        add(f, "%010zu 00000 %c \n", f->off[i], f->off[i] ? 'n' : 'f');
    add(f, "trailer\n<< /Size %u /Root 1 0 R %s >>\nstartxref\n%zu\n%%%%EOF\n", count, extra, x);
}

static CGPDFDocumentRef doc(const void *bytes, size_t n) {
    CFDataRef d = CFDataCreate(nullptr, bytes, (CFIndex)n);
    CGDataProviderRef p = CGDataProviderCreateWithCFData(d);
    CGPDFDocumentRef r = CGPDFDocumentCreateWithProvider(p);
    CGDataProviderRelease(p);
    CFRelease(d);
    return r;
}

typedef struct {
    size_t pages;
    CGRect media;
    bool shadow;
} view;

/* What CoreGraphics shows for the first page. */
static view describe(const void *bytes, size_t n) {
    CGPDFDocumentRef d = doc(bytes, n);
    assert(d);
    view v = {.pages = CGPDFDocumentGetNumberOfPages(d)};
    CGPDFPageRef p = CGPDFDocumentGetPage(d, 1);
    v.media = CGPDFPageGetBoxRect(p, kCGPDFMediaBox);
    CGPDFBoolean shadow = false;
    CGPDFDictionaryGetBoolean(CGPDFPageGetDictionary(p), "Shadow", &shadow);
    v.shadow = shadow;
    CGPDFDocumentRelease(d);
    return v;
}

static rmr_plan *mark(void) {
    rmr_element path[] = {{.kind = RMR_MOVE, .end = {-50, 50}},
                          {.kind = RMR_LINE, .end = {50, 50}},
                          {.kind = RMR_LINE, .end = {50, 150}},
                          {.kind = RMR_CLOSE}};
    rmr_draw draw = {.path = {path, 4}, .mode = RMR_FILL, .color = {1, 0, 0, 1}};
    rmr_plan *p = nullptr;
    assert(rmr_plan_create(&draw, 1, (rmr_rect){-100, 0, 200, 200}, nullptr, &p, nullptr) ==
           RMR_OK);
    return p;
}

/* End to end through the public Apple API used by rmr_apple_document_pdf_preserving. */
static rmr_status incremental(const void *bytes, size_t n, rmr_buffer *out, rmr_error *e) {
    rmr_apple_pdf_source *src = nullptr;
    assert(rmr_apple_pdf_source_create(bytes, n, nullptr, &src, nullptr) == RMR_OK);
    size_t count = rmr_apple_pdf_source_count(src);
    assert(count == 1);

    rmr_plan *plan = mark();
    rmr_point size = {0};
    assert(rmr_apple_pdf_source_size(src, 0, &size, nullptr) == RMR_OK);

    rmr_pdf_page page = {
        .marks = plan,
        .sheet = {.width = size.x, .height = size.y, .transform = {1, 0, 0, -1, 100, size.y}},
        .source_rect = {-100, 0, 200, 200},
        .has_source = true,
        .source_visible = true};

    rmr_status s =
        rmr_apple_pdf_incremental(src, &page, 1, nullptr, nullptr, nullptr, out, nullptr, e);

    rmr_plan_free(plan);
    rmr_apple_pdf_source_free(src);
    return s;
}

int main(void) {
    /* Hybrid file: the classic table maps object 3 to the visible page, the
     * /XRefStm companion to a shadow page. The table is authoritative. */
    buf h = {0};
    add(&h, "%%PDF-1.7\n");
    obj(&h, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&h, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&h, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Visible true >>");
    size_t real3 = h.off[3];
    obj(&h, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 999 999] /Shadow true >>");
    size_t shadow3 = h.off[3];
    h.off[3] = real3;
    size_t xs = h.n;
    uint8_t row[7] = {1,
                      (uint8_t)(shadow3 >> 24),
                      (uint8_t)(shadow3 >> 16),
                      (uint8_t)(shadow3 >> 8),
                      (uint8_t)shadow3,
                      0,
                      0};
    add(&h, "4 0 obj\n<< /Type /XRef /Size 5 /W [1 4 2] /Index [3 1] /Length 7 >>\nstream\n");
    raw(&h, row, 7);
    add(&h, "\nendstream\nendobj\n");
    h.off[4] = xs;
    char extra[64];
    snprintf(extra, sizeof extra, "/XRefStm %zu", xs);
    xref(&h, 5, extra);

    rmr_buffer out = {0};
    rmr_error e = {0};
    view before = describe(h.b, h.n);
    assert(before.pages == 1 && !before.shadow && before.media.size.width == 200 &&
           before.media.size.height == 200);
    assert(incremental(h.b, h.n, &out, &e) == RMR_OK);
    view after = describe(out.bytes, out.size);
    assert(after.pages == 1 && !after.shadow && after.media.size.width == 200 &&
           after.media.size.height == 200);
    assert(!memmem(out.bytes + h.n, out.size - h.n, "/Shadow", 7));
    rmr_buffer_free(&out);

    /* /Count disagrees with /Kids: CoreGraphics and the writer see different
     * page trees, so the update is refused rather than applied to a guess. */
    buf c = {0};
    add(&c, "%%PDF-1.7\n");
    obj(&c, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&c, 2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 1 >>");
    obj(&c, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    obj(&c, 4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 999 999] >>");
    xref(&c, 5, "");

    out = (rmr_buffer){(uint8_t *)(uintptr_t)1, 7};
    rmr_status s = incremental(c.b, c.n, &out, &e);
    assert(s == RMR_ERR_UNSUPPORTED && strstr(e.message, "page count"));
    assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 7);

    /* An empty intermediate /Pages node is the same tree for both readers. */
    buf t = {0};
    add(&t, "%%PDF-1.7\n");
    obj(&t, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&t, 2, "<< /Type /Pages /Kids [4 0 R 3 0 R] /Count 1 >>");
    obj(&t, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    obj(&t, 4, "<< /Type /Pages /Kids [] /Count 0 >>");
    xref(&t, 5, "");
    out = (rmr_buffer){0};
    assert(incremental(t.b, t.n, &out, &e) == RMR_OK);
    rmr_buffer_free(&out);
    return 0;
}
