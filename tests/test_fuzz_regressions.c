/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Inputs the fuzz target found, kept with what they must do, and the hostile
 * outlines behind the first of them, on either backend:
 *
 * - tests/fuzz-regressions/outline-cycle.pdf, imported, has an outline whose
 *   root names itself as its first entry. Its exports finish promptly, as do
 *   those of outlines whose siblings or children loop, and a fresh export
 *   drops such an outline (CoreGraphics would never finish reading it); an
 *   outline nested deeper than 128 levels is refused with "PDF outline
 *   limit".
 * - Tall eraser pages render on both backends. Cairo lowers each tall page's
 *   fallback resolution so merged cuts and enclosing shader groups fit its
 *   image limit. Planned cuts cover the rounded boundary, adjacent groups
 *   and a nested group, with nothing left for the leak checks to find.
 *
 *   test_fuzz_regressions <fonts directory> <regressions directory> */
#include <rmlines/parser.h>
#include <rmlines/writer.h>
#include <stdarg.h>
#include <time.h>

#include "../src/rmr_pdf_edit.h"
#ifdef RMR_TEST_LINUX
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdfview.h>
#include <rmrender/linux.h>
#include <rmrender/linux_document.h>
#include <rmrender/linux_pdf.h>

#include "../backends/linux/rmr_linux_internal.h"
#ifdef RMR_TEST_FONTCONFIG
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#endif
#define PDF_SOURCE rmr_linux_pdf_source
#define SOURCE_CREATE rmr_linux_pdf_source_create
#define SOURCE_FREE rmr_linux_pdf_source_free
#define OVERLAY rmr_linux_pdf_overlay
#define DOCUMENT_PDF rmr_linux_document_pdf
#define PAGE_PLAN rmr_linux_page_plan
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
#define FONTS rmr_linux_fonts
#define FONTS_DIRECTORY rmr_linux_fonts_create_directory
#define FONTS_FREE rmr_linux_fonts_free
#define GRAIN_GROUPS true
#else
#include <rmrender/apple.h>
#include <rmrender/apple_document.h>
#include <rmrender/apple_pdf.h>
#define PDF_SOURCE rmr_apple_pdf_source
#define SOURCE_CREATE rmr_apple_pdf_source_create
#define SOURCE_FREE rmr_apple_pdf_source_free
#define OVERLAY rmr_apple_pdf_overlay
#define DOCUMENT_PDF rmr_apple_document_pdf
#define PAGE_PLAN rmr_apple_page_plan
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
#define FONTS rmr_apple_fonts
#define FONTS_DIRECTORY rmr_apple_fonts_create_directory
#define FONTS_FREE rmr_apple_fonts_free
#define GRAIN_GROUPS false
#endif
/* The fixture's documents and helpers, not its cases. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "archive_fixture.h"
#pragma GCC diagnostic pop

/* An export of a hostile file must not take long: before the outline walk
 * was bounded, one of these took seconds and gigabytes. Generous enough for
 * a sanitized build on a busy machine. */
constexpr double PROMPT_SECONDS = 5;

static double seconds(void) {
  struct timespec t;
  timespec_get(&t, TIME_UTC);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* The whole file at path; freed with free. */
static uint8_t* slurp(const char* directory, const char* name, size_t* size) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", directory, name);
  FILE* f = fopen(path, "rb");
  CHECK(f);
  uint8_t* bytes = malloc(1 << 20);
  CHECK(bytes);
  *size = fread(bytes, 1, 1 << 20, f);
  CHECK(!ferror(f) && *size && *size < (1 << 20));
  fclose(f);
  return bytes;
}

/* Growable bytes, for files larger than a fixture holds. */
typedef struct {
  char* bytes;
  size_t n, capacity;
} text;

static void put(text* t, const char* format, ...) {
  if (!t->bytes) {
    t->capacity = 4096;
    t->bytes = malloc(t->capacity);
    CHECK(t->bytes);
  }
  for (;;) {
    va_list a;
    va_start(a, format);
    int n = vsnprintf(t->bytes + t->n, t->capacity - t->n, format, a);
    va_end(a);
    CHECK(n >= 0);
    if ((size_t)n < t->capacity - t->n) {
      t->n += (size_t)n;
      return;
    }
    t->capacity = 2 * t->capacity + (size_t)n + 64;
    t->bytes = realloc(t->bytes, t->capacity);
    CHECK(t->bytes);
  }
}

/* A two-page PDF whose outline root and entries are the given
 * dictionaries, objects 3 (the root) and 6 onward; the pages are objects 4
 * and 5, and both draw the content stream that follows the entries. Freed
 * with free. */
static text outlined_with_names(const char* const* items, size_t count,
                                bool named_links) {
  text f = {0};
  size_t content = count + 5;
  size_t last = content + (named_links ? 2 : 0);
  size_t* offsets = calloc(last + 1, sizeof *offsets);
  CHECK(offsets);
  char page[192], catalog[192], annotations[48] = "";
  if (named_links)
    snprintf(annotations, sizeof annotations, "/Annots [%zu 0 R]", content + 2);
  snprintf(catalog, sizeof catalog,
           "<< /Type /Catalog /Pages 2 0 R /Outlines 3 0 R %s%zu%s >>",
           named_links ? "/Names << /Dests " : "/Unused ",
           named_links ? content + 1 : 0, named_links ? " 0 R >>" : "");
  snprintf(page, sizeof page,
           "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents "
           "%zu 0 R %s >>",
           content, annotations);
  put(&f, "%%PDF-1.7\n");
  const char* fixed[] = {catalog,
                         "<< /Type /Pages /Kids [4 0 R 5 0 R] /Count 2 >>",
                         nullptr, page, page};
  for (size_t id = 1; id < content; id++) {
    offsets[id] = f.n;
    const char* body = id == 3  ? items[0]
                       : id < 6 ? fixed[id - 1]
                                : items[id - 5];
    put(&f, "%zu 0 obj\n%s\nendobj\n", id, body);
  }
  offsets[content] = f.n;
  put(&f,
      "%zu 0 obj\n<< /Length 25 >>\nstream\n0 0 1 rg 10 10 50 50 re f\n"
      "endstream\nendobj\n",
      content);
  if (named_links) {
    offsets[content + 1] = f.n;
    put(&f, "%zu 0 obj\n<< /Names [", content + 1);
    for (size_t i = 0; i < 340; i++)
      put(&f, "(N%03zu) [4 0 R /XYZ 0 200 null] ", i);
    put(&f, "] >>\nendobj\n");
    offsets[content + 2] = f.n;
    put(&f,
        "%zu 0 obj\n<< /Type /Annot /Subtype /Link "
        "/Rect [10 10 20 20] /Dest (N339) >>\nendobj\n",
        content + 2);
  }
  size_t x = f.n;
  put(&f, "xref\n0 %zu\n0000000000 65535 f \n", last + 1);
  for (size_t id = 1; id <= last; id++)
    put(&f, "%010zu 00000 n \n", offsets[id]);
  put(&f, "trailer\n<< /Size %zu /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n",
      last + 1, x);
  free(offsets);
  return f;
}

/* The same outline without local links or a name tree. */
static text outlined(const char* const* items, size_t count) {
  return outlined_with_names(items, count, false);
}

/* Statuses an outline case expects, fresh, preserving and recomposed;
 * UNTRIED leaves a mode out. */
constexpr rmr_status UNTRIED = (rmr_status)0x7fffffffu;

/* pdf imported under an annotated page and a placeholder one, as trio
 * imports a fixture, exported in each mode want names: each export returns
 * want[mode], a fresh one that fails with `message`, and none takes long. */
static void outline_case(const char* name, const uint8_t* pdf, size_t size,
                         const FONTS* fonts, const rmr_status want[3],
                         const char* message, bool dropped) {
  rmd_bytes ink = stroked_page(4, 30), junk = {(const uint8_t*)"x", 1};
  built_archive a = {0};
  const char content[] =
      "{\"cPages\":{\"pages\":["
      "{\"id\":\"A\",\"redir\":{\"value\":0}},"
      "{\"id\":\"B\",\"redir\":{\"value\":1}}]}}";
  a.content = strdup(content);
  CHECK(a.content);
  put_file(&a, CASE_DOC ".content",
           (rmd_bytes){(const uint8_t*)a.content, strlen(content)});
  put_file(&a, CASE_DOC ".pdf", (rmd_bytes){pdf, size});
  put_file(&a, CASE_DOC "/A.rm", ink);
  put_file(&a, CASE_DOC "/B.rm", junk);
  rmr_archive_document* d = open_archive(&a, false);
  rmr_pdf_options options = rmr_pdf_options_default();
  options.unreadable = RMR_UNREADABLE_PLACEHOLDER;
  for (int mode = 0; mode < 3; mode++) {
    if (want[mode] == UNTRIED) continue;
    rmr_buffer out = {0};
    rmr_error e = {0};
    double started = seconds();
    rmr_status s =
        mode == 0   ? DOCUMENT_PDF_WITH_OPTIONS(d, nullptr, fonts, RMR_PAPER_A4,
                                                0, nullptr, &options, nullptr,
                                                nullptr, &out, nullptr, &e)
        : mode == 1 ? DOCUMENT_PDF_PRESERVING_WITH_OPTIONS(
                          d, nullptr, fonts, RMR_PAPER_A4, 0, nullptr, &options,
                          nullptr, nullptr, &out, nullptr, &e)
                    : DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS(
                          d, nullptr, fonts, RMR_PAPER_A4, 0, nullptr, &options,
                          nullptr, nullptr, &out, nullptr, &e);
    double took = seconds() - started;
    size_t titles = 0;
    for (size_t i = 0; i + 6 <= out.size; i++)
      titles += !memcmp(out.bytes + i, "/Title", 6);
    printf("%-30s mode %d: status %d \"%s\", %zu titles, %.3f s\n", name, mode,
           (int)s, s ? e.message : "", titles, took);
    CHECK(s == want[mode] && took < PROMPT_SECONDS);
    if (s != RMR_OK) CHECK(!out.bytes && e.message[0]);
    if (s != RMR_OK && !mode) CHECK(!strcmp(e.message, message));
    /* A dropped outline leaves no entry in a fresh export. */
    if (s == RMR_OK && !mode && dropped) CHECK(!titles);
    rmr_buffer_free(&out);
  }
  rmr_archive_document_free(d);
  archive_free(&a);
  rm_bytes_free((uint8_t*)(uintptr_t)ink.bytes);
}

/* outline_case over the PDF of the given outline. */
static void outline_items(const char* name, const char* const* items,
                          size_t count, const FONTS* fonts,
                          const rmr_status want[3], const char* message,
                          bool dropped) {
  text f = outlined(items, count);
  outline_case(name, (const uint8_t*)f.bytes, f.n, fonts, want, message,
               dropped);
  free(f.bytes);
}

/* Entries for outlined: the root, then `top` top-level entries, objects 6
 * onward, each the next of the one before. Entry `chain` (an object number,
 * 0 for none) heads a chain of DEEP entries, each the only child of the one
 * before; entry `looped` is its own first child. The strings live until the
 * next call; *count receives how many there are. */
enum { DEEP = 140 };
static const char* const* outline_of(size_t top, size_t chain, size_t looped,
                                     size_t* count) {
  static char** items;
  static size_t made;
  for (size_t i = 0; i < made; i++) free(items[i]);
  size_t n = 1 + top + (chain ? (size_t)DEEP : 0);
  items = realloc(items, n * sizeof *items);
  CHECK(items);
  made = n;
  char buffer[256];
  size_t last_top = 5 + top;
#define DEST "/Dest [4 0 R /XYZ 0 200 null]"
  snprintf(buffer, sizeof buffer,
           "<< /Type /Outlines /First 6 0 R /Last %zu 0 R /Count %zu >>",
           last_top, top);
  items[0] = strdup(buffer);
  for (size_t i = 0; i < top; i++) {
    size_t id = 6 + i;
    char next[32] = "", child[64] = "";
    if (i + 1 < top) snprintf(next, sizeof next, "/Next %zu 0 R ", id + 1);
    if (id == looped)
      snprintf(child, sizeof child, "/First %zu 0 R /Last %zu 0 R ", id, id);
    else if (id == chain)
      snprintf(child, sizeof child, "/First %zu 0 R /Last %zu 0 R ",
               last_top + 1, last_top + 1);
    snprintf(buffer, sizeof buffer,
             "<< /Title (T) /Parent 3 0 R %s%s" DEST " >>", next, child);
    items[1 + i] = strdup(buffer);
  }
  for (size_t k = 0; chain && k < DEEP; k++) {
    size_t id = last_top + 1 + k;
    char child[64] = "";
    if (k + 1 < DEEP)
      snprintf(child, sizeof child, "/First %zu 0 R /Last %zu 0 R ", id + 1,
               id + 1);
    snprintf(buffer, sizeof buffer,
             "<< /Title (D) /Parent %zu 0 R %s" DEST " >>", k ? id - 1 : chain,
             child);
    items[1 + top + k] = strdup(buffer);
  }
#undef DEST
  for (size_t i = 0; i < n; i++) CHECK(items[i]);
  *count = n;
  return (const char* const*)items;
}

static void outlines(const char* directory, const FONTS* fonts) {
  size_t size = 0;
  uint8_t* found = slurp(directory, "outline-cycle.pdf", &size);
  /* Its other damage fails a recomposition, which rewrites every object. */
  outline_case("fuzzed self-rooted", found, size, fonts,
               (rmr_status[]){RMR_OK, RMR_OK, RMR_ERR_INVALID_ARGUMENT},
               nullptr, true);
  free(found);
  /* Only a fresh export rebuilds the outline; the others keep its bytes. */
  static const rmr_status clean[3] = {RMR_OK, RMR_OK, RMR_OK},
                          limit[3] = {RMR_ERR_LIMIT, RMR_OK, RMR_OK},
                          fresh_ok[3] = {RMR_OK, UNTRIED, UNTRIED},
                          fresh_limit[3] = {RMR_ERR_LIMIT, UNTRIED, UNTRIED};

#define DEST "/Dest [4 0 R /XYZ 0 200 null]"
  const char* self[] = {"<< /Type /Outlines /First 3 0 R /Last 3 0 R >>"};
  outline_items("root its own entry", self, 1, fonts, clean, nullptr, true);

  const char* siblings[] = {
      "<< /Type /Outlines /First 6 0 R /Last 7 0 R /Count 2 >>",
      "<< /Title (A) /Parent 3 0 R /Next 7 0 R " DEST " >>",
      "<< /Title (B) /Parent 3 0 R /Prev 6 0 R /Next 6 0 R " DEST " >>"};
  outline_items("siblings in a loop", siblings, 3, fonts, clean, nullptr, true);

  const char* child[] = {
      "<< /Type /Outlines /First 6 0 R /Last 6 0 R /Count 1 >>",
      "<< /Title (A) /Parent 3 0 R /First 6 0 R /Last 6 0 R /Count 1 " DEST
      " >>"};
  outline_items("its own child", child, 2, fonts, clean, nullptr, true);
#undef DEST

  /* A loop and a limit in one outline: the walk, depth first with an
   * entry's children before its next sibling, meets one of them first, and
   * that one decides, on either backend. */
  size_t count = 0;
  const char* const* items = outline_of(1, 6, 0, &count);
  outline_items("140 levels deep", items, count, fonts, limit,
                "PDF outline limit", false);
  items = outline_of(2, 6, 7, &count);
  outline_items("deep, then a loop", items, count, fonts, limit,
                "PDF outline limit", false);
  items = outline_of(2, 7, 6, &count);
  outline_items("loop, then deep", items, count, fonts, clean, nullptr, true);
  items = outline_of(65537, 0, 6, &count);
  outline_items("loop, then 65,537 entries", items, count, fonts, fresh_ok,
                nullptr, true);
  items = outline_of(65537, 0, 65537 + 5, &count);
  outline_items("65,537 entries, then a loop", items, count, fonts, fresh_limit,
                "PDF outline limit", false);
  items = outline_of(65536, 0, 0, &count);
  outline_items("65,536 entries", items, count, fonts, fresh_ok, nullptr,
                false);
  char** mutable_items = (char**)(uintptr_t)items;
  free(mutable_items[count - 1]);
  mutable_items[count - 1] = strdup(
      "<< /Title (T) /Parent 3 0 R /Next 3 0 R "
      "/Dest [4 0 R /XYZ 0 200 null] >>");
  CHECK(mutable_items[count - 1]);
  outline_items("65,536 entries, then root", items, count, fonts, fresh_ok,
                nullptr, true);
}

/* Cairo's raw pre-walk has its own budget, as Apple's native one does.
 * A large name tree in a separate object is not traversed to check an
 * outline. This check isolates that budget from later assembly work. */
static void outline_work(void) {
  size_t count = 0;
  const char* const* items = outline_of(20, 0, 25, &count);
  rmr_limits limits = rmr_limits_default();
  limits.maximum_elements = 768;
  for (int names = 0; names < 2; names++) {
    text pdf = outlined_with_names(items, count, names != 0);
    bool usable = true;
    size_t entries = SIZE_MAX;
    CHECK(rmr_pdf_outline_shape((const uint8_t*)pdf.bytes, pdf.n, &limits,
                                &usable, &entries) == RMR_OK &&
          !usable);
    rmr_limits small = limits;
    small.maximum_elements = 1;
    CHECK(rmr_pdf_outline_shape((const uint8_t*)pdf.bytes, pdf.n, &small,
                                &usable, &entries) == RMR_ERR_LIMIT &&
          !usable);
    free(pdf.bytes);
  }

  /* Link collection costs Apple 345 visits while PDFium charges one unit.
   * A further 380 outline entries must get a budget of their own: sharing
   * the 700-unit budget would refuse the outline before reaching its loop. */
  items = outline_of(380, 0, 385, &count);
  text pdf = outlined_with_names(items, count, true);
  PDF_SOURCE* source = nullptr;
  OK(SOURCE_CREATE((const uint8_t*)pdf.bytes, pdf.n, nullptr, &source,
                   nullptr));
  rmr_plan* empty = nullptr;
  OK(rmr_plan_create(nullptr, 0, (rmr_rect){0, 0, 200, 200}, nullptr, &empty,
                     nullptr));
  rmr_pdf_page page = {.has_source = true,
                       .source_visible = true,
                       .source_rect = {0, 0, 200, 200},
                       .sheet = {.width = 200,
                                 .height = 200,
                                 .transform = {1, 0, 0, -1, 0, 200}},
                       .marks = empty};
  limits.maximum_elements = 700;
  rmr_buffer out = {0};
  rmr_error error = {0};
  rmr_status status = OVERLAY(source, &page, 1, &limits, nullptr, nullptr, &out,
                              nullptr, &error);
  printf("outline with link work: status %d \"%s\", %zu bytes\n", (int)status,
         status ? error.message : "", out.size);
  CHECK(status == RMR_OK && out.size);
  rmr_buffer_free(&out);
  rmr_plan_free(empty);
  SOURCE_FREE(source);
  free(pdf.bytes);
}

/* One rectangle of ink cut by one eraser rectangle, on a page of width by
 * height points, exported at one point per unit. Coordinates are the page's,
 * from its top left. Every cut renders on both backends. */
static void eraser_case(const char* name, double width, double height,
                        rmr_rect ink, rmr_rect cut) {
  rmr_element ink_path[] = {
      {.kind = RMR_MOVE, .end = {ink.x, ink.y}},
      {.kind = RMR_LINE, .end = {ink.x + ink.width, ink.y}},
      {.kind = RMR_LINE, .end = {ink.x + ink.width, ink.y + ink.height}},
      {.kind = RMR_LINE, .end = {ink.x, ink.y + ink.height}},
      {.kind = RMR_CLOSE}};
  rmr_element cut_path[] = {
      {.kind = RMR_MOVE, .end = {cut.x, cut.y}},
      {.kind = RMR_LINE, .end = {cut.x + cut.width, cut.y}},
      {.kind = RMR_LINE, .end = {cut.x + cut.width, cut.y + cut.height}},
      {.kind = RMR_LINE, .end = {cut.x, cut.y + cut.height}},
      {.kind = RMR_CLOSE}};
  rmr_path_view eraser = {cut_path, 5};
  rmr_draw draw = {.path = {ink_path, 5},
                   .mode = RMR_FILL,
                   .color = {0, 0, 0, 1},
                   .erasers = &eraser,
                   .eraser_count = 1};
  rmr_plan* plan = nullptr;
  OK(rmr_plan_create(&draw, 1, (rmr_rect){0, 0, width, height}, nullptr, &plan,
                     nullptr));
  const rmr_plan* plans[] = {plan};
  for (int run = 0; run < 3; run++) {
    rmr_buffer out = {0};
    rmr_error e = {0};
    rmr_status s = PLANS_PDF(plans, 1, 1, nullptr, nullptr, nullptr, &out, &e);
    printf("%-30s status %d \"%s\", %zu bytes\n", name, (int)s,
           s ? e.message : "", out.size);
    CHECK(s == RMR_OK && out.size > 0);
    rmr_buffer_free(&out);
  }
  rmr_plan_free(plan);
}

/* Rectangle paths remain live until plan creation copies them. */
static rmr_path_view rectangle_path(rmr_element elements[5], rmr_rect r) {
  elements[0] = (rmr_element){.kind = RMR_MOVE, .end = {r.x, r.y}};
  elements[1] = (rmr_element){.kind = RMR_LINE, .end = {r.x + r.width, r.y}};
  elements[2] =
      (rmr_element){.kind = RMR_LINE, .end = {r.x + r.width, r.y + r.height}};
  elements[3] = (rmr_element){.kind = RMR_LINE, .end = {r.x, r.y + r.height}};
  elements[4] = (rmr_element){.kind = RMR_CLOSE};
  return (rmr_path_view){elements, 5};
}

#ifdef RMR_TEST_LINUX
/* The fallback images' largest side, and paths Cairo kept as vectors,
 * including objects inside its nested forms. */
static void fallback_objects(FPDF_PAGEOBJECT object, unsigned int* largest,
                             size_t* vectors, size_t budget) {
  int type = FPDFPageObj_GetType(object);
  if (type == FPDF_PAGEOBJ_IMAGE) {
    FPDF_IMAGEOBJ_METADATA metadata;
    CHECK(FPDFImageObj_GetImageMetadata(object, nullptr, &metadata));
    CHECK(metadata.width <= 32767 && metadata.height <= 32767);
    CHECK((uint64_t)metadata.width * metadata.height * 4 <= budget);
    if (metadata.width > *largest) *largest = metadata.width;
    if (metadata.height > *largest) *largest = metadata.height;
  } else if (type == FPDF_PAGEOBJ_PATH) {
    ++*vectors;
  } else if (type == FPDF_PAGEOBJ_FORM) {
    for (int i = 0; i < FPDFFormObj_CountObjects(object); i++)
      fallback_objects(FPDFFormObj_GetObject(object, (unsigned long)i), largest,
                       vectors, budget);
  }
}

/* The tall page uses 78.63 ppi; the following short page uses 300 again. Both
 * retain a separate vector witness outside the fallback regions. */
static void fallback_pages(rmr_buffer pdf, double tall_extent) {
  FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
  CHECK(doc && FPDF_GetPageCount(doc) == 2);
  for (int i = 0; i < 2; i++) {
    FPDF_PAGE page = FPDF_LoadPage(doc, i);
    CHECK(page);
    unsigned int largest = 0;
    size_t vectors = 0;
    for (int k = 0; k < FPDFPage_CountObjects(page); k++)
      fallback_objects(FPDFPage_GetObject(page, k), &largest, &vectors,
                       SIZE_MAX);
    double expected = i ? 50 * 300.0 / 72 : tall_extent * 78.63 / 72;
    printf("fallback page %d: %u pixels, %zu vector paths\n", i, largest,
           vectors);
    CHECK(largest >= expected - 8 && largest <= expected + 8 && vectors);
    FPDF_ClosePage(page);
  }
  FPDF_CloseDocument(doc);
}
#endif

/* Two touching cuts merge into one fallback region. A gap is the control.
 * Nested cuts force the whole outer group, including its tall stroke, into
 * that region. Each case is followed by a short page in the same export. */
static void merged_cuts(double gap, bool nested) {
  rmr_element paths[4][5], cuts[2][5];
  rmr_path_view erasers[2] = {
      rectangle_path(cuts[0], (rmr_rect){110, 110, 10, 10}),
      rectangle_path(cuts[1], (rmr_rect){110, 4110 + gap, 10, 10})};
  rmr_draw draws[4] = {
      {.path = rectangle_path(paths[0], (rmr_rect){100, 100, 50, 4000}),
       .mode = RMR_FILL,
       .color = {0, 0, 0, 1},
       .erasers = &erasers[0],
       .eraser_count = 1},
      {.path = rectangle_path(paths[1], (rmr_rect){100, 4100 + gap, 50, 4000}),
       .mode = RMR_FILL,
       .color = {0, 0, 0, 1},
       .erasers = &erasers[1],
       .eraser_count = 1},
      {.path = rectangle_path(paths[2], (rmr_rect){500, 50, 10, 10}),
       .mode = RMR_FILL,
       .color = {0, 0, 1, 1}}};
  size_t count = 3;
  if (nested) {
    draws[0].path = rectangle_path(paths[0], (rmr_rect){100, 100, 50, 50});
    draws[1] = (rmr_draw){
        .path = rectangle_path(paths[1], (rmr_rect){300, 100, 5, 20000}),
        .mode = RMR_FILL,
        .color = {0, 0, 1, 1}};
    for (size_t i = 0; i < 2; i++) {
      draws[i].group = 1;
      draws[i].group_alpha = .5;
      draws[i].group_blend = RMR_BLEND_MULTIPLY;
    }
  }
  rmr_plan* tall = nullptr;
  OK(rmr_plan_create(draws, count, (rmr_rect){0, 0, 702, 30000}, nullptr, &tall,
                     nullptr));
  draws[0].path = rectangle_path(paths[3], (rmr_rect){100, 100, 50, 50});
  draws[0].group = 0;
  draws[1] = draws[2];
  rmr_plan* short_page = nullptr;
  OK(rmr_plan_create(draws, 2, (rmr_rect){0, 0, 702, 200}, nullptr, &short_page,
                     nullptr));
  const rmr_plan* plans[] = {tall, short_page};
  for (int run = 0; run < 3; run++) {
    rmr_buffer pdf = {0};
    OK(PLANS_PDF(plans, 2, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
#ifdef RMR_TEST_LINUX
    fallback_pages(pdf, nested ? 20000 : gap > 0 ? 4000 : 8000);
#endif
    rmr_buffer_free(&pdf);
  }
  rmr_plan_free(tall);
  rmr_plan_free(short_page);
}

/* All earlier extent probes now succeed, including fractional boundaries. */
static void eraser_extents(void) {
  eraser_case("small ink, page-tall eraser", 702, 30000,
              (rmr_rect){95, 15000, 10, 10}, (rmr_rect){90, 5, 20, 29990});
  eraser_case("page-tall ink, small eraser", 702, 30000,
              (rmr_rect){95, 10, 10, 29980}, (rmr_rect){90, 55, 20, 10});
  eraser_case("small ink and eraser", 702, 30000, (rmr_rect){95, 15000, 10, 10},
              (rmr_rect){90, 15002, 20, 4});
  eraser_case("7,863 points wide", 9000, 200, (rmr_rect){0, 50, 7863, 20},
              (rmr_rect){90, 55, 20, 10});
  eraser_case("7,862.2 from 0.9", 9000, 200, (rmr_rect){0.9, 50, 7862.2, 20},
              (rmr_rect){90, 55, 20, 10});
  eraser_case("7,864 points wide", 9000, 200, (rmr_rect){0, 50, 7864, 20},
              (rmr_rect){90, 55, 20, 10});
  eraser_case("7,862.4 from 0.5", 9000, 200, (rmr_rect){0.5, 50, 7862.4, 20},
              (rmr_rect){90, 55, 20, 10});
  eraser_case("7,863 point page", 702, 7863, (rmr_rect){95, 10, 10, 7840},
              (rmr_rect){90, 55, 20, 10});
  merged_cuts(0, false);
  merged_cuts(50, false);
  merged_cuts(0, true);
}

/* Overlay sheets also come from documents and can exceed the direct PDF
 * API's sheet bound. A plain page must not depend on raster resolution. */
static void huge_overlay(void) {
  rmr_element path[5];
  rmr_draw draw = {.path = rectangle_path(path, (rmr_rect){10, 10, 80, 80}),
                   .mode = RMR_FILL,
                   .color = {0, 0, 0, 1}};
  rmr_plan* plan = nullptr;
  OK(rmr_plan_create(&draw, 1, (rmr_rect){0, 0, 702, 3000000}, nullptr, &plan,
                     nullptr));
  rmr_pdf_page page = {.marks = plan,
                       .sheet = {702, 3000000, {1, 0, 0, -1, 0, 3000000}}};
  rmr_buffer pdf = {0};
  OK(OVERLAY(nullptr, &page, 1, nullptr, nullptr, nullptr, &pdf, nullptr,
             nullptr));
  CHECK(pdf.size);
  printf("3,000,000-point overlay: %zu bytes\n", pdf.size);
  rmr_buffer_free(&pdf);
  rmr_plan_free(plan);
}

#ifdef RMR_TEST_LINUX
/* A cut covering most of a large page must obey the bitmap budget after
 * Cairo rounds both image dimensions, including on very narrow pages. */
static void fallback_budget(double width, double height, size_t budget) {
  rmr_element path[5], cut_path[5];
  rmr_path_view cut = rectangle_path(
      cut_path, (rmr_rect){fmin(10, width / 4), 10, fmin(10, width / 2), 10});
  rmr_draw draw = {
      .path = rectangle_path(path, (rmr_rect){0, 0, width, height}),
      .mode = RMR_FILL,
      .color = {0, 0, 0, 1},
      .erasers = &cut,
      .eraser_count = 1};
  rmr_limits limits = rmr_limits_default();
  limits.maximum_bitmap_bytes = budget;
  rmr_plan* plan = nullptr;
  OK(rmr_plan_create(&draw, 1, (rmr_rect){0, 0, width, height}, &limits, &plan,
                     nullptr));
  rmr_pdf_page sheet = {.marks = plan,
                        .sheet = {width, height, {1, 0, 0, -1, 0, height}}};
  rmr_buffer pdf = {0};
  OK(OVERLAY(nullptr, &sheet, 1, &limits, nullptr, nullptr, &pdf, nullptr,
             nullptr));
  FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
  CHECK(doc);
  FPDF_PAGE page = FPDF_LoadPage(doc, 0);
  CHECK(page);
  unsigned int largest = 0;
  size_t vectors = 0;
  for (int i = 0; i < FPDFPage_CountObjects(page); i++)
    fallback_objects(FPDFPage_GetObject(page, i), &largest, &vectors, budget);
  CHECK(largest);
  printf("fallback %.1f x %.1f: %u-pixel side, budget %zu bytes\n", width,
         height, largest, budget);
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);
  rmr_buffer_free(&pdf);
  rmr_plan_free(plan);
}

/* A native collection is capped independently of its reader's resolution of
 * the file. Exhaust the count on siblings and children, and on the first
 * entry of a nonempty outline. */
static void outline_collection_cap(void) {
  const char* items[] = {
      "<< /Type /Outlines /First 6 0 R >>",
      "<< /Title (parent) /Parent 3 0 R /First 7 0 R /Next 8 0 R >>",
      "<< /Title (child) /Parent 6 0 R >>",
      "<< /Title (sibling) /Parent 3 0 R >>"};
  text pdf = outlined(items, 4);
  rmr_limits limits = rmr_limits_default();
  bool usable = false;
  size_t entries = 0;
  OK(rmr_pdf_outline_shape((const uint8_t*)pdf.bytes, pdf.n, &limits, &usable,
                           &entries));
  CHECK(usable && entries == 3);
  PDF_SOURCE* source = nullptr;
  OK(SOURCE_CREATE((const uint8_t*)pdf.bytes, pdf.n, &limits, &source,
                   nullptr));
  FPDF_DOCUMENT native = FPDF_LoadMemDocument64(pdf.bytes, pdf.n, nullptr);
  CHECK(native);
  for (size_t cap = 0; cap <= entries; cap++) {
    size_t remaining = cap;
    bool overflow = false;
    FPDF_BOOKMARK first = rmr_linux_bookmark_next(native, nullptr, nullptr,
                                                  &remaining, &overflow);
    CHECK((first != nullptr) == (cap > 0));
    if (first) {
      FPDF_BOOKMARK next = rmr_linux_bookmark_next(native, nullptr, first,
                                                   &remaining, &overflow);
      CHECK((next != nullptr) == (cap > 1));
      if (next) {
        CHECK(!rmr_linux_bookmark_next(native, nullptr, next, &remaining,
                                       &overflow));
        CHECK(!overflow);
        FPDF_BOOKMARK child = rmr_linux_bookmark_next(native, first, nullptr,
                                                      &remaining, &overflow);
        CHECK((child != nullptr) == (cap > 2));
        if (child)
          CHECK(!rmr_linux_bookmark_next(native, first, child, &remaining,
                                         &overflow));
      }
    }
    CHECK(!remaining && overflow == (cap < entries));
  }
  puts("native bookmark caps 0 through 3: siblings and children bounded");
  FPDF_CloseDocument(native);
  SOURCE_FREE(source);
  free(pdf.bytes);
}

#endif

/* A tall page, planned and exported three times at half scale. */
static void tall_page(const char* directory, const char* name,
                      const FONTS* fonts) {
  size_t size = 0;
  uint8_t* bytes = slurp(directory, name, &size);
  rm_page* page = nullptr;
  CHECK(rm_parse_page(bytes, size, &page, nullptr) == RM_OK);
  rmr_plan* plan = nullptr;
  OK(PAGE_PLAN(page, (rmr_point){1404, 1872}, RMR_EDGE_RIBBON, 0, nullptr,
               fonts, nullptr, &plan, nullptr));
  const rmr_plan* plans[] = {plan};
  for (int run = 0; run < 3; run++) {
    rmr_buffer out = {0};
    rmr_error e = {0};
    rmr_status s =
        PLANS_PDF(plans, 1, 0.5, nullptr, nullptr, nullptr, &out, &e);
    printf("%-30s status %d \"%s\", %zu bytes\n", name, (int)s,
           s ? e.message : "", out.size);
    CHECK(s == RMR_OK);
    CHECK(s == RMR_OK ? out.size > 0
                      : !out.bytes && !strcmp(e.message, "PDF drawing failed"));
    rmr_buffer_free(&out);
  }
  rmr_plan_free(plan);
  rm_page_free(page);
  free(bytes);
}

int main(int argc, char** argv) {
  CHECK(argc == 3);
  setvbuf(stdout, nullptr, _IOLBF, 0);
  FONTS* fonts = nullptr;
  OK(FONTS_DIRECTORY(argv[1], nullptr, &fonts, nullptr));
  outlines(argv[2], fonts);
  outline_work();
  tall_page(argv[2], "tall-eraser.rm", fonts);
  tall_page(argv[2], "tall-shader.rm", fonts);
  eraser_extents();
  huge_overlay();
#ifdef RMR_TEST_LINUX
  fallback_budget(30000, 30000, 4u << 20);
  fallback_budget(702.1, 30000.1, 1u << 20);
  fallback_budget(1, 30000, 4096);
  outline_collection_cap();
#endif
  FONTS_FREE(fonts);
#ifdef RMR_TEST_FONTCONFIG
  /* Standalone test owns the process: release the default Pango font map,
   * then Fontconfig's global caches, for LSan. */
  pango_cairo_font_map_set_default(nullptr);
  FcFini();
#endif
  return 0;
}
