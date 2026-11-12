/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <rmrender/pdf.h>
#include <stdlib.h>
#include <zlib.h>

#include "pdf_fixture.h"
#include "rmr_internal.h"
#include "rmr_pdf_edit.h"

static fixture overlay_fixture(void) {
  fixture f = {0};
  add(&f, "%%PDF-1.7\n");
  obj(&f, 1, 0, "<< /Type /Catalog /Pages 2 0 R >>");
  obj(&f, 2, 0,
      "<< /Type /Pages /Kids [3 4 R] /Count 1 /MediaBox [0 0 200 200] >>");
  obj(&f, 3, 4,
      "<< /Type /Page /Parent 2 0 R /Resources << >> /Contents 4 0 R >>");

  f.offsets[4] = f.n;
  const char* paint = "1 0 0 rg 50 50 100 100 re f\n";
  add(&f, "4 0 obj\n<< /Length 5 0 R >>\nstream\n%s\nendstream\nendobj\n",
      paint);
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

static size_t lastxref(fixture* f) {
  char *last = nullptr, *p = f->bytes;
  while ((p = strstr(p, "startxref"))) {
    last = p;
    p++;
  }
  assert(last);
  return (size_t)strtoull(last + 9, nullptr, 10);
}

static void revision(fixture* f, const char* rows, const char* extra) {
  size_t prev = lastxref(f), offset = f->n;
  add(f,
      "xref\n%s\ntrailer\n<< /Size 17 /Prev %zu %s "
      ">>\nstartxref\n%zu\n%%%%EOF\n",
      rows, prev, extra, offset);
}

static rmr_status run(fixture* f, rmr_pdf_overlay* o, rmr_limits* limits,
                      rmr_buffer* out) {
  return rmr_pdf_incremental((uint8_t*)f->bytes, f->n, o, o ? 1 : 0, limits,
                             nullptr, nullptr, out, nullptr, nullptr);
}

static fixture stream_fixture(bool packed, bool compressed, bool predictor,
                              bool hybrid, bool bad) {
  fixture f = {0};
  add(&f, "%%PDF-1.7\n");
  const char* objects[] = {"<< /Type /Catalog /Pages 2 0 R >>",
                           "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                           ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 "
                            "200] /Resources << >> >>")};

  if (packed) {
    char header[128], payload[512];
    snprintf(header, sizeof header, "1 0 2 %zu 3 %zu ", strlen(objects[0]) + 1,
             strlen(objects[0]) + strlen(objects[1]) + 2);
    snprintf(payload, sizeof payload, "%s%s %s %s ", header, objects[0],
             objects[1], objects[2]);
    f.offsets[5] = f.n;
    add(&f,
        "5 0 obj\n<< /Type /ObjStm /N 3 /First %zu /Length %zu "
        ">>\nstream\n%s\nendstream\nendobj\n",
        strlen(header), strlen(payload), payload);
  } else {
    for (unsigned i = 0; i < 3; i++) obj(&f, i + 1, 0, objects[i]);
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
    if (predictor) raw[n++] = 2;
    for (unsigned k = 0; k < 7; k++)
      raw[n++] = predictor ? (uint8_t)(row[k] - previous[k]) : row[k];
    memcpy(previous, row, 7);
  }

  const uint8_t* data = raw;
  if (compressed) {
    uLongf size = sizeof encoded;
    assert(compress(encoded, &size, raw, (uLong)n) == Z_OK);
    data = encoded;
    n = (size_t)size;
  }

  add(&f,
      "6 0 obj\n<< /Type /XRef /Root 1 0 R /Size 7 /W [1 4 2] /Length %zu %s "
      "%s >>\nstream\n",
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

static bool cancel_rewrite(void* context) {
  (void)context;
  return true;
}

static void rewrite_check(const uint8_t* bytes, size_t size,
                          size_t expected_pages) {
  rmr_buffer fresh = {0};
  assert(rmr_pdf_rewrite(bytes, size, nullptr, nullptr, nullptr, &fresh,
                         nullptr) == RMR_OK);
  size_t count = 0;
  assert(rmr_pdf_pages(fresh.bytes, fresh.size, nullptr, nullptr, 0, &count,
                       nullptr) == RMR_OK);
  assert(count == expected_pages);

  /* A subsequent append must resolve the newly emitted xref independently. */
  fixture ov = overlay_fixture();
  rmr_pdf_overlay o = {
      .pdf = (uint8_t*)ov.bytes, .size = ov.n, .transform = {1, 0, 0, 1, 0, 0}};
  rmr_buffer next = {0};
  assert(rmr_pdf_incremental(fresh.bytes, fresh.size, &o, 1, nullptr, nullptr,
                             nullptr, &next, nullptr, nullptr) == RMR_OK);
  rmr_buffer_free(&next);
  rmr_buffer_free(&fresh);
}

static void rewrite_failures(fixture* f) {
  rmr_buffer out = {(uint8_t*)(uintptr_t)1, 19};
  assert(rmr_pdf_rewrite((uint8_t*)f->bytes, f->n, nullptr, cancel_rewrite,
                         nullptr, &out, nullptr) == RMR_ERR_CANCELLED);
  assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);

  size_t allocations;
  for (allocations = 0; allocations < 3000; allocations++) {
    rmr_test_fail_after(allocations);
    rmr_status s = rmr_pdf_rewrite((uint8_t*)f->bytes, f->n, nullptr, nullptr,
                                   nullptr, &out, nullptr);
    rmr_test_fail_after(SIZE_MAX);
    if (s == RMR_OK) {
      rmr_buffer_free(&out);
      break;
    }
    assert(s == RMR_ERR_OUT_OF_MEMORY);
    assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);
  }
  assert(allocations < 3000);

  rmr_limits l = rmr_limits_default();
  l.maximum_plan_bytes = 64;
  assert(rmr_pdf_rewrite((uint8_t*)f->bytes, f->n, &l, nullptr, nullptr, &out,
                         nullptr) == RMR_ERR_LIMIT);

  fixture sparse = *f;
  size_t previous = lastxref(&sparse), at = sparse.n;
  add(&sparse,
      "xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 1000000 /Prev %zu "
      ">>\nstartxref\n%zu\n%%%%EOF\n",
      previous, at);
  l = rmr_limits_default();
  l.maximum_elements = 5000;
  out = (rmr_buffer){(uint8_t*)(uintptr_t)1, 19};
  assert(rmr_pdf_rewrite((uint8_t*)sparse.bytes, sparse.n, &l, nullptr, nullptr,
                         &out, nullptr) == RMR_ERR_LIMIT);
  assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);

  fixture certified = source(true);
  assert(rmr_pdf_rewrite((uint8_t*)certified.bytes, certified.n, nullptr,
                         nullptr, nullptr, &out,
                         nullptr) == RMR_ERR_UNSUPPORTED);
}

static void select_check(void) {
  fixture f = source(false);
  const size_t order[] = {1, 0, 0};
  rmr_buffer out = {0};
  assert(rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, order, 3, nullptr,
                              nullptr, nullptr, &out, nullptr) == RMR_OK);

  rmr_pdf_page_geometry g[3];
  size_t count;
  assert(rmr_pdf_pages(out.bytes, out.size, nullptr, g, 3, &count, nullptr) ==
         RMR_OK);
  assert(count == 3 && g[0].rotate == 90 && g[1].rotate == 0 &&
         g[2].rotate == 0);

  rmr_buffer_free(&out);
  out = (rmr_buffer){(uint8_t*)(uintptr_t)1, 19};
  assert(rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, order, 3, nullptr,
                              cancel_rewrite, nullptr, &out,
                              nullptr) == RMR_ERR_CANCELLED);

  const size_t invalid[] = {2};
  assert(rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, invalid, 1, nullptr,
                              nullptr, nullptr, &out,
                              nullptr) == RMR_ERR_INVALID_ARGUMENT);

  rmr_limits limits = rmr_limits_default();
  limits.maximum_draws = 2;
  assert(rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, order, 3, &limits,
                              nullptr, nullptr, &out,
                              nullptr) == RMR_ERR_LIMIT);
  assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);

  size_t allocations;
  for (allocations = 0; allocations < 3000; allocations++) {
    out = (rmr_buffer){(uint8_t*)(uintptr_t)1, 19};
    rmr_test_fail_after(allocations);
    rmr_status status =
        rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, order, 3, nullptr, nullptr,
                             nullptr, &out, nullptr);
    rmr_test_fail_after(SIZE_MAX);
    if (status == RMR_OK) {
      rmr_buffer_free(&out);
      break;
    }
    assert(status == RMR_ERR_OUT_OF_MEMORY);
    assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);
  }
  assert(allocations < 3000);
}

static bool squashed_contains(const rmr_buffer* b, const char* needle) {
  char* text = malloc(b->size + 1);
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
      "<< /Type /Catalog /Pages 2 0 R /PageLabels << /Nums [0 << /S /r >> 2 6 "
      "0 R] >> >>");
  obj(&f, 2, 0,
      "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 /MediaBox [0 0 100 "
      "100] >>");
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
  assert(rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, order, 4, nullptr,
                              nullptr, nullptr, &out, nullptr) == RMR_OK);
  assert(squashed_contains(
      &out, "/PageLabels<</Nums[0<</S/D/St5>>1<</St2/S/r>>2<</St1/S/r>>]>>"));
  size_t count;
  assert(rmr_pdf_pages(out.bytes, out.size, nullptr, nullptr, 0, &count,
                       nullptr) == RMR_OK);
  assert(count == 4);
  rmr_buffer_free(&out);

  char* start = strstr(f.bytes, "/St 5 >>");
  assert(start);
  memcpy(start, "/St +5>>", 8);
  assert(rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, order, 4, nullptr,
                              nullptr, nullptr, &out, nullptr) == RMR_OK);
  assert(squashed_contains(&out, "/Nums[0<</S/D/St5>>"));
  rmr_buffer_free(&out);

  const char* invalid[] = {"/St(5)>>", "/St 0 >>"};
  for (size_t i = 0; i < 2; i++) {
    memcpy(start, invalid[i], 8);
    out = (rmr_buffer){(uint8_t*)(uintptr_t)1, 19};
    assert(rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, order, 4, nullptr,
                                nullptr, nullptr, &out,
                                nullptr) == RMR_ERR_INVALID_ARGUMENT);
    assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);
  }
}

/* Quartz writes an in-use xref row at offset 0 for an object number it never
 * emits; the file is still read, updated, rewritten and paged. */
static void quartz_row_check(void) {
  fixture f = overlay_fixture();
  char* xref = strstr(f.bytes, "xref\n0 6\n");
  char* trailer = strstr(f.bytes, "trailer\n<< /Size 6 ");
  assert(xref && trailer);
  char rest[512];
  snprintf(rest, sizeof rest, "%s", trailer + strlen("trailer\n<< /Size 6 "));
  xref[7] = '7';
  f.n = (size_t)(trailer - f.bytes);
  add(&f, "0000000000 00000 n \ntrailer\n<< /Size 7 %s", rest);

  size_t count = 0;
  assert(rmr_pdf_pages((uint8_t*)f.bytes, f.n, nullptr, nullptr, 0, &count,
                       nullptr) == RMR_OK);
  assert(count == 1);

  fixture ov = overlay_fixture();
  rmr_pdf_overlay o = {
      .pdf = (uint8_t*)ov.bytes, .size = ov.n, .transform = {1, 0, 0, 1, 0, 0}};
  rmr_buffer out = {0};
  assert(rmr_pdf_incremental((uint8_t*)f.bytes, f.n, &o, 1, nullptr, nullptr,
                             nullptr, &out, nullptr, nullptr) == RMR_OK);
  rmr_buffer_free(&out);

  assert(rmr_pdf_rewrite((uint8_t*)f.bytes, f.n, nullptr, nullptr, nullptr,
                         &out, nullptr) == RMR_OK);
  rmr_buffer_free(&out);

  const size_t order[] = {0, 0};
  assert(rmr_pdf_select_pages((uint8_t*)f.bytes, f.n, order, 2, nullptr,
                              nullptr, nullptr, &out, nullptr) == RMR_OK);
  rmr_buffer_free(&out);
}

/* Object 0 heads the free list and a catalog must be an indirect object; a
 * fresh file keeps a well-formed declared version and normalizes any other. */
static void reserved_check(void) {
  rmr_buffer out = {(uint8_t*)(uintptr_t)1, 19};
  fixture zero = overlay_fixture();
  char* row = strstr(zero.bytes, "0000000000 65535 f ");
  assert(row);
  memcpy(row, "0000000009 00000 n ", 19);
  assert(rmr_pdf_rewrite((uint8_t*)zero.bytes, zero.n, nullptr, nullptr,
                         nullptr, &out, nullptr) == RMR_ERR_INVALID_ARGUMENT);
  assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);

  fixture direct = overlay_fixture();
  char* trailer = strstr(direct.bytes, "trailer");
  assert(trailer);
  size_t x = lastxref(&direct);
  direct.n = (size_t)(trailer - direct.bytes);
  add(&direct,
      "trailer\n<< /Size 6 /Root << /Type /Catalog /Pages 2 0 R >> "
      ">>\nstartxref\n%zu\n%%%%EOF\n",
      x);
  size_t count;
  assert(rmr_pdf_pages((uint8_t*)direct.bytes, direct.n, nullptr, nullptr, 0,
                       &count, nullptr) == RMR_ERR_INVALID_ARGUMENT);

  /* The last header runs into object 1 without a line break. */
  const char* const headers[][2] = {{"%PDF-2.0\n", "%PDF-2.0\n"},
                                    {"%PDF-x.7\n", "%PDF-1.7\n"},
                                    {"%PDF-1.71", "%PDF-1.7\n"}};
  for (size_t i = 0; i < 3; i++) {
    fixture f = overlay_fixture();
    memcpy(f.bytes, headers[i][0], 9);
    assert(rmr_pdf_rewrite((uint8_t*)f.bytes, f.n, nullptr, nullptr, nullptr,
                           &out, nullptr) == RMR_OK);
    assert(out.size > 9 && !memcmp(out.bytes, headers[i][1], 9));
    rmr_buffer_free(&out);
  }
}

/* A PDF made in memory, its objects numbered from 1 in the order begun. */
typedef struct {
  char* bytes;
  size_t n, capacity, objects, slots;
  size_t* offsets;
} generated;

static void grow_add(generated* g, const char* format, ...) {
  for (;;) {
    va_list a;
    va_start(a, format);
    int n = vsnprintf(g->bytes ? g->bytes + g->n : nullptr,
                      g->bytes ? g->capacity - g->n : 0, format, a);
    va_end(a);
    assert(n >= 0);
    if (g->bytes && (size_t)n < g->capacity - g->n) {
      g->n += (size_t)n;
      return;
    }
    size_t grown = g->capacity ? g->capacity * 2 : 1 << 16;
    while (grown - g->n <= (size_t)n) grown *= 2;
    g->bytes = realloc(g->bytes, grown);
    assert(g->bytes);
    g->capacity = grown;
  }
}

static void grow_bytes(generated* g, const uint8_t* bytes, size_t size) {
  if (size >= g->capacity - g->n) {
    size_t grown = g->capacity ? g->capacity * 2 : 1 << 16;
    while (grown - g->n <= size) grown *= 2;
    g->bytes = realloc(g->bytes, grown);
    assert(g->bytes);
    g->capacity = grown;
  }
  memcpy(g->bytes + g->n, bytes, size);
  g->n += size;
}

/* Begins the next object, returning its number. */
static size_t grow_object(generated* g) {
  if (g->objects + 2 > g->slots) {
    g->slots = g->slots ? g->slots * 2 : 1024;
    g->offsets = realloc(g->offsets, g->slots * sizeof *g->offsets);
    assert(g->offsets);
  }
  g->offsets[++g->objects] = g->n;
  grow_add(g, "%zu 0 obj\n", g->objects);
  return g->objects;
}

/* The cross-reference table and trailer, object 1 the catalog. */
static void grow_finish(generated* g) {
  size_t x = g->n;
  grow_add(g, "xref\n0 %zu\n0000000000 65535 f \n", g->objects + 1);
  for (size_t i = 1; i <= g->objects; i++)
    grow_add(g, "%010zu 00000 n \n", g->offsets[i]);
  grow_add(g, "trailer\n<< /Size %zu /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n",
           g->objects + 1, x);
}

static void grow_free(generated* g) {
  free(g->bytes);
  free(g->offsets);
  *g = (generated){0};
}

/* A source of count pages sharing one content stream. */
static generated long_source(size_t count) {
  generated g = {0};
  grow_add(&g, "%%PDF-1.7\n");
  grow_object(&g);
  grow_add(&g, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  grow_object(&g);
  grow_add(&g, "<< /Type /Pages /Count %zu /Kids [", count);
  for (size_t i = 0; i < count; i++) grow_add(&g, "%zu 0 R ", i + 4);
  grow_add(&g, "] >>\nendobj\n");
  const char* paint = "0 0 1 rg 10 10 50 50 re f";
  grow_object(&g);
  grow_add(&g, "<< /Length %zu >>\nstream\n%s\nendstream\nendobj\n",
           strlen(paint), paint);
  for (size_t i = 0; i < count; i++) {
    grow_object(&g);
    grow_add(&g,
             "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 3 "
             "0 R /Resources << >> >>\nendobj\n");
  }
  grow_finish(&g);
  return g;
}

/* A one-page overlay drawing images one-bit masks, each an object of its
 * own its resources name, the first name repeated at the end when twice is
 * set: what a page of grain-textured ink looks like to the importer. */
static generated dense_overlay(size_t images, bool twice) {
  generated g = {0};
  grow_add(&g, "%%PDF-1.7\n");
  grow_object(&g);
  grow_add(&g, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  grow_object(&g);
  grow_add(&g, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
  grow_object(&g);
  grow_add(&g,
           "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 "
           "0 R /Resources << /XObject <<");
  for (size_t i = 0; i < images; i++) grow_add(&g, " /I%zu %zu 0 R", i, i + 5);
  if (twice) grow_add(&g, " /I0 5 0 R");
  grow_add(&g, " >> >> >>\nendobj\n");
  generated content = {0};
  for (size_t i = 0; i < images; i++)
    grow_add(&content, "q 2 0 0 2 %zu %zu cm /I%zu Do Q\n", i % 100 * 2,
             i / 100 * 2, i);
  grow_object(&g);
  grow_add(&g, "<< /Length %zu >>\nstream\n%s\nendstream\nendobj\n", content.n,
           content.bytes);
  grow_free(&content);
  for (size_t i = 0; i < images; i++) {
    grow_object(&g);
    grow_add(&g,
             "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ImageMask "
             "true /BitsPerComponent 1 /Length 1 >>\nstream\n%c\nendstream\n"
             "endobj\n",
             (int)('A' + i % 26));
  }
  grow_finish(&g);
  return g;
}

/* An rmr_output collecting what it is given. */
typedef struct {
  generated bytes;
  size_t calls;
} collected;

static rmr_status collect_output(void* context, const uint8_t* bytes,
                                 size_t size) {
  collected* c = context;
  c->calls++;
  grow_bytes(&c->bytes, bytes, size);
  return RMR_OK;
}

static bool has_text(const uint8_t* bytes, size_t size, const char* text) {
  size_t n = strlen(text);
  for (size_t i = 0; i + n <= size; i++)
    if (!memcmp(bytes + i, text, n)) return true;
  return false;
}

/* Each page of base given the overlay, through an update (fresh: a rewrite)
 * written to output when one is given. */
static rmr_status update_all(const generated* base, const generated* overlay,
                             size_t pages, bool fresh, const rmr_limits* limits,
                             const rmr_output* output, rmr_buffer* out,
                             size_t* before_finish, collected* sink) {
  rmr_pdf_update* u = nullptr;
  rmr_status s =
      rmr_pdf_update_create((const uint8_t*)base->bytes, base->n, fresh, limits,
                            nullptr, nullptr, output, &u, nullptr);
  for (size_t i = 0; s == RMR_OK && i < pages; i++) {
    rmr_pdf_overlay o = {.page = i,
                         .pdf = (const uint8_t*)overlay->bytes,
                         .size = overlay->n,
                         .transform = {1, 0, 0, 1, 0, 0}};
    s = rmr_pdf_update_page(u, &o, 1, nullptr);
  }
  if (before_finish) *before_finish = sink->bytes.n;
  if (s == RMR_OK) s = rmr_pdf_update_finish(u, out, nullptr, nullptr);
  rmr_pdf_update_free(u);
  return s;
}

/* Two thousand pages, each given an overlay of 64 images, under the default
 * limits: neither the parse's scratch and work nor the overlays held grow
 * with the page count, in an update or a rewrite, collected or streamed. */
static void long_document_check(void) {
  enum { PAGES = 2000 };
  generated base = long_source(PAGES), overlay = dense_overlay(64, false);
  rmr_pdf_overlay* overlays = calloc(PAGES, sizeof *overlays);
  assert(overlays);
  for (size_t i = 0; i < PAGES; i++)
    overlays[i] = (rmr_pdf_overlay){.page = i,
                                    .pdf = (const uint8_t*)overlay.bytes,
                                    .size = overlay.n,
                                    .transform = {1, 0, 0, 1, 0, 0}};
  rmr_buffer updated = {0};
  rmr_pdf_incremental_report report = {0};
  assert(rmr_pdf_incremental((const uint8_t*)base.bytes, base.n, overlays,
                             PAGES, nullptr, nullptr, nullptr, &updated,
                             &report, nullptr) == RMR_OK);
  assert(report.pages_updated == PAGES && report.original_bytes == base.n);
  assert(updated.size > base.n && !memcmp(updated.bytes, base.bytes, base.n));
  size_t count = 0;
  assert(rmr_pdf_pages(updated.bytes, updated.size, nullptr, nullptr, 0, &count,
                       nullptr) == RMR_OK &&
         count == PAGES);

  /* Streamed, the same file leaves in pieces while pages are still being
   * given their overlays (fewer of them for the masks every page shares). */
  collected sink = {0};
  rmr_output output = {RMR_OUTPUT_ABI, sizeof output, collect_output, &sink};
  size_t before = 0;
  assert(update_all(&base, &overlay, PAGES, false, nullptr, &output, nullptr,
                    &before, &sink) == RMR_OK);
  assert(sink.calls >= 2 && before > base.n && before < updated.size);
  assert(sink.bytes.n == updated.size &&
         !memcmp(sink.bytes.bytes, updated.bytes, updated.size));
  grow_free(&sink.bytes);

  /* A rewrite carries the same pages without the original revision. */
  rmr_buffer fresh = {0};
  assert(update_all(&base, &overlay, PAGES, true, nullptr, nullptr, &fresh,
                    nullptr, nullptr) == RMR_OK);
  assert(rmr_pdf_pages(fresh.bytes, fresh.size, nullptr, nullptr, 0, &count,
                       nullptr) == RMR_OK &&
         count == PAGES);
  assert(!has_text(fresh.bytes, fresh.size, "/Prev"));
  sink = (collected){0};
  assert(update_all(&base, &overlay, PAGES, true, nullptr, &output, nullptr,
                    &before, &sink) == RMR_OK);
  assert(sink.bytes.n == fresh.size &&
         !memcmp(sink.bytes.bytes, fresh.bytes, fresh.size));
  grow_free(&sink.bytes);

  rmr_buffer_free(&fresh);
  rmr_buffer_free(&updated);
  free(overlays);
  grow_free(&overlay);
  grow_free(&base);
}

/* A rewrite of a small update renders the same pages as rmr_pdf_rewrite of
 * that update: the same geometry, the same objects drawn. */
static void fresh_matches_rewrite(void) {
  fixture f = source(false), ov = overlay_fixture();
  rmr_pdf_overlay o = {.page = 1,
                       .pdf = (uint8_t*)ov.bytes,
                       .size = ov.n,
                       .transform = {1, 0, 0, 1, 0, 0}};
  rmr_buffer painted = {0}, rewritten = {0}, fresh = {0};
  assert(rmr_pdf_incremental((uint8_t*)f.bytes, f.n, &o, 1, nullptr, nullptr,
                             nullptr, &painted, nullptr, nullptr) == RMR_OK);
  assert(rmr_pdf_rewrite(painted.bytes, painted.size, nullptr, nullptr, nullptr,
                         &rewritten, nullptr) == RMR_OK);
  rmr_pdf_update* u = nullptr;
  assert(rmr_pdf_update_create((uint8_t*)f.bytes, f.n, true, nullptr, nullptr,
                               nullptr, nullptr, &u, nullptr) == RMR_OK);
  assert(rmr_pdf_update_page(u, &o, 1, nullptr) == RMR_OK);
  /* Each page takes its overlays once. */
  rmr_error error = {0};
  assert(rmr_pdf_update_page(u, &o, 1, &error) == RMR_ERR_INVALID_ARGUMENT);
  rmr_pdf_update_free(u);

  assert(rmr_pdf_update_create((uint8_t*)f.bytes, f.n, true, nullptr, nullptr,
                               nullptr, nullptr, &u, nullptr) == RMR_OK);
  assert(rmr_pdf_update_page(u, &o, 1, nullptr) == RMR_OK);
  assert(rmr_pdf_update_finish(u, &fresh, nullptr, nullptr) == RMR_OK);
  rmr_pdf_update_free(u);

  rmr_pdf_page_geometry a[2], b[2];
  size_t na = 0, nb = 0;
  assert(rmr_pdf_pages(rewritten.bytes, rewritten.size, nullptr, a, 2, &na,
                       nullptr) == RMR_OK);
  assert(rmr_pdf_pages(fresh.bytes, fresh.size, nullptr, b, 2, &nb, nullptr) ==
         RMR_OK);
  assert(na == 2 && nb == 2 && !memcmp(a, b, sizeof a));
  assert(!memcmp(fresh.bytes, rewritten.bytes, 9));
  assert(has_text(fresh.bytes, fresh.size, "<metadata>unchanged</metadata>"));
  assert(has_text(fresh.bytes, fresh.size, "1 0 0 rg 50 50 100 100 re f"));
  assert(!has_text(fresh.bytes, fresh.size, "/Prev"));
  rewrite_check(fresh.bytes, fresh.size, 2);
  rmr_buffer_free(&fresh);
  rmr_buffer_free(&rewritten);
  rmr_buffer_free(&painted);
}

/* The limits stay what an adversary meets: per page, a page's overlays get
 * the whole work budget and no more, however many pages share the file; a
 * dictionary naming a key twice is still refused, however large; the file
 * as a whole stays within maximum_pdf_bytes. */
static void page_budget_check(void) {
  generated base = long_source(200), small = dense_overlay(64, false);
  generated large = dense_overlay(5000, false), twice = dense_overlay(40, true);
  rmr_buffer out = {(uint8_t*)(uintptr_t)1, 19};

  /* Thousands of images on one page cost n log n to check, inside the
   * default budget; one page cannot exceed a budget of its own. */
  assert(update_all(&base, &large, 1, false, nullptr, nullptr, &out, nullptr,
                    nullptr) == RMR_OK);
  rmr_buffer_free(&out);
  rmr_limits limits = rmr_limits_default();
  limits.maximum_elements = 30000;
  out = (rmr_buffer){(uint8_t*)(uintptr_t)1, 19};
  assert(update_all(&base, &large, 1, false, &limits, nullptr, &out, nullptr,
                    nullptr) == RMR_ERR_LIMIT);
  assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);
  /* Two hundred pages each well inside it pass, together far beyond it. */
  assert(update_all(&base, &small, 200, false, &limits, nullptr, &out, nullptr,
                    nullptr) == RMR_OK);
  rmr_buffer_free(&out);

  limits = rmr_limits_default();
  limits.maximum_plan_bytes = 1 << 20;
  out = (rmr_buffer){(uint8_t*)(uintptr_t)1, 19};
  assert(update_all(&base, &large, 1, true, &limits, nullptr, &out, nullptr,
                    nullptr) == RMR_ERR_LIMIT);
  assert(update_all(&base, &small, 200, true, &limits, nullptr, &out, nullptr,
                    nullptr) == RMR_OK);
  rmr_buffer_free(&out);

  out = (rmr_buffer){(uint8_t*)(uintptr_t)1, 19};
  assert(update_all(&base, &twice, 1, false, nullptr, nullptr, &out, nullptr,
                    nullptr) == RMR_ERR_INVALID_ARGUMENT);
  assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);

  limits = rmr_limits_default();
  limits.maximum_pdf_bytes = base.n + 20 * small.n;
  assert(update_all(&base, &small, 200, false, &limits, nullptr, &out, nullptr,
                    nullptr) == RMR_ERR_LIMIT);
  assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 19);

  grow_free(&twice);
  grow_free(&large);
  grow_free(&small);
  grow_free(&base);
}

/* A one-page overlay drawing a one-bit mask of byte `mask`, width wide, in a
 * gray ICC color space, the way a page of grain-textured ink brings its
 * masks and profile. With cycle, its graphics state takes part in a
 * reference cycle; with optional, a second image is optional content. */
static generated textured_overlay(char mask, unsigned width, bool cycle,
                                  bool optional) {
  generated g = {0};
  size_t next = 5, image = next++, space = next++, profile = next++;
  size_t state = cycle ? next++ : 0, back = cycle ? next++ : 0;
  size_t layered = optional ? next++ : 0, group = optional ? next++ : 0;
  grow_add(&g, "%%PDF-1.7\n");
  grow_object(&g);
  grow_add(&g, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  grow_object(&g);
  grow_add(&g, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
  grow_object(&g);
  grow_add(&g,
           "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 "
           "0 R /Resources << /XObject << /I0 %zu 0 R",
           image);
  if (optional) grow_add(&g, " /I1 %zu 0 R", layered);
  grow_add(&g, " >> /ColorSpace << /C0 %zu 0 R >>", space);
  if (cycle) grow_add(&g, " /ExtGState << /G0 %zu 0 R >>", state);
  grow_add(&g, " >> >>\nendobj\n");
  const char* paint = "q /G0 gs Q q 9 0 0 9 0 0 cm /I0 Do Q";
  grow_object(&g);
  grow_add(&g, "<< /Length %zu >>\nstream\n%s\nendstream\nendobj\n",
           strlen(paint), paint);
  grow_object(&g);
  grow_add(&g,
           "<< /Type /XObject /Subtype /Image /Width %u /Height 1 /ImageMask "
           "true /BitsPerComponent 1 /Length 1 >>\nstream\n%c\nendstream\n"
           "endobj\n",
           width, mask);
  grow_object(&g);
  grow_add(&g, "[/ICCBased %zu 0 R]\nendobj\n", profile);
  grow_object(&g);
  grow_add(&g,
           "<< /N 1 /Alternate /DeviceGray /Length 12 >>\nstream\nTESTPROFILE!"
           "\nendstream\nendobj\n");
  if (cycle) {
    grow_object(&g);
    grow_add(&g, "<< /Type /ExtGState /ca 0.5 /Cycle %zu 0 R >>\nendobj\n",
             back);
    grow_object(&g);
    grow_add(&g, "<< /Back %zu 0 R >>\nendobj\n", state);
  }
  if (optional) {
    grow_object(&g);
    grow_add(&g,
             "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ImageMask "
             "true /BitsPerComponent 1 /OC %zu 0 R /Length 1 >>\nstream\nO\n"
             "endstream\nendobj\n",
             group);
    grow_object(&g);
    grow_add(&g, "<< /Type /OCG /Name (Layer) >>\nendobj\n");
  }
  grow_finish(&g);
  return g;
}

static size_t count_text(const uint8_t* bytes, size_t size, const char* text) {
  size_t n = strlen(text), found = 0;
  for (size_t i = 0; i + n <= size; i++)
    if (!memcmp(bytes + i, text, n)) found++;
  return found;
}

/* Page i of base given overlays[i % count], through an update (fresh: a
 * rewrite) written to output when one is given. */
static rmr_status update_cycling(const generated* base,
                                 const generated* overlays, size_t count,
                                 size_t pages, bool fresh,
                                 const rmr_output* output, rmr_buffer* out) {
  rmr_pdf_update* u = nullptr;
  rmr_status s =
      rmr_pdf_update_create((const uint8_t*)base->bytes, base->n, fresh,
                            nullptr, nullptr, nullptr, output, &u, nullptr);
  for (size_t i = 0; s == RMR_OK && i < pages; i++) {
    const generated* g = &overlays[i % count];
    rmr_pdf_overlay o = {.page = i,
                         .pdf = (const uint8_t*)g->bytes,
                         .size = g->n,
                         .transform = {1, 0, 0, 1, 0, 0}};
    s = rmr_pdf_update_page(u, &o, 1, nullptr);
  }
  if (s == RMR_OK) s = rmr_pdf_update_finish(u, out, nullptr, nullptr);
  rmr_pdf_update_free(u);
  return s;
}

/* Masks and profiles identical across pages are written once and referred
 * to by every later page, in an update and a rewrite alike; masks differing
 * in their bytes or their dimensions stay apart; objects in a cycle and
 * optional content are each page's own. The file is the same, run again or
 * streamed. */
static void shared_objects_check(void) {
  enum { PAGES = 30 };
  generated base = long_source(PAGES);
  generated overlays[] = {textured_overlay('A', 1, true, false),
                          textured_overlay('B', 1, true, false),
                          textured_overlay('A', 2, true, false)};
  for (int fresh = 0; fresh < 2; fresh++) {
    rmr_buffer out = {0}, again = {0};
    assert(update_cycling(&base, overlays, 3, PAGES, fresh, nullptr, &out) ==
           RMR_OK);
    assert(count_text(out.bytes, out.size, "/ImageMask true") == 3);
    assert(count_text(out.bytes, out.size, "/ICCBased") == 1);
    assert(count_text(out.bytes, out.size, "TESTPROFILE!") == 1);
    assert(count_text(out.bytes, out.size, "/Cycle") == PAGES);
    assert(count_text(out.bytes, out.size, "/Back") == PAGES);
    assert(count_text(out.bytes, out.size, "/Subtype /Form") == PAGES);
    size_t count = 0;
    assert(rmr_pdf_pages(out.bytes, out.size, nullptr, nullptr, 0, &count,
                         nullptr) == RMR_OK &&
           count == PAGES);
    /* Every reference resolves: a rewrite renumbers them all. */
    rmr_buffer rewritten = {0};
    assert(rmr_pdf_rewrite(out.bytes, out.size, nullptr, nullptr, nullptr,
                           &rewritten, nullptr) == RMR_OK);
    rmr_buffer_free(&rewritten);

    assert(update_cycling(&base, overlays, 3, PAGES, fresh, nullptr, &again) ==
           RMR_OK);
    assert(again.size == out.size && !memcmp(again.bytes, out.bytes, out.size));
    collected sink = {0};
    rmr_output output = {RMR_OUTPUT_ABI, sizeof output, collect_output, &sink};
    assert(update_cycling(&base, overlays, 3, PAGES, fresh, &output, nullptr) ==
           RMR_OK);
    assert(sink.bytes.n == out.size &&
           !memcmp(sink.bytes.bytes, out.bytes, out.size));
    grow_free(&sink.bytes);
    rmr_buffer_free(&again);
    rmr_buffer_free(&out);
  }

  /* Optional content keeps its identity, and the image it governs with it;
   * the mask and profile beside them are still shared. */
  generated layered = textured_overlay('A', 1, false, true);
  rmr_buffer out = {0};
  assert(update_cycling(&base, &layered, 1, PAGES, false, nullptr, &out) ==
         RMR_OK);
  assert(count_text(out.bytes, out.size, "/Type /OCG") == PAGES);
  assert(count_text(out.bytes, out.size, "/OC ") == PAGES);
  assert(count_text(out.bytes, out.size, "/ImageMask true") == PAGES + 1);
  assert(count_text(out.bytes, out.size, "TESTPROFILE!") == 1);
  rmr_buffer_free(&out);

  grow_free(&layered);
  for (size_t i = 0; i < 3; i++) grow_free(&overlays[i]);
  grow_free(&base);
}

int main(void) {
  long_document_check();
  fresh_matches_rewrite();
  page_budget_check();
  shared_objects_check();
  select_check();
  select_labels_check();
  reserved_check();
  quartz_row_check();

  fixture f = source(false), ov = overlay_fixture();
  rmr_pdf_overlay o = {
      .pdf = (uint8_t*)ov.bytes, .size = ov.n, .transform = {1, 0, 0, 1, 0, 0}};

  rewrite_check((uint8_t*)f.bytes, f.n, 2);
  rewrite_failures(&f);

  rmr_buffer output = {0};
  assert(run(&f, &o, nullptr, &output) == RMR_OK);
  assert(output.size > f.n && !memcmp(output.bytes, f.bytes, f.n));
  rmr_buffer_free(&output);

  for (unsigned flags = 0; flags < 16; flags++) {
    bool packed = (flags & 1) != 0, compressed = (flags & 2) != 0;
    bool predictor = compressed && (flags & 4), hybrid = (flags & 8) != 0;
    fixture input =
        stream_fixture(packed, compressed, predictor, hybrid, false);
    assert(run(&input, &o, nullptr, &output) == RMR_OK);
    assert(output.size > input.n &&
           !memcmp(output.bytes, input.bytes, input.n));
    rmr_buffer again = {0};
    assert(rmr_pdf_incremental(output.bytes, output.size, &o, 1, nullptr,
                               nullptr, nullptr, &again, nullptr,
                               nullptr) == RMR_OK);
    assert(again.size > output.size &&
           !memcmp(again.bytes, output.bytes, output.size));
    rewrite_check(again.bytes, again.size, 1);
    rmr_buffer_free(&again);
    rmr_buffer_free(&output);
  }

  fixture compressed_input = stream_fixture(true, true, true, true, false);
  size_t failed_allocations = 0;
  for (; failed_allocations < 3000; failed_allocations++) {
    rmr_buffer tested = {(uint8_t*)(uintptr_t)1, 19};
    rmr_test_fail_after(failed_allocations);
    rmr_status status = run(&compressed_input, &o, nullptr, &tested);
    rmr_test_fail_after(SIZE_MAX);
    if (status == RMR_OK) {
      rmr_buffer_free(&tested);
      break;
    }
    assert(status == RMR_ERR_OUT_OF_MEMORY);
    assert(tested.bytes == (uint8_t*)(uintptr_t)1 && tested.size == 19);
  }
  assert(failed_allocations < 3000);

  fixture invalid_index = stream_fixture(true, true, true, true, true);
  assert(run(&invalid_index, &o, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);

  /* Quartz's unused live offset-zero row is an absent object, in an overlay
   * and in a source alike. Once reachable, here by shadowing the overlay's
   * content stream in a later revision, it is a hard error. */
  fixture reserved = ov;
  revision(&reserved, "6 1\n0000000000 00000 n ", "");
  rmr_pdf_overlay unused = o;
  unused.pdf = (uint8_t*)reserved.bytes;
  unused.size = reserved.n;
  assert(run(&f, &unused, nullptr, &output) == RMR_OK);
  rmr_buffer_free(&output);
  assert(run(&reserved, nullptr, nullptr, &output) == RMR_OK);
  rmr_buffer_free(&output);
  revision(&reserved, "4 1\n0000000000 00000 n ", "");
  unused.size = reserved.n;
  assert(run(&f, &unused, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);

  /* The default unit may be written as an integer or real. Non-default and
   * nonnumeric values are refused; no truncation of 1.5 to 1 is allowed. */
  const char* units[] = {"1", "1.0", "+1.00", "2", "1.5", "0", "null"};
  for (size_t i = 0; i < sizeof units / sizeof *units; i++) {
    fixture scaled = f;
    char page_body[256], row[64];
    snprintf(page_body, sizeof page_body,
             "<< /Type /Page /Parent 2 0 R /Contents 16 0 R /UserUnit %s >>",
             units[i]);
    obj(&scaled, 3, 7, page_body);
    snprintf(row, sizeof row, "3 1\n%010zu 00007 n ", scaled.offsets[3]);
    revision(&scaled, row, "");
    rmr_buffer changed = {(uint8_t*)(uintptr_t)1, 17};
    rmr_status status = run(&scaled, &o, nullptr, &changed);
    if (i < 3) {
      assert(status == RMR_OK);
      rmr_buffer_free(&changed);
    } else {
      assert(status == RMR_ERR_UNSUPPORTED);
      assert(changed.bytes == (uint8_t*)(uintptr_t)1 && changed.size == 17);
    }
  }

  size_t allocations = 0;
  for (; allocations < 2000; allocations++) {
    rmr_buffer out = {(uint8_t*)(uintptr_t)1, 22};
    rmr_test_fail_after(allocations);
    rmr_status s = run(&f, &o, nullptr, &out);
    rmr_test_fail_after(SIZE_MAX);
    if (s == RMR_OK) {
      rmr_buffer_free(&out);
      break;
    }
    assert(s == RMR_ERR_OUT_OF_MEMORY);
    assert(out.bytes == (uint8_t*)(uintptr_t)1 && out.size == 22);
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
  add(&bad, "17 0 obj\n<< /Type /XRef >>\nendobj\nstartxref\n%zu\n%%%%EOF\n",
      x);
  assert(run(&bad, &o, nullptr, &output) == RMR_ERR_INVALID_ARGUMENT);

  bad = f;
  revision(&bad, "0 1\n0000000000 65535 f ", "");
  assert(run(&bad, &o, nullptr, &output) == RMR_OK);
  rmr_buffer_free(&output);

  bad = f;
  char* page = strstr(bad.bytes, "/Kids [3 7 R");
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
      "xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 17 /Root 1 0 R /Prev "
      "0000000000 >>\n");
  size_t main_offset = bad.n;
  char* prev = strstr(bad.bytes + first, "/Prev ");
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
  assert(rmr_pdf_incremental((uint8_t*)f.bytes, f.n, duplicate, 2, nullptr,
                             nullptr, nullptr, &output, nullptr,
                             nullptr) == RMR_ERR_INVALID_ARGUMENT);

  puts("portable PDF update/refusal/allocation tests passed");
  return 0;
}
