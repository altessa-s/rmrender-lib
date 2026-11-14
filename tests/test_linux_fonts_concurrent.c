/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Font maps created concurrently from threads, as parallel callers in one
 * process do. On Windows they share a font cache, pointed here at a fresh
 * directory so the first round starts cold; elsewhere this exercises
 * concurrent Fontconfig, FreeType and Pango setup on private configs. */
#include <glib.h>
#include <glib/gstdio.h>
#include <rmrender/linux.h>
#include <rmrender/linux_text.h>
#include <rmrender/pdf.h>
#ifdef RMR_TEST_FONTCONFIG
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include "fontconfig_fixture.h"
#endif

#include "test.h"
#include "test_paths.h"

enum { CREATORS = 6, ROUNDS = 2 };

/* Creators released together to load the fonts in directory. */
typedef struct {
  GMutex lock;
  GCond start;
  int waiting;
  const char* directory;
  bool mixed;
} race;

/* One creator's outcome. */
typedef struct {
  race* race;
  rmr_status status;
  rmr_error error;
  double width;
} creator;

/* Interleaves text, grouped cuts and imported text PDFs across page workers.
 * The imported page splits a text-bearing segment from a vector-only one. */
static void mixed_pages(const rmr_plan* imported) {
  rmr_rect viewport = {0, 0, 702, 200};
  rmr_draw text = {.mode = RMR_TEXT,
                   .color = {0, 0, 0, 1},
                   .text = {.utf8 = "Hello Привет العربية हिन्दी",
                            .font_size = 24,
                            .origin = {12, 40}}};
  text.text.size = strlen(text.text.utf8);
  rmr_text_provider provider = rmr_linux_text_provider();
  rmr_text_metrics metrics = {0};
  OK(provider.measure(provider.context, text.text.utf8, text.text.size,
                      text.text.font_size, &metrics));
  text.text.bounds = metrics.bounds;
  rmr_element box[] = {{.kind = RMR_MOVE, .end = {20, 80}},
                       {.kind = RMR_LINE, .end = {100, 80}},
                       {.kind = RMR_LINE, .end = {100, 120}},
                       {.kind = RMR_LINE, .end = {20, 120}},
                       {.kind = RMR_CLOSE}};
  rmr_element hole[] = {{.kind = RMR_MOVE, .end = {50, 70}},
                        {.kind = RMR_LINE, .end = {60, 70}},
                        {.kind = RMR_LINE, .end = {60, 130}},
                        {.kind = RMR_LINE, .end = {50, 130}},
                        {.kind = RMR_CLOSE}};
  rmr_path_view cut = {hole, 5};
  rmr_draw ink = {.mode = RMR_FILL,
                  .path = {box, 5},
                  .color = {.2, .3, .6, 1},
                  .group = 1,
                  .group_alpha = .5,
                  .erasers = &cut,
                  .eraser_count = 1};
  CHECK(rmr_plan_count(imported) == 1);
  rmr_draw split[] = {text, *rmr_plan_draw(imported, 0), ink};
  rmr_plan *letters = nullptr, *vectors = nullptr, *segments = nullptr;
  OK(rmr_plan_create(&text, 1, viewport, nullptr, &letters, nullptr));
  OK(rmr_plan_create(&ink, 1, viewport, nullptr, &vectors, nullptr));
  OK(rmr_plan_create(split, 3, viewport, nullptr, &segments, nullptr));
  const rmr_plan* pages[] = {letters, vectors,  segments, vectors,
                             letters, segments, vectors,  letters};
  rmr_buffer pdf = {0};
  OK(rmr_linux_pdf(pages, sizeof pages / sizeof *pages, 1, nullptr, nullptr,
                   nullptr, &pdf, nullptr));
  CHECK(pdf.size > 100 && !memcmp(pdf.bytes, "%PDF-", 5));
  size_t count = 0;
  rmr_pdf_page_geometry geometry[sizeof pages / sizeof *pages] = {0};
  OK(rmr_pdf_pages(pdf.bytes, pdf.size, nullptr, geometry,
                   sizeof geometry / sizeof *geometry, &count, nullptr));
  CHECK(count == sizeof pages / sizeof *pages);
  for (size_t i = 0; i < count; i++)
    CHECK(geometry[i].has_media && geometry[i].media.width == 702 &&
          geometry[i].media.height == 200);
  rmr_buffer_free(&pdf);
  rmr_plan_free(letters);
  rmr_plan_free(vectors);
  rmr_plan_free(segments);
}

/* Waits for every creator, then creates a font map, measures a line with it
 * and frees it. */
static void* create_concurrently(void* data) {
  creator* c = data;
  race* r = c->race;
  g_mutex_lock(&r->lock);
  if (++r->waiting == CREATORS) g_cond_broadcast(&r->start);
  while (r->waiting < CREATORS) g_cond_wait(&r->start, &r->lock);
  g_mutex_unlock(&r->lock);

  rmr_linux_fonts* fonts = nullptr;
  c->status = rmr_linux_fonts_create_directory(r->directory, nullptr, &fonts,
                                               &c->error);
  if (c->status == RMR_OK)
    c->status = rmr_linux_text_width((rm_str){"Hello", 5}, 32, RMR_FACE_SANS,
                                     false, false, false, fonts, nullptr,
                                     &c->width, &c->error);
  /* Exercise distinct sizes and variations, then embed the resulting fonts
   * in a PDF while other callers can be shaping or destroying their maps. */
  const char* sample = "Hello Привет العربية हिन्दी";
  for (int i = 0; i < 3 && c->status == RMR_OK; i++) {
    double width = 0;
    c->status = rmr_linux_text_width(
        (rm_str){sample, strlen(sample)}, 24 + 8 * i, (rmr_type_face)i, i == 1,
        i == 2, false, fonts, nullptr, &width, &c->error);
    if (c->status == RMR_OK && !(width > 0)) c->status = RMR_ERR_BACKEND;
  }
  rm_paragraph paragraph = {.text = {sample, strlen(sample)}};
  rm_text_block block = {.paragraphs = &paragraph, .paragraph_count = 1};
  rmr_linux_layout* layout = nullptr;
  rmr_plan* plan = nullptr;
  if (c->status == RMR_OK)
    c->status = rmr_linux_layout_create(&block, 702, fonts, nullptr, &layout,
                                        &c->error);
  if (c->status == RMR_OK)
    c->status = rmr_linux_layout_plan(layout, (rmr_rect){0, 0, 702, 200},
                                      nullptr, &plan, &c->error);
  if (c->status == RMR_OK && r->mixed) mixed_pages(plan);
  rmr_plan_free(plan);
  rmr_linux_layout_free(layout);
  rmr_linux_fonts_free(fonts);
  return nullptr;
}

typedef struct {
  const char* directory;
  char* bytes;
  size_t calls;
} resolver;

/* Measures on another thread so a callback holding the font lock deadlocks. */
static void* measure_elsewhere(void* data) {
  (void)data;
  rmr_text_provider provider = rmr_linux_text_provider();
  rmr_text_metrics metrics = {0};
  OK(provider.measure(provider.context, "Hello", 5, 24, &metrics));
  CHECK(metrics.width > 0);
  return nullptr;
}

/* Reuses one buffer, as host resolvers may, after waiting for a text caller. */
static rmr_status resolve_while_measuring(void* context, const char* name,
                                          rmr_asset_view* out) {
  resolver* r = context;
  GThread* thread = g_thread_new("measure", measure_elsewhere, nullptr);
  g_thread_join(thread);
  r->calls++;
  g_free(r->bytes);
  r->bytes = nullptr;
  char* filename = g_strconcat(name, ".ttf", nullptr);
  char* path = g_build_filename(r->directory, filename, nullptr);
  gsize size = 0;
  bool ok = g_file_get_contents(path, &r->bytes, &size, nullptr);
  g_free(path);
  g_free(filename);
  if (!ok) return RMR_ERR_UNSUPPORTED;
  *out = (rmr_asset_view){(const uint8_t*)r->bytes, size};
  return RMR_OK;
}

static void resolver_waits(const char* directory) {
  resolver r = {.directory = directory};
  rmr_linux_fonts* fonts = nullptr;
  OK(rmr_linux_fonts_create(resolve_while_measuring, &r, nullptr, &fonts,
                            nullptr));
  CHECK(r.calls == 14);
  double width = 0;
  OK(rmr_linux_text_width((rm_str){"Hello", 5}, 24, RMR_FACE_SANS, false, false,
                          false, fonts, nullptr, &width, nullptr));
  CHECK(width > 0);
  rmr_linux_fonts_free(fonts);
  g_free(r.bytes);
}

/* Removes the files in directory and then directory, as far as possible. */
static void remove_directory(const char* directory) {
  GDir* dir = g_dir_open(directory, 0, nullptr);
  if (dir) {
    for (const char* name; (name = g_dir_read_name(dir));) {
      char* path = g_build_filename(directory, name, nullptr);
      g_unlink(path);
      g_free(path);
    }
    g_dir_close(dir);
  }
  g_rmdir(directory);
}

int main(int argc, char** argv) {
  argv = test_arguments(argc, argv);
  CHECK(argc == 2 || argc == 3);
  (void)test_fopen;

  /* Before anything asks GLib for the user data directory, which it keeps. */
  char* root = g_dir_make_tmp("rmr-fonts-race-XXXXXX", nullptr);
  CHECK(root && g_setenv("XDG_DATA_HOME", root, true));
#ifdef RMR_TEST_FONTCONFIG
  char* fontconfig = test_fontconfig_directory(argv[1]);
#endif

  bool mixed = argc == 3 && !strcmp(argv[2], "mixed");
  bool resolve = argc == 3 && !strcmp(argv[2], "resolver");
  CHECK(argc == 2 || mixed || resolve);
  if (resolve) resolver_waits(argv[1]);
  for (int round = 0; !resolve && round < ROUNDS; round++) {
    race r = {.directory = argv[1], .mixed = mixed};
    g_mutex_init(&r.lock);
    g_cond_init(&r.start);
    creator creators[CREATORS];
    GThread* threads[CREATORS];
    for (int i = 0; i < CREATORS; i++) {
      creators[i] = (creator){.race = &r};
      threads[i] = g_thread_new("fonts", create_concurrently, &creators[i]);
    }
    for (int i = 0; i < CREATORS; i++) g_thread_join(threads[i]);
    for (int i = 0; i < CREATORS; i++) {
      if (creators[i].status != RMR_OK)
        fprintf(stderr, "round %d, creator %d: status %d: %s\n", round, i,
                (int)creators[i].status, creators[i].error.message);
      CHECK(creators[i].status == RMR_OK);
      CHECK(creators[i].width > 0 && creators[i].width == creators[0].width);
    }
    g_cond_clear(&r.start);
    g_mutex_clear(&r.lock);
  }

  char* data = g_build_filename(root, "rmrender", nullptr);
  char* cache = g_build_filename(data, "fonts", nullptr);
#ifdef _WIN32
  /* One file per font, none of them temporary. */
  size_t fonts = 0;
  GDir* dir = g_dir_open(cache, 0, nullptr);
  CHECK(dir);
  for (const char* name; (name = g_dir_read_name(dir));) {
    CHECK(g_str_has_suffix(name, ".ttf"));
    fonts++;
  }
  g_dir_close(dir);
  CHECK(fonts >= 13);
#endif
  /* Windows may keep the fonts open until exit; leave them then. */
  remove_directory(cache);
  g_rmdir(data);
  g_rmdir(root);
  g_free(cache);
  g_free(data);
  g_free(root);

#ifdef RMR_TEST_FONTCONFIG
  /* Standalone test owns the process. Pango loads Fontconfig's default
   * configuration in a thread of its own; wait for that through a font map,
   * then release the default map and Fontconfig's global caches for LSan. */
  PangoFontMap* map =
      pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
  if (map) {
    (void)pango_fc_font_map_get_config(PANGO_FC_FONT_MAP(map));
    g_object_unref(map);
  }
  pango_cairo_font_map_set_default(nullptr);
  FcFini();
  test_fontconfig_free(fontconfig);
#endif
  puts("concurrent font map creation passed");
  return 0;
}
