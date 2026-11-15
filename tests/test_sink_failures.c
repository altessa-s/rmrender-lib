/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Every document PDF export must survive its output failing at any byte:
 * maximum_pdf_bytes reached part way, or an rmr_output refusing a write or a
 * byte. Each failure must return its status and a message naming its cause,
 * leave no output set, and never call the output again once it refused; no
 * export may succeed below its full size; and the next clean export must
 * match a clean run byte for byte, apart from the dates and file identifier
 * a backend stamps, so that no state survives the failure. The documents are
 * a notebook drawn with every tool, one with an unreadable page drawn as a
 * placeholder, and annotated pages over an imported PDF with an outline in
 * each of the fresh, preserving and recomposed modes, each into a buffer and
 * into an output.
 *
 * A limit or refused byte anywhere inside one write takes the path a failure
 * at that write's first byte takes, so by default the sweep tries every value
 * past the file header, both sides of each 4 KiB boundary and of each write's
 * end, a few values between and the last bytes before the full size, and
 * checks the clean export after the first failure of each kind in a run and
 * after the run's last; the output also refuses every write in turn.
 * `exhaustive` after the fonts directory tries every byte value instead and
 * checks the clean export after every failure.
 *
 *   test_sink_failures <fonts directory> [exhaustive] */
#include <rmlines/parser.h>
#include <rmlines/writer.h>
#include <time.h>
#ifdef RMR_TEST_LINUX
#include <rmrender/linux_document.h>
#ifdef RMR_TEST_FONTCONFIG
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#endif
#define DOCUMENT_PDF rmr_linux_document_pdf
#define PAGE_PLAN rmr_linux_page_plan
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
#include <rmrender/apple_document.h>
#define DOCUMENT_PDF rmr_apple_document_pdf
#define PAGE_PLAN rmr_apple_page_plan
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
/* The sweep needs the fixture's documents and helpers, not its cases. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "archive_fixture.h"
#pragma GCC diagnostic pop
#include "../backends/common/rmr_pdf_output.h"

static const char* current = "";
static size_t current_k;
static rmr_error last_error;

#define EXPECT(x)                                                           \
  do {                                                                      \
    if (!(x)) {                                                             \
      fprintf(                                                              \
          stderr,                                                           \
          "%s:%d: %s (%s, limit or position %zu; last status %d \"%s\")\n", \
          __FILE__, __LINE__, #x, current, current_k, (int)last_error.code, \
          last_error.message);                                              \
      exit(1);                                                              \
    }                                                                       \
  } while (0)

/* The output's refusal is a status these exports never produce themselves
 * here, so returning it proves the refusal passed through unchanged. */
constexpr rmr_status REFUSED = RMR_ERR_UNSUPPORTED;
constexpr size_t NEVER = SIZE_MAX;

/* An output keeping what it is given and refusing its fail_call-th write, or
 * the write that would take it past fail_byte bytes. */
typedef struct {
  uint8_t* bytes;
  size_t size, capacity, calls, fail_call, fail_byte, after_refusal;
  size_t* ends; /* the size after each accepted write */
  size_t ends_capacity;
  bool refused;
} sink;

static void* grow(void* data, size_t* capacity, size_t needed, size_t width) {
  if (needed <= *capacity) return data;
  size_t larger = needed * 2;
  void* grown = realloc(data, larger * width);
  EXPECT(grown);
  *capacity = larger;
  return grown;
}

static rmr_status collect(void* context, const uint8_t* data, size_t size) {
  sink* s = context;
  EXPECT(data && size);
  if (s->refused) {
    s->after_refusal++;
    return REFUSED;
  }
  s->calls++;
  if (s->calls == s->fail_call ||
      (s->fail_byte != NEVER && size > s->fail_byte - s->size)) {
    s->refused = true;
    return REFUSED;
  }
  s->ends = grow(s->ends, &s->ends_capacity, s->calls, sizeof *s->ends);
  s->bytes = grow(s->bytes, &s->capacity, s->size + size, 1);
  memcpy(s->bytes + s->size, data, size);
  s->size += size;
  s->ends[s->calls - 1] = s->size;
  return RMR_OK;
}

static sink fresh(void) {
  return (sink){.fail_call = NEVER, .fail_byte = NEVER};
}

static void reset(sink* s) {
  free(s->bytes);
  free(s->ends);
  *s = fresh();
}

/* A document exported in one mode. */
typedef struct {
  const char* name;
  rmr_archive_document* d;
  const FONTS* fonts;
  export_mode mode;
  bool label; /* draws the placeholder of an unreadable page */
} subject;

/* Counts cancellation polls and asks to stop at poll `at`. */
typedef struct {
  size_t polls, at;
} poller;

static bool poll_cancel(void* context) {
  poller* p = context;
  return ++p->polls >= p->at;
}

/* x exported under limits (null for the defaults) into a buffer, copied into
 * s, or into s itself when via_output is set. A failing buffer export must
 * leave its result unset. */
static rmr_status export(const subject* x, bool via_output,
                         const rmr_limits* limits, sink* s, poller* p) {
  rmr_pdf_options options = rmr_pdf_options_default();
  options.unreadable = RMR_UNREADABLE_PLACEHOLDER;
  static size_t templates;
  rmr_page_resources resources = {.template = counted_template,
                                  .template_context = &templates};
  rmr_cancel_fn cancel = p ? poll_cancel : nullptr;
  rmr_error e = {0};
  rmr_status status;
  if (via_output) {
    rmr_output output = {RMR_OUTPUT_ABI, sizeof output, collect, s};
    status = x->mode == FRESH
                 ? DOCUMENT_PDF_TO_OUTPUT(x->d, &resources, x->fonts,
                                          RMR_PAPER_A4, 0, limits, &options,
                                          cancel, p, &output, nullptr, &e)
             : x->mode == PRESERVING
                 ? DOCUMENT_PDF_PRESERVING_TO_OUTPUT(
                       x->d, &resources, x->fonts, RMR_PAPER_A4, 0, limits,
                       &options, cancel, p, &output, nullptr, &e)
                 : DOCUMENT_PDF_RECOMPOSED_TO_OUTPUT(
                       x->d, &resources, x->fonts, RMR_PAPER_A4, 0, limits,
                       &options, cancel, p, &output, nullptr, &e);
  } else {
    rmr_buffer out = {0};
    status = x->mode == FRESH
                 ? DOCUMENT_PDF_WITH_OPTIONS(x->d, &resources, x->fonts,
                                             RMR_PAPER_A4, 0, limits, &options,
                                             cancel, p, &out, nullptr, &e)
             : x->mode == PRESERVING
                 ? DOCUMENT_PDF_PRESERVING_WITH_OPTIONS(
                       x->d, &resources, x->fonts, RMR_PAPER_A4, 0, limits,
                       &options, cancel, p, &out, nullptr, &e)
                 : DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS(
                       x->d, &resources, x->fonts, RMR_PAPER_A4, 0, limits,
                       &options, cancel, p, &out, nullptr, &e);
    if (status != RMR_OK)
      EXPECT(!out.bytes && !out.size);
    else {
      EXPECT(out.bytes && out.size);
      s->bytes = out.bytes;
      s->size = s->capacity = out.size;
    }
  }
  EXPECT(status == RMR_OK || (e.code == status && e.message[0]));
  last_error = e;
  return status;
}

/* Whether two exports are the same file once dates and IDs are blanked. */
static bool same(sink* a, sink* b) {
  rmr_buffer x = {a->bytes, a->size}, y = {b->bytes, b->size};
  blank_volatile(&x);
  blank_volatile(&y);
  return x.size == y.size && !memcmp(x.bytes, y.bytes, x.size);
}

static bool one_of(const char* message, const char* const* list, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (!strcmp(message, list[i])) return true;
  return false;
}

/* Every cause a limit failure of these exports may name: the output, the
 * imported PDF or the placeholder's label outgrowing maximum_pdf_bytes, or
 * the writer's own memory for a page of it. */
static const char* const limit_causes[] = {
    "PDF output byte limit", "PDF input size limit", "source PDF limit",
    "label drawing failed", "PDF output memory limit or allocation failure"};

/* The causes a cancellation may be reported with, by where it was seen. The
 * Apple backend words one seen between the pages of a fresh export as its
 * overlay failing. */
static const char* const cancel_causes[] = {
    "document render cancelled",
    "PDF collection cancelled",
    "PDF recomposition cancelled",
    "PDF update cancelled",
    "cancelled",
    "backend execution failed or cancelled",
#ifdef RMR_TEST_LINUX
    "PDF composition cancelled",
#else
    "PDF overlay failed",
#endif
};

#if defined(__SANITIZE_ADDRESS__)
constexpr bool SANITIZED = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool SANITIZED = true;
#else
constexpr bool SANITIZED = false;
#endif
#else
constexpr bool SANITIZED = false;
#endif

typedef struct {
  size_t* items;
  size_t count, capacity;
} values;

static void add_value(values* v, size_t value, size_t full) {
  if (value > full + 1) return;
  v->items = grow(v->items, &v->capacity, v->count + 1, sizeof *v->items);
  v->items[v->count++] = value;
}

static int ascending(const void* a, const void* b) {
  size_t x = *(const size_t*)a, y = *(const size_t*)b;
  return (x > y) - (x < y);
}

/* Limits or byte positions to try for a file of clean->size bytes written in
 * clean->calls writes, sorted and without repeats: every value up to past
 * the file header, both sides of each 4 KiB boundary and of each write's
 * end, a few values between and the last bytes. Under AddressSanitizer an
 * export costs ten times more, so the dense values stop sooner and fewer lie
 * between. */
static values positions(const sink* clean, bool exhaustive) {
  values v = {0};
  size_t full = clean->size, dense = SANITIZED ? 8 : 16,
         between = SANITIZED ? 2 : 4;
  for (size_t k = 0; k <= full + 1; k++)
    if (exhaustive || k <= dense || k + 2 >= full) add_value(&v, k, full);
  if (!exhaustive) {
    for (size_t b = 4096; b <= full + 1; b += 4096) {
      add_value(&v, b - 1, full);
      add_value(&v, b + 1, full);
    }
    for (size_t j = 0; j < clean->calls; j++) {
      add_value(&v, clean->ends[j] - 1, full);
      add_value(&v, clean->ends[j] + 1, full);
    }
    for (size_t i = 1; i < between; i++)
      add_value(&v, full * i / between, full);
  }
  qsort(v.items, v.count, sizeof *v.items, ascending);
  size_t kept = 0;
  for (size_t i = 0; i < v.count; i++)
    if (!kept || v.items[kept - 1] != v.items[i]) v.items[kept++] = v.items[i];
  v.count = kept;
  return v;
}

static size_t exports;

/* A clean export after a failure is the clean file. */
static void expect_clean(const subject* x, bool via_output, sink* clean) {
  sink s = fresh();
  EXPECT(export(x, via_output, nullptr, &s, nullptr) == RMR_OK);
  EXPECT(same(&s, clean));
  exports++;
  reset(&s);
}

/* Whether a clean export is checked after this failure: after every one when
 * exhaustive, otherwise after the first failure with each message in a run
 * of them, and after the run's last. seen holds the previous message. */
static bool clean_due(char seen[static sizeof last_error.message],
                      bool exhaustive, bool last) {
  bool due = exhaustive || last || strcmp(seen, last_error.message);
  snprintf(seen, sizeof last_error.message, "%s", last_error.message);
  return due;
}

/* Wall-clock seconds: the exports deflate on worker threads. */
static double seconds(void) {
  struct timespec t;
  timespec_get(&t, TIME_UTC);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void sweep(const subject* x, bool exhaustive) {
  static const char* const modes[] = {"fresh", "preserving", "recomposed"};
  char title[96], name[128];
  double started = seconds();
  size_t before = exports;
  sink clean = fresh(), streamed = fresh();
  snprintf(title, sizeof title, "%s, %s", x->name, modes[x->mode]);
  current = title;
  current_k = 0;
  EXPECT(export(x, false, nullptr, &clean, nullptr) == RMR_OK);
  EXPECT(export(x, true, nullptr, &streamed, nullptr) == RMR_OK);
  EXPECT(same(&clean, &streamed));
  size_t full = clean.size;
  values tried = positions(&streamed, exhaustive);
  /* The last position below the full size, and the last at most it. */
  size_t below = 0, within = 0;
  for (size_t i = 0; i < tried.count; i++) {
    if (tried.items[i] < full) below = i;
    if (tried.items[i] <= full) within = i;
  }
  char seen[sizeof last_error.message];

  for (int via = 0; via < 2; via++) {
    snprintf(name, sizeof name, "%s via %s, maximum_pdf_bytes", title,
             via ? "output" : "buffer");
    current = name;
    seen[0] = 0;
    for (size_t i = 0; i < tried.count; i++) {
      size_t limit = current_k = tried.items[i];
      rmr_limits limits = rmr_limits_default();
      limits.maximum_pdf_bytes = limit;
      sink s = fresh();
      rmr_status status = export(x, via, &limits, &s, nullptr);
      exports++;
      if (limit < full) {
        EXPECT(status == RMR_ERR_LIMIT);
        EXPECT(one_of(last_error.message, limit_causes,
                      sizeof limit_causes / sizeof *limit_causes));
        EXPECT(!s.refused);
        if (x->label && !via) {
          /* The label and the pages around it fail on different threads;
           * the same call fails the same way whichever gets there first. */
          rmr_error first = last_error;
          sink again = fresh();
          EXPECT(export(x, via, &limits, &again, nullptr) == status);
          EXPECT(!strcmp(last_error.message, first.message));
          exports++;
          reset(&again);
        }
        reset(&s);
        if (clean_due(seen, exhaustive, i == below))
          expect_clean(x, via, &clean);
      } else {
        EXPECT(status == RMR_OK && same(&s, &clean));
        reset(&s);
      }
    }
  }

  /* The output refusing each write in turn, then each byte. */
  snprintf(name, sizeof name, "%s, refused write", title);
  current = name;
  for (size_t k = 1; k <= streamed.calls; k++) {
    current_k = k;
    sink s = fresh();
    s.fail_call = k;
    EXPECT(export(x, true, nullptr, &s, nullptr) == REFUSED);
    EXPECT(s.refused && s.calls == k && !s.after_refusal);
    EXPECT(!strcmp(last_error.message, "PDF output write failed"));
    exports++;
    reset(&s);
    expect_clean(x, true, &clean);
  }
  snprintf(name, sizeof name, "%s, refused byte", title);
  seen[0] = 0;
  for (size_t i = 0; i < tried.count; i++) {
    size_t k = current_k = tried.items[i];
    if (!k) continue;
    sink s = fresh();
    s.fail_byte = k - 1;
    rmr_status status = export(x, true, nullptr, &s, nullptr);
    exports++;
    if (k > full) {
      EXPECT(status == RMR_OK && same(&s, &clean));
      reset(&s);
      continue;
    }
    EXPECT(status == REFUSED && s.refused && !s.after_refusal && s.size < k);
    EXPECT(!strcmp(last_error.message, "PDF output write failed"));
    reset(&s);
    if (clean_due(seen, exhaustive, i == within)) expect_clean(x, true, &clean);
  }
  printf("%-34s %6zu bytes %3zu writes %4zu positions %5zu exports %6.2f s\n",
         title, full, streamed.calls, tried.count, exports - before,
         seconds() - started);
  fflush(stdout);
  free(tried.items);
  reset(&clean);
  reset(&streamed);
}

/* Each cause with its status and message, the same on both backends: a
 * limit below the file header and any limit below the whole file of a
 * notebook name the output byte limit; a refused write names the output;
 * a cancellation names where it was seen, and over an imported PDF that is
 * also the collection of its destinations. */
static void causes(const subject* tools, const subject* imported) {
  current = "causes";
  for (size_t limit = 0; limit <= 20; limit++)
    for (int via = 0; via < 2; via++) {
      current_k = limit;
      rmr_limits limits = rmr_limits_default();
      limits.maximum_pdf_bytes = limit;
      sink s = fresh();
      EXPECT(export(tools, via, &limits, &s, nullptr) == RMR_ERR_LIMIT);
      EXPECT(!strcmp(last_error.message, "PDF output byte limit"));
      reset(&s);
    }

  for (export_mode mode = FRESH; mode <= RECOMPOSED; mode++) {
    subject x = *imported;
    x.mode = mode;
    sink s = fresh();
    s.fail_call = 1;
    EXPECT(export(&x, true, nullptr, &s, nullptr) == REFUSED);
    EXPECT(!strcmp(last_error.message, "PDF output write failed"));
    reset(&s);
  }

  bool collection = false;
  for (size_t at = 1;; at++) {
    current_k = at;
    poller p = {.at = at};
    sink s = fresh();
    rmr_status status = export(imported, true, nullptr, &s, &p);
    reset(&s);
    if (status == RMR_OK) break;
    EXPECT(status == RMR_ERR_CANCELLED);
    EXPECT(one_of(last_error.message, cancel_causes,
                  sizeof cancel_causes / sizeof *cancel_causes));
    collection |= !strcmp(last_error.message, "PDF collection cancelled");
    EXPECT(at < 100000);
  }
  EXPECT(collection);
  printf("causes: limits 0 to 20, refused writes and cancellation\n");
}

/* A template resolver that fails its fail_at-th call, the export's own
 * failure, noting how many writes the output had taken by then. */
typedef struct {
  size_t calls, fail_at, writes_then;
  const sink* out;
} failing_resolver;

static rmr_status failing_template(void* context, rm_str name,
                                   const rmr_template** out) {
  failing_resolver* r = context;
  (void)name;
  *out = nullptr;
  if (++r->calls != r->fail_at) return RMR_OK;
  r->writes_then = r->out->calls;
  return RMR_ERR_BACKEND;
}

/* Once the export has decided to fail, its output hears nothing more, even
 * while pages before the failing one are finished to learn whether one of
 * them failed first. The exports close their output guard (backends/common/
 * rmr_pdf_output.h) before finishing those pages: a closed guard passes
 * nothing on. End to end, the output is written in 4 MiB pieces as pages
 * are deflated; the notebook's dense pages pass 4 MiB on one of its last two,
 * and the export fails at its last page. Whether those pages are still being
 * finished then depends on the workers, so this catches a regression often,
 * not always. */
static void quiet_after_failure(void) {
  sink direct = fresh();
  rmr_output given = {RMR_OUTPUT_ABI, sizeof given, collect, &direct};
  rmr_output_guard guard;
  rmr_output_guard_init(&guard, &given);
  const uint8_t bytes[] = {1, 2, 3};
  EXPECT(guard.output.write(guard.output.context, bytes, 3) == RMR_OK);
  guard.closed = true;
  EXPECT(guard.output.write(guard.output.context, bytes, 3) == RMR_OK);
  EXPECT(direct.calls == 1 && direct.size == 3);
  reset(&direct);

  constexpr size_t RUNS = SANITIZED ? 1 : 3;
  current = "output after the export's own failure";
  rmd_bytes dense = stroked_page(80, 80);
  char one[1][40];
  built_archive probe = notebook(1, dense, one, nullptr);
  rmr_archive_document* d = open_archive(&probe, false);
  rmr_buffer page = {0};
  OK(DOCUMENT_PDF(d, nullptr, nullptr, RMR_PAPER_A4, 0, nullptr, nullptr,
                  nullptr, &page, nullptr, nullptr));
  size_t pages = (4u << 20) / page.size + 2;
  rmr_buffer_free(&page);
  rmr_archive_document_free(d);
  archive_free(&probe);

  char (*ids)[40] = calloc(pages, sizeof *ids);
  EXPECT(ids);
  built_archive a = notebook(pages, dense, ids, "Grid");
  d = open_archive(&a, false);
  failing_resolver clean = {.fail_at = SIZE_MAX};
  sink s = fresh();
  clean.out = &s;
  rmr_page_resources resources = {.template = failing_template,
                                  .template_context = &clean};
  rmr_output output = {RMR_OUTPUT_ABI, sizeof output, collect, &s};
  OK(DOCUMENT_PDF_TO_OUTPUT(d, &resources, nullptr, RMR_PAPER_A4, 0, nullptr,
                            nullptr, nullptr, nullptr, &output, nullptr,
                            nullptr));
  EXPECT(clean.calls >= pages && s.calls > 1);
  reset(&s);
  for (size_t run = 0; run < RUNS; run++) {
    current_k = run;
    failing_resolver r = {.fail_at = clean.calls, .out = &s};
    resources.template_context = &r;
    rmr_error e = {0};
    EXPECT(DOCUMENT_PDF_TO_OUTPUT(d, &resources, nullptr, RMR_PAPER_A4, 0,
                                  nullptr, nullptr, nullptr, nullptr, &output,
                                  nullptr, &e) == RMR_ERR_BACKEND);
    EXPECT(r.calls == r.fail_at && s.calls == r.writes_then && e.message[0]);
    reset(&s);
  }
  rmr_archive_document_free(d);
  archive_free(&a);
  free(ids);
  rm_bytes_free((uint8_t*)(uintptr_t)dense.bytes);
  printf("no output after the export's own failure: %zu pages, %zu runs\n",
         pages, RUNS);
}

/* A page with one stroke of every tool the format names, in colour where
 * the tool takes one. */
static rmd_bytes every_tool(void) {
  static const rm_pen_tool tools[] = {RM_TOOL_PAINTBRUSH_1,
                                      RM_TOOL_PENCIL_1,
                                      RM_TOOL_BALLPOINT_1,
                                      RM_TOOL_MARKER_1,
                                      RM_TOOL_FINELINER_1,
                                      RM_TOOL_HIGHLIGHTER_1,
                                      RM_TOOL_MECHANICAL_PENCIL_1,
                                      RM_TOOL_PAINTBRUSH_2,
                                      RM_TOOL_MECHANICAL_PENCIL_2,
                                      RM_TOOL_PENCIL_2,
                                      RM_TOOL_BALLPOINT_2,
                                      RM_TOOL_MARKER_2,
                                      RM_TOOL_FINELINER_2,
                                      RM_TOOL_HIGHLIGHTER_2,
                                      RM_TOOL_CALIGRAPHY,
                                      RM_TOOL_SHADER,
                                      RM_TOOL_ERASER,
                                      RM_TOOL_ERASER_AREA};
  constexpr size_t N = sizeof tools / sizeof *tools;
  constexpr size_t M = 12;
  rm_draft_point* points = calloc(N * M, sizeof *points);
  rm_draft_stroke* strokes = calloc(N, sizeof *strokes);
  EXPECT(points && strokes);
  for (size_t i = 0; i < N; i++) {
    for (size_t j = 0; j < M; j++)
      points[i * M + j] = rm_draft_point_make(
          (float)(-600 + 1200.0 * (double)j / M),
          (float)(60 + 95.0 * (double)i + (j % 2 ? 25 : 0)));
    strokes[i] = rm_draft_stroke_make(
        tools[i], i % 3 ? RM_COLOR_BLACK : RM_COLOR_BLUE, points + i * M, M);
  }
  rm_page_draft draft = {.sheet = {1404, 1872, 1404, 1872},
                         .column = rm_draft_column_of(RM_COLUMN_MEDIUM),
                         .strokes = strokes,
                         .stroke_count = N};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT(rm_write_page(&draft, &bytes, &size, nullptr) == RM_OK);
  free(points);
  free(strokes);
  return (rmd_bytes){bytes, size};
}

int main(int argc, char** argv) {
  CHECK(argc == 2 || argc == 3);
  bool exhaustive = argc == 3 && !strcmp(argv[2], "exhaustive");
  CHECK(argc == 2 || exhaustive);
  FONTS* fonts = nullptr;
  OK(FONTS_DIRECTORY(argv[1], nullptr, &fonts, nullptr));
  double started = seconds();

  rmd_bytes ink = every_tool(), junk = {(const uint8_t*)"not a page", 10};
  char ids[1][40];
  built_archive a = notebook(1, ink, ids, "Grid");
  built_archive b = trio(ink, junk, nullptr);
  fixture pdf = source(false);
  built_archive c = trio(ink, junk, &pdf);
  subject tools = {"every tool", open_archive(&a, false), fonts, FRESH, false},
          unreadable = {"unreadable page", open_archive(&b, false), fonts,
                        FRESH, true},
          imported = {"imported outlined PDF", open_archive(&c, false), fonts,
                      FRESH, true};

  causes(&tools, &imported);
  quiet_after_failure();
  sweep(&tools, exhaustive);
  sweep(&unreadable, exhaustive);
  for (export_mode mode = FRESH; mode <= RECOMPOSED; mode++) {
    imported.mode = mode;
    sweep(&imported, exhaustive);
  }
  printf("%zu exports in %.1f s%s\n", exports, seconds() - started,
         exhaustive ? " (every value)" : "");

  rmr_archive_document_free(tools.d);
  rmr_archive_document_free(unreadable.d);
  rmr_archive_document_free(imported.d);
  archive_free(&a);
  archive_free(&b);
  archive_free(&c);
  rm_bytes_free((uint8_t*)(uintptr_t)ink.bytes);
  FONTS_FREE(fonts);
#ifdef RMR_TEST_FONTCONFIG
  /* Standalone test owns the process: release the default Pango font map,
   * then Fontconfig's global caches, for LSan. */
  pango_cairo_font_map_set_default(nullptr);
  FcFini();
#endif
  return 0;
}
