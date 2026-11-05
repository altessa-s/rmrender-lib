/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Fontconfig's default configuration around font map creation. Pango loads
 * it although rendering uses private configurations only. On Windows, where
 * an application may ship Fontconfig without its fonts.conf, font map
 * creation points FONTCONFIG_FILE at an empty configuration in the font
 * cache, unless the user set FONTCONFIG_FILE or FONTCONFIG_PATH or the
 * default is found; elsewhere the environment is left alone. With a second
 * argument the test expects FONTCONFIG_FILE preset to that value. */
#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <rmrender/linux_text.h>

#include "../backends/linux/rmr_font_cache.h"
#include "test.h"
#include "test_paths.h"

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
  const char* preset = argc == 3 ? argv[2] : nullptr;
  if (preset)
    CHECK(g_getenv("FONTCONFIG_FILE") &&
          !strcmp(g_getenv("FONTCONFIG_FILE"), preset));
  else {
    g_unsetenv("FONTCONFIG_FILE");
    g_unsetenv("FONTCONFIG_PATH");
  }

  /* Before anything asks GLib for the user data directory, which it keeps. */
  char* root = g_dir_make_tmp("rmr-fontconfig-XXXXXX", nullptr);
  CHECK(root && g_setenv("XDG_DATA_HOME", root, true));
  char* data = g_build_filename(root, "rmrender", nullptr);
  char* cache = g_build_filename(data, "fonts", nullptr);

  /* Whether Fontconfig finds its default fonts.conf, resolved on a private
   * configuration so the default is not loaded yet. */
  FcConfig* probe = FcConfigCreate();
  CHECK(probe);
  FcChar8* found = FcConfigGetFilename(probe, nullptr);
  bool missing = !found;
  FcStrFree(found);
  FcConfigDestroy(probe);

  /* The spelling Fontconfig opens, as the font cache uses it. */
  char* spelling = rmr_font_cache_directory(cache);
  CHECK(spelling);

  rmr_linux_fonts* fonts = nullptr;
  OK(rmr_linux_fonts_create_directory(argv[1], nullptr, &fonts, nullptr));
  double width = 0;
  OK(rmr_linux_text_width((rm_str){"Hello", 5}, 32, RMR_FACE_SANS, false, false,
                          false, fonts, nullptr, &width, nullptr));
  CHECK(width > 0);
  rmr_linux_fonts_free(fonts);

  const char* file = g_getenv("FONTCONFIG_FILE");
#ifdef _WIN32
  if (preset)
    CHECK(file && !strcmp(file, preset));
  else if (missing) {
    /* The empty configuration in the cache, in the cache's spelling. */
    char* expected = rmr_font_cache_config(spelling, nullptr);
    CHECK(expected && file && !strcmp(file, expected));
    g_free(expected);
  } else
    CHECK(!file);
#else
  (void)missing;
  CHECK(preset ? file && !strcmp(file, preset) : !file);
#endif

  /* The default configuration Pango loads parses, and so does the empty one. */
  FcConfig* config = FcConfigCreate();
  CHECK(config && FcConfigParseAndLoad(config, nullptr, FcTrue));
  FcConfigDestroy(config);
  char* empty = rmr_font_cache_config(spelling, nullptr);
  config = FcConfigCreate();
  CHECK(empty && config &&
        FcConfigParseAndLoad(config, (const FcChar8*)empty, FcTrue));
  FcConfigDestroy(config);
  g_free(empty);

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

  /* Windows may keep the fonts open until exit; leave them then. */
  remove_directory(cache);
  g_free(spelling);
  g_rmdir(data);
  g_rmdir(root);
  g_free(cache);
  g_free(data);
  g_free(root);

  puts("default Fontconfig configuration passed");
  return 0;
}
