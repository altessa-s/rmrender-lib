/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Fontconfig's default configuration around font map creation. Pango loads
 * it although rendering uses private configurations only, and Fontconfig
 * reports on stderr when it finds none. Font map creation then points
 * FONTCONFIG_FILE at an empty configuration: in the font cache on Windows,
 * where an application may ship Fontconfig without its fonts.conf, and in
 * the user cache directory elsewhere, as in containers without /etc/fonts.
 * It does so unless the user set FONTCONFIG_FILE, FONTCONFIG_PATH or
 * FONTCONFIG_SYSROOT or the default is found, and then leaves the
 * environment alone. Outside Windows nothing may reach stderr meanwhile.
 * With a second argument the test expects FONTCONFIG_FILE preset to that
 * value. With RMR_TEST_FONTCONFIG_MISSING set and no preset it requires that
 * Fontconfig finds no default, so a run on a system without one cannot pass
 * through the other branch. */
#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <rmrender/linux_text.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

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

  /* Before anything asks GLib for the user directories, which it keeps. */
  char* root = g_dir_make_tmp("rmr-fontconfig-XXXXXX", nullptr);
  CHECK(root && g_setenv("XDG_DATA_HOME", root, true));
#ifdef _WIN32
  char* cache = g_build_filename(root, "rmrender", "fonts", nullptr);
#else
  char* user = g_build_filename(root, "cache", nullptr);
  CHECK(g_setenv("XDG_CACHE_HOME", user, true));
  g_free(user);
  char* cache = g_build_filename(root, "cache", "rmrender", nullptr);
#endif

  /* Whether Fontconfig finds its default fonts.conf, resolved on a private
   * configuration so the default is not loaded yet. */
  FcConfig* probe = FcConfigCreate();
  CHECK(probe);
  FcChar8* found = FcConfigGetFilename(probe, nullptr);
  bool missing = !found;
  FcStrFree(found);
  FcConfigDestroy(probe);
  if (!preset && g_getenv("RMR_TEST_FONTCONFIG_MISSING")) CHECK(missing);

#ifdef _WIN32
  /* The spelling Fontconfig opens, as the font cache uses it. */
  char* spelling = rmr_font_cache_directory(cache);
  CHECK(spelling);
#else
  /* Fontconfig reports on stderr, partly from Pango's thread: collect it
   * from here until that thread has loaded the default configuration. */
  CHECK(fflush(stderr) == 0);
  char* captured = nullptr;
  int capture = g_file_open_tmp("rmr-stderr-XXXXXX", &captured, nullptr);
  int saved = dup(STDERR_FILENO);
  CHECK(capture >= 0 && saved >= 0 && dup2(capture, STDERR_FILENO) >= 0);
#endif

  rmr_linux_fonts* fonts = nullptr;
  OK(rmr_linux_fonts_create_directory(argv[1], nullptr, &fonts, nullptr));
  double width = 0;
  OK(rmr_linux_text_width((rm_str){"Hello", 5}, 32, RMR_FACE_SANS, false, false,
                          false, fonts, nullptr, &width, nullptr));
  CHECK(width > 0);
  rmr_linux_fonts_free(fonts);

  /* Pango loads Fontconfig's default configuration in a thread of its own;
   * wait for that through a font map. */
  PangoFontMap* map =
      pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
  if (map) {
    (void)pango_fc_font_map_get_config(PANGO_FC_FONT_MAP(map));
    g_object_unref(map);
  }

  /* The default configuration Pango loads parses, without complaint. */
  FcConfig* config = FcConfigCreate();
  CHECK(config && FcConfigParseAndLoad(config, nullptr, FcTrue));
  FcConfigDestroy(config);

#ifndef _WIN32
  CHECK(fflush(stderr) == 0 && dup2(saved, STDERR_FILENO) >= 0);
  close(saved);
  close(capture);
  char* output = nullptr;
  gsize length = 0;
  CHECK(g_file_get_contents(captured, &output, &length, nullptr));
  if (length) fprintf(stderr, "unexpected stderr:\n%s", output);
  CHECK(!length);
  g_free(output);
  g_unlink(captured);
  g_free(captured);

  /* Created only for the empty configuration, as the cache spells it. */
  bool created = g_file_test(cache, G_FILE_TEST_IS_DIR);
  CHECK(created == (missing && !preset));
  GStatBuf status;
  if (created)
    CHECK(g_stat(cache, &status) == 0 && (status.st_mode & 0777) == 0700);
  char* spelling = rmr_font_cache_directory(cache);
  CHECK(spelling && !strcmp(spelling, cache));
#endif

  const char* file = g_getenv("FONTCONFIG_FILE");
  if (preset)
    CHECK(file && !strcmp(file, preset));
  else if (missing) {
    /* The empty configuration in the cache, in the cache's spelling. */
    char* expected = rmr_font_cache_config(spelling, nullptr);
    CHECK(expected && file && !strcmp(file, expected));
    char* content = nullptr;
    CHECK(g_file_get_contents(file, &content, nullptr, nullptr));
    CHECK(!strcmp(content, "<?xml version=\"1.0\"?>\n<fontconfig/>\n"));
    g_free(content);
    g_free(expected);
  } else
    CHECK(!file);

  /* The empty configuration parses. */
  char* empty = rmr_font_cache_config(spelling, nullptr);
  config = FcConfigCreate();
  CHECK(empty && config &&
        FcConfigParseAndLoad(config, (const FcChar8*)empty, FcTrue));
  FcConfigDestroy(config);
  g_free(empty);

  /* Standalone test owns the process: release the default map and
   * Fontconfig's global caches for LSan. */
  pango_cairo_font_map_set_default(nullptr);
  FcFini();

  /* Windows may keep the fonts open until exit; leave them then. */
  remove_directory(cache);
  g_free(spelling);
  char* parent = g_path_get_dirname(cache);
  g_rmdir(parent);
  g_rmdir(root);
  g_free(parent);
  g_free(cache);
  g_free(root);

  puts("default Fontconfig configuration passed");
  return 0;
}
