/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* The PDF writers' shared pieces: CoreGraphics-compatible number printing,
 * the ordered deflate pipeline and the streaming builder. */

#include <rmrender/pdf.h>
#include <stdarg.h>
#include <zlib.h>

#include "../backends/common/rmr_pdf_options.h"
#include "../backends/common/rmr_pdf_stream.h"
#include "../src/rmr_pdf_edit.h"
#include "pdf_number_reference.h"
#include "test.h"

static uint64_t state = 0x9e3779b97f4a7c15u;

static uint64_t next(void) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

static double uniform(double low, double high) {
  return low + (high - low) * (double)(next() >> 11) / 9007199254740992.0;
}

static size_t format_c(char* out, size_t size, const char* f, ...) {
  va_list a;
  va_start(a, f);
  int n = vsnprintf(out, size, f, a);
  va_end(a);
  CHECK(n > 0 && (size_t)n < size);
  return (size_t)n;
}

/* CoreGraphics' PDFWriteNumber, decision for decision, printing through
 * printf as it does: the reference the fast formatter must equal. */
static size_t reference(char* out, size_t size, double v) {
  if (isnan(v)) return format_c(out, size, "0");
  if (isinf(v)) return format_c(out, size, "%d", v > 0 ? INT32_MAX : INT32_MIN);
  if (v >= -2147483648.0 && v <= 2147483647.0) {
    double r = v < 0 ? ceil(v - 0.5) : floor(v + 0.5);
    int n = (int)r;
    if (fabs(v - (double)n) < 1e-6) return format_c(out, size, "%d", n);
  }
  double a = fabs(v);
  if (a >= 1)
    return a > 1e7 ? format_c(out, size, "%0.0f", v)
                   : format_c(out, size, "%0.7g", v);
  if (a < 1e-6) return format_c(out, size, "0");
  unsigned leading = 0, extra = 0, multiplier = 1;
  while (!(a * (double)multiplier >= 1)) {
    multiplier *= 10;
    if (++leading == 6) break;
  }
  for (;;) {
    double x = a * (double)multiplier;
    if (fabs(x - round(x)) < 1e-5) break;
    multiplier *= 10;
    if (++extra == 6) break;
  }
  return format_c(out, size, "%0.*f", (int)(leading + extra), v);
}

static void check_number(double v) {
  char mine[RMR_PDF_NUMBER_SIZE], expected[512];
  size_t n = rmr_pdf_number(mine, v),
         m = reference(expected, sizeof expected, v);
  if (n != m || memcmp(mine, expected, n)) {
    fprintf(stderr, "%a: %.*s, printf %s\n", v, (int)n, mine, expected);
    CHECK(false);
  }
}

static void numbers(void) {
  /* Exactly what CoreGraphics printed. */
  for (size_t i = 0;
       i < sizeof pdf_number_reference / sizeof *pdf_number_reference; i++) {
    char mine[RMR_PDF_NUMBER_SIZE];
    size_t n = rmr_pdf_number(mine, pdf_number_reference[i].value);
    const char* text = pdf_number_reference[i].text;
    if (n != strlen(text) || memcmp(mine, text, n)) {
      fprintf(stderr, "%a: %.*s, CoreGraphics %s\n",
              pdf_number_reference[i].value, (int)n, mine, text);
      CHECK(false);
    }
  }

  /* Edges: ties, powers of ten, the int32 range, tiny and huge values. */
  const double edges[] = {0,
                          -0.0,
                          1,
                          -1,
                          0.5,
                          1e-6,
                          9.99999e-7,
                          1.5e-6,
                          1e-5,
                          1.25e-5,
                          0.30000001,
                          0.9999999,
                          0.99999995,
                          9999999.4,
                          9999999.5,
                          1e7,
                          10000000.5,
                          10000001.5,
                          2147483647,
                          2147483647.5,
                          -2147483648,
                          -2147483649,
                          4503599627370495.5,
                          1e15,
                          1e20,
                          1e300,
                          -1e300,
                          INFINITY,
                          -INFINITY,
                          NAN,
                          5e-324,
                          123.456049,
                          123.45605,
                          0.0012697600570558986};
  for (size_t i = 0; i < sizeof edges / sizeof *edges; i++)
    check_number(edges[i]);
  for (int k = -12; k <= 12; k++)
    for (int j = 1; j < 10; j++) {
      check_number(j * pow(10, k));
      check_number(-j * pow(10, k));
      check_number(nextafter(j * pow(10, k), 0));
      check_number(nextafter(j * pow(10, k), INFINITY));
    }

  /* Random values, uniform and across magnitudes, and exact 7-digit ties. */
  for (int i = 0; i < 300000; i++) {
    check_number(uniform(-2000, 2000));
    check_number((next() & 1 ? 1 : -1) * pow(10, uniform(-8, 9)));
    double tie = ((double)(1000000 + next() % 9000000) + 0.5) *
                 pow(10, (double)(int)(next() % 13) - 12);
    check_number(tie);
  }
  for (int i = 0; i < 100000; i++) {
    uint64_t bits = next();
    double v;
    memcpy(&v, &bits, sizeof v);
    check_number(v);
  }

  /* Rounded coordinates: at most the requested decimals, no -0, nearest. */
  char out[RMR_PDF_NUMBER_SIZE];
  CHECK(rmr_pdf_decimal(out, -0.001, 2) == 1 && out[0] == '0');
  CHECK(rmr_pdf_decimal(out, 142.0369, 2) == 6 && !memcmp(out, "142.04", 6));
  CHECK(rmr_pdf_decimal(out, 3.1, 2) == 3 && !memcmp(out, "3.1", 3));
  CHECK(rmr_pdf_decimal(out, -7.999, 2) == 2 && !memcmp(out, "-8", 2));
  for (int i = 0; i < 100000; i++) {
    double v = uniform(-1e5, 1e5);
    unsigned d = 1 + (unsigned)(next() % 6);
    size_t n = rmr_pdf_decimal(out, v, d);
    out[n] = 0;
    double back = strtod(out, nullptr);
    CHECK(fabs(back - v) <= 0.5000001 * pow(10, -(double)d));
    const char* dot = strchr(out, '.');
    CHECK(!dot || strlen(dot + 1) <= d);
  }
}

/* A batch of stream objects whose data is drawn from seed. */
static rmr_pdf_batch* batch(uint32_t first, size_t count, uint64_t seed) {
  rmr_pdf_batch* b = calloc(1, sizeof *b);
  CHECK(b);
  for (size_t i = 0; i < count; i++) {
    rmr_pdf_object* o = rmr_pdf_batch_add(b, first + (uint32_t)i, i % 3 != 2);
    CHECK(o);
    if (o->stream) {
      size_t size = (size_t)(seed % 50000);
      o->data = malloc(size + 1);
      CHECK(o->data);
      for (size_t k = 0; k < size; k++)
        o->data[k] = (uint8_t)("0123456789 lmh."[(seed >> (k % 40)) % 15]);
      o->size = size;
      rmr_pdf_text_format(&o->dict, "/Seed %llu", (unsigned long long)seed);
    } else
      rmr_pdf_text_format(&o->dict, "<< /Seed %llu >>",
                          (unsigned long long)seed);
    seed = seed * 6364136223846793005u + 1442695040888963407u;
  }
  return b;
}

/* Serializes the same batches through a pipeline of the given workers. */
static rmr_pdf_text through(size_t workers, size_t batches) {
  rmr_pdf_pipeline* p = rmr_pdf_pipeline_create(workers);
  CHECK(p);
  rmr_pdf_text all = {0};
  uint32_t number = 1;
  for (size_t i = 0; i < batches; i++) {
    rmr_pdf_batch* b = batch(number, 1 + i % 7, i * 7919 + 13);
    b->tag = i;
    number += (uint32_t)b->count;
    rmr_pdf_pipeline_submit(p, b);
    /* Take finished batches now and then, as the writers do. */
    rmr_pdf_batch* done;
    while ((done = rmr_pdf_pipeline_next(p, rmr_pdf_pipeline_pending(p) > 5))) {
      OK(done->status);
      rmr_pdf_text_append(&all, done->chunk.bytes, done->chunk.size);
      rmr_pdf_batch_free(done);
    }
  }
  rmr_pdf_batch* done;
  while ((done = rmr_pdf_pipeline_next(p, true))) {
    OK(done->status);
    rmr_pdf_text_append(&all, done->chunk.bytes, done->chunk.size);
    rmr_pdf_batch_free(done);
  }
  CHECK(!rmr_pdf_pipeline_pending(p));
  rmr_pdf_pipeline_free(p);
  CHECK(!all.failed);
  return all;
}

static size_t discarded;

static void count_discard(rmr_pdf_batch* b) {
  discarded++;
  free(b->work);
  b->work = nullptr;
}

static void pipeline(void) {
  /* Byte-identical output, in submission order, whatever the workers. */
  rmr_pdf_text serial = through(0, 120);
  for (size_t workers = 1; workers <= 8; workers *= 2)
    for (int run = 0; run < 3; run++) {
      rmr_pdf_text parallel = through(workers, 120);
      CHECK(parallel.size == serial.size &&
            !memcmp(parallel.bytes, serial.bytes, serial.size));
      rmr_pdf_text_free(&parallel);
    }

  /* Objects come out numbered in order, their streams inflating back. */
  const char* at = (const char*)serial.bytes;
  const char* end = at + serial.size;
  uint32_t expected = 1;
  while (at < end) {
    unsigned number = 0;
    CHECK(sscanf(at, "%u 0 obj", &number) == 1 && number == expected++);
    /* The dictionary ends before any stream data, which can hold NULs. */
    const char* stream = memmem(at, (size_t)(end - at), ">>\nstream\n", 10);
    const char* next_object = memmem(at, (size_t)(end - at), "\nendobj\n", 8);
    CHECK(next_object);
    if (stream && stream < next_object) {
      stream += 3;
      const char* length = memmem(at, (size_t)(stream - at), "/Length ", 8);
      CHECK(length);
      size_t size = (size_t)strtoull(length + 8, nullptr, 10);
      uLongf decoded = 60000;
      uint8_t* data = malloc(decoded);
      CHECK(data);
      CHECK(uncompress(data, &decoded, (const Bytef*)stream + 7, (uLong)size) ==
            Z_OK);
      free(data);
      next_object = stream + 7 + size;
      CHECK(!memcmp(next_object, "\nendstream\nendobj\n", 18));
      next_object += 10;
    }
    at = next_object + 8;
  }
  rmr_pdf_text_free(&serial);

  /* Freeing a pipeline with work queued releases it. */
  rmr_pdf_pipeline* p = rmr_pdf_pipeline_create(2);
  CHECK(p);
  for (int i = 0; i < 50; i++) {
    rmr_pdf_batch* b = batch(1, 3, (uint64_t)i);
    b->work = malloc(16);
    b->discard = count_discard;
    rmr_pdf_pipeline_submit(p, b);
  }
  rmr_pdf_pipeline_free(p);
  CHECK(discarded <= 50);

  /* A whole-file batch is a valid PDF. */
  rmr_pdf_pipeline* serial_pipeline = rmr_pdf_pipeline_create(0);
  rmr_pdf_batch* b = calloc(1, sizeof *b);
  CHECK(serial_pipeline && b);
  rmr_pdf_text_string(&rmr_pdf_batch_add(b, 1, false)->dict,
                      "<< /Type /Catalog /Pages 2 0 R >>");
  rmr_pdf_text_string(&rmr_pdf_batch_add(b, 2, false)->dict,
                      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
  rmr_pdf_text_string(&rmr_pdf_batch_add(b, 3, false)->dict,
                      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 20] >>");
  b->root = 1;
  rmr_pdf_pipeline_submit(serial_pipeline, b);
  b = rmr_pdf_pipeline_next(serial_pipeline, true);
  OK(b->status);
  size_t count = 0;
  rmr_pdf_page_geometry geometry;
  OK(rmr_pdf_pages(b->chunk.bytes, b->chunk.size, nullptr, &geometry, 1, &count,
                   nullptr));
  CHECK(count == 1 && geometry.media.height == 20);
  rmr_pdf_batch_free(b);
  rmr_pdf_pipeline_free(serial_pipeline);
}

/* A one-page PDF drawing a rectangle, with its content in `streams` content
 * streams and an /XObject resource. */
static rmr_buffer one_page(int streams) {
  rmr_pdf_text t = {0};
  size_t offsets[8];
  rmr_pdf_text_string(&t, "%PDF-1.4\n");
  offsets[1] = t.size;
  rmr_pdf_text_string(&t,
                      "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  offsets[2] = t.size;
  rmr_pdf_text_string(
      &t, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
  offsets[3] = t.size;
  rmr_pdf_text_format(&t,
                      "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
                      "200 100] /Resources << /XObject << /I 6 0 R >> >> "
                      "/Contents %s >>\nendobj\n",
                      streams == 1 ? "4 0 R" : "[4 0 R 5 0 R]");
  offsets[4] = t.size;
  rmr_pdf_text_string(&t,
                      "4 0 obj\n<< /Length 15 >>\nstream\n0 0 10 10 re "
                      "f\n\nendstream\nendobj\n");
  offsets[5] = t.size;
  rmr_pdf_text_string(
      &t,
      "5 0 obj\n<< /Length 10 >>\nstream\n/I Do\n0 g\n\nendstream\nendobj\n");
  offsets[6] = t.size;
  rmr_pdf_text_string(&t,
                      "6 0 obj\n<< /Type /XObject /Subtype /Image /Width 1 "
                      "/Height 1 /ImageMask true /Length 1 >>\nstream\n\x80\n"
                      "endstream\nendobj\n");
  size_t xref = t.size;
  rmr_pdf_text_string(&t, "xref\n0 7\n0000000000 65535 f \n");
  for (int i = 1; i <= 6; i++)
    rmr_pdf_text_format(&t, "%010zu 00000 n \n", offsets[i]);
  rmr_pdf_text_format(
      &t, "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n",
      xref);
  CHECK(!t.failed);
  return (rmr_buffer){t.bytes, t.size};
}

static void builder(void) {
  rmr_buffer page = one_page(1), joined = one_page(2);

  /* Pages, forms, links and an outline in a streamed file. */
  rmr_pdf_builder* b = nullptr;
  OK(rmr_pdf_builder_create(nullptr, nullptr, nullptr, &b));
  enum { PAGES = 40 };
  CHECK(rmr_pdf_builder_pages(b, PAGES));
  for (size_t i = 0; i < PAGES; i++) {
    uint32_t forms[2], wrapped;
    OK(rmr_pdf_builder_form(b, page.bytes, page.size, &forms[0], nullptr));
    OK(rmr_pdf_builder_form(b, joined.bytes, joined.size, &forms[1], nullptr));
    OK(rmr_pdf_builder_wrap(b, 200, 100, forms, 2, &wrapped, nullptr));
    rmr_pdf_local_link link = {.page = i,
                               .rect = {1, 2, 3, 4},
                               .destination = {(i + 7) % PAGES, {5, 6}}};
    if (i % 2)
      OK(rmr_pdf_builder_page(b, i, page.bytes, page.size, 0, 0, &wrapped, 1,
                              &link, 1, nullptr));
    else
      OK(rmr_pdf_builder_page(b, i, nullptr, 0, 200, 100, forms, 2, &link, 1,
                              nullptr));
  }
  const uint8_t title[] = {'A', 0, 'B', 0};
  rmr_pdf_outline outline[] = {{SIZE_MAX, title, 4, {3, {0, 0}}, true},
                               {0, title, 2, {PAGES - 1, {1, 1}}, false}};
  rmr_buffer out = {0};
  OK(rmr_pdf_builder_finish(b, outline, 2, &out, nullptr));
  rmr_pdf_builder_free(b);

  /* The bounded parser reads every page; a rewrite round-trips it. */
  size_t count = 0;
  OK(rmr_pdf_pages(out.bytes, out.size, nullptr, nullptr, 0, &count, nullptr));
  CHECK(count == PAGES);
  rmr_buffer rewritten = {0};
  OK(rmr_pdf_rewrite(out.bytes, out.size, nullptr, nullptr, nullptr, &rewritten,
                     nullptr));
  rmr_buffer_free(&rewritten);
  CHECK(memmem(out.bytes, out.size, "/Outlines", 9));
  CHECK(memmem(out.bytes, out.size, "/Subtype /Link", 14));
  CHECK(!memmem(out.bytes, out.size, "/Group", 6));
  rmr_buffer_free(&out);

  /* Output of many chunks comes back whole and exactly sized. */
  OK(rmr_pdf_builder_create(nullptr, nullptr, nullptr, &b));
  CHECK(rmr_pdf_builder_pages(b, 1));
  size_t big = 9 << 20;
  uint8_t* filler = malloc(big);
  CHECK(filler);
  memset(filler, 'x', big);
  for (size_t k = 0; k < 6; k++) {
    uint32_t n = rmr_pdf_builder_reserve(b);
    char head[64];
    int h = snprintf(head, sizeof head, "%u 0 obj\n<< /Length %zu >>\nstream\n",
                     n, big);
    size_t size = (size_t)h + big + 18;
    uint8_t* object = malloc(size);
    CHECK(object);
    memcpy(object, head, (size_t)h);
    memcpy(object + h, filler, big);
    memcpy(object + h + big, "\nendstream\nendobj\n", 18);
    size_t zero = 0;
    OK(rmr_pdf_builder_append(b, object, size, &n, &zero, 1, nullptr));
    free(object);
  }
  free(filler);
  OK(rmr_pdf_builder_page(b, 0, nullptr, 0, 10, 10, nullptr, 0, nullptr, 0,
                          nullptr));
  OK(rmr_pdf_builder_finish(b, nullptr, 0, &out, nullptr));
  rmr_pdf_builder_free(b);
  CHECK(out.size > 6 * big);
  OK(rmr_pdf_pages(out.bytes, out.size, nullptr, nullptr, 0, &count, nullptr));
  CHECK(count == 1);
  rmr_buffer_free(&out);

  /* The output limit holds while streaming. */
  rmr_limits small = rmr_limits_default();
  small.maximum_pdf_bytes = 4096;
  OK(rmr_pdf_builder_create(&small, nullptr, nullptr, &b));
  CHECK(rmr_pdf_builder_pages(b, 1));
  rmr_status s = RMR_OK;
  for (int i = 0; i < 100 && s == RMR_OK; i++) {
    uint32_t form;
    s = rmr_pdf_builder_form(b, page.bytes, page.size, &form, nullptr);
  }
  CHECK(s == RMR_ERR_LIMIT);
  rmr_pdf_builder_free(b);

  /* A page left out fails the file. */
  OK(rmr_pdf_builder_create(nullptr, nullptr, nullptr, &b));
  CHECK(rmr_pdf_builder_pages(b, 2));
  OK(rmr_pdf_builder_page(b, 0, nullptr, 0, 10, 10, nullptr, 0, nullptr, 0,
                          nullptr));
  rmr_error e = {0};
  CHECK(rmr_pdf_builder_finish(b, nullptr, 0, &out, &e) ==
        RMR_ERR_INVALID_ARGUMENT);
  rmr_pdf_builder_free(b);

  rmr_buffer_free(&page);
  rmr_buffer_free(&joined);
}

static void options(void) {
  /* Precision reduction is opt-in: off by default and for callers built
   * before the field existed. */
  rmr_pdf_options o = rmr_pdf_options_default(), read;
  CHECK(o.decimals == 0 && o.grain == RMR_PDF_GRAIN_MASK);
  CHECK(o.struct_size == sizeof o);
  CHECK(rmr_pdf_options_read(nullptr, &read) && read.decimals == 0);
  o.decimals = 2;
  CHECK(rmr_pdf_options_read(&o, &read) && read.decimals == 2);
  o.struct_size = offsetof(rmr_pdf_options, grain) + sizeof o.grain;
  CHECK(rmr_pdf_options_read(&o, &read) && read.decimals == 0);
  o = rmr_pdf_options_default();
  o.decimals = 7;
  CHECK(!rmr_pdf_options_read(&o, &read));
}

int main(void) {
  numbers();
  pipeline();
  builder();
  options();
  puts("pdf stream ok");
  return 0;
}
