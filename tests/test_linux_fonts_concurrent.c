/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Font maps created concurrently from threads, as parallel callers in one
 * process do. On Windows they share a font cache, pointed here at a fresh
 * directory so the first round starts cold; elsewhere this exercises
 * concurrent Fontconfig, FreeType and Pango setup on private configs. */
#include <glib.h>
#include <glib/gstdio.h>
#include <rmrender/linux_text.h>
#ifdef RMR_TEST_FONTCONFIG
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
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
} race;

/* One creator's outcome. */
typedef struct {
  race* race;
  rmr_status status;
  rmr_error error;
  double width;
} creator;

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
  rmr_linux_fonts_free(fonts);
  return nullptr;
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
  CHECK(argc == 2);
  (void)test_fopen;

  /* Before anything asks GLib for the user data directory, which it keeps. */
  char* root = g_dir_make_tmp("rmr-fonts-race-XXXXXX", nullptr);
  CHECK(root && g_setenv("XDG_DATA_HOME", root, true));

  for (int round = 0; round < ROUNDS; round++) {
    race r = {.directory = argv[1]};
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
#endif
  puts("concurrent font map creation passed");
  return 0;
}
