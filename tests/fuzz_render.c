/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* libFuzzer target over the untrusted inputs the library reads: a PDF a
 * notebook imports (exported fresh, preserving or recomposed), a .rm page
 * (as a bitmap, a PDF or an SVG) and an .rmdoc archive (as a PDF or SVG
 * pages). Each execution runs one operation, through a buffer or an output
 * that may refuse a write, under limits that keep it to tens of milliseconds,
 * and checks the API's contracts on the result. Built against the Apple
 * backend, or the Cairo one with RMR_TEST_LINUX; RMR_FUZZ_FONTS names the
 * bundled fonts directory. */
#include "fuzz_render.h"

#include <rmdoc/rmdoc.h>
#include <rmlines/parser.h>
#include <rmlines/writer.h>
#include <rmrender/archive.h>
#include <rmrender/pdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef RMR_TEST_LINUX
#include <rmrender/linux.h>
#include <rmrender/linux_document.h>
#ifdef RMR_TEST_FONTCONFIG
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#endif
#define FONTS rmr_linux_fonts
#define FONTS_DIRECTORY rmr_linux_fonts_create_directory
#define FONTS_FREE rmr_linux_fonts_free
#define PAGE_PLAN rmr_linux_page_plan
#define PAGE_SVG rmr_linux_page_svg
#define BITMAP rmr_linux_bitmap
#define PLANS_PDF rmr_linux_pdf
#define DOCUMENT_PDF_WITH_OPTIONS rmr_linux_document_pdf_with_options
#define DOCUMENT_PDF_PRESERVING_WITH_OPTIONS \
  rmr_linux_document_pdf_preserving_with_options
#define DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS \
  rmr_linux_document_pdf_recomposed_with_options
#define DOCUMENT_PDF_TO_OUTPUT rmr_linux_document_pdf_to_output
#define DOCUMENT_PDF_PRESERVING_TO_OUTPUT \
  rmr_linux_document_pdf_preserving_to_output
#define DOCUMENT_PDF_RECOMPOSED_TO_OUTPUT \
  rmr_linux_document_pdf_recomposed_to_output
#define DOCUMENT_SVG_WITH_OPTIONS rmr_linux_document_svg_with_options
#else
#include <rmrender/apple.h>
#include <rmrender/apple_document.h>
#define FONTS rmr_apple_fonts
#define FONTS_DIRECTORY rmr_apple_fonts_create_directory
#define FONTS_FREE rmr_apple_fonts_free
#define PAGE_PLAN rmr_apple_page_plan
#define PAGE_SVG rmr_apple_page_svg
#define BITMAP rmr_apple_bitmap
#define PLANS_PDF rmr_apple_pdf
#define DOCUMENT_PDF_WITH_OPTIONS rmr_apple_document_pdf_with_options
#define DOCUMENT_PDF_PRESERVING_WITH_OPTIONS \
  rmr_apple_document_pdf_preserving_with_options
#define DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS \
  rmr_apple_document_pdf_recomposed_with_options
#define DOCUMENT_PDF_TO_OUTPUT rmr_apple_document_pdf_to_output
#define DOCUMENT_PDF_PRESERVING_TO_OUTPUT \
  rmr_apple_document_pdf_preserving_to_output
#define DOCUMENT_PDF_RECOMPOSED_TO_OUTPUT \
  rmr_apple_document_pdf_recomposed_to_output
#define DOCUMENT_SVG_WITH_OPTIONS rmr_apple_document_svg_with_options
#endif

/* A violated contract must stop the run in every configuration, including
 * Release builds of the replay test where assert would compile away. */
#define FUZZ_CHECK(x)                                                   \
  do {                                                                  \
    if (!(x)) {                                                         \
      fprintf(stderr, "%s:%d: contract: %s\n", __FILE__, __LINE__, #x); \
      abort();                                                          \
    }                                                                   \
  } while (0)

#define DOCUMENT "44444444-4444-4444-4444-444444444444"
constexpr uint32_t NEVER = UINT32_MAX;
constexpr rmr_status UNTOUCHED = (rmr_status)0x7fffffffu;
/* The refusing output returns a value outside the library's statuses, so a
 * result equal to it can only be the output's own status passed through. */
constexpr rmr_status REFUSED = (rmr_status)0x52454655u;

typedef struct {
  uint32_t kind; /* the export mode, render or archive output */
  bool streamed, placeholder, tight;
  uint32_t fail_after, cancel_after;
  size_t stop_at;
} choice;

/* A byte count drawn log-uniformly from 1 to 65,536, so that headers, small
 * objects and every larger band are as likely as each other. */
static size_t byte_count(uint32_t bits) {
  uint32_t h = bits * 2654435761u;
  h ^= h >> 15;
  uint32_t band = h % 17, r = h / 17;
  if (!band) return 1;
  size_t low = (size_t)1 << (band - 1);
  return low + r % (low + 1);
}

/* Variants that end a call early are kept rare: cheap, but a campaign spent
 * mostly on them would seldom draw a page. */
static choice decode(uint32_t rest) {
  choice c = {.kind = rest % 3, .stop_at = byte_count(rest)};
  rest /= 3;
  c.streamed = rest & 1;
  rest >>= 1;
  c.placeholder = rest & 1;
  rest >>= 1;
  uint32_t failure = rest & 15;
  rest >>= 4;
  c.fail_after = failure == 12   ? 0
                 : failure == 13 ? (uint32_t)c.stop_at
                                 : NEVER;
  c.cancel_after = failure == 14 ? 0 : failure == 15 ? (rest & 255) : NEVER;
  rest >>= 8;
  c.tight = (rest & 7) == 7;
  return c;
}

/* Memory stays at a few tens of megabytes and a call at tens of
 * milliseconds; the tight profile fails the output at the drawn byte
 * count. */
static rmr_limits limits(const choice* c) {
  rmr_limits l = rmr_limits_default();
  l.maximum_points = 200000;
  l.maximum_elements = 400000;
  l.maximum_draws = 4096;
  l.maximum_plan_bytes = 64u << 20;
  l.maximum_bitmap_bytes = 16u << 20;
  l.maximum_bitmap_side = 4096;
  l.maximum_pdf_bytes = c->tight ? c->stop_at : 8u << 20;
  return l;
}

typedef struct {
  size_t written, fail_after, calls_after;
  bool failed;
} sink;

static rmr_status write_bytes(void* context, const uint8_t* bytes,
                              size_t size) {
  sink* s = context;
  FUZZ_CHECK(bytes && size);
  if (s->failed) {
    s->calls_after++;
    return REFUSED;
  }
  if (s->fail_after != NEVER && size > s->fail_after - s->written) {
    s->failed = true;
    return REFUSED;
  }
  s->written += size;
  return RMR_OK;
}

typedef struct {
  uint32_t calls, limit;
} counter;

static bool cancel(void* context) {
  counter* c = context;
  return c->limit != NEVER && c->calls++ >= c->limit;
}

static bool zero(const void* value, size_t size) {
  const uint8_t* bytes = value;
  for (size_t i = 0; i < size; i++)
    if (bytes[i]) return false;
  return true;
}

/* The fonts, loaded once per process and released at its exit, before the
 * leak check; with Fontconfig, its and Pango's global caches go too. */
static FONTS* loaded;

static void release_fonts(void) {
  FONTS_FREE(loaded);
  loaded = nullptr;
#ifdef RMR_TEST_FONTCONFIG
  pango_cairo_font_map_set_default(nullptr);
  FcFini();
#endif
}

static const FONTS* fonts(void) {
  static bool tried;
  if (!tried) {
    tried = true;
    FUZZ_CHECK(FONTS_DIRECTORY(RMR_FUZZ_FONTS, nullptr, &loaded, nullptr) ==
               RMR_OK);
    FUZZ_CHECK(!atexit(release_fonts));
  }
  return loaded;
}

/* Loading the fonts before the first input keeps them out of libFuzzer's
 * per-input leak accounting. */
int LLVMFuzzerInitialize(int* argc, char*** argv) {
  (void)argc;
  (void)argv;
  (void)fonts();
  return 0;
}

/* A small inked page, made once per process. */
static rmd_bytes inked_page(void) {
  static uint8_t* bytes;
  static size_t size;
  if (!bytes) {
    rm_draft_point points[8];
    for (size_t i = 0; i < 8; i++)
      points[i] = rm_draft_point_make((float)(-400 + 100 * (double)i),
                                      (float)(300 + (i % 2) * 40));
    rm_draft_stroke strokes[] = {
        rm_draft_stroke_make(RM_TOOL_BALLPOINT_2, RM_COLOR_BLACK, points, 8),
        rm_draft_stroke_make(RM_TOOL_PENCIL_2, RM_COLOR_BLUE, points, 8),
        rm_draft_stroke_make(RM_TOOL_HIGHLIGHTER_2, RM_COLOR_YELLOW, points,
                             8)};
    rm_page_draft draft = {.sheet = {1404, 1872, 1404, 1872},
                           .column = rm_draft_column_of(RM_COLUMN_MEDIUM),
                           .strokes = strokes,
                           .stroke_count = 3};
    FUZZ_CHECK(rm_write_page(&draft, &bytes, &size, nullptr) == RM_OK);
  }
  return (rmd_bytes){bytes, size};
}

/* The contracts every call keeps: a documented status, or the refusing
 * output's own; an error filled on failure and untouched on success; and an
 * output never called again once it refused. */
static void contract(rmr_status s, const rmr_error* e, const sink* stream,
                     bool streamed) {
  FUZZ_CHECK(s <= RMR_ERR_CANCELLED || (streamed && s == REFUSED));
  if (s != RMR_OK)
    FUZZ_CHECK(e->code == s && memchr(e->message, 0, sizeof e->message) &&
               e->message[0]);
  else
    FUZZ_CHECK(e->code == UNTOUCHED);
  if (!streamed) return;
  FUZZ_CHECK(!stream->calls_after);
  FUZZ_CHECK(!stream->failed || s != RMR_OK);
  FUZZ_CHECK(s != REFUSED || (stream->failed &&
                              !strcmp(e->message, "PDF output write failed")));
  FUZZ_CHECK(s != RMR_OK || stream->written > 5);
}

/* Resolves every template name to none: the pages draw without one. */
static rmr_status no_template(void* context, rm_str name,
                              const rmr_template** out) {
  (void)context;
  (void)name;
  *out = nullptr;
  return RMR_OK;
}

/* d exported as c says: a PDF in one mode, or SVG pages. */
static void document(const rmr_archive_document* d, const choice* c, bool svg) {
  rmr_page_resources templates = {.template = no_template};
  const rmr_page_resources* resources = &templates;
  rmr_limits l = limits(c);
  rmr_pdf_options options = rmr_pdf_options_default();
  options.unreadable =
      c->placeholder ? RMR_UNREADABLE_PLACEHOLDER : RMR_UNREADABLE_BLANK;
  counter polls = {0, c->cancel_after};
  sink stream = {.fail_after = c->fail_after};
  rmr_output output = {RMR_OUTPUT_ABI, sizeof output, write_bytes, &stream};
  rmr_error e = {.code = UNTOUCHED};
  rmr_buffer out = {0};
  rmr_svg_pages pages = {0};
  rmr_status s;
  bool streamed = !svg && c->streamed;
  if (svg) {
    s = DOCUMENT_SVG_WITH_OPTIONS(d, resources, fonts(), RMR_PAPER_A4, 4, 72,
                                  &l, &options, cancel, &polls, &pages, nullptr,
                                  &e);
    if (s != RMR_OK)
      FUZZ_CHECK(zero(&pages, sizeof pages));
    else
      for (size_t i = 0; i < pages.count; i++)
        FUZZ_CHECK(pages.pages[i].size > 5 &&
                   !memcmp(pages.pages[i].bytes, "<?xml", 5));
    rmr_svg_pages_free(&pages);
  } else if (streamed) {
    s = c->kind == 0 ? DOCUMENT_PDF_TO_OUTPUT(
                           d, resources, fonts(), RMR_PAPER_A4, 4, &l, &options,
                           cancel, &polls, &output, nullptr, &e)
        : c->kind == 1 ? DOCUMENT_PDF_PRESERVING_TO_OUTPUT(
                             d, resources, fonts(), RMR_PAPER_A4, 0, &l,
                             &options, cancel, &polls, &output, nullptr, &e)
                       : DOCUMENT_PDF_RECOMPOSED_TO_OUTPUT(
                             d, resources, fonts(), RMR_PAPER_A4, 4, &l,
                             &options, cancel, &polls, &output, nullptr, &e);
  } else {
    s = c->kind == 0 ? DOCUMENT_PDF_WITH_OPTIONS(
                           d, resources, fonts(), RMR_PAPER_A4, 4, &l, &options,
                           cancel, &polls, &out, nullptr, &e)
        : c->kind == 1 ? DOCUMENT_PDF_PRESERVING_WITH_OPTIONS(
                             d, resources, fonts(), RMR_PAPER_A4, 0, &l,
                             &options, cancel, &polls, &out, nullptr, &e)
                       : DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS(
                             d, resources, fonts(), RMR_PAPER_A4, 4, &l,
                             &options, cancel, &polls, &out, nullptr, &e);
    if (s != RMR_OK)
      FUZZ_CHECK(zero(&out, sizeof out));
    else
      FUZZ_CHECK(out.size > 5 && out.size <= l.maximum_pdf_bytes &&
                 !memcmp(out.bytes, "%PDF-", 5));
    rmr_buffer_free(&out);
  }
  contract(s, &e, &stream, streamed);
}

/* data as the PDF of a notebook whose first two pages are annotated. */
static void imported(const uint8_t* data, size_t size, const choice* c) {
  rmr_limits l = limits(c);
  size_t count = 0;
  rmr_error e = {.code = UNTOUCHED};
  rmr_status s = rmr_pdf_pages(data, size, &l, nullptr, 0, &count, &e);
  contract(s, &e, nullptr, false);

  static const char content[] =
      "{\"cPages\":{\"pages\":[{\"id\":\"A\",\"redir\":{\"value\":0}},"
      "{\"id\":\"B\",\"redir\":{\"value\":1}}]}}";
  static const char junk[] = "not a page";
  rmd_file files[] = {
      {{DOCUMENT ".content", sizeof DOCUMENT ".content" - 1},
       {(const uint8_t*)content, sizeof content - 1}},
      {{DOCUMENT ".pdf", sizeof DOCUMENT ".pdf" - 1}, {data, size}},
      {{DOCUMENT "/A.rm", sizeof DOCUMENT "/A.rm" - 1}, inked_page()},
      {{DOCUMENT "/B.rm", sizeof DOCUMENT "/B.rm" - 1},
       {(const uint8_t*)junk, sizeof junk - 1}}};
  rmd_archive archive = {.files = files, .count = 4};
  rmr_archive_document* d = nullptr;
  e = (rmr_error){.code = UNTOUCHED};
  s = rmr_archive_document_create(&archive, DOCUMENT, false, &l, &d, &e);
  contract(s, &e, nullptr, false);
  if (s != RMR_OK) {
    FUZZ_CHECK(!d);
    return;
  }
  document(d, c, false);
  rmr_archive_document_free(d);
}

/* data as a .rm page, planned and rendered one way. */
static void page(const uint8_t* data, size_t size, const choice* c) {
  rm_page* p = nullptr;
  if (rm_parse_page(data, size, &p, nullptr) != RM_OK) return;
  rmr_page_resources templates = {.template = no_template};
  rmr_limits l = limits(c);
  counter polls = {0, c->cancel_after};
  rmr_error e = {.code = UNTOUCHED};
  rmr_status s;
  if (c->kind == 2) {
    rmr_buffer out = {0};
    s = PAGE_SVG(p, (rmr_point){1404, 1872}, RMR_EDGE_RIBBON, 0, &templates,
                 fonts(), RMR_PAPER_A4, 72, &l, cancel, &polls, &out, &e);
    if (s != RMR_OK)
      FUZZ_CHECK(zero(&out, sizeof out));
    else
      FUZZ_CHECK(out.size > 5 && !memcmp(out.bytes, "<?xml", 5));
    rmr_buffer_free(&out);
    contract(s, &e, nullptr, false);
    rm_page_free(p);
    return;
  }
  rmr_plan* plan = nullptr;
  s = PAGE_PLAN(p, (rmr_point){1404, 1872}, RMR_EDGE_RIBBON, 0, &templates,
                fonts(), &l, &plan, &e);
  contract(s, &e, nullptr, false);
  rm_page_free(p);
  if (s != RMR_OK) {
    FUZZ_CHECK(!plan);
    return;
  }
  e = (rmr_error){.code = UNTOUCHED};
  if (c->kind == 0) {
    rmr_bitmap bitmap = {0};
    s = BITMAP(plan, 1.0 / 8, (rmr_color){1, 1, 1, 1}, &l, cancel, &polls,
               &bitmap, &e);
    if (s != RMR_OK)
      FUZZ_CHECK(zero(&bitmap, sizeof bitmap));
    else
      FUZZ_CHECK(bitmap.pixels && bitmap.width && bitmap.height &&
                 bitmap.stride >= bitmap.width * 4);
    rmr_bitmap_free(&bitmap);
  } else {
    rmr_buffer out = {0};
    const rmr_plan* plans[] = {plan};
    s = PLANS_PDF(plans, 1, 0.5, &l, cancel, &polls, &out, &e);
    if (s != RMR_OK)
      FUZZ_CHECK(zero(&out, sizeof out));
    else
      FUZZ_CHECK(out.size > 5 && out.size <= l.maximum_pdf_bytes &&
                 !memcmp(out.bytes, "%PDF-", 5));
    rmr_buffer_free(&out);
  }
  contract(s, &e, nullptr, false);
  rmr_plan_free(plan);
}

/* data as an .rmdoc, its first document exported as a PDF or SVG pages. */
static void archived(const uint8_t* data, size_t size, const choice* c) {
  rmd_limits archive_limits = rmd_limits_format();
  rmd_reader* reader = nullptr;
  if (rmd_reader_open((rmd_bytes){data, size}, &archive_limits, &reader,
                      nullptr) != RMD_OK)
    return;
  rmd_strings ids = {0};
  rmd_archive archive = {0};
  char id[128] = "";
  if (rmd_reader_document_ids(reader, &ids, nullptr) == RMD_OK && ids.count &&
      ids.values[0].length < sizeof id) {
    memcpy(id, ids.values[0].bytes, ids.values[0].length);
    id[ids.values[0].length] = 0;
  }
  rmd_strings_free(&ids);
  if (id[0] && rmd_reader_unpack(reader, nullptr, nullptr, &archive, nullptr) ==
                   RMD_OK) {
    rmr_limits l = limits(c);
    rmr_archive_document* d = nullptr;
    rmr_error e = {.code = UNTOUCHED};
    rmr_status s =
        rmr_archive_document_create(&archive, id, c->placeholder, &l, &d, &e);
    contract(s, &e, nullptr, false);
    if (s == RMR_OK) {
      choice fresh = *c;
      fresh.kind = 0;
      document(d, &fresh, c->kind == 2);
    } else
      FUZZ_CHECK(!d);
    rmr_archive_document_free(d);
    rmd_archive_free(&archive);
  }
  rmd_reader_close(reader);
}

uint32_t fuzz_selector(const uint8_t* data, size_t size) {
  uint64_t h = 14695981039346656037u;
  for (size_t i = 0; i < size; i++) h = (h ^ data[i]) * 1099511628211u;
  return (uint32_t)(h ^ (h >> 32));
}

int fuzz_render(const uint8_t* data, size_t size, fuzz_operation operation,
                uint32_t rest) {
  if (size > FUZZ_MAXIMUM_INPUT || !size) return 0;
  choice c = decode(rest);
  switch (operation) {
    case FUZZ_PDF:
      imported(data, size, &c);
      break;
    case FUZZ_PAGE:
      page(data, size, &c);
      break;
    default:
      archived(data, size, &c);
      break;
  }
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  uint32_t h = fuzz_selector(data, size);
  return fuzz_render(data, size, (fuzz_operation)(h % FUZZ_OPERATIONS),
                     h / FUZZ_OPERATIONS);
}
