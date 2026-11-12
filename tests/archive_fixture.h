/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Archive cases shared by the Apple and Cairo archive tests: page matching,
 * recovery, long documents, PDF options and output. The includer defines
 * DOCUMENT_PDF, DOCUMENT_PDF_WITH_OPTIONS and PAGE_PLAN as its backend's
 * rmr_*_document_pdf, rmr_*_document_pdf_with_options and rmr_*_page_plan,
 * DOCUMENT_PDF_PRESERVING_WITH_OPTIONS, DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS
 * and DOCUMENT_PDF{,_PRESERVING,_RECOMPOSED}_TO_OUTPUT as its other PDF
 * exports, DOCUMENT_SVG_WITH_OPTIONS as its SVG export, FONTS,
 * FONTS_DIRECTORY and FONTS_FREE as its fonts type and the functions making
 * them from a directory and freeing them, and GRAIN_GROUPS as whether
 * RMR_PDF_GRAIN_GROUP changes its output. */
#ifndef ARCHIVE_FIXTURE_H
#define ARCHIVE_FIXTURE_H
#include <rmlines/parser.h>
#include <rmlines/writer.h>
#include <rmrender/archive.h>
#include <rmrender/pdf.h>
#include <stddef.h>

#include "pdf_fixture.h"
#include "test.h"

#define CASE_DOC "44444444-4444-4444-4444-444444444444"

/* A page of n zigzag ballpoint strokes of m points each across the sheet;
 * free with rm_bytes_free. */
static rmd_bytes stroked_page(size_t n, size_t m) {
  rm_draft_point* points = calloc(n * m, sizeof *points);
  rm_draft_stroke* strokes = calloc(n, sizeof *strokes);
  CHECK(points && strokes);
  for (size_t i = 0; i < n; i++) {
    for (size_t j = 0; j < m; j++)
      points[i * m + j] = rm_draft_point_make(
          (float)(-650 + 1300.0 * (double)j / (double)m),
          (float)(40 + 1800.0 * (double)i / (double)n + (j % 2 ? 12 : 0)));
    strokes[i] = rm_draft_stroke_make(RM_TOOL_BALLPOINT_2, RM_COLOR_BLACK,
                                      points + i * m, m);
  }
  rm_page_draft draft = {.sheet = {1404, 1872, 1404, 1872},
                         .column = rm_draft_column_of(RM_COLUMN_MEDIUM),
                         .strokes = strokes,
                         .stroke_count = n};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  CHECK(rm_write_page(&draft, &bytes, &size, nullptr) == RM_OK);
  free(points);
  free(strokes);
  return (rmd_bytes){bytes, size};
}

/* An in-memory archive: names and the .content are owned, page data is
 * borrowed. archive must outlive the documents opened from it. */
typedef struct {
  rmd_file* files;
  char** names;
  char* content;
  size_t count;
  rmd_archive archive;
} built_archive;

/* Append a file named name holding data to a. */
static void put_file(built_archive* a, const char* name, rmd_bytes data) {
  a->files = realloc(a->files, (a->count + 1) * sizeof *a->files);
  a->names = realloc(a->names, (a->count + 1) * sizeof *a->names);
  CHECK(a->files && a->names);
  a->names[a->count] = strdup(name);
  CHECK(a->names[a->count]);
  a->files[a->count] = (rmd_file){{a->names[a->count], strlen(name)}, data};
  a->count++;
}

/* Add CASE_DOC's .content listing ids (cPages), with deleted ids marked and
 * the listed pages on template when it is given. */
static void put_content(built_archive* a, const char* const* ids, size_t n,
                        const char* const* deleted, size_t d,
                        const char* template) {
  size_t size = 64 + (n + d) * (128 + (template ? strlen(template) : 0));
  a->content = malloc(size);
  CHECK(a->content);
  size_t at = (size_t)snprintf(a->content, size, "{\"cPages\":{\"pages\":[");
  for (size_t i = 0; i < n + d; i++) {
    at += (size_t)snprintf(a->content + at, size - at, "%s{\"id\":\"%s\"%s",
                           i ? "," : "", i < n ? ids[i] : deleted[i - n],
                           i < n ? "" : ",\"deleted\":{\"value\":1}");
    if (i < n && template)
      at += (size_t)snprintf(a->content + at, size - at,
                             ",\"template\":{\"value\":\"%s\"}", template);
    at += (size_t)snprintf(a->content + at, size - at, "}");
  }
  at += (size_t)snprintf(a->content + at, size - at, "]}}");
  CHECK(at < size);
  put_file(a, CASE_DOC ".content", (rmd_bytes){(const uint8_t*)a->content, at});
}

static void archive_free(built_archive* a) {
  for (size_t i = 0; i < a->count; i++) free(a->names[i]);
  free(a->names);
  free(a->files);
  free(a->content);
  *a = (built_archive){0};
}

/* Opens a with default limits. */
static rmr_archive_document* open_archive(built_archive* a, bool recover) {
  a->archive = (rmd_archive){.files = a->files, .count = a->count};
  rmr_archive_document* d = nullptr;
  rmr_error error = {0};
  rmr_status s = rmr_archive_document_create(&a->archive, CASE_DOC, recover,
                                             nullptr, &d, &error);
  if (s != RMR_OK) fprintf(stderr, "open: %s\n", error.message);
  OK(s);
  return d;
}

/* Strokes on page i of d. */
static size_t strokes_on(const rmr_archive_document* d, size_t i) {
  rm_page* page = nullptr;
  OK(rmr_archive_document_parse_page(d, i, &page, nullptr));
  size_t n = page->stroke_count;
  rm_page_free(page);
  return n;
}

/* A page ID's .rm files: the last in archive order is the page's, whichever
 * folder it is in; recovery takes unlisted files by name order, one per
 * base name, never a deleted or listed page's. */
static void matching(void) {
  rmd_bytes ink = stroked_page(1, 4),
            empty = {(const uint8_t*)RM_HEADER, RM_HEADER_LENGTH};
  const char* listed[] = {"P"};
  for (int pass = 0; pass < 2; pass++) {
    built_archive a = {0};
    put_content(&a, listed, 1, nullptr, 0, nullptr);
    put_file(&a, CASE_DOC "/x/P.rm", pass ? empty : ink);
    put_file(&a, CASE_DOC "/P.rm", pass ? ink : empty);
    rmr_archive_document* d = open_archive(&a, false);
    CHECK(rmr_archive_document_count(d) == 1);
    CHECK(strokes_on(d, 0) == (pass ? 1u : 0u));
    rmr_archive_document_free(d);
    archive_free(&a);
  }

  const char* gone[] = {"Z"};
  built_archive a = {0};
  put_content(&a, listed, 1, gone, 1, nullptr);
  put_file(&a, CASE_DOC "/b/X.rm", ink);
  put_file(&a, CASE_DOC "/Z.rm", ink);
  put_file(&a, CASE_DOC "/Y.rm", ink);
  put_file(&a, CASE_DOC "/a/X.rm", empty);
  put_file(&a, CASE_DOC "/P.rm", empty);
  rmr_archive_document* d = open_archive(&a, true);
  CHECK(rmr_archive_document_count(d) == 3);
  CHECK(!strcmp(rmr_archive_document_page(d, 0)->id, "P") &&
        !strcmp(rmr_archive_document_page(d, 1)->id, "Y") &&
        !strcmp(rmr_archive_document_page(d, 2)->id, "X"));
  CHECK(strokes_on(d, 0) == 0 && strokes_on(d, 1) == 1 &&
        strokes_on(d, 2) == 0);
  rmr_archive_document_free(d);
  archive_free(&a);
  rm_bytes_free((uint8_t*)(uintptr_t)ink.bytes);
}

/* A notebook of n pages, each holding page and on template (none when
 * null); ids[i] receives page i's ID. */
static built_archive notebook(size_t n, rmd_bytes page, char (*ids)[40],
                              const char* template) {
  const char** listed = calloc(n, sizeof *listed);
  CHECK(listed);
  for (size_t i = 0; i < n; i++) {
    snprintf(ids[i], 40, "%08zx-0000-4000-8000-%012zx", i * 2654435761u % n, i);
    listed[i] = ids[i];
  }
  built_archive a = {0};
  put_content(&a, listed, n, nullptr, 0, template);
  for (size_t i = 0; i < n; i++) {
    char name[96];
    snprintf(name, sizeof name, CASE_DOC "/%s.rm", ids[i]);
    put_file(&a, name, page);
  }
  free(listed);
  return a;
}

/* Every page of a 2,000-page notebook is annotated: the archive opens under
 * the default limits and each page finds its own file. */
static void many_files(void) {
  enum { PAGES = 2000 };
  char (*ids)[40] = calloc(PAGES, sizeof *ids);
  CHECK(ids);
  built_archive a =
      notebook(PAGES, (rmd_bytes){(const uint8_t*)RM_HEADER, RM_HEADER_LENGTH},
               ids, nullptr);
  rmr_archive_document* d = open_archive(&a, true);
  CHECK(rmr_archive_document_count(d) == PAGES);
  for (size_t i = 0; i < PAGES; i++)
    CHECK(!strcmp(rmr_archive_document_page(d, i)->id, ids[i]));
  CHECK(strokes_on(d, PAGES - 1) == 0);
  rmr_archive_document_free(d);
  archive_free(&a);
  free(ids);
}

/* Bytes of one dense page's plan, as the document export builds it. */
static size_t page_plan_bytes(rmd_bytes data) {
  rm_page* page = nullptr;
  CHECK(rm_parse_page(data.bytes, data.size, &page, nullptr) == RM_OK);
  rmr_plan* plan = nullptr;
  OK(PAGE_PLAN(page, (rmr_point){1404, 1872}, RMR_EDGE_RIBBON, 0, nullptr,
               nullptr, nullptr, &plan, nullptr));
  size_t bytes = rmr_plan_bytes(plan);
  rmr_plan_free(plan);
  rm_page_free(page);
  return bytes;
}

/* Template resolver counting its calls in *context, one per page plan
 * built; resolves no template, so the pages draw none. */
static rmr_status counted_template(void* context, rm_str name,
                                   const rmr_template** out) {
  (void)name;
  ++*(size_t*)context;
  *out = nullptr;
  return RMR_OK;
}

/* The PDF of n dense pages on a template under limits (null for the
 * defaults); *plans receives how many page plans the export built. */
static rmr_buffer dense_pdf(size_t n, rmd_bytes data, const rmr_limits* limits,
                            size_t* plans) {
  char (*ids)[40] = calloc(n, sizeof *ids);
  CHECK(ids);
  built_archive a = notebook(n, data, ids, "Grid");
  rmr_archive_document* d = open_archive(&a, false);
  *plans = 0;
  rmr_page_resources resources = {.template = counted_template,
                                  .template_context = plans};
  rmr_buffer out = {0};
  rmr_document_report report = {0};
  rmr_error error = {0};
  rmr_status s = DOCUMENT_PDF(d, &resources, nullptr, RMR_PAPER_A4, 0, limits,
                              nullptr, nullptr, &out, &report, &error);
  if (s != RMR_OK) fprintf(stderr, "dense PDF: %s\n", error.message);
  OK(s);
  CHECK(report.document_pages == n && report.rendered_pages == n);
  size_t pages = 0;
  OK(rmr_pdf_pages(out.bytes, out.size, nullptr, nullptr, 0, &pages, nullptr));
  CHECK(pages == n);
  rmr_archive_document_free(d);
  archive_free(&a);
  free(ids);
  return out;
}

/* Blank what two exports of the same pages may not share: the dates and the
 * document ID, written at fixed widths. */
static void blank_volatile(rmr_buffer* b) {
  for (size_t i = 0; i + 3 < b->size; i++)
    if (!memcmp(b->bytes + i, "(D:", 3) || !memcmp(b->bytes + i, "/ID", 3))
      for (i += 3; i < b->size && b->bytes[i] != ')' && b->bytes[i] != ']'; i++)
        b->bytes[i] = '0';
}

/* A long densely annotated notebook: each page's plan is built once, as the
 * page is drawn, and freed after, so the export needs one page's plan to fit
 * the budget rather than all of them and draws the same bytes either way. */
static void dense_document(void) {
  enum { PAGES = 16 };
  rmd_bytes data = stroked_page(40, 30);
  size_t plan = page_plan_bytes(data), whole_plans = 0, streamed_plans = 0;
  rmr_buffer whole = dense_pdf(PAGES, data, nullptr, &whole_plans);

  rmr_limits tight = rmr_limits_default();
  tight.maximum_plan_bytes = 6 * plan;
  CHECK(PAGES * plan > tight.maximum_plan_bytes);
  rmr_buffer streamed = dense_pdf(PAGES, data, &tight, &streamed_plans);
  CHECK(whole_plans == PAGES && streamed_plans == PAGES);
  blank_volatile(&whole);
  blank_volatile(&streamed);
  CHECK(whole.size == streamed.size &&
        !memcmp(whole.bytes, streamed.bytes, whole.size));
  rmr_buffer_free(&whole);
  rmr_buffer_free(&streamed);

  /* A page whose plan alone exceeds the budget still fails, by name. */
  tight.maximum_plan_bytes = plan / 2;
  char (*ids)[40] = calloc(2, sizeof *ids);
  CHECK(ids);
  built_archive a = notebook(2, data, ids, nullptr);
  rmr_archive_document* d = open_archive(&a, false);
  rmr_buffer out = {0};
  rmr_error error = {0};
  CHECK(DOCUMENT_PDF(d, nullptr, nullptr, RMR_PAPER_A4, 0, &tight, nullptr,
                     nullptr, &out, nullptr, &error) == RMR_ERR_LIMIT);
  CHECK(error.message[0] && strcmp(error.message, "document render failed"));
  rmr_archive_document_free(d);
  archive_free(&a);
  free(ids);
  rm_bytes_free((uint8_t*)(uintptr_t)data.bytes);
}

/* Document export by rmr_pdf_options: no options and the defaults draw what
 * the export without options draws; GROUP puts grained strokes in groups of
 * their own on Cairo and changes nothing on Apple; invalid options fail
 * before any page is planned. */
static void document_options(void) {
  rmd_bytes data = stroked_page(4, 30);
  char ids[3][40];
  built_archive a = notebook(3, data, ids, "Grid");
  rmr_archive_document* d = open_archive(&a, false);
  size_t plans = 0;
  rmr_page_resources resources = {.template = counted_template,
                                  .template_context = &plans};
  rmr_pdf_options options = rmr_pdf_options_default();
  rmr_buffer plain = {0}, unset = {0}, mask = {0}, group = {0};
  OK(DOCUMENT_PDF(d, &resources, nullptr, RMR_PAPER_A4, 0, nullptr, nullptr,
                  nullptr, &plain, nullptr, nullptr));
  OK(DOCUMENT_PDF_WITH_OPTIONS(d, &resources, nullptr, RMR_PAPER_A4, 0, nullptr,
                               nullptr, nullptr, nullptr, &unset, nullptr,
                               nullptr));
  OK(DOCUMENT_PDF_WITH_OPTIONS(d, &resources, nullptr, RMR_PAPER_A4, 0, nullptr,
                               &options, nullptr, nullptr, &mask, nullptr,
                               nullptr));
  options.grain = RMR_PDF_GRAIN_GROUP;
  OK(DOCUMENT_PDF_WITH_OPTIONS(d, &resources, nullptr, RMR_PAPER_A4, 0, nullptr,
                               &options, nullptr, nullptr, &group, nullptr,
                               nullptr));
  rmr_buffer* outputs[] = {&plain, &unset, &mask, &group};
  for (size_t i = 0; i < 4; i++) blank_volatile(outputs[i]);
  CHECK(plain.size == unset.size && plain.size == mask.size &&
        !memcmp(plain.bytes, unset.bytes, plain.size) &&
        !memcmp(plain.bytes, mask.bytes, plain.size));
  if (GRAIN_GROUPS)
    CHECK(group.size > mask.size);
  else
    CHECK(group.size == mask.size &&
          !memcmp(group.bytes, mask.bytes, mask.size));
  for (size_t i = 0; i < 4; i++) rmr_buffer_free(outputs[i]);

  plans = 0;
  options.abi_version = 0;
  rmr_buffer out = {0};
  rmr_error error = {0};
  CHECK(DOCUMENT_PDF_WITH_OPTIONS(d, &resources, nullptr, RMR_PAPER_A4, 0,
                                  nullptr, &options, nullptr, nullptr, &out,
                                  nullptr, &error) == RMR_ERR_INVALID_ARGUMENT);
  CHECK(!out.bytes && !plans && !strcmp(error.message, "invalid PDF options"));
  rmr_archive_document_free(d);
  archive_free(&a);
  rm_bytes_free((uint8_t*)(uintptr_t)data.bytes);
}

/* An rmr_output keeping what it is given, failing with fail_status once more
 * than fail_after bytes would have arrived, and cancelling the export through
 * output_cancelled once cancel_after writes have been made. */
typedef struct {
  rmr_buffer bytes;
  size_t calls, after_failure, fail_after, cancel_after;
  rmr_status fail_status;
  bool failed;
} received;

static rmr_status receive(void* context, const uint8_t* bytes, size_t size) {
  received* r = context;
  CHECK(bytes && size);
  if (r->failed) r->after_failure++;
  r->calls++;
  if (r->fail_status != RMR_OK && r->bytes.size + size > r->fail_after) {
    r->failed = true;
    return r->fail_status;
  }
  uint8_t* grown = realloc(r->bytes.bytes, r->bytes.size + size);
  CHECK(grown);
  memcpy(grown + r->bytes.size, bytes, size);
  r->bytes = (rmr_buffer){grown, r->bytes.size + size};
  return RMR_OK;
}

static bool output_cancelled(void* context) {
  const received* r = context;
  return r->cancel_after && r->calls >= r->cancel_after;
}

typedef enum { FRESH, PRESERVING, RECOMPOSED } export_mode;

/* d exported in mode into a buffer, or into r when r is given, under limits
 * (null for the defaults); report and error as the export fills them. */
static rmr_status export_pdf(const rmr_archive_document* d, export_mode mode,
                             const rmr_limits* limits, received* r,
                             rmr_buffer* out, rmr_document_report* report,
                             rmr_error* error) {
  if (!r)
    return mode == FRESH ? DOCUMENT_PDF_WITH_OPTIONS(
                               d, nullptr, nullptr, RMR_PAPER_A4, 0, limits,
                               nullptr, nullptr, nullptr, out, report, error)
           : mode == PRESERVING
               ? DOCUMENT_PDF_PRESERVING_WITH_OPTIONS(
                     d, nullptr, nullptr, RMR_PAPER_A4, 0, limits, nullptr,
                     nullptr, nullptr, out, report, error)
               : DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS(
                     d, nullptr, nullptr, RMR_PAPER_A4, 0, limits, nullptr,
                     nullptr, nullptr, out, report, error);
  rmr_output output = {RMR_OUTPUT_ABI, sizeof output, receive, r};
  return mode == FRESH
             ? DOCUMENT_PDF_TO_OUTPUT(d, nullptr, nullptr, RMR_PAPER_A4, 0,
                                      limits, nullptr, output_cancelled, r,
                                      &output, report, error)
         : mode == PRESERVING
             ? DOCUMENT_PDF_PRESERVING_TO_OUTPUT(
                   d, nullptr, nullptr, RMR_PAPER_A4, 0, limits, nullptr,
                   output_cancelled, r, &output, report, error)
             : DOCUMENT_PDF_RECOMPOSED_TO_OUTPUT(
                   d, nullptr, nullptr, RMR_PAPER_A4, 0, limits, nullptr,
                   output_cancelled, r, &output, report, error);
}

/* d exported in mode both ways gives the same file and report; returns the
 * output's write count. */
static size_t same_file(const rmr_archive_document* d, export_mode mode) {
  rmr_buffer whole = {0};
  rmr_document_report expected = {0}, report = {0};
  received r = {0};
  OK(export_pdf(d, mode, nullptr, nullptr, &whole, &expected, nullptr));
  OK(export_pdf(d, mode, nullptr, &r, nullptr, &report, nullptr));
  blank_volatile(&whole);
  blank_volatile(&r.bytes);
  CHECK(r.bytes.size == whole.size &&
        !memcmp(r.bytes.bytes, whole.bytes, whole.size));
  CHECK(!memcmp(&report, &expected, sizeof report));
  rmr_buffer_free(&whole);
  rmr_buffer_free(&r.bytes);
  return r.calls;
}

/* Exports written to an rmr_output as they are produced: the buffer's file,
 * in order and in pieces, in every mode and over an imported PDF; an output
 * failing at any point stops the export with its status, never to be called
 * again; cancellation is unchanged; maximum_pdf_bytes bounds the file; a
 * malformed output is refused before anything is written. */
static void document_output(void) {
  enum { PAGES = 64 };
  rmd_bytes data = stroked_page(80, 80);
  char (*ids)[40] = calloc(PAGES, sizeof *ids);
  CHECK(ids);
  built_archive a = notebook(PAGES, data, ids, nullptr);
  rmr_archive_document* d = open_archive(&a, false);

  rmr_buffer whole = {0};
  OK(export_pdf(d, FRESH, nullptr, nullptr, &whole, nullptr, nullptr));
  CHECK(whole.size > (8u << 20));
  CHECK(same_file(d, FRESH) > 2);

  /* Failing at the first write, in the middle and at the last byte. */
  size_t points[] = {0, whole.size / 2, whole.size - 1};
  rmr_status failures[] = {RMR_ERR_BACKEND, RMR_ERR_UNSUPPORTED,
                           RMR_ERR_OUT_OF_MEMORY};
  for (size_t i = 0; i < 3; i++) {
    received r = {.fail_after = points[i], .fail_status = failures[i]};
    rmr_error error = {0};
    CHECK(export_pdf(d, FRESH, nullptr, &r, nullptr, nullptr, &error) ==
          failures[i]);
    CHECK(r.failed && !r.after_failure && r.bytes.size <= points[i]);
    CHECK(error.code == failures[i] &&
          !strcmp(error.message, "PDF output write failed"));
    rmr_buffer_free(&r.bytes);
  }

  /* Cancelled after the first write: reported as cancellation. */
  received r = {.cancel_after = 1};
  rmr_error error = {0};
  CHECK(export_pdf(d, FRESH, nullptr, &r, nullptr, nullptr, &error) ==
        RMR_ERR_CANCELLED);
  CHECK(r.calls >= 1 && r.bytes.size < whole.size &&
        strcmp(error.message, "PDF output write failed"));
  rmr_buffer_free(&r.bytes);

  /* The limit is the file's size, written or returned. */
  rmr_limits limits = rmr_limits_default();
  limits.maximum_pdf_bytes = whole.size;
  r = (received){0};
  OK(export_pdf(d, FRESH, &limits, &r, nullptr, nullptr, nullptr));
  CHECK(r.bytes.size == whole.size);
  rmr_buffer_free(&r.bytes);
  limits.maximum_pdf_bytes = whole.size - 1;
  r = (received){0};
  CHECK(export_pdf(d, FRESH, &limits, &r, nullptr, nullptr, &error) ==
        RMR_ERR_LIMIT);
  CHECK(r.bytes.size < whole.size);
  rmr_buffer_free(&r.bytes);
  rmr_buffer_free(&whole);

  /* A malformed output, or none, writes nothing. */
  r = (received){0};
  rmr_output bad[] = {
      {RMR_OUTPUT_ABI + 1, sizeof(rmr_output), receive, &r},
      {RMR_OUTPUT_ABI, offsetof(rmr_output, context), receive, &r},
      {RMR_OUTPUT_ABI, sizeof(rmr_output), nullptr, &r}};
  for (size_t i = 0; i < 4; i++) {
    CHECK(DOCUMENT_PDF_TO_OUTPUT(d, nullptr, nullptr, RMR_PAPER_A4, 0, nullptr,
                                 nullptr, nullptr, nullptr,
                                 i < 3 ? &bad[i] : nullptr, nullptr,
                                 &error) == RMR_ERR_INVALID_ARGUMENT);
    CHECK(!strcmp(error.message, "invalid PDF output"));
  }
  CHECK(!r.calls);
  rmr_archive_document_free(d);
  archive_free(&a);
  free(ids);

  /* Annotated pages of an imported PDF, in every mode. */
  fixture pdf = source(false);
  const char* listed[] = {"P0", "P1"};
  a = (built_archive){0};
  put_content(&a, listed, 2, nullptr, 0, nullptr);
  free(a.content);
  const char content[] =
      "{\"cPages\":{\"pages\":["
      "{\"id\":\"P0\",\"redir\":{\"value\":0}},"
      "{\"id\":\"P1\",\"redir\":{\"value\":1}}]}}";
  a.content = strdup(content);
  CHECK(a.content);
  a.files[0].data = (rmd_bytes){(const uint8_t*)a.content, strlen(content)};
  put_file(&a, CASE_DOC "/P0.rm", data);
  put_file(&a, CASE_DOC "/P1.rm", data);
  put_file(&a, CASE_DOC ".pdf", (rmd_bytes){(const uint8_t*)pdf.bytes, pdf.n});
  d = open_archive(&a, false);
  CHECK(rmr_archive_document_page(d, 1)->has_source);
  for (export_mode mode = FRESH; mode <= RECOMPOSED; mode++)
    CHECK(same_file(d, mode) >= 1);
  r = (received){.fail_after = pdf.n, .fail_status = RMR_ERR_BACKEND};
  CHECK(export_pdf(d, PRESERVING, nullptr, &r, nullptr, nullptr, &error) ==
        RMR_ERR_BACKEND);
  CHECK(!r.after_failure && !strcmp(error.message, "PDF output write failed"));
  rmr_buffer_free(&r.bytes);
  rmr_archive_document_free(d);
  archive_free(&a);
  rm_bytes_free((uint8_t*)(uintptr_t)data.bytes);
}

/* The unreadable pages an export told of, and with which status. */
typedef struct {
  size_t pages[4];
  rmr_status statuses[4];
  size_t count;
} told;

static void note_unreadable(void* context, size_t page, rmr_status status) {
  told* t = context;
  CHECK(t->count < 4);
  t->pages[t->count] = page;
  t->statuses[t->count++] = status;
}

/* Whether b holds the bytes of s. */
static bool holds(const rmr_buffer* b, const char* s) {
  size_t n = strlen(s);
  for (size_t i = 0; n <= b->size && i <= b->size - n; i++)
    if (!memcmp(b->bytes + i, s, n)) return true;
  return false;
}

/* Pages A, B and C, each holding ink on the Grid template, except B, which
 * holds middle, or has no .rm file when middle is empty. With pdf, A and B
 * alone, over its pages 0 and 1. */
static built_archive trio(rmd_bytes ink, rmd_bytes middle, const fixture* pdf) {
  const char* ids[] = {"A", "B", "C"};
  built_archive a = {0};
  if (pdf) {
    const char content[] =
        "{\"cPages\":{\"pages\":["
        "{\"id\":\"A\",\"redir\":{\"value\":0}},"
        "{\"id\":\"B\",\"redir\":{\"value\":1}}]}}";
    a.content = strdup(content);
    CHECK(a.content);
    put_file(&a, CASE_DOC ".content",
             (rmd_bytes){(const uint8_t*)a.content, strlen(content)});
    put_file(&a, CASE_DOC ".pdf",
             (rmd_bytes){(const uint8_t*)pdf->bytes, pdf->n});
  } else {
    put_content(&a, ids, 3, nullptr, 0, "Grid");
  }
  put_file(&a, CASE_DOC "/A.rm", ink);
  if (middle.size) put_file(&a, CASE_DOC "/B.rm", middle);
  if (!pdf) put_file(&a, CASE_DOC "/C.rm", ink);
  return a;
}

/* d exported in mode, or as SVG pages into svg when it is given, with fonts,
 * options, and resources counting the page plans built on a template into
 * *templates. */
static rmr_status unreadable_export(
    const rmr_archive_document* d, export_mode mode, const FONTS* fonts,
    const rmr_pdf_options* options, size_t* templates, rmr_buffer* out,
    rmr_svg_pages* svg, rmr_document_report* report, rmr_error* error) {
  *templates = 0;
  rmr_page_resources resources = {.template = counted_template,
                                  .template_context = templates};
  if (svg)
    return DOCUMENT_SVG_WITH_OPTIONS(d, &resources, fonts, RMR_PAPER_A4, 0, 0,
                                     nullptr, options, nullptr, nullptr, svg,
                                     report, error);
  return mode == FRESH ? DOCUMENT_PDF_WITH_OPTIONS(
                             d, &resources, fonts, RMR_PAPER_A4, 0, nullptr,
                             options, nullptr, nullptr, out, report, error)
         : mode == PRESERVING
             ? DOCUMENT_PDF_PRESERVING_WITH_OPTIONS(
                   d, &resources, fonts, RMR_PAPER_A4, 0, nullptr, options,
                   nullptr, nullptr, out, report, error)
             : DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS(
                   d, &resources, fonts, RMR_PAPER_A4, 0, nullptr, options,
                   nullptr, nullptr, out, report, error);
}

/* Whether two exports are the same file once their dates and IDs are. */
static bool same_bytes(rmr_buffer* x, rmr_buffer* y) {
  blank_volatile(x);
  blank_volatile(y);
  return x->size == y->size && !memcmp(x->bytes, y->bytes, x->size);
}

/* Pages whose .rm file does not parse, as rmr_unreadable_policy says: STRICT
 * by default fails naming the page, BLANK draws what a page without a .rm
 * file draws, PLACEHOLDER draws the label in the bundled face, on a sheet
 * without its template or over the imported PDF page, in every mode and in
 * SVG; the callback hears of each page reached; an older options struct reads
 * STRICT. */
static void unreadable_pages(const char* fonts_directory) {
  FONTS* fonts = nullptr;
  OK(FONTS_DIRECTORY(fonts_directory, nullptr, &fonts, nullptr));
  rmd_bytes ink = stroked_page(2, 20),
            junk = {(const uint8_t*)"not a page", 10},
            newer = {(const uint8_t*)RM_HEADER_PREFIX "9          ",
                     RM_HEADER_LENGTH},
            none = {0};

  built_archive a = trio(ink, junk, nullptr), gone = trio(ink, none, nullptr);
  rmr_archive_document *d = open_archive(&a, false),
                       *without = open_archive(&gone, false);
  rm_page* page = nullptr;
  rmr_status unreadable = RMR_ERR_LIMIT;
  OK(rmr_archive_document_read_page(d, 0, &page, &unreadable, nullptr));
  CHECK(unreadable == RMR_OK && page->stroke_count == 2);
  rm_page_free(page);
  OK(rmr_archive_document_read_page(d, 1, &page, &unreadable, nullptr));
  CHECK(unreadable == RMR_ERR_BACKEND && !page->stroke_count &&
        page->has_template_name);
  rm_page_free(page);
  OK(rmr_archive_document_read_page(without, 1, &page, &unreadable, nullptr));
  CHECK(unreadable == RMR_OK && !page->stroke_count);
  rm_page_free(page);
  page = nullptr;
  rmr_error error = {0};
  CHECK(rmr_archive_document_parse_page(d, 1, &page, &error) ==
            RMR_ERR_BACKEND &&
        !page);

  /* STRICT, the default: the export fails, naming page 1. */
  size_t templates = 0;
  told heard = {0};
  rmr_pdf_options options = rmr_pdf_options_default();
  options.on_unreadable = note_unreadable;
  options.unreadable_context = &heard;
  rmr_buffer out = {0};
  CHECK(DOCUMENT_PDF(d, nullptr, fonts, RMR_PAPER_A4, 0, nullptr, nullptr,
                     nullptr, &out, nullptr, &error) == RMR_ERR_BACKEND);
  CHECK(!out.bytes && error.code == RMR_ERR_BACKEND && error.index == 1 &&
        !strcmp(error.message,
                "unreadable page 1 (B): RMLines could not parse it"));
  for (export_mode mode = FRESH; mode <= RECOMPOSED; mode++) {
    heard = (told){0};
    CHECK(unreadable_export(d, mode, fonts, &options, &templates, &out, nullptr,
                            nullptr, &error) == RMR_ERR_BACKEND);
    CHECK(!out.bytes && error.index == 1 && heard.count == 1 &&
          heard.pages[0] == 1 && heard.statuses[0] == RMR_ERR_BACKEND);
  }
  heard = (told){0};
  rmr_page_resources resources = {.template = counted_template,
                                  .template_context = &templates};
  OK(DOCUMENT_PDF_WITH_OPTIONS(d, &resources, fonts, RMR_PAPER_A4, 1, nullptr,
                               &options, nullptr, nullptr, &out, nullptr,
                               nullptr));
  CHECK(!heard.count);
  rmr_buffer_free(&out);

  /* An options struct from before the policy reads STRICT whatever follows
   * it; an unknown policy is refused. */
  options.unreadable = RMR_UNREADABLE_BLANK;
  options.struct_size = offsetof(rmr_pdf_options, unreadable);
  CHECK(unreadable_export(d, FRESH, fonts, &options, &templates, &out, nullptr,
                          nullptr, &error) == RMR_ERR_BACKEND);
  options.struct_size = sizeof options;
  options.unreadable = (rmr_unreadable_policy)3;
  CHECK(unreadable_export(d, FRESH, fonts, &options, &templates, &out, nullptr,
                          nullptr, &error) == RMR_ERR_INVALID_ARGUMENT);
  CHECK(!strcmp(error.message, "invalid PDF options"));

  /* BLANK: the file of the notebook without page 1's .rm, in every mode. */
  options.unreadable = RMR_UNREADABLE_BLANK;
  for (export_mode mode = FRESH; mode <= RECOMPOSED; mode++) {
    rmr_buffer blank = {0}, expected = {0};
    rmr_document_report report = {0};
    heard = (told){0};
    OK(unreadable_export(d, mode, fonts, &options, &templates, &blank, nullptr,
                         &report, nullptr));
    CHECK(templates == 3 && heard.count == 1 && heard.pages[0] == 1 &&
          report.document_pages == 3 && report.rendered_pages == 3);
    OK(unreadable_export(without, mode, fonts, nullptr, &templates, &expected,
                         nullptr, nullptr, nullptr));
    CHECK(same_bytes(&blank, &expected));
    rmr_buffer_free(&blank);
    rmr_buffer_free(&expected);
  }

  /* PLACEHOLDER: the label needs fonts; with them it is drawn in Noto Sans,
   * the same each time, on page 1 without its template. */
  options.unreadable = RMR_UNREADABLE_PLACEHOLDER;
  CHECK(unreadable_export(d, FRESH, nullptr, &options, &templates, &out,
                          nullptr, nullptr, &error) == RMR_ERR_UNSUPPORTED);
  CHECK(!out.bytes &&
        !strcmp(error.message, "document font provider required"));
  rmr_buffer blank = {0};
  OK(unreadable_export(without, FRESH, fonts, nullptr, &templates, &blank,
                       nullptr, nullptr, nullptr));
  for (export_mode mode = FRESH; mode <= RECOMPOSED; mode++) {
    rmr_buffer first = {0}, again = {0};
    heard = (told){0};
    OK(unreadable_export(d, mode, fonts, &options, &templates, &first, nullptr,
                         nullptr, nullptr));
    CHECK(templates == 2 && heard.count == 1 && heard.pages[0] == 1);
    size_t pages = 0;
    OK(rmr_pdf_pages(first.bytes, first.size, nullptr, nullptr, 0, &pages,
                     nullptr));
    CHECK(pages == 3 && holds(&first, "NotoSans"));
    CHECK(!holds(&blank, "NotoSans"));
    OK(unreadable_export(d, mode, fonts, &options, &templates, &again, nullptr,
                         nullptr, nullptr));
    CHECK(same_bytes(&first, &again));
    rmr_buffer_free(&first);
    rmr_buffer_free(&again);
  }
  rmr_buffer_free(&blank);

  /* SVG: the label as outlines on page 1 alone; STRICT fails as PDF does. */
  rmr_svg_pages blank_svg = {0}, label_svg = {0};
  options.unreadable = RMR_UNREADABLE_BLANK;
  OK(unreadable_export(d, FRESH, fonts, &options, &templates, nullptr,
                       &blank_svg, nullptr, nullptr));
  options.unreadable = RMR_UNREADABLE_PLACEHOLDER;
  heard = (told){0};
  OK(unreadable_export(d, FRESH, fonts, &options, &templates, nullptr,
                       &label_svg, nullptr, nullptr));
  CHECK(blank_svg.count == 3 && label_svg.count == 3 && heard.count == 1);
  for (size_t i = 0; i < 3; i += 2)
    CHECK(blank_svg.pages[i].size == label_svg.pages[i].size &&
          !memcmp(blank_svg.pages[i].bytes, label_svg.pages[i].bytes,
                  blank_svg.pages[i].size));
  CHECK(holds(&label_svg.pages[1], "#404040") &&
        holds(&label_svg.pages[1], "#f2f2f2") &&
        !holds(&blank_svg.pages[1], "#404040"));
  rmr_svg_pages_free(&blank_svg);
  rmr_svg_pages_free(&label_svg);
  options.unreadable = RMR_UNREADABLE_STRICT;
  CHECK(unreadable_export(d, FRESH, fonts, &options, &templates, nullptr,
                          &blank_svg, nullptr, &error) == RMR_ERR_BACKEND);
  CHECK(error.index == 1 && !blank_svg.pages);
  rmr_archive_document_free(d);
  rmr_archive_document_free(without);
  archive_free(&a);
  archive_free(&gone);

  /* A page of a newer version fails as UNSUPPORTED, and is told so. */
  a = trio(ink, newer, nullptr);
  d = open_archive(&a, false);
  heard = (told){0};
  CHECK(unreadable_export(d, FRESH, fonts, &options, &templates, &out, nullptr,
                          nullptr, &error) == RMR_ERR_UNSUPPORTED);
  CHECK(error.index == 1 && heard.count == 1 &&
        heard.statuses[0] == RMR_ERR_UNSUPPORTED);
  rmr_archive_document_free(d);
  archive_free(&a);

  /* Over an imported PDF: the PDF page under the label in every mode, the
   * original's bytes kept by a preserving export. */
  fixture pdf = source(false);
  a = trio(ink, junk, &pdf);
  d = open_archive(&a, false);
  options.unreadable = RMR_UNREADABLE_PLACEHOLDER;
  for (export_mode mode = FRESH; mode <= RECOMPOSED; mode++) {
    rmr_buffer labelled = {0};
    OK(unreadable_export(d, mode, fonts, &options, &templates, &labelled,
                         nullptr, nullptr, nullptr));
    CHECK(holds(&labelled, "NotoSans"));
    if (mode == PRESERVING)
      CHECK(labelled.size > pdf.n && !memcmp(labelled.bytes, pdf.bytes, pdf.n));
    rmr_buffer_free(&labelled);
  }
  rmr_archive_document_free(d);
  archive_free(&a);
  FONTS_FREE(fonts);
  rm_bytes_free((uint8_t*)(uintptr_t)ink.bytes);
}

#endif
