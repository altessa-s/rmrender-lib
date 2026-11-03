/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "pdf_fixture.h"
#include "rmr_internal.h"
#include <rmrender/pdf.h>
#include <zlib.h>

static fixture overlay_fixture(void) {
    fixture f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, 0, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, 0, "<< /Type /Pages /Kids [3 4 R] /Count 1 /MediaBox [0 0 200 200] >>");
    obj(&f, 3, 4, "<< /Type /Page /Parent 2 0 R /Resources << >> /Contents 4 0 R >>");
    f.offsets[4] = f.n;
    const char *paint = "1 0 0 rg 50 50 100 100 re f\n";
    add(&f, "4 0 obj\n<< /Length 5 0 R >>\nstream\n%s\nendstream\nendobj\n", paint);
    char length[30];
    snprintf(length, sizeof length, "%zu", strlen(paint));
    obj(&f, 5, 0, length);
    size_t x = f.n;
    add(&f, "xref\n0 6\n0000000000 65535 f \n");
    for (unsigned i = 1; i < 6; i++)
        add(&f, "%010zu %05u n \n", f.offsets[i], f.gen[i]);
    add(&f, "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", x);
    return f;
}

static size_t lastxref(fixture *f) {
    char *last = nullptr, *p = f->bytes;
    while ((p = strstr(p, "startxref"))) {
        last = p;
        p++;
    }
    assert(last);
    return (size_t)strtoull(last + 9, nullptr, 10);
}

static void revision(fixture *f, const char *rows, const char *extra) {
    size_t prev = lastxref(f), offset = f->n;
    add(f, "xref\n%s\ntrailer\n<< /Size 17 /Prev %zu %s >>\nstartxref\n%zu\n%%%%EOF\n", rows, prev,
        extra, offset);
}

static rmr_status run(fixture *f, rmr_pdf_overlay *o, rmr_limits *limits, rmr_buffer *out) {
    return rmr_pdf_incremental((uint8_t *)f->bytes, f->n, o, o ? 1 : 0, limits, nullptr, nullptr,
                               out, nullptr, nullptr);
}

static fixture stream_fixture(bool packed, bool compressed, bool predictor, bool hybrid, bool bad) {
    fixture f = {0};
    add(&f, "%%PDF-1.7\n");
    const char *objects[] = {
        "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> >>"};
    if (packed) {
        char header[128], payload[512];
        snprintf(header, sizeof header, "1 0 2 %zu 3 %zu ", strlen(objects[0]) + 1,
                 strlen(objects[0]) + strlen(objects[1]) + 2);
        snprintf(payload, sizeof payload, "%s%s %s %s ", header, objects[0], objects[1],
                 objects[2]);
        f.offsets[5] = f.n;
        add(&f,
            "5 0 obj\n<< /Type /ObjStm /N 3 /First %zu /Length %zu "
            ">>\nstream\n%s\nendstream\nendobj\n",
            strlen(header), strlen(payload), payload);
    } else {
        for (unsigned i = 0; i < 3; i++)
            obj(&f, i + 1, 0, objects[i]);
    }
    f.offsets[6] = f.n;
    uint8_t raw[56] = {0}, encoded[256];
    size_t n = 0;
    uint8_t previous[7] = {0};
    for (unsigned i = 0; i < 7; i++) {
        bool inside = packed && i >= 1 && i <= 3;
        uint32_t at = inside ? 5 : (uint32_t)f.offsets[i];
        unsigned extra = inside ? (bad ? 99 : i - 1) : i == 0 ? 65535 : 0;
        uint8_t row[7] = {(uint8_t)(inside ? 2
                                    : at   ? 1
                                           : 0),
                          (uint8_t)(at >> 24),
                          (uint8_t)(at >> 16),
                          (uint8_t)(at >> 8),
                          (uint8_t)at,
                          (uint8_t)(extra >> 8),
                          (uint8_t)extra};
        if (predictor)
            raw[n++] = 2;
        for (unsigned k = 0; k < 7; k++)
            raw[n++] = predictor ? (uint8_t)(row[k] - previous[k]) : row[k];
        memcpy(previous, row, 7);
    }
    const uint8_t *data = raw;
    if (compressed) {
        uLongf size = sizeof encoded;
        assert(compress(encoded, &size, raw, (uLong)n) == Z_OK);
        data = encoded;
        n = (size_t)size;
    }
    add(&f, "6 0 obj\n<< /Type /XRef /Root 1 0 R /Size 7 /W [1 4 2] /Length %zu %s %s >>\nstream\n",
        n, compressed ? "/Filter /FlateDecode" : "",
        predictor ? "/DecodeParms << /Predictor 12 /Columns 7 >>" : "");
    assert(f.n + n < sizeof f.bytes);
    memcpy(f.bytes + f.n, data, n);
    f.n += n;
    add(&f, "\nendstream\nendobj\n");
    size_t start = f.offsets[6];
    if (hybrid) {
        start = f.n;
        add(&f, "xref\n0 7\n0000000000 65535 f \n");
        for (unsigned i = 1; i < 7; i++)
            add(&f, "%010zu 00000 %c \n", f.offsets[i], f.offsets[i] ? 'n' : 'f');
        add(&f, "trailer\n<< /Root 1 0 R /Size 7 /XRefStm %zu >>\n", f.offsets[6]);
    }
    add(&f, "startxref\n%zu\n%%%%EOF\n", start);
    return f;
}

static bool cancel_rewrite(void *context) {
    (void)context;
    return true;
}

static void rewrite_check(const uint8_t *bytes, size_t size, size_t expected_pages) {
    rmr_buffer fresh = {0};
    assert(rmr_pdf_rewrite(bytes, size, nullptr, nullptr, nullptr, &fresh, nullptr) == RMR_OK);
    size_t count = 0;
    assert(rmr_pdf_pages(fresh.bytes, fresh.size, nullptr, nullptr, 0, &count, nullptr) == RMR_OK);
    assert(count == expected_pages);
    /* A subsequent append must resolve the newly emitted xref independently. */
    fixture ov = overlay_fixture();
    rmr_pdf_overlay o = {.pdf = (uint8_t *)ov.bytes, .size = ov.n, .transform = {1, 0, 0, 1, 0, 0}};
    rmr_buffer next = {0};
    assert(rmr_pdf_incremental(fresh.bytes, fresh.size, &o, 1, nullptr, nullptr, nullptr, &next,
                               nullptr, nullptr) == RMR_OK);
    rmr_buffer_free(&next);
    rmr_buffer_free(&fresh);
}

static void rewrite_failures(fixture *f) {
    rmr_buffer out = {(uint8_t *)(uintptr_t)1, 19};
    assert(rmr_pdf_rewrite((uint8_t *)f->bytes, f->n, nullptr, cancel_rewrite, nullptr, &out,
                           nullptr) == RMR_ERR_CANCELLED);
    assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 19);
    size_t allocations;
    for (allocations = 0; allocations < 3000; allocations++) {
        rmr_test_fail_after(allocations);
        rmr_status s =
            rmr_pdf_rewrite((uint8_t *)f->bytes, f->n, nullptr, nullptr, nullptr, &out, nullptr);
        rmr_test_fail_after(SIZE_MAX);
        if (s == RMR_OK) {
            rmr_buffer_free(&out);
            break;
        }
        assert(s == RMR_ERR_OUT_OF_MEMORY);
        assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 19);
    }
    assert(allocations < 3000);
    rmr_limits l = rmr_limits_default();
    l.maximum_plan_bytes = 64;
    assert(rmr_pdf_rewrite((uint8_t *)f->bytes, f->n, &l, nullptr, nullptr, &out, nullptr) ==
           RMR_ERR_LIMIT);
    fixture sparse = *f;
    size_t previous = lastxref(&sparse), at = sparse.n;
    add(&sparse,
        "xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 1000000 /Prev %zu "
        ">>\nstartxref\n%zu\n%%%%EOF\n",
        previous, at);
    l = rmr_limits_default();
    l.maximum_elements = 5000;
    out = (rmr_buffer){(uint8_t *)(uintptr_t)1, 19};
    assert(rmr_pdf_rewrite((uint8_t *)sparse.bytes, sparse.n, &l, nullptr, nullptr, &out,
                           nullptr) == RMR_ERR_LIMIT);
    assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 19);
    fixture certified = source(true);
    assert(rmr_pdf_rewrite((uint8_t *)certified.bytes, certified.n, nullptr, nullptr, nullptr, &out,
                           nullptr) == RMR_ERR_UNSUPPORTED);
}

static void select_check(void) {
    fixture f = source(false);
    const size_t order[] = {1, 0, 0};
    rmr_buffer out = {0};
    assert(rmr_pdf_select_pages((uint8_t *)f.bytes, f.n, order, 3, nullptr, nullptr, nullptr, &out,
                                nullptr) == RMR_OK);
    rmr_pdf_page_geometry g[3];
    size_t count;
    assert(rmr_pdf_pages(out.bytes, out.size, nullptr, g, 3, &count, nullptr) == RMR_OK);
    assert(count == 3 && g[0].rotate == 90 && g[1].rotate == 0 && g[2].rotate == 0);
    rmr_buffer_free(&out);
    out = (rmr_buffer){(uint8_t *)(uintptr_t)1, 19};
    assert(rmr_pdf_select_pages((uint8_t *)f.bytes, f.n, order, 3, nullptr, cancel_rewrite, nullptr,
                                &out, nullptr) == RMR_ERR_CANCELLED);
    const size_t invalid[] = {2};
    assert(rmr_pdf_select_pages((uint8_t *)f.bytes, f.n, invalid, 1, nullptr, nullptr, nullptr,
                                &out, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_draws = 2;
    assert(rmr_pdf_select_pages((uint8_t *)f.bytes, f.n, order, 3, &limits, nullptr, nullptr, &out,
                                nullptr) == RMR_ERR_LIMIT);
    assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 19);
    size_t allocations;
    for (allocations = 0; allocations < 3000; allocations++) {
        out = (rmr_buffer){(uint8_t *)(uintptr_t)1, 19};
        rmr_test_fail_after(allocations);
        rmr_status status = rmr_pdf_select_pages((uint8_t *)f.bytes, f.n, order, 3, nullptr,
                                                 nullptr, nullptr, &out, nullptr);
        rmr_test_fail_after(SIZE_MAX);
        if (status == RMR_OK) {
            rmr_buffer_free(&out);
            break;
        }
        assert(status == RMR_ERR_OUT_OF_MEMORY);
        assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 19);
    }
    assert(allocations < 3000);
}

static bool squashed_contains(const rmr_buffer *b, const char *needle) {
    char *text = malloc(b->size + 1);
    assert(text);
    size_t n = 0;
    for (size_t i = 0; i < b->size; i++)
        if (b->bytes[i] != ' ' && b->bytes[i] != '\n' && b->bytes[i] != '\r')
            text[n++] = b->bytes[i] ? (char)b->bytes[i] : '.';
    text[n] = 0;
    bool found = strstr(text, needle) != nullptr;
    free(text);
    return found;
}

/* Page labels follow the selected pages: i, ii, 5 selected as 5, ii, i, ii. */
static void select_labels_check(void) {
    fixture f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, 0,
        "<< /Type /Catalog /Pages 2 0 R /PageLabels << /Nums [0 << /S /r >> 2 6 0 R] >> >>");
    obj(&f, 2, 0, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 /MediaBox [0 0 100 100] >>");
    obj(&f, 3, 0, "<< /Type /Page /Parent 2 0 R >>");
    obj(&f, 4, 0, "<< /Type /Page /Parent 2 0 R >>");
    obj(&f, 5, 0, "<< /Type /Page /Parent 2 0 R >>");
    obj(&f, 6, 0, "<< /S /D /St 5 >>");
    size_t x = f.n;
    add(&f, "xref\n0 7\n0000000000 65535 f \n");
    for (unsigned i = 1; i < 7; i++)
        add(&f, "%010zu %05u n \n", f.offsets[i], f.gen[i]);
    add(&f, "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", x);
    const size_t order[] = {2, 1, 0, 1};
    rmr_buffer out = {0};
    assert(rmr_pdf_select_pages((uint8_t *)f.bytes, f.n, order, 4, nullptr, nullptr, nullptr, &out,
                                nullptr) == RMR_OK);
    assert(
        squashed_contains(&out, "/PageLabels<</Nums[0<</S/D/St5>>1<</St2/S/r>>2<</St1/S/r>>]>>"));
    size_t count;
    assert(rmr_pdf_pages(out.bytes, out.size, nullptr, nullptr, 0, &count, nullptr) == RMR_OK);
    assert(count == 4);
    rmr_buffer_free(&out);
    char *start = strstr(f.bytes, "/St 5 >>");
    assert(start);
    memcpy(start, "/St +5>>", 8);
    assert(rmr_pdf_select_pages((uint8_t *)f.bytes, f.n, order, 4, nullptr, nullptr, nullptr, &out,
                                nullptr) == RMR_OK);
    assert(squashed_contains(&out, "/Nums[0<</S/D/St5>>"));
    rmr_buffer_free(&out);
    const char *invalid[] = {"/St(5)>>", "/St 0 >>"};
    for (size_t i = 0; i < 2; i++) {
        memcpy(start, invalid[i], 8);
        out = (rmr_buffer){(uint8_t *)(uintptr_t)1, 19};
        assert(rmr_pdf_select_pages((uint8_t *)f.bytes, f.n, order, 4, nullptr, nullptr, nullptr,
                                    &out, nullptr) == RMR_ERR_INVALID_ARGUMENT);
        assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 19);
    }
}

/* Object 0 heads the free list and a catalog must be an indirect object; a
 * fresh file keeps a well-formed declared version and normalizes any other. */
static void reserved_check(void) {
    rmr_buffer out = {(uint8_t *)(uintptr_t)1, 19};
    fixture zero = overlay_fixture();
    char *row = strstr(zero.bytes, "0000000000 65535 f ");
    assert(row);
    memcpy(row, "0000000009 00000 n ", 19);
    assert(rmr_pdf_rewrite((uint8_t *)zero.bytes, zero.n, nullptr, nullptr, nullptr, &out,
                           nullptr) == RMR_ERR_INVALID_ARGUMENT);
    assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 19);
    fixture direct = overlay_fixture();
    char *trailer = strstr(direct.bytes, "trailer");
    assert(trailer);
    size_t x = lastxref(&direct);
    direct.n = (size_t)(trailer - direct.bytes);
    add(&direct,
        "trailer\n<< /Size 6 /Root << /Type /Catalog /Pages 2 0 R >> >>\nstartxref\n%zu\n%%%%EOF\n",
        x);
    size_t count;
    assert(rmr_pdf_pages((uint8_t *)direct.bytes, direct.n, nullptr, nullptr, 0, &count, nullptr) ==
           RMR_ERR_INVALID_ARGUMENT);
    /* The last header runs into object 1 without a line break. */
    const char *const headers[][2] = {
        {"%PDF-2.0\n", "%PDF-2.0\n"}, {"%PDF-x.7\n", "%PDF-1.7\n"}, {"%PDF-1.71", "%PDF-1.7\n"}};
    for (size_t i = 0; i < 3; i++) {
        fixture f = overlay_fixture();
        memcpy(f.bytes, headers[i][0], 9);
        assert(rmr_pdf_rewrite((uint8_t *)f.bytes, f.n, nullptr, nullptr, nullptr, &out, nullptr) ==
               RMR_OK);
        assert(out.size > 9 && !memcmp(out.bytes, headers[i][1], 9));
        rmr_buffer_free(&out);
    }
}

int main(void) {
    select_check();
    select_labels_check();
    reserved_check();
    fixture f = source(false), ov = overlay_fixture();
    rmr_pdf_overlay o = {.pdf = (uint8_t *)ov.bytes, .size = ov.n, .transform = {1, 0, 0, 1, 0, 0}};
    rewrite_check((uint8_t *)f.bytes, f.n, 2);
    rewrite_failures(&f);
    rmr_buffer output = {0};
    assert(run(&f, &o, nullptr, &output) == RMR_OK);
    assert(output.size > f.n && !memcmp(output.bytes, f.bytes, f.n));
    rmr_buffer_free(&output);
    for (unsigned flags = 0; flags < 16; flags++) {
        bool packed = (flags & 1) != 0, compressed = (flags & 2) != 0;
        bool predictor = compressed && (flags & 4), hybrid = (flags & 8) != 0;
        fixture input = stream_fixture(packed, compressed, predictor, hybrid, false);
        assert(run(&input, &o, nullptr, &output) == RMR_OK);
        assert(output.size > input.n && !memcmp(output.bytes, input.bytes, input.n));
        rmr_buffer again = {0};
        assert(rmr_pdf_incremental(output.bytes, output.size, &o, 1, nullptr, nullptr, nullptr,
                                   &again, nullptr, nullptr) == RMR_OK);
        assert(again.size > output.size && !memcmp(again.bytes, output.bytes, output.size));
        rewrite_check(again.bytes, again.size, 1);
        rmr_buffer_free(&again);
        rmr_buffer_free(&output);
    }
    fixture compressed_input = stream_fixture(true, true, true, true, false);
    size_t failed_allocations = 0;
    for (; failed_allocations < 3000; failed_allocations++) {
        rmr_buffer tested = {(uint8_t *)(uintptr_t)1, 19};
        rmr_test_fail_after(failed_allocations);
        rmr_status status = run(&compressed_input, &o, nullptr, &tested);
        rmr_test_fail_after(SIZE_MAX);
        if (status == RMR_OK) {
            rmr_buffer_free(&tested);
            break;
        }
        assert(status == RMR_ERR_OUT_OF_MEMORY);
        assert(tested.bytes == (uint8_t *)(uintptr_t)1 && tested.size == 19);
    }
    assert(failed_allocations < 3000);
    fixture invalid_index = stream_fixture(true, true, true, true, true);
    assert(run(&invalid_index, &o, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);
    /* Quartz's unused live offset-zero row is allowed only in an overlay.
     * If reachable, it remains a hard error; source validation stays strict. */
    fixture reserved = ov;
    revision(&reserved, "6 1\n0000000000 00000 n ", "");
    rmr_pdf_overlay unused = o;
    unused.pdf = (uint8_t *)reserved.bytes;
    unused.size = reserved.n;
    assert(run(&f, &unused, nullptr, &output) == RMR_OK);
    rmr_buffer_free(&output);
    assert(run(&reserved, nullptr, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);
    revision(&reserved, "4 1\n0000000000 00000 n ", "");
    unused.size = reserved.n;
    assert(run(&f, &unused, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);
    /* The default unit may be written as an integer or real. Non-default and
     * nonnumeric values are refused; no truncation of 1.5 to 1 is allowed. */
    const char *units[] = {"1", "1.0", "+1.00", "2", "1.5", "0", "null"};
    for (size_t i = 0; i < sizeof units / sizeof *units; i++) {
        fixture scaled = f;
        char page_body[256], row[64];
        snprintf(page_body, sizeof page_body,
                 "<< /Type /Page /Parent 2 0 R /Contents 16 0 R /UserUnit %s >>", units[i]);
        obj(&scaled, 3, 7, page_body);
        snprintf(row, sizeof row, "3 1\n%010zu 00007 n ", scaled.offsets[3]);
        revision(&scaled, row, "");
        rmr_buffer changed = {(uint8_t *)(uintptr_t)1, 17};
        rmr_status status = run(&scaled, &o, nullptr, &changed);
        if (i < 3) {
            assert(status == RMR_OK);
            rmr_buffer_free(&changed);
        } else {
            assert(status == RMR_ERR_UNSUPPORTED);
            assert(changed.bytes == (uint8_t *)(uintptr_t)1 && changed.size == 17);
        }
    }
    size_t allocations = 0;
    for (; allocations < 2000; allocations++) {
        rmr_buffer out = {(uint8_t *)(uintptr_t)1, 22};
        rmr_test_fail_after(allocations);
        rmr_status s = run(&f, &o, nullptr, &out);
        rmr_test_fail_after(SIZE_MAX);
        if (s == RMR_OK) {
            rmr_buffer_free(&out);
            break;
        }
        assert(s == RMR_ERR_OUT_OF_MEMORY);
        assert(out.bytes == (uint8_t *)(uintptr_t)1 && out.size == 22);
    }
    assert(allocations < 2000);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_plan_bytes = 64;
    assert(run(&f, &o, &limits, &output) == RMR_ERR_LIMIT);
    limits = rmr_limits_default();
    limits.maximum_elements = 5;
    assert(run(&f, &o, &limits, &output) == RMR_ERR_LIMIT);
    limits = rmr_limits_default();
    limits.maximum_pdf_bytes = f.n;
    assert(run(&f, &o, &limits, &output) == RMR_ERR_LIMIT);
    fixture bad = f;
    revision(&bad, "1 1\n0000000000 00001 f ", "");
    assert(run(&bad, &o, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);
    bad = f;
    revision(&bad, "0 1\n0000000000 65535 f ", "/XRefStm 20");
    assert(run(&bad, &o, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);
    bad = f;
    revision(&bad, "0 1\n0000000000 65535 f ", "/Encrypt null");
    assert(run(&bad, &o, nullptr, &output) == RMR_ERR_UNSUPPORTED);
    bad = f;
    size_t x = bad.n;
    add(&bad, "17 0 obj\n<< /Type /XRef >>\nendobj\nstartxref\n%zu\n%%%%EOF\n", x);
    assert(run(&bad, &o, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);
    bad = f;
    revision(&bad, "0 1\n0000000000 65535 f ", "");
    assert(run(&bad, &o, nullptr, &output) == RMR_OK);
    rmr_buffer_free(&output);
    bad = f;
    char *page = strstr(bad.bytes, "/Kids [3 7 R");
    assert(page);
    page[9] = '0';
    assert(run(&bad, &o, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);
    /* A linearized-style first xref can point forward to the main table.
     * /Prev means revision order, not necessarily decreasing byte offsets. */
    bad = f;
    size_t first = lastxref(&bad);
    char main_table[8192];
    size_t main_size = bad.n - first;
    assert(main_size < sizeof main_table);
    memcpy(main_table, bad.bytes + first, main_size);
    bad.n = first;
    add(&bad,
        "xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 17 /Root 1 0 R /Prev 0000000000 >>\n");
    size_t main_offset = bad.n;
    char *prev = strstr(bad.bytes + first, "/Prev ");
    assert(prev);
    char offset_text[11];
    snprintf(offset_text, sizeof offset_text, "%010zu", main_offset);
    memcpy(prev + 6, offset_text, 10);
    memcpy(bad.bytes + bad.n, main_table, main_size);
    bad.n += main_size;
    bad.bytes[bad.n] = 0;
    add(&bad, "startxref\n%zu\n%%%%EOF\n", first);
    assert(run(&bad, &o, nullptr, &output) == RMR_OK);
    rmr_buffer_free(&output);
    /* The same chain made cyclic must fail without publishing partial output. */
    size_t root_prev = (size_t)(prev - bad.bytes) + 6;
    snprintf(offset_text, sizeof offset_text, "%010zu", first);
    memcpy(bad.bytes + root_prev, offset_text, 10);
    assert(run(&bad, &o, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);
    rmr_pdf_overlay duplicate[] = {o, o};
    assert(rmr_pdf_incremental((uint8_t *)f.bytes, f.n, duplicate, 2, nullptr, nullptr, nullptr,
                               &output, nullptr, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    puts("portable PDF update/refusal/allocation tests passed");
    return 0;
}
