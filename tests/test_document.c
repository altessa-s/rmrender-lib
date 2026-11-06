/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmrender/document.h>
#include <stdarg.h>
#include <stdint.h>

#include "test.h"

enum { MANY = 2000 };

typedef struct {
  char* bytes;
  size_t size, capacity;
} text;

/* Append printf-formatted text to t, growing it as needed. */
static void append(text* t, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int n = vsnprintf(nullptr, 0, format, args);
  va_end(args);
  CHECK(n >= 0);
  if (t->size + (size_t)n + 1 > t->capacity) {
    t->capacity = (t->size + (size_t)n + 1) * 2;
    t->bytes = realloc(t->bytes, t->capacity);
    CHECK(t->bytes);
  }
  va_start(args, format);
  vsnprintf(t->bytes + t->size, t->capacity - t->size, format, args);
  va_end(args);
  t->size += (size_t)n;
}

/* Deterministic UUID-shaped page id for page i, in no particular order. */
static void page_id(size_t i, char out[37]) {
  uint64_t x = (i + 1) * 0x9e3779b97f4a7c15u;
  x ^= x >> 31;
  x *= 0xbf58476d1ce4e5b9u;
  x ^= x >> 27;
  snprintf(out, 37, "%08x-%04x-4%03x-8%03x-%012llx", (unsigned)(x >> 32),
           (unsigned)(x >> 16) & 0xffff, (unsigned)x & 0xfff,
           (unsigned)(i & 0xfff), (unsigned long long)(i * 7919));
}

/* Device-style cPages .content for MANY pages (template, redirect and a
 * fractional idx on each, scroll state on every third), followed by extra
 * entries: page 10's id again under an idx that sorts first, page 20's again
 * under one that sorts last, and a deleted entry for page 30's id. */
static text crdt_content(void) {
  text t = {0};
  char id[37];
  append(&t,
         "{\"cPages\":{\"lastOpened\":{\"timestamp\":\"1:1\",\"value\":"
         "\"\"},\"pages\":[");
  for (size_t i = 0; i < MANY; i++) {
    page_id(i, id);
    append(&t,
           "%s{\"id\":\"%s\",\"idx\":{\"timestamp\":\"1:2\",\"value\":"
           "\"b%c%c%c\"},\"redir\":{\"timestamp\":\"1:2\",\"value\":%zu},"
           "\"template\":{\"timestamp\":\"1:1\",\"value\":\"Blank\"}",
           i ? "," : "", id, 'a' + (int)(i / 676 % 26),
           'a' + (int)(i / 26 % 26), 'a' + (int)(i % 26), i);
    if (i % 3 == 0)
      append(&t,
             ",\"scrollTime\":{\"timestamp\":\"1:1\",\"value\":"
             "\"1715000000000\"},\"verticalScroll\":{\"timestamp\":"
             "\"1:1\",\"value\":0}");
    append(&t, "}");
  }
  page_id(10, id);
  append(&t,
         ",{\"id\":\"%s\",\"idx\":{\"value\":\"a\"},\"template\":{\"value\":"
         "\"Grid\"}}",
         id);
  page_id(20, id);
  append(&t,
         ",{\"id\":\"%s\",\"idx\":{\"value\":\"zz\"},\"redir\":{\"value\":9}}",
         id);
  page_id(30, id);
  append(&t, ",{\"id\":\"%s\",\"deleted\":{\"value\":1}}", id);
  append(&t,
         "]},\"customZoomPageHeight\":1872,\"customZoomPageWidth\":1404,"
         "\"formatVersion\":2,\"orientation\":\"portrait\"}");
  return t;
}

/* Legacy pages[]/redirectionPageMap .content for MANY pages, with page 5's
 * and page 7's ids listed again at the end under other redirects. */
static text flat_content(void) {
  text t = {0};
  char id[37];
  append(&t, "{\"pages\":[");
  for (size_t i = 0; i < MANY; i++) {
    page_id(i, id);
    append(&t, "%s\"%s\"", i ? "," : "", id);
  }
  page_id(5, id);
  append(&t, ",\"%s\"", id);
  page_id(7, id);
  append(&t, ",\"%s\"],\"redirectionPageMap\":[", id);
  for (size_t i = 0; i < MANY; i++) append(&t, "%zu,", i);
  append(&t, "1,2]}");
  return t;
}

/* Thousands of pages decode under the default limits, and duplicate ids keep
 * the first entry in page order (idx order for cPages, list order for the
 * legacy pages[]); ids sharing long prefixes still exhaust the work budget. */
static void many_pages(void) {
  char id[37];
  rmr_document_layout* d = nullptr;
  text t = crdt_content();
  OK(rmr_document_layout_decode(t.bytes, t.size, nullptr, &d, nullptr));
  CHECK(rmr_document_layout_count(d) == MANY &&
        rmr_document_layout_deleted_count(d) == 1);
  page_id(30, id);
  CHECK(!strcmp(rmr_document_layout_deleted(d, 0), id));
  page_id(10, id);
  const rmr_document_page* first = rmr_document_layout_page(d, 0);
  CHECK(!strcmp(first->id, id) && !strcmp(first->template_name, "Grid") &&
        !first->has_source);
  for (size_t i = 0, at = 1; i < MANY; i++) {
    if (i == 10) continue;
    const rmr_document_page* page = rmr_document_layout_page(d, at++);
    page_id(i, id);
    CHECK(!strcmp(page->id, id) && !strcmp(page->template_name, "Blank"));
    CHECK(page->has_source && page->source_page == i);
  }
  rmr_document_layout_free(d);
  free(t.bytes);

  t = flat_content();
  OK(rmr_document_layout_decode(t.bytes, t.size, nullptr, &d, nullptr));
  CHECK(rmr_document_layout_count(d) == MANY &&
        rmr_document_layout_deleted_count(d) == 0);
  for (size_t i = 0; i < MANY; i++) {
    const rmr_document_page* page = rmr_document_layout_page(d, i);
    page_id(i, id);
    CHECK(!strcmp(page->id, id) && page->has_source && page->source_page == i);
  }
  rmr_document_layout_free(d);
  free(t.bytes);

  t = (text){0};
  append(&t, "{\"pages\":[");
  for (size_t i = 0; i < MANY; i++)
    append(&t, "%s\"%01000zu\"", i ? "," : "", i);
  append(&t, "]}");
  rmr_error error = {0};
  d = nullptr;
  CHECK(rmr_document_layout_decode(t.bytes, t.size, nullptr, &d, &error) ==
            RMR_ERR_LIMIT &&
        !d && !strcmp(error.message, "JSON work limit"));
  free(t.bytes);
}

int main(void) {
  many_pages();

  const char json[] =
      "{\"orientation\":{\"value\":\"landscape\"},\"customZoomPageWidth\":1404,"
      "\"customZoomPageHeight\":1872,\"cPages\":{\"pages\":["
      "{\"id\":\"b\",\"idx\":{\"value\":\"bb\"},\"redir\":{\"value\":1}},"
      "{\"id\":\"dead\",\"deleted\":{\"value\":1}},"
      "{\"id\":\"a\",\"idx\":{\"value\":\"ba\"},\"template\":{\"value\":"
      "\"Grid\"}"
      ",\"redir\":{\"value\":1.5}},"
      "{\"id\":\"b\",\"idx\":{\"value\":\"bc\"}}]}}";
  rmr_document_layout* d = nullptr;
  OK(rmr_document_layout_decode(json, sizeof json - 1, nullptr, &d, nullptr));
  CHECK(rmr_document_layout_count(d) == 2 &&
        rmr_document_layout_deleted_count(d) == 1);
  CHECK(!strcmp(rmr_document_layout_page(d, 0)->id, "a"));
  CHECK(!strcmp(rmr_document_layout_page(d, 0)->template_name, "Grid"));
  CHECK(rmr_document_layout_page(d, 0)->refused_source);
  CHECK(rmr_document_layout_page(d, 1)->source_page == 1);
  CHECK(rmr_document_layout_landscape(d) &&
        rmr_document_layout_canvas(d).x == 1404);
  rmr_document_page checked =
      rmr_document_page_checked(*rmr_document_layout_page(d, 1), 1);
  CHECK(!checked.has_source && checked.refused_source);

  for (size_t i = 0; i < sizeof json - 2; i++) {
    rmr_document_layout* sentinel = d;
    CHECK(rmr_document_layout_decode(json, i, nullptr, &sentinel, nullptr) !=
          RMR_OK);
    CHECK(sentinel == d);
  }
  rmr_document_layout_free(d);

  const char flat[] =
      "{\"pages\":[\"x\",\"x\",\"y\"],\"redirectionPageMap\":[0,9,-1]}";
  OK(rmr_document_layout_decode(flat, sizeof flat - 1, nullptr, &d, nullptr));
  CHECK(rmr_document_layout_count(d) == 2 &&
        rmr_document_layout_page(d, 0)->source_page == 0 &&
        rmr_document_layout_page(d, 1)->refused_source);
  rmr_document_layout_free(d);

  const char exact[] =
      "{\"pages\":[\"a\",\"b\",\"c\",\"d\",\"e\"],\"redirectionPageMap\":["
      "9223372036854775807,1."
      "0000000000000000001,10e-1,1e-9999,9223372036854775808]}";
  OK(rmr_document_layout_decode(exact, sizeof exact - 1, nullptr, &d, nullptr));
  CHECK(rmr_document_layout_page(d, 0)->source_page == (size_t)INT64_MAX);
  CHECK(rmr_document_layout_page(d, 1)->refused_source);
  CHECK(rmr_document_layout_page(d, 2)->has_source &&
        rmr_document_layout_page(d, 2)->source_page == 1);
  CHECK(rmr_document_layout_page(d, 3)->refused_source &&
        rmr_document_layout_page(d, 4)->refused_source);
  rmr_document_layout_free(d);

  rmr_plan *a = rectangle((rmr_color){1, 0, 0, 1}, RMR_BLEND_NORMAL),
           *b = nullptr;
  const rmr_plan* parts[] = {a, a};
  OK(rmr_plan_concat(parts, 2, (rmr_rect){-32, 0, 64, 64}, nullptr, &b,
                     nullptr));
  rmr_plan_free(a);
  CHECK(rmr_plan_count(b) == 2);
  rmr_plan_free(b);

  return 0;
}
