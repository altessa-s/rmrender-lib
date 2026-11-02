/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
/* Hostile source PDFs for rmr_pdf_incremental: resource bounds, xref
 * precedence, object streams and byte-exact copying. Run under ASan/UBSan too. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rmr_internal.h"
#include <assert.h>
#include <rmrender/pdf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

typedef struct {
    char *b;
    size_t n, cap;
    size_t off[8192];
    unsigned gen[8192];
} buf;
static void addv(buf *f, const char *fmt, va_list a) {
    va_list c;
    va_copy(c, a);
    int n = vsnprintf(nullptr, 0, fmt, c);
    va_end(c);
    assert(n >= 0);
    if (f->n + (size_t)n + 1 > f->cap) {
        size_t cap = f->cap ? f->cap : 65536;
        while (cap < f->n + (size_t)n + 1)
            cap *= 2;
        f->b = realloc(f->b, cap);
        assert(f->b);
        f->cap = cap;
    }
    vsnprintf(f->b + f->n, (size_t)n + 1, fmt, a);
    f->n += (size_t)n;
}
static void add(buf *f, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    addv(f, fmt, a);
    va_end(a);
}
static void raw(buf *f, const void *p, size_t n) {
    if (f->n + n + 1 > f->cap) {
        size_t cap = f->cap ? f->cap : 65536;
        while (cap < f->n + n + 1)
            cap *= 2;
        f->b = realloc(f->b, cap);
        assert(f->b);
        f->cap = cap;
    }
    memcpy(f->b + f->n, p, n);
    f->n += n;
}
static void obj(buf *f, unsigned id, const char *fmt, ...) {
    f->off[id] = f->n;
    add(f, "%u 0 obj\n", id);
    va_list a;
    va_start(a, fmt);
    addv(f, fmt, a);
    va_end(a);
    add(f, "\nendobj\n");
}
static void xref(buf *f, unsigned count, const char *extra) {
    size_t x = f->n;
    add(f, "xref\n0 %u\n0000000000 65535 f \n", count);
    for (unsigned i = 1; i < count; i++)
        add(f, "%010zu %05u %c \n", f->off[i], f->gen[i], f->off[i] ? 'n' : 'f');
    add(f, "trailer\n<< /Size %u /Root 1 0 R %s >>\nstartxref\n%zu\n%%%%EOF\n", count, extra, x);
}
/* Minimal trusted overlay (single page, one content stream). */
static buf overlay_pdf(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /Resources << >> /Contents 4 0 R >>");
    f.off[4] = f.n;
    add(&f, "4 0 obj\n<< /Length 4 >>\nstream\n0 g\n\nendstream\nendobj\n");
    xref(&f, 5, "");
    return f;
}
static const char *name(rmr_status s) {
    switch (s) {
    case RMR_OK:
        return "OK";
    case RMR_ERR_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case RMR_ERR_LIMIT:
        return "LIMIT";
    case RMR_ERR_UNSUPPORTED:
        return "UNSUPPORTED";
    case RMR_ERR_OUT_OF_MEMORY:
        return "OUT_OF_MEMORY";
    default:
        return "other";
    }
}
static double now(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
/* Whether the n bytes at hay contain the len bytes of needle. */
static bool found(const void *hay, size_t n, const char *needle, size_t len) {
    const unsigned char *h = hay;
    for (size_t i = 0; len <= n && i <= n - len; i++)
        if (!memcmp(h + i, needle, len))
            return true;
    return false;
}
static rmr_status run(buf *f, size_t page, rmr_limits *l, rmr_buffer *out, rmr_error *e,
                      bool replace_box) {
    static buf ov;
    if (!ov.n)
        ov = overlay_pdf();
    rmr_pdf_overlay o = {.page = page,
                         .pdf = (uint8_t *)ov.b,
                         .size = ov.n,
                         .transform = {1, 0, 0, 1, 0, 0},
                         .replace_box = replace_box,
                         .box = {0, 0, 300, 300}};
    *out = (rmr_buffer){0};
    return rmr_pdf_incremental((uint8_t *)f->b, f->n, &o, 1, l, nullptr, nullptr, out, nullptr, e);
}
static void report(const char *probe, rmr_status s, rmr_error *e, double secs) {
    printf("%s: %s (%s) %.3fs\n", probe, name(s), s == RMR_OK ? "" : e->message, secs);
}

/* C1: indirect /Length chains and deep nesting must not overflow the stack. */
static void hostile_c1(void) {
    enum { CHAIN = 6000 };
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>");
    /* Stream k's /Length is stream k+1: resolving one length requires the next. */
    unsigned last = 4 + CHAIN;
    for (unsigned k = 4; k < last; k++) {
        f.off[k] = f.n;
        add(&f,
            "%u 0 obj\n<< /N "
            "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]"
            "]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]] /Length %u 0 R >>\nstream\nx\nend"
            "stream\nendobj\n",
            k, k + 1);
    }
    f.off[last] = f.n;
    add(&f, "%u 0 obj\n1\nendobj\n", last);
    xref(&f, last + 1, "");
    rmr_buffer out;
    rmr_error e = {0};
    double t = now();
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    report("C1 length-chain x nesting", s, &e, now() - t);
    assert(s == RMR_ERR_LIMIT);
    rmr_buffer_free(&out);
    /* Deep array nesting in the page dict itself. */
    buf g = {0};
    add(&g, "%%PDF-1.7\n");
    obj(&g, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&g, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    g.off[3] = g.n;
    add(&g, "3 0 obj\n<< /Type /Page /MediaBox [0 0 200 200] /X ");
    for (int i = 0; i < 200000; i++)
        add(&g, "[");
    add(&g, "\nendobj\n");
    xref(&g, 4, "");
    t = now();
    s = run(&g, 0, nullptr, &out, &e, false);
    report("C1 200k nested arrays", s, &e, now() - t);
    assert(s == RMR_ERR_LIMIT);
    rmr_buffer_free(&out);
    free(f.b);
    free(g.b);
}

/* H1: N page objects packed in one ObjStm with a large decoded body. The
 * container must be inflated once per file, not once per packed object. */
static rmr_status hostile_h1(unsigned pages, size_t padding) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    buf kids = {0};
    for (unsigned i = 0; i < pages; i++)
        add(&kids, "%u 0 R ", 10 + i);
    obj(&f, 2, "<< /Type /Pages /Kids [%s] /Count %u >>", kids.b, pages);
    /* ObjStm payload: header, then objects; padding placed after the last object
     * as whitespace inside the final object's slice (valid). */
    buf hdr = {0}, body = {0};
    for (unsigned i = 0; i < pages; i++) {
        add(&hdr, "%u %zu ", 10 + i, body.n);
        add(&body, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>\n");
    }
    char *pad = malloc(padding);
    memset(pad, ' ', padding);
    raw(&body, pad, padding);
    free(pad);
    buf payload = {0};
    raw(&payload, hdr.b, hdr.n);
    raw(&payload, body.b, body.n);
    uLongf zn = compressBound((uLong)payload.n);
    uint8_t *z = malloc(zn);
    assert(compress2(z, &zn, (uint8_t *)payload.b, (uLong)payload.n, 9) == Z_OK);
    f.off[3] = f.n;
    add(&f,
        "3 0 obj\n<< /Type /ObjStm /N %u /First %zu /Filter /FlateDecode /Length %lu >>\nstream\n",
        pages, hdr.n, (unsigned long)zn);
    raw(&f, z, zn);
    add(&f, "\nendstream\nendobj\n");
    free(z);
    /* xref stream: objects 0..(10+pages-1). */
    unsigned size = 10 + pages + 1;
    unsigned self = 10 + pages;
    size_t xs = f.n;
    buf rows = {0};
    for (unsigned i = 0; i < size; i++) {
        uint8_t r[7] = {0};
        if (i == 1 || i == 2 || i == 3 || i == self) {
            size_t at = i == self ? xs : f.off[i];
            r[0] = 1;
            r[1] = (uint8_t)(at >> 24);
            r[2] = (uint8_t)(at >> 16);
            r[3] = (uint8_t)(at >> 8);
            r[4] = (uint8_t)at;
        } else if (i >= 10 && i < self) {
            r[0] = 2;
            r[4] = 3;
            r[5] = (uint8_t)((i - 10) >> 8);
            r[6] = (uint8_t)(i - 10);
        }
        raw(&rows, r, 7);
    }
    add(&f, "%u 0 obj\n<< /Type /XRef /Root 1 0 R /Size %u /W [1 4 2] /Length %zu >>\nstream\n",
        self, size, rows.n);
    raw(&f, rows.b, rows.n);
    add(&f, "\nendstream\nendobj\nstartxref\n%zu\n%%%%EOF\n", xs);
    rmr_limits l = rmr_limits_default();
    rmr_buffer out;
    rmr_error e = {0};
    double t = now();
    rmr_status s = run(&f, 0, &l, &out, &e, false);
    char label[160];
    snprintf(label, sizeof label, "H1 ObjStm %u pages, decoded %zu KiB, file %zu KiB, plan %zu MiB",
             pages, payload.n / 1024, f.n / 1024, l.maximum_plan_bytes >> 20);
    double elapsed = now() - t;
    report(label, s, &e, elapsed);
    assert(s != RMR_OK || elapsed < 10);
    rmr_buffer_free(&out);
    free(f.b);
    free(kids.b);
    free(hdr.b);
    free(body.b);
    free(payload.b);
    free(rows.b);
    return s;
}

/* H2: hybrid file. Classic table (authoritative per ISO 32000 7.5.8.4) maps
 * page 3 to the visible page; the /XRefStm companion maps 3 to a shadow body. */
static void hostile_h2(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Visible true >>");
    size_t real3 = f.off[3];
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 999 999] /Shadow true >>");
    size_t shadow3 = f.off[3];
    f.off[3] = real3;
    /* XRefStm: one row, object 3 -> shadow. */
    size_t xs = f.n;
    uint8_t row[7] = {1,
                      (uint8_t)(shadow3 >> 24),
                      (uint8_t)(shadow3 >> 16),
                      (uint8_t)(shadow3 >> 8),
                      (uint8_t)shadow3,
                      0,
                      0};
    add(&f, "4 0 obj\n<< /Type /XRef /Size 5 /W [1 4 2] /Index [3 1] /Length 7 >>\nstream\n");
    raw(&f, row, 7);
    add(&f, "\nendstream\nendobj\n");
    f.off[4] = xs;
    char extra[64];
    snprintf(extra, sizeof extra, "/XRefStm %zu", xs);
    xref(&f, 5, extra);
    rmr_buffer out;
    rmr_error e = {0};
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    report("H2 hybrid table-vs-XRefStm", s, &e, 0);
    if (s == RMR_OK) {
        const char *tail = (const char *)out.bytes + f.n;
        size_t n = out.size - f.n;
        bool shadow = found(tail, n, "/Shadow", 7);
        bool visible = found(tail, n, "/Visible", 8);
        assert(!shadow && visible);
    }
    assert(s == RMR_OK);
    rmr_buffer_free(&out);
    free(f.b);
}

/* M1: many /Prev revisions re-listing the same rows. */
static rmr_status hostile_m1(unsigned revisions, unsigned objects) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    size_t prev = 0;
    for (unsigned r = 0; r < revisions; r++) {
        size_t x = f.n;
        add(&f, "xref\n0 %u\n0000000000 65535 f \n", objects);
        for (unsigned i = 1; i < objects; i++)
            add(&f, "%010zu 00000 %c \n", i < 4 ? f.off[i] : (size_t)0, i < 4 ? 'n' : 'f');
        if (r)
            add(&f, "trailer\n<< /Size %u /Root 1 0 R /Prev %zu >>\n", objects, prev);
        else
            add(&f, "trailer\n<< /Size %u /Root 1 0 R >>\n", objects);
        add(&f, "startxref\n%zu\n%%%%EOF\n", x);
        prev = x;
    }
    rmr_buffer out;
    rmr_error e = {0};
    double t = now();
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    char label[128];
    snprintf(label, sizeof label, "M1 %u revisions x %u rows (file %zu KiB)", revisions, objects,
             f.n / 1024);
    double elapsed = now() - t;
    report(label, s, &e, elapsed);
    assert(s != RMR_OK || elapsed < 10);
    rmr_buffer_free(&out);
    free(f.b);
    return s;
}

/* M3: a name containing a non-UTF-8 escape must round-trip byte-exactly. */
static void hostile_m3(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /XObject << /Caf#e9 "
        "5 0 R >> >> /Tag /Caf#e9#20x /Str (\\351\\)) >>");
    f.off[5] = f.n;
    add(&f, "5 0 obj\n<< /Type /XObject /Subtype /Form /BBox [0 0 1 1] /Length 0 >>\nstream\n\nend"
            "stream\nendobj\n");
    f.off[4] = 0;
    xref(&f, 6, "");
    rmr_buffer out;
    rmr_error e = {0};
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    report("M3 non-UTF-8 names", s, &e, 0);
    if (s == RMR_OK) {
        const char *tail = (const char *)out.bytes + f.n;
        size_t n = out.size - f.n;
        assert(found(tail, n, "/Caf#e9 ", 8) && found(tail, n, "/Caf#e9#20x", 11) &&
               found(tail, n, "(\\351\\))", 8));
    }
    assert(s == RMR_OK);
    rmr_buffer_free(&out);
    free(f.b);
}

/* M4: unfiltered ObjStm (raw slice). */
static void hostile_m4(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [10 0 R] /Count 1 >>");
    const char *pg = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Packed true >>";
    char hdr[32];
    snprintf(hdr, sizeof hdr, "10 0 ");
    f.off[3] = f.n;
    add(&f,
        "3 0 obj\n<< /Type /ObjStm /N 1 /First %zu /Length %zu "
        ">>\nstream\n%s%s\nendstream\nendobj\n",
        strlen(hdr), strlen(hdr) + strlen(pg), hdr, pg);
    size_t xs = f.n;
    buf rows = {0};
    for (unsigned i = 0; i < 12; i++) {
        uint8_t r[7] = {0};
        if (i >= 1 && i <= 3) {
            size_t at = f.off[i];
            r[0] = 1, r[3] = (uint8_t)(at >> 8), r[4] = (uint8_t)at;
        } else if (i == 10)
            r[0] = 2, r[4] = 3;
        else if (i == 11)
            r[0] = 1, r[1] = (uint8_t)(xs >> 24), r[2] = (uint8_t)(xs >> 16),
            r[3] = (uint8_t)(xs >> 8), r[4] = (uint8_t)xs;
        raw(&rows, r, 7);
    }
    add(&f, "11 0 obj\n<< /Type /XRef /Root 1 0 R /Size 12 /W [1 4 2] /Length %zu >>\nstream\n",
        rows.n);
    raw(&f, rows.b, rows.n);
    add(&f, "\nendstream\nendobj\nstartxref\n%zu\n%%%%EOF\n", xs);
    rmr_buffer out;
    rmr_error e = {0};
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    report("M4 unfiltered ObjStm", s, &e, 0);
    if (s == RMR_OK)
        assert(found(out.bytes + f.n, out.size - f.n, "/Packed true", 12));
    assert(s == RMR_OK);
    rmr_buffer_free(&out);
    free(f.b);
    free(rows.b);
}

/* L-trailer: stream-only keys of a source XRef dictionary (/F, /DL, /FDecodeParms)
 * are copied into the newly written XRef stream dictionary. */
static void hostile_trailer_keys(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    size_t xs = f.n;
    f.off[4] = xs;
    buf rows = {0};
    for (unsigned i = 0; i < 5; i++) {
        uint8_t r[7] = {0};
        if (i) {
            size_t at = f.off[i];
            r[0] = 1, r[1] = (uint8_t)(at >> 24), r[2] = (uint8_t)(at >> 16),
            r[3] = (uint8_t)(at >> 8), r[4] = (uint8_t)at;
        }
        raw(&rows, r, 7);
    }
    add(&f,
        "4 0 obj\n<< /Type /XRef /Root 1 0 R /Size 5 /W [1 4 2] /DL 35 /F (evil.bin) /Length %zu "
        ">>\nstream\n",
        rows.n);
    raw(&f, rows.b, rows.n);
    add(&f, "\nendstream\nendobj\nstartxref\n%zu\n%%%%EOF\n", xs);
    rmr_buffer out;
    rmr_error e = {0};
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    report("L trailer stream-key carry", s, &e, 0);
    if (s == RMR_OK)
        assert(!found(out.bytes + f.n, out.size - f.n, "/F (", 4) &&
               !found(out.bytes + f.n, out.size - f.n, "/DL", 3) &&
               found(out.bytes + f.n, out.size - f.n, "/Root 1 0 R", 11));
    assert(s == RMR_OK);
    rmr_buffer_free(&out);
    free(f.b);
    free(rows.b);
}

/* Huge declared /Size and object numbers near UINT32_MAX for the writer. */
static void hostile_numbers(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    size_t x = f.n;
    add(&f, "xref\n0 4\n0000000000 65535 f \n");
    for (unsigned i = 1; i < 4; i++)
        add(&f, "%010zu 00000 n \n", f.off[i]);
    add(&f, "4294967290 3\n0000000000 00000 f \n0000000000 00000 f \n0000000000 00000 f \n");
    add(&f, "trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", x);
    rmr_buffer out;
    rmr_error e = {0};
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    report("L object numbers near UINT32_MAX", s, &e, 0);
    assert(s == RMR_ERR_LIMIT);
    rmr_buffer_free(&out);
    free(f.b);
}

/* Predictor: hostile /Columns with tiny data (no allocation must follow /Columns). */
static void hostile_predictor(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    uint8_t rawrow[8] = {2, 1, 0, 0, 0, 0, 0, 0};
    uint8_t z[64];
    uLongf zn = sizeof z;
    assert(compress(z, &zn, rawrow, sizeof rawrow) == Z_OK);
    size_t xs = f.n;
    add(&f,
        "4 0 obj\n<< /Type /XRef /Root 1 0 R /Size 5 /W [1 4 2] /Filter /FlateDecode /DecodeParms "
        "<< /Predictor 12 /Columns 67108863 /Colors 1 >> /Length %lu >>\nstream\n",
        (unsigned long)zn);
    raw(&f, z, zn);
    add(&f, "\nendstream\nendobj\nstartxref\n%zu\n%%%%EOF\n", xs);
    rmr_buffer out;
    rmr_error e = {0};
    double t = now();
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    report("L4 predictor hostile /Columns", s, &e, now() - t);
    assert(s == RMR_ERR_INVALID_ARGUMENT);
    rmr_buffer_free(&out);
    free(f.b);
}

static buf simple_source(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    xref(&f, 4, "");
    return f;
}
/* A row listed twice inside one classic section stays invalid. */
static void hostile_duplicate_row(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    size_t x = f.n;
    add(&f, "xref\n0 4\n0000000000 65535 f \n");
    for (unsigned i = 1; i < 4; i++)
        add(&f, "%010zu 00000 n \n", f.off[i]);
    add(&f, "3 1\n%010zu 00000 n \n", f.off[3]);
    add(&f, "trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", x);
    rmr_buffer out;
    rmr_error e = {0};
    rmr_status s = run(&f, 0, nullptr, &out, &e, false);
    report("duplicate row in one section", s, &e, 0);
    assert(s == RMR_ERR_INVALID_ARGUMENT);
    free(f.b);
}
/* Overlay objects packed in an object stream are imported; the container and
 * the xref stream are not copied. */
static void hostile_packed_overlay(void) {
    buf ov = {0};
    add(&ov, "%%PDF-1.7\n");
    obj(&ov, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&ov, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>");
    ov.off[4] = ov.n;
    add(&ov, "4 0 obj\n<< /Length 13 >>\nstream\n/G0 gs 0 0 m\nendstream\nendobj\n");
    const char *page = "<< /Type /Page /Parent 2 0 R /Resources 6 0 R /Contents 4 0 R >>";
    const char *res = "<< /ExtGState << /G0 << /ca 0.5 >> >> >>";
    char hdr[32], body[256];
    snprintf(hdr, sizeof hdr, "3 0 6 %zu ", strlen(page) + 1);
    snprintf(body, sizeof body, "%s%s %s", hdr, page, res);
    ov.off[5] = ov.n;
    add(&ov,
        "5 0 obj\n<< /Type /ObjStm /N 2 /First %zu /Length %zu >>\nstream\n%s\nendstream\nendobj\n",
        strlen(hdr), strlen(body), body);
    size_t xs = ov.n;
    ov.off[7] = xs;
    buf rows = {0};
    for (unsigned i = 0; i < 8; i++) {
        uint8_t r[7] = {0};
        if (i == 3 || i == 6)
            r[0] = 2, r[4] = 5, r[6] = i == 6;
        else if (i) {
            size_t at = ov.off[i];
            r[0] = 1, r[1] = (uint8_t)(at >> 24), r[2] = (uint8_t)(at >> 16),
            r[3] = (uint8_t)(at >> 8), r[4] = (uint8_t)at;
        }
        raw(&rows, r, 7);
    }
    add(&ov, "7 0 obj\n<< /Type /XRef /Root 1 0 R /Size 8 /W [1 4 2] /Length %zu >>\nstream\n",
        rows.n);
    raw(&ov, rows.b, rows.n);
    add(&ov, "\nendstream\nendobj\nstartxref\n%zu\n%%%%EOF\n", xs);
    buf f = simple_source();
    rmr_pdf_overlay o = {.pdf = (uint8_t *)ov.b, .size = ov.n, .transform = {1, 0, 0, 1, 0, 0}};
    rmr_buffer out = {0};
    rmr_error e = {0};
    rmr_status s = rmr_pdf_incremental((uint8_t *)f.b, f.n, &o, 1, nullptr, nullptr, nullptr, &out,
                                       nullptr, &e);
    report("packed overlay objects", s, &e, 0);
    assert(s == RMR_OK);
    const char *tail = (const char *)out.bytes + f.n;
    size_t n = out.size - f.n;
    assert(found(tail, n, "/ca 0.5", 7) && !found(tail, n, "/ObjStm", 7) &&
           !found(tail, n, "/Type /XRef", 11));
    rmr_buffer_free(&out);
    free(f.b);
    free(ov.b);
    free(rows.b);
}
/* rmr_pdf_pages reports what the updater parses: count, inherited box, rotation. */
static void hostile_pages(void) {
    buf f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 1 /MediaBox [0 0 200 300] >>");
    obj(&f, 3, "<< /Type /Page /Parent 2 0 R /Rotate -90 >>");
    obj(&f, 4, "<< /Type /Page /Parent 2 0 R /MediaBox [50 60 10 20] >>");
    xref(&f, 5, "");
    rmr_pdf_page_geometry g[2];
    size_t count = 0;
    assert(rmr_pdf_pages((uint8_t *)f.b, f.n, nullptr, g, 1, &count, nullptr) == RMR_OK);
    assert(count == 2 && g[0].has_media && g[0].media.width == 200 && g[0].rotate == 270);
    assert(rmr_pdf_pages((uint8_t *)f.b, f.n, nullptr, g, 2, &count, nullptr) == RMR_OK);
    assert(g[1].media.x == 10 && g[1].media.y == 20 && g[1].media.width == 40 && g[1].rotate == 0);
    free(f.b);
}
int main(void) {
    hostile_c1();
    /* Object streams decode once per file: 64 pages packed with ~2 MiB of
     * decoded data, and 2000 small packed pages, both update. */
    assert(hostile_h1(64, 2u << 20) == RMR_OK);
    assert(hostile_h1(2000, 0) == RMR_OK);
    /* A single stream decoding beyond 64 MiB is refused. */
    assert(hostile_h1(1, (size_t)65 << 20) == RMR_ERR_LIMIT);
    hostile_h2();
    /* 50 saves re-listing 5000 objects: duplicate rows resolve in O(n log n). */
    assert(hostile_m1(50, 5000) == RMR_OK);
    assert(hostile_m1(200, 2000) == RMR_OK);
    /* Total rows remain capped by the work limit. */
    assert(hostile_m1(256, 10000) == RMR_ERR_LIMIT);
    hostile_m3();
    hostile_m4();
    hostile_trailer_keys();
    hostile_numbers();
    hostile_predictor();
    hostile_duplicate_row();
    hostile_packed_overlay();
    hostile_pages();
    return 0;
}
